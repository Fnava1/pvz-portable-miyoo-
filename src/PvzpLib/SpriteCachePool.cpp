#include "SpriteCachePool.h"
#include "SpriteStreamer.h"
#include "PvzpDebug.h"
#include "../SexyAppFramework/graphics/Graphics.h"
#include "../SexyAppFramework/graphics/MemoryImage.h"
#include "../Lawn/Board.h"
#include "../Lawn/SeedPacket.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <new>
#include <cstdio>
#include <string>

#if defined(__linux__)
#include <sys/mman.h>
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

static inline uint8x8_t vraddhn_u8(uint16x8_t a, uint16x8_t b)
{
    return vraddhn_u16(a, b);
}

#if !defined(__aarch64__)
static inline uint16_t vmaxvq_u16(uint16x8_t v)
{
    uint16x4_t m = vpmax_u16(vget_low_u16(v), vget_high_u16(v));
    m = vpmax_u16(m, m);
    m = vpmax_u16(m, m);
    return vget_lane_u16(m, 0);
}
#endif
#endif

static uint8_t* sArenaBase = nullptr;
static size_t sPlantArenaOffset = 0;
static size_t sZombieArenaOffset = 0;
static size_t sTotalAllocatedBytes = 0;
static size_t sPeakAllocatedBytes = 0;
static uint32_t sCacheHitCount = 0;
static uint32_t sReanimFallbackCount = 0;
static SpriteCacheTelemetry sTelemetry;
static SpriteCacheSlot sPlantSlots[SPC_MAX_PLANT_SLOTS];
static SpriteCacheSlot sZombieSlots[SPC_MAX_ZOMBIE_SLOTS];

void SpriteCachePool::InitLevelArena()
{
    if (!sArenaBase)
    {
#if defined(__linux__)
        sArenaBase = (uint8_t*)mmap(nullptr, POOL_MAX_BUDGET, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (sArenaBase == MAP_FAILED)
        {
            sArenaBase = nullptr;
        }
#else
        sArenaBase = new (std::nothrow) uint8_t[POOL_MAX_BUDGET];
#endif
    }
    sPlantArenaOffset = 0;
    sZombieArenaOffset = 0;
    sTotalAllocatedBytes = 0;
    sPeakAllocatedBytes = 0;
    sCacheHitCount = 0;
    sReanimFallbackCount = 0;
    sTelemetry = SpriteCacheTelemetry();
    ClearAllSlots();
}

void SpriteCachePool::FreeLevelArena()
{
    ClearAllSlots();
#if defined(__linux__)
    if (sArenaBase)
    {
        munmap(sArenaBase, POOL_MAX_BUDGET);
        sArenaBase = nullptr;
    }
#else
    if (sArenaBase)
    {
        delete[] sArenaBase;
        sArenaBase = nullptr;
    }
#endif
    sPlantArenaOffset = 0;
    sZombieArenaOffset = 0;
    sTotalAllocatedBytes = 0;
    sPeakAllocatedBytes = 0;
}

void SpriteCachePool::FreeSlot(SpriteCacheSlot* theSlot)
{
    if (theSlot)
    {
        theSlot->Reset();
    }
}

void SpriteCachePool::InitPool()
{
    InitLevelArena();
}

void SpriteCachePool::FreePool()
{
    FreeLevelArena();
}

bool SpriteCachePool::IsPoolInitialized()
{
    return sArenaBase != nullptr;
}

size_t SpriteCachePool::GetTotalMemoryUsage()
{
    return sTotalAllocatedBytes;
}

size_t SpriteCachePool::GetTotalAllocatedBytes()
{
    return sTotalAllocatedBytes;
}

size_t SpriteCachePool::GetPeakAllocatedBytes()
{
    return sPeakAllocatedBytes;
}

void SpriteCachePool::GetActiveSlotCounts(int& thePlants, int& theZombies)
{
    thePlants = 0;
    theZombies = 0;
    for (int i = 0; i < SPC_MAX_PLANT_SLOTS; i++)
    {
        if (sPlantSlots[i].mId != -1)
            thePlants++;
    }
    for (int i = 0; i < SPC_MAX_ZOMBIE_SLOTS; i++)
    {
        if (sZombieSlots[i].mId != -1)
            theZombies++;
    }
}

uint32_t SpriteCachePool::GetCacheHitCount()
{
    return sCacheHitCount;
}

uint32_t SpriteCachePool::GetReanimFallbackCount()
{
    return sReanimFallbackCount;
}

SpriteCacheTelemetry& SpriteCachePool::GetTelemetry()
{
    return sTelemetry;
}

SpriteCacheSlot* SpriteCachePool::GetPlantSlot(SeedType theSeed)
{
    for (int i = 0; i < SPC_MAX_PLANT_SLOTS; i++)
    {
        if (sPlantSlots[i].mId == theSeed)
            return &sPlantSlots[i];
    }
    return nullptr;
}

SpriteCacheSlot* SpriteCachePool::GetZombieSlot(ZombieType theZombie)
{
    for (int i = 0; i < SPC_MAX_ZOMBIE_SLOTS; i++)
    {
        if (sZombieSlots[i].mId == theZombie)
            return &sZombieSlots[i];
    }
    return nullptr;
}

bool SpriteCachePool::IsPlantReady(SeedType theSeed)
{
    SpriteCacheSlot* slot = GetPlantSlot(theSeed);
    return slot && slot->mIsReady;
}

bool SpriteCachePool::IsZombieReady(ZombieType theZombie)
{
    SpriteCacheSlot* slot = GetZombieSlot(theZombie);
    return slot && slot->mIsReady;
}

SpriteCacheSlot* SpriteCachePool::AllocatePlantSlot(SeedType theSeed, const SpcHeader& theHeader)
{
    if (!sArenaBase)
    {
        InitLevelArena();
        if (!sArenaBase)
            return nullptr;
    }

    // Check if already allocated
    SpriteCacheSlot* existing = GetPlantSlot(theSeed);
    if (existing)
        return existing;

    // Find first free slot
    int freeIdx = -1;
    for (int i = 0; i < SPC_MAX_PLANT_SLOTS; i++)
    {
        if (sPlantSlots[i].mId == -1)
        {
            freeIdx = i;
            break;
        }
    }

    if (freeIdx == -1)
        return nullptr; // No free plant slots

    size_t reqBytes = theHeader.mTotalDataBytes;
    if (sPlantArenaOffset + reqBytes > PLANT_ZONE_SIZE)
    {
        PvzpLogLn("[SpriteCachePool] WARNING: Plant zone budget exceeded (offset {} + req {} > {}). Allocation rejected.",
                  (uint32_t)sPlantArenaOffset, (uint32_t)reqBytes, (uint32_t)PLANT_ZONE_SIZE);
        return nullptr; // Exceeded plant arena budget
    }

    SpriteCacheSlot& slot = sPlantSlots[freeIdx];
    slot.mId = theSeed;
    slot.mIsReady = false;
    slot.mWidth = theHeader.mWidth;
    slot.mHeight = theHeader.mHeight;
    slot.mOffsetX = theHeader.mOffsetX;
    slot.mOffsetY = theHeader.mOffsetY;
    slot.mFrameCount = theHeader.mFrameCount;
    slot.mFrameSizeBytes = theHeader.mFrameSizeBytes;
    slot.mPixelData = sArenaBase + sPlantArenaOffset;
    slot.mAllocatedBytes = reqBytes;

    sPlantArenaOffset += reqBytes;
    sTotalAllocatedBytes = sPlantArenaOffset + sZombieArenaOffset;
    if (sTotalAllocatedBytes > sPeakAllocatedBytes)
        sPeakAllocatedBytes = sTotalAllocatedBytes;

    return &slot;
}

SpriteCacheSlot* SpriteCachePool::AllocateZombieSlot(ZombieType theZombie, const SpcHeader& theHeader)
{
    if (!sArenaBase)
    {
        InitLevelArena();
        if (!sArenaBase)
            return nullptr;
    }

    SpriteCacheSlot* existing = GetZombieSlot(theZombie);
    if (existing)
        return existing;

    int freeIdx = -1;
    for (int i = 0; i < SPC_MAX_ZOMBIE_SLOTS; i++)
    {
        if (sZombieSlots[i].mId == -1)
        {
            freeIdx = i;
            break;
        }
    }

    if (freeIdx == -1)
        return nullptr;

    size_t reqBytes = theHeader.mTotalDataBytes;
    if (sZombieArenaOffset + reqBytes > ZOMBIE_ZONE_SIZE)
    {
        PvzpLogLn("[SpriteCachePool] WARNING: Zombie zone budget exceeded (offset {} + req {} > {}). Allocation rejected.",
                  (uint32_t)sZombieArenaOffset, (uint32_t)reqBytes, (uint32_t)ZOMBIE_ZONE_SIZE);
        return nullptr;
    }

    SpriteCacheSlot& slot = sZombieSlots[freeIdx];
    slot.mId = theZombie;
    slot.mIsReady = false;
    slot.mWidth = theHeader.mWidth;
    slot.mHeight = theHeader.mHeight;
    slot.mOffsetX = theHeader.mOffsetX;
    slot.mOffsetY = theHeader.mOffsetY;
    slot.mFrameCount = theHeader.mFrameCount;
    slot.mFrameSizeBytes = theHeader.mFrameSizeBytes;
    slot.mPixelData = sArenaBase + PLANT_ZONE_SIZE + sZombieArenaOffset;
    slot.mAllocatedBytes = reqBytes;

    sZombieArenaOffset += reqBytes;
    sTotalAllocatedBytes = sPlantArenaOffset + sZombieArenaOffset;
    if (sTotalAllocatedBytes > sPeakAllocatedBytes)
        sPeakAllocatedBytes = sTotalAllocatedBytes;

    return &slot;
}

void SpriteCachePool::ClearZombieSlots()
{
    SpriteStreamer::CancelZombies();

    for (int i = 0; i < SPC_MAX_ZOMBIE_SLOTS; i++)
    {
        sZombieSlots[i].Reset();
    }

    sZombieArenaOffset = 0;
    sTotalAllocatedBytes = sPlantArenaOffset;

#if defined(__linux__)
    if (sArenaBase)
    {
        madvise(sArenaBase + PLANT_ZONE_SIZE, ZOMBIE_ZONE_SIZE, MADV_DONTNEED);
    }
#endif

    PvzpLogLn("[SpriteCachePool] Cleared zombie slots. Physical pages purged via madvise. Active pool: {} / {} bytes",
              (uint32_t)sTotalAllocatedBytes, (uint32_t)POOL_MAX_BUDGET);
}

void SpriteCachePool::ClearPlantSlots()
{
    for (int i = 0; i < SPC_MAX_PLANT_SLOTS; i++)
    {
        sPlantSlots[i].Reset();
    }

    sPlantArenaOffset = 0;
    sTotalAllocatedBytes = sZombieArenaOffset;

#if defined(__linux__)
    if (sArenaBase)
    {
        madvise(sArenaBase, PLANT_ZONE_SIZE, MADV_DONTNEED);
    }
#endif
}

void SpriteCachePool::ClearAllSlots()
{
    ClearPlantSlots();
    ClearZombieSlots();
}

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static void DrawFrame_NEON(const uint16_t* srcFrame, int srcPitch, int srcX, int srcY,
                           uint32_t* dstBits, int dstPitch, int dstX, int dstY,
                           int w, int h, bool theIsChilled)
{
    for (int y = 0; y < h; y++)
    {
        uint32_t* dRow = dstBits + (dstY + y) * dstPitch + dstX;
        const uint16_t* sRow = srcFrame + (srcY + y) * srcPitch + srcX;

        int x = 0;
        for (; x + 7 < w; x += 8)
        {
            uint16x8_t qSrc = vld1q_u16(sRow + x);
            uint16x8_t qAlpha = vshrq_n_u16(qSrc, 12);

            // Transparent block early-out: if all 8 alphas are 0 (vmaxvq_u16(qAlpha) == 0),
            // skip reading/writing destination memory (protects SSD202D 16-bit DDR3 bus bandwidth).
            if (vmaxvq_u16(qAlpha) == 0)
                continue;

            // Unpack components to 8-bit
            uint16x8_t qR = vandq_u16(vshrq_n_u16(qSrc, 8), vdupq_n_u16(0x0F));
            uint16x8_t qG = vandq_u16(vshrq_n_u16(qSrc, 4), vdupq_n_u16(0x0F));
            uint16x8_t qB = vandq_u16(qSrc, vdupq_n_u16(0x0F));

            uint8x8_t vA = vmovn_u16(vorrq_u16(vshlq_n_u16(qAlpha, 4), qAlpha));
            uint8x8_t vR = vmovn_u16(vorrq_u16(vshlq_n_u16(qR, 4), qR));
            uint8x8_t vG = vmovn_u16(vorrq_u16(vshlq_n_u16(qG, 4), qG));
            uint8x8_t vB = vmovn_u16(vorrq_u16(vshlq_n_u16(qB, 4), qB));

            // If theIsChilled is true: modulate R and G by 75/255 branchless
            if (theIsChilled)
            {
                uint16x8_t prodR = vmull_u8(vR, vdup_n_u8(75));
                uint16x8_t tR = vaddq_u16(prodR, vdupq_n_u16(1));
                tR = vaddq_u16(tR, vshrq_n_u16(prodR, 8));
                vR = vmovn_u16(vshrq_n_u16(tR, 8));

                uint16x8_t prodG = vmull_u8(vG, vdup_n_u8(75));
                uint16x8_t tG = vaddq_u16(prodG, vdupq_n_u16(1));
                tG = vaddq_u16(tG, vshrq_n_u16(prodG, 8));
                vG = vmovn_u16(vshrq_n_u16(tG, 8));
            }

            // Load destination pixels (32 bytes = 8 pixels ARGB8888)
            uint8x8x4_t dstPixels = vld4_u8(reinterpret_cast<const uint8_t*>(dRow + x));
            uint8x8_t invA = vsub_u8(vdup_n_u8(255), vA);

            // Accurate alpha blending using vraddhn_u8 on 16-bit products (src * a + dst * (255 - a)), zero integer divisions
            uint8x8_t outB = vraddhn_u8(vmull_u8(vB, vA), vmull_u8(dstPixels.val[0], invA));
            uint8x8_t outG = vraddhn_u8(vmull_u8(vG, vA), vmull_u8(dstPixels.val[1], invA));
            uint8x8_t outR = vraddhn_u8(vmull_u8(vR, vA), vmull_u8(dstPixels.val[2], invA));
            uint8x8_t outA = vdup_n_u8(0xFF);

            // Store 8 pixels ARGB8888 (vst1q_u32)
            uint8x8x2_t bg = vzip_u8(outB, outG);
            uint8x8x2_t ra = vzip_u8(outR, outA);
            uint16x4x2_t p0 = vzip_u16(vreinterpret_u16_u8(bg.val[0]), vreinterpret_u16_u8(ra.val[0]));
            uint16x4x2_t p1 = vzip_u16(vreinterpret_u16_u8(bg.val[1]), vreinterpret_u16_u8(ra.val[1]));
            uint32x4_t out0 = vcombine_u32(vreinterpret_u32_u16(p0.val[0]), vreinterpret_u32_u16(p0.val[1]));
            uint32x4_t out1 = vcombine_u32(vreinterpret_u32_u16(p1.val[0]), vreinterpret_u32_u16(p1.val[1]));

            vst1q_u32(dRow + x, out0);
            vst1q_u32(dRow + x + 4, out1);
        }

        // Scalar tail cleanup loop for (w % 8) pixels
        for (; x < w; x++)
        {
            uint16_t c = sRow[x];
            uint32_t a = (c >> 12) & 0x0F;
            if (a == 0)
                continue;

            uint32_t r = (c >> 8) & 0x0F;
            uint32_t g_val = (c >> 4) & 0x0F;
            uint32_t b = c & 0x0F;

            r = (r << 4) | r;
            g_val = (g_val << 4) | g_val;
            b = (b << 4) | b;

            if (theIsChilled)
            {
                uint32_t prodR = r * 75;
                r = (prodR + 1 + (prodR >> 8)) >> 8;
                uint32_t prodG = g_val * 75;
                g_val = (prodG + 1 + (prodG >> 8)) >> 8;
            }

            if (a == 0x0F)
            {
                dRow[x] = 0xFF000000 | (r << 16) | (g_val << 8) | b;
            }
            else
            {
                a = (a << 4) | a;
                uint32_t dstColor = dRow[x];
                uint32_t dr = (dstColor >> 16) & 0xFF;
                uint32_t dg = (dstColor >> 8) & 0xFF;
                uint32_t db = dstColor & 0xFF;

                uint32_t prodR = r * a + dr * (255 - a);
                uint32_t prodG = g_val * a + dg * (255 - a);
                uint32_t prodB = b * a + db * (255 - a);

                uint32_t outR = (prodR + 1 + (prodR >> 8)) >> 8;
                uint32_t outG = (prodG + 1 + (prodG >> 8)) >> 8;
                uint32_t outB = (prodB + 1 + (prodB >> 8)) >> 8;

                dRow[x] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
            }
        }
    }
}
#else
static void DrawFrame_Scalar(const uint16_t* srcFrame, int srcPitch, int srcX, int srcY,
                             uint32_t* dstBits, int dstPitch, int dstX, int dstY,
                             int w, int h, bool theIsChilled)
{
    for (int y = 0; y < h; y++)
    {
        uint32_t* dRow = dstBits + (dstY + y) * dstPitch + dstX;
        const uint16_t* sRow = srcFrame + (srcY + y) * srcPitch + srcX;

        for (int x = 0; x < w; x++)
        {
            uint16_t c = sRow[x];
            uint32_t a = (c >> 12) & 0x0F;
            if (a == 0)
                continue;

            uint32_t r = (c >> 8) & 0x0F;
            uint32_t g_val = (c >> 4) & 0x0F;
            uint32_t b = c & 0x0F;

            r = (r << 4) | r;
            g_val = (g_val << 4) | g_val;
            b = (b << 4) | b;

            if (theIsChilled)
            {
                uint32_t prodR = r * 75;
                r = (prodR + 1 + (prodR >> 8)) >> 8;
                uint32_t prodG = g_val * 75;
                g_val = (prodG + 1 + (prodG >> 8)) >> 8;
            }

            if (a == 0x0F)
            {
                dRow[x] = 0xFF000000 | (r << 16) | (g_val << 8) | b;
            }
            else
            {
                a = (a << 4) | a;
                uint32_t dstColor = dRow[x];
                uint32_t dr = (dstColor >> 16) & 0xFF;
                uint32_t dg = (dstColor >> 8) & 0xFF;
                uint32_t db = dstColor & 0xFF;

                uint32_t prodR = r * a + dr * (255 - a);
                uint32_t prodG = g_val * a + dg * (255 - a);
                uint32_t prodB = b * a + db * (255 - a);

                uint32_t outR = (prodR + 1 + (prodR >> 8)) >> 8;
                uint32_t outG = (prodG + 1 + (prodG >> 8)) >> 8;
                uint32_t outB = (prodB + 1 + (prodB >> 8)) >> 8;

                dRow[x] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
            }
        }
    }
}
#endif

void SpriteCachePool::DrawFrame(Graphics* g, float thePosX, float thePosY, const SpriteCacheSlot* theSlot, int theFrame, bool theIsChilled)
{
    if (!g || !theSlot || !theSlot->mIsReady || !theSlot->mPixelData)
        return;

    int frameIndex = std::clamp(theFrame, 0, (int)theSlot->mFrameCount - 1);
    const uint16_t* srcFrame = (const uint16_t*)(theSlot->mPixelData + frameIndex * theSlot->mFrameSizeBytes);

    int dstX = (int)std::floor(thePosX + theSlot->mOffsetX + g->mTransX);
    int dstY = (int)std::floor(thePosY + theSlot->mOffsetY + g->mTransY);
    int w = theSlot->mWidth;
    int h = theSlot->mHeight;

    Rect clip = g->mClipRect;
    int srcX = 0;
    int srcY = 0;

    if (dstX < clip.mX)
    {
        int d = clip.mX - dstX;
        srcX += d;
        w -= d;
        dstX = clip.mX;
    }
    if (dstY < clip.mY)
    {
        int d = clip.mY - dstY;
        srcY += d;
        h -= d;
        dstY = clip.mY;
    }
    if (dstX + w > clip.mX + clip.mWidth)
    {
        w = clip.mX + clip.mWidth - dstX;
    }
    if (dstY + h > clip.mY + clip.mHeight)
    {
        h = clip.mY + clip.mHeight - dstY;
    }

    if (w <= 0 || h <= 0)
        return;

    MemoryImage* destImg = static_cast<MemoryImage*>(g->mDestImage);
    if (!destImg || !destImg->GetBits())
        return;

    uint32_t* dstBits = destImg->GetBits();
    int dstPitch = destImg->GetWidth();
    int srcPitch = theSlot->mWidth;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    DrawFrame_NEON(srcFrame, srcPitch, srcX, srcY, dstBits, dstPitch, dstX, dstY, w, h, theIsChilled);
#else
    DrawFrame_Scalar(srcFrame, srcPitch, srcX, srcY, dstBits, dstPitch, dstX, dstY, w, h, theIsChilled);
#endif

    destImg->BitsChanged();
}

bool SpriteCachePool::DrawPlant(Graphics* g, float thePosX, float thePosY, SeedType theSeed, float theAnimTime)
{
    SpriteCacheSlot* slot = GetPlantSlot(theSeed);
    if (!slot || !slot->mIsReady)
    {
        sReanimFallbackCount++;
        return false;
    }

    sCacheHitCount++;
    float norm = theAnimTime - std::floor(theAnimTime);
    int frame = std::clamp((int)(norm * slot->mFrameCount), 0, (int)slot->mFrameCount - 1);
    DrawFrame(g, thePosX, thePosY, slot, frame, false);
    return true;
}

bool SpriteCachePool::DrawZombie(Graphics* g, float thePosX, float thePosY, ZombieType theZombie, float theAnimTime, bool theIsChilled)
{
    SpriteCacheSlot* slot = GetZombieSlot(theZombie);
    if (!slot || !slot->mIsReady)
    {
        sReanimFallbackCount++;
        return false;
    }

    sCacheHitCount++;
    if (theIsChilled)
        sTelemetry.mHitsChilled++;
    else
        sTelemetry.mHitsNormal++;
    sTelemetry.mHitsTotal++;

    float norm = theAnimTime - std::floor(theAnimTime);
    int frame = std::clamp((int)(norm * slot->mFrameCount), 0, (int)slot->mFrameCount - 1);
    DrawFrame(g, thePosX, thePosY, slot, frame, theIsChilled);
    return true;
}

void SpriteCachePool::PreloadSynchronous(Board* theBoard)
{
    if (!theBoard)
        return;

    SpriteStreamer::CancelAll();
    SpriteStreamer::ResetFailedRequests();

    if (!sArenaBase)
    {
        InitLevelArena();
        if (!sArenaBase)
            return;
    }

    bool aPlantNeeded[SeedType::NUM_SEED_TYPES] = {};
    for (Plant* aPlant : theBoard->mPlants)
    {
        if (aPlant && !aPlant->mDead && aPlant->mSeedType >= 0 && aPlant->mSeedType < SeedType::NUM_SEED_TYPES)
        {
            aPlantNeeded[aPlant->mSeedType] = true;
            if (aPlant->mImitaterType >= 0 && aPlant->mImitaterType < SeedType::NUM_SEED_TYPES)
            {
                aPlantNeeded[aPlant->mImitaterType] = true;
            }
        }
    }
    if (theBoard->mSeedBank)
    {
        for (int i = 0; i < theBoard->mSeedBank->mNumPackets && i < SEEDBANK_MAX; ++i)
        {
            SeedPacket* aPacket = &theBoard->mSeedBank->mSeedPackets[i];
            if (aPacket->mPacketType >= 0 && aPacket->mPacketType < SeedType::NUM_SEED_TYPES)
            {
                aPlantNeeded[aPacket->mPacketType] = true;
            }
            if (aPacket->mImitaterType >= 0 && aPacket->mImitaterType < SeedType::NUM_SEED_TYPES)
            {
                aPlantNeeded[aPacket->mImitaterType] = true;
            }
        }
    }

    for (int i = 0; i < SeedType::NUM_SEED_TYPES; ++i)
    {
        if (!aPlantNeeded[i])
            continue;

        SeedType st = static_cast<SeedType>(i);
        const SpriteCacheEntityInfo* info = SpriteCache_GetPlantEntityInfo(st);
        if (!info)
            continue;

        if (IsPlantReady(st))
            continue;

        std::string path = std::string(SPC_CACHE_DIR) + "/" + info->mFileName;
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp)
            continue;

        SpcHeader header = {};
        if (fread(&header, sizeof(header), 1, fp) != 1 || header.mMagic != SPC_MAGIC || header.mVersion != SPC_VERSION)
        {
            fclose(fp);
            continue;
        }

        SpriteCacheSlot* slot = AllocatePlantSlot(st, header);
        if (!slot)
        {
            fclose(fp);
            continue;
        }

        if (fread(slot->mPixelData, 1, header.mTotalDataBytes, fp) == header.mTotalDataBytes)
        {
            slot->mIsReady = true;
        }
        else
        {
            slot->mIsReady = false;
        }
        fclose(fp);
    }

    bool aZombieNeeded[ZombieType::NUM_ZOMBIE_TYPES] = {};
    for (int z = 0; z < ZombieType::NUM_ZOMBIE_TYPES; ++z)
    {
        if (theBoard->mZombieAllowed[z])
        {
            aZombieNeeded[z] = true;
        }
    }
    for (Zombie* aZombie : theBoard->mZombies)
    {
        if (aZombie && !aZombie->mDead && aZombie->mZombieType >= 0 && aZombie->mZombieType < ZombieType::NUM_ZOMBIE_TYPES)
        {
            aZombieNeeded[aZombie->mZombieType] = true;
        }
    }

    for (int z = 0; z < ZombieType::NUM_ZOMBIE_TYPES; ++z)
    {
        if (!aZombieNeeded[z])
            continue;

        ZombieType zt = static_cast<ZombieType>(z);
        const SpriteCacheEntityInfo* info = SpriteCache_GetZombieEntityInfo(zt);
        if (!info)
            continue;

        if (IsZombieReady(zt))
            continue;

        std::string path = std::string(SPC_CACHE_DIR) + "/" + info->mFileName;
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp)
            continue;

        SpcHeader header = {};
        if (fread(&header, sizeof(header), 1, fp) != 1 || header.mMagic != SPC_MAGIC || header.mVersion != SPC_VERSION)
        {
            fclose(fp);
            continue;
        }

        SpriteCacheSlot* slot = AllocateZombieSlot(zt, header);
        if (!slot)
        {
            fclose(fp);
            continue;
        }

        if (fread(slot->mPixelData, 1, header.mTotalDataBytes, fp) == header.mTotalDataBytes)
        {
            slot->mIsReady = true;
        }
        else
        {
            slot->mIsReady = false;
        }
        fclose(fp);
    }

    PvzpLogLn("[SpriteCachePool] PreloadSynchronous complete. Plant offset: {}, Zombie offset: {}",
              (uint32_t)sPlantArenaOffset, (uint32_t)sZombieArenaOffset);
}
