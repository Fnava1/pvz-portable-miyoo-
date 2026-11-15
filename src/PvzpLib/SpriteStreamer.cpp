#include "SpriteStreamer.h"
#include "PvzpDebug.h"
#include <deque>
#include <filesystem>
#include <algorithm>

static std::deque<StreamRequest> sQueue;
static bool sFailedPlants[SeedType::NUM_SEED_TYPES] = {};
static bool sFailedZombies[ZombieType::NUM_ZOMBIE_TYPES] = {};

void SpriteStreamer::ResetFailedRequests()
{
    std::fill(std::begin(sFailedPlants), std::end(sFailedPlants), false);
    std::fill(std::begin(sFailedZombies), std::end(sFailedZombies), false);
}

bool SpriteStreamer::IsStreaming()
{
    return !sQueue.empty();
}

bool SpriteStreamer::IsStreamingItem(bool theIsPlant, int theId)
{
    for (const auto& req : sQueue)
    {
        if (req.mIsPlant == theIsPlant && req.mId == theId)
            return true;
    }
    return false;
}

bool SpriteStreamer::RequestPlant(SeedType theSeed)
{
    if (theSeed < 0 || theSeed >= SeedType::NUM_SEED_TYPES)
        return false;

    if (sFailedPlants[theSeed])
        return false;

    if (SpriteCachePool::IsPlantReady(theSeed))
        return true;

    if (IsStreamingItem(true, theSeed))
        return true;

    const SpriteCacheEntityInfo* info = SpriteCache_GetPlantEntityInfo(theSeed);
    if (!info)
    {
        sFailedPlants[theSeed] = true;
        return false;
    }

    std::string path = std::string(SPC_CACHE_DIR) + "/" + info->mFileName;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        sFailedPlants[theSeed] = true;
        return false;
    }

    SpcHeader header = {};
    if (fread(&header, sizeof(header), 1, fp) != 1 || header.mMagic != SPC_MAGIC || header.mVersion != SPC_VERSION)
    {
        fclose(fp);
        sFailedPlants[theSeed] = true;
        return false;
    }

    SpriteCacheSlot* slot = SpriteCachePool::AllocatePlantSlot(theSeed, header);
    if (!slot)
    {
        fclose(fp);
        sFailedPlants[theSeed] = true;
        return false;
    }

    StreamRequest req = {};
    req.mIsPlant = true;
    req.mId = theSeed;
    req.mFile = fp;
    req.mTotalFrames = header.mFrameCount;
    req.mCurrentFrame = 0;
    req.mFrameSizeBytes = header.mFrameSizeBytes;
    req.mSlot = slot;

    sQueue.push_back(req);
    return true;
}

bool SpriteStreamer::RequestZombie(ZombieType theZombie)
{
    if (theZombie < 0 || theZombie >= ZombieType::NUM_ZOMBIE_TYPES)
        return false;

    if (sFailedZombies[theZombie])
        return false;

    if (SpriteCachePool::IsZombieReady(theZombie))
        return true;

    if (IsStreamingItem(false, theZombie))
        return true;

    const SpriteCacheEntityInfo* info = SpriteCache_GetZombieEntityInfo(theZombie);
    if (!info)
    {
        sFailedZombies[theZombie] = true;
        return false;
    }

    std::string path = std::string(SPC_CACHE_DIR) + "/" + info->mFileName;
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        sFailedZombies[theZombie] = true;
        return false;
    }

    SpcHeader header = {};
    if (fread(&header, sizeof(header), 1, fp) != 1 || header.mMagic != SPC_MAGIC || header.mVersion != SPC_VERSION)
    {
        fclose(fp);
        sFailedZombies[theZombie] = true;
        return false;
    }

    SpriteCacheSlot* slot = SpriteCachePool::AllocateZombieSlot(theZombie, header);
    if (!slot)
    {
        fclose(fp);
        sFailedZombies[theZombie] = true;
        // [SpriteStreamer] Pool budget of 14 MB saturated; Entity flagged uncacheable
        PvzpLogLn("[SpriteStreamer] Pool budget of 14 MB saturated; Entity {} flagged uncacheable", (int)theZombie);
        return false;
    }

    StreamRequest req = {};
    req.mIsPlant = false;
    req.mId = theZombie;
    req.mFile = fp;
    req.mTotalFrames = header.mFrameCount;
    req.mCurrentFrame = 0;
    req.mFrameSizeBytes = header.mFrameSizeBytes;
    req.mSlot = slot;

    sQueue.push_back(req);
    return true;
}

void SpriteStreamer::Update()
{
    if (sQueue.empty())
        return;

    StreamRequest& req = sQueue.front();
    if (!req.mFile || !req.mSlot || !req.mSlot->mPixelData)
    {
        if (req.mFile)
            fclose(req.mFile);
        if (req.mSlot)
            SpriteCachePool::FreeSlot(req.mSlot);
        sQueue.pop_front();
        return;
    }

    if (req.mCurrentFrame < req.mTotalFrames)
    {
        uint8_t* dst = req.mSlot->mPixelData + req.mCurrentFrame * req.mFrameSizeBytes;
        size_t readBytes = fread(dst, 1, req.mFrameSizeBytes, req.mFile);
        if (readBytes != req.mFrameSizeBytes)
        {
            fclose(req.mFile);
            req.mFile = nullptr;
            if (req.mIsPlant)
                sFailedPlants[req.mId] = true;
            else
                sFailedZombies[req.mId] = true;
            SpriteCachePool::FreeSlot(req.mSlot);
            req.mSlot = nullptr;
            sQueue.pop_front();
            return;
        }
        req.mCurrentFrame++;
    }

    if (req.mCurrentFrame >= req.mTotalFrames)
    {
        fclose(req.mFile);
        req.mFile = nullptr;
        req.mSlot->mIsReady = true;
        sQueue.pop_front();
    }
}

void SpriteStreamer::CancelAll()
{
    for (auto& req : sQueue)
    {
        if (req.mFile)
        {
            fclose(req.mFile);
            req.mFile = nullptr;
        }
        if (req.mSlot)
        {
            SpriteCachePool::FreeSlot(req.mSlot);
            req.mSlot = nullptr;
        }
    }
    sQueue.clear();
    std::fill(std::begin(sFailedPlants), std::end(sFailedPlants), false);
    std::fill(std::begin(sFailedZombies), std::end(sFailedZombies), false);
}

void SpriteStreamer::CancelZombies()
{
    for (size_t i = 0; i < sQueue.size(); )
    {
        if (!sQueue[i].mIsPlant)
        {
            if (sQueue[i].mFile)
            {
                fclose(sQueue[i].mFile);
                sQueue[i].mFile = nullptr;
            }
            if (sQueue[i].mSlot)
            {
                SpriteCachePool::FreeSlot(sQueue[i].mSlot);
                sQueue[i].mSlot = nullptr;
            }
            sQueue.erase(sQueue.begin() + i);
        }
        else
        {
            ++i;
        }
    }
    std::fill(std::begin(sFailedZombies), std::end(sFailedZombies), false);
}
