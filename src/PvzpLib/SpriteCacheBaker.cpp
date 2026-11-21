#include "SpriteCacheBaker.h"
#include "SpriteCacheDef.h"
#include "EffectSystem.h"
#include "../LawnApp.h"
#include "../Lawn/System/ReanimationLawn.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fs = std::filesystem;

bool SpriteCacheBaker::IsCacheValid()
{
    if (!fs::exists(SPC_VERSION_FILE))
        return false;

    std::ifstream vfile(SPC_VERSION_FILE);
    if (!vfile.is_open())
        return false;

    int ver = 0;
    vfile >> ver;
    if (ver != SPC_VERSION)
        return false;

    // Check all plant cache files
    for (size_t i = 0; i < gNumPlantCacheEntities; i++)
    {
        std::string path = std::string(SPC_CACHE_DIR) + "/" + gPlantCacheEntities[i].mFileName;
        if (!fs::exists(path))
            return false;

        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        if (ec || sz <= sizeof(SpcHeader))
            return false;
    }

    // Check all zombie cache files
    for (size_t i = 0; i < gNumZombieCacheEntities; i++)
    {
        std::string path = std::string(SPC_CACHE_DIR) + "/" + gZombieCacheEntities[i].mFileName;
        if (!fs::exists(path))
            return false;

        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        if (ec || sz <= sizeof(SpcHeader))
            return false;
    }

    return true;
}

bool SpriteCacheBaker::BakePlant(LawnApp* theApp, SeedType theSeed, const char* theFileName)
{
    if (!theApp || !theApp->mReanimatorCache)
        return false;

    int aDefOffsetX = -20, aDefOffsetY = -20, aDefWidth = 120, aDefHeight = 120;
    theApp->mReanimatorCache->GetPlantImageSize(theSeed, aDefOffsetX, aDefOffsetY, aDefWidth, aDefHeight);

    std::vector<std::unique_ptr<MemoryImage>> frames;
    frames.reserve(SPC_TOTAL_FRAMES);

    for (int i = 0; i < SPC_TOTAL_FRAMES; i++)
    {
        float animTime = (float)i / (float)SPC_TOTAL_FRAMES;
        auto img = theApp->mReanimatorCache->MakeCachedPlantIdleFrame(theSeed, animTime);
        if (!img)
            return false;
        frames.push_back(std::move(img));
    }

    int canvasW = aDefWidth;
    int canvasH = aDefHeight;

    // Compute union bounding box of non-zero pixels
    int minX = canvasW, minY = canvasH, maxX = -1, maxY = -1;
    for (int f = 0; f < SPC_TOTAL_FRAMES; f++)
    {
        const uint32_t* bits = frames[f]->GetBits();
        for (int y = 0; y < canvasH; y++)
        {
            for (int x = 0; x < canvasW; x++)
            {
                uint32_t p = bits[y * canvasW + x];
                if ((p >> 24) > 0)
                {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
    }

    if (maxX < minX || maxY < minY)
    {
        minX = 0;
        minY = 0;
        maxX = canvasW - 1;
        maxY = canvasH - 1;
    }

    // Add 1 pixel margin, clamped
    minX = std::max(0, minX - 1);
    minY = std::max(0, minY - 1);
    maxX = std::min(canvasW - 1, maxX + 1);
    maxY = std::min(canvasH - 1, maxY + 1);

    // Keep even width for 32-bit/SIMD alignment
    int cropW = maxX - minX + 1;
    if (cropW % 2 != 0)
    {
        if (maxX + 1 < canvasW) maxX++;
        else if (minX > 0) minX--;
        cropW = maxX - minX + 1;
    }
    int cropH = maxY - minY + 1;

    SpcHeader header = {};
    header.mMagic = SPC_MAGIC;
    header.mVersion = SPC_VERSION;
    header.mFormat = SPC_FORMAT_ARGB4444;
    header.mWidth = (uint16_t)cropW;
    header.mHeight = (uint16_t)cropH;
    header.mOffsetX = (int16_t)(aDefOffsetX + minX);
    header.mOffsetY = (int16_t)(aDefOffsetY + minY);
    header.mFrameCount = SPC_TOTAL_FRAMES;
    header.mFrameSizeBytes = cropW * cropH * sizeof(uint16_t);
    header.mTotalDataBytes = header.mFrameSizeBytes * SPC_TOTAL_FRAMES;

    std::vector<uint16_t> dest16(cropW * cropH * SPC_TOTAL_FRAMES);

    for (int f = 0; f < SPC_TOTAL_FRAMES; f++)
    {
        const uint32_t* srcBits = frames[f]->GetBits();
        uint16_t* dstFrame = &dest16[f * cropW * cropH];

        for (int y = 0; y < cropH; y++)
        {
            for (int x = 0; x < cropW; x++)
            {
                uint32_t p = srcBits[(minY + y) * canvasW + (minX + x)];
                dstFrame[y * cropW + x] = ARGB8888_To_ARGB4444(p);
            }
        }
    }

    std::string outPath = std::string(SPC_CACHE_DIR) + "/" + theFileName;
    FILE* fp = fopen(outPath.c_str(), "wb");
    if (!fp)
        return false;

    fwrite(&header, sizeof(header), 1, fp);
    fwrite(dest16.data(), 1, header.mTotalDataBytes, fp);
    fclose(fp);

    return true;
}

bool SpriteCacheBaker::BakeZombie(LawnApp* theApp, ZombieType theZombie, const char* theFileName)
{
    if (!theApp || !theApp->mReanimatorCache)
        return false;

    int canvasW = 180;
    int canvasH = 200;
    float aBaseX = 60.0f;
    float aBaseY = 60.0f;
    int baseOffsetX = (int)(15.0f - aBaseX); // -45
    int baseOffsetY = (int)(-8.0f - aBaseY); // -68

    std::vector<std::unique_ptr<MemoryImage>> frames;
    frames.reserve(SPC_TOTAL_FRAMES);

    for (int i = 0; i < SPC_TOTAL_FRAMES; i++)
    {
        float animTime = (float)i / (float)SPC_TOTAL_FRAMES;
        auto img = theApp->mReanimatorCache->MakeCachedZombieWalkFrame(theZombie, animTime);
        if (!img)
            return false;
        frames.push_back(std::move(img));
    }

    // Compute union bounding box of non-zero pixels
    int minX = canvasW, minY = canvasH, maxX = -1, maxY = -1;
    for (int f = 0; f < SPC_TOTAL_FRAMES; f++)
    {
        const uint32_t* bits = frames[f]->GetBits();
        for (int y = 0; y < canvasH; y++)
        {
            for (int x = 0; x < canvasW; x++)
            {
                uint32_t p = bits[y * canvasW + x];
                if ((p >> 24) > 0)
                {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
    }

    if (maxX < minX || maxY < minY)
    {
        minX = 0;
        minY = 0;
        maxX = canvasW - 1;
        maxY = canvasH - 1;
    }

    minX = std::max(0, minX - 1);
    minY = std::max(0, minY - 1);
    maxX = std::min(canvasW - 1, maxX + 1);
    maxY = std::min(canvasH - 1, maxY + 1);

    int cropW = maxX - minX + 1;
    if (cropW % 2 != 0)
    {
        if (maxX + 1 < canvasW) maxX++;
        else if (minX > 0) minX--;
        cropW = maxX - minX + 1;
    }
    int cropH = maxY - minY + 1;

    SpcHeader header = {};
    header.mMagic = SPC_MAGIC;
    header.mVersion = SPC_VERSION;
    header.mFormat = SPC_FORMAT_ARGB4444;
    header.mWidth = (uint16_t)cropW;
    header.mHeight = (uint16_t)cropH;
    header.mOffsetX = (int16_t)(baseOffsetX + minX);
    header.mOffsetY = (int16_t)(baseOffsetY + minY);
    header.mFrameCount = SPC_TOTAL_FRAMES;
    header.mFrameSizeBytes = cropW * cropH * sizeof(uint16_t);
    header.mTotalDataBytes = header.mFrameSizeBytes * SPC_TOTAL_FRAMES;

    std::vector<uint16_t> dest16(cropW * cropH * SPC_TOTAL_FRAMES);

    for (int f = 0; f < SPC_TOTAL_FRAMES; f++)
    {
        const uint32_t* srcBits = frames[f]->GetBits();
        uint16_t* dstFrame = &dest16[f * cropW * cropH];

        for (int y = 0; y < cropH; y++)
        {
            for (int x = 0; x < cropW; x++)
            {
                uint32_t p = srcBits[(minY + y) * canvasW + (minX + x)];
                dstFrame[y * cropW + x] = ARGB8888_To_ARGB4444(p);
            }
        }
    }

    std::string outPath = std::string(SPC_CACHE_DIR) + "/" + theFileName;
    FILE* fp = fopen(outPath.c_str(), "wb");
    if (!fp)
        return false;

    fwrite(&header, sizeof(header), 1, fp);
    fwrite(dest16.data(), 1, header.mTotalDataBytes, fp);
    fclose(fp);

    return true;
}

bool SpriteCacheBaker::BakeAll(LawnApp* theApp)
{
    if (!theApp)
        return false;

    std::error_code ec;
    fs::create_directories(SPC_CACHE_DIR, ec);

    int total = (int)(gNumPlantCacheEntities + gNumZombieCacheEntities);
    theApp->mBakingTotalEntities.store(total);
    theApp->mBakingCurrentIndex.store(0);
    theApp->mIsBakingSpriteCache.store(true);

    int current = 0;
    for (size_t i = 0; i < gNumPlantCacheEntities; i++)
    {
        current++;
        theApp->mBakingCurrentIndex.store(current);
        SeedType st = (SeedType)gPlantCacheEntities[i].mId;
        const char* fn = gPlantCacheEntities[i].mFileName;
        if (!BakePlant(theApp, st, fn))
        {
            if (theApp->mEffectSystem) theApp->mEffectSystem->ProcessDeleteQueue();
            theApp->mIsBakingSpriteCache.store(false);
            return false;
        }
    }

    for (size_t i = 0; i < gNumZombieCacheEntities; i++)
    {
        current++;
        theApp->mBakingCurrentIndex.store(current);
        ZombieType zt = (ZombieType)gZombieCacheEntities[i].mId;
        const char* fn = gZombieCacheEntities[i].mFileName;
        if (!BakeZombie(theApp, zt, fn))
        {
            if (theApp->mEffectSystem) theApp->mEffectSystem->ProcessDeleteQueue();
            theApp->mIsBakingSpriteCache.store(false);
            return false;
        }
    }

    // Write .version marker
    std::ofstream vfile(SPC_VERSION_FILE);
    if (!vfile.is_open())
    {
        theApp->mIsBakingSpriteCache.store(false);
        return false;
    }

    vfile << SPC_VERSION << "\n";
    vfile.close();

    if (theApp->mEffectSystem)
    {
        theApp->mEffectSystem->ProcessDeleteQueue();
    }
    theApp->mIsBakingSpriteCache.store(false);
    return true;
}
