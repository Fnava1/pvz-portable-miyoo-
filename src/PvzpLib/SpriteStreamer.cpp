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
    (void)theSeed;
    return false;
}

bool SpriteStreamer::RequestZombie(ZombieType theZombie)
{
    (void)theZombie;
    return false;
}

void SpriteStreamer::Update()
{
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
