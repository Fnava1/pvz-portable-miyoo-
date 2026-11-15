#ifndef __SPRITECACHEPOOL_H__
#define __SPRITECACHEPOOL_H__

#include "SpriteCacheDef.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Sexy {
    class Graphics;
}
using namespace Sexy;

class Board;

struct SpriteCacheSlot
{
    int         mId = -1;             // SeedType or ZombieType (-1 if unused)
    bool        mIsReady = false;     // Ready for rendering
    uint16_t    mWidth = 0;          // Frame width
    uint16_t    mHeight = 0;         // Frame height
    int16_t     mOffsetX = 0;        // Draw offset X
    int16_t     mOffsetY = 0;        // Draw offset Y
    uint16_t    mFrameCount = 0;     // Total frames (24)
    uint32_t    mFrameSizeBytes = 0; // mWidth * mHeight * 2
    uint8_t*    mPixelData = nullptr;// Pointer into contiguous pool buffer
    uint32_t    mAllocatedBytes = 0; // Total bytes in pool

    void Reset()
    {
        mId = -1;
        mIsReady = false;
        mWidth = 0;
        mHeight = 0;
        mOffsetX = 0;
        mOffsetY = 0;
        mFrameCount = 0;
        mFrameSizeBytes = 0;
        mPixelData = nullptr;
        mAllocatedBytes = 0;
    }
};

struct SpriteCacheTelemetry
{
    union {
        uint32_t mHitsNormal;
        uint32_t hitsNormal;
    };
    union {
        uint32_t mHitsChilled;
        uint32_t hitsChilled;
    };
    union {
        uint32_t mHitsTotal;
        uint32_t hitsTotal;
        uint32_t mHits;
        uint32_t hits;
    };
    union {
        uint32_t mMissesDamaged;
        uint32_t missesDamaged;
    };
    union {
        uint32_t mMissesAccessories;
        uint32_t missesAccessories;
    };
    union {
        uint32_t mMissesEating;
        uint32_t missesEating;
    };
    union {
        uint32_t mMissesTotal;
        uint32_t missesTotal;
        uint32_t mMisses;
        uint32_t misses;
    };
    union {
        uint32_t mActiveVanguards;
        uint32_t activeVanguards;
    };
    union {
        uint32_t mPumpkinCacheHits;
        uint32_t pumpkinCacheHits;
    };

    SpriteCacheTelemetry()
        : mHitsNormal(0), mHitsChilled(0), mHitsTotal(0),
          mMissesDamaged(0), mMissesAccessories(0), mMissesEating(0), mMissesTotal(0),
          mActiveVanguards(0), mPumpkinCacheHits(0)
    {
    }

    uint32_t GetHitTotal() const { return mHitsNormal + mHitsChilled; }
    uint32_t GetMissTotal() const { return mMissesDamaged + mMissesAccessories + mMissesEating; }
};

class SpriteCachePool
{
public:
    static constexpr size_t POOL_MAX_BUDGET = 12 * 1024 * 1024;
    static constexpr size_t POOL_CAPACITY = POOL_MAX_BUDGET;
    static constexpr size_t PLANT_ZONE_SIZE = 4 * 1024 * 1024;
    static constexpr size_t ZOMBIE_ZONE_SIZE = 8 * 1024 * 1024;

    static void InitPool();
    static void FreePool();
    static bool IsPoolInitialized();

    static void InitLevelArena();
    static void FreeLevelArena();
    static void FreeSlot(SpriteCacheSlot* theSlot);

    static SpriteCacheSlot* GetPlantSlot(SeedType theSeed);
    static SpriteCacheSlot* GetZombieSlot(ZombieType theZombie);

    static bool IsPlantReady(SeedType theSeed);
    static bool IsZombieReady(ZombieType theZombie);

    static SpriteCacheSlot* AllocatePlantSlot(SeedType theSeed, const SpcHeader& theHeader);
    static SpriteCacheSlot* AllocateZombieSlot(ZombieType theZombie, const SpcHeader& theHeader);

    static void ClearZombieSlots();
    static void ClearPlantSlots();
    static void ClearAllSlots();
    static void PreloadSynchronous(Board* theBoard);

    static void DrawFrame(Graphics* g, float thePosX, float thePosY, const SpriteCacheSlot* theSlot, int theFrame, bool theIsChilled = false);
    static bool DrawPlant(Graphics* g, float thePosX, float thePosY, SeedType theSeed, float theAnimTime);
    static bool DrawZombie(Graphics* g, float thePosX, float thePosY, ZombieType theZombie, float theAnimTime, bool theIsChilled = false);

    static size_t GetTotalMemoryUsage();
    static size_t GetPoolCapacity() { return POOL_CAPACITY; }

    static size_t GetTotalAllocatedBytes();
    static size_t GetPeakAllocatedBytes();
    static void GetActiveSlotCounts(int& thePlants, int& theZombies);
    static uint32_t GetReanimFallbackCount();
    static uint32_t GetCacheHitCount();
    static SpriteCacheTelemetry& GetTelemetry();
};

#endif // __SPRITECACHEPOOL_H__
