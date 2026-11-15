#ifndef __SPRITESTREAMER_H__
#define __SPRITESTREAMER_H__

#include "SpriteCacheDef.h"
#include "SpriteCachePool.h"
#include <queue>
#include <string>
#include <cstdio>

struct StreamRequest
{
    bool                mIsPlant;
    int                 mId;
    FILE*               mFile;
    uint32_t            mTotalFrames;
    uint32_t            mCurrentFrame;
    uint32_t            mFrameSizeBytes;
    SpriteCacheSlot*    mSlot;
};

class SpriteStreamer
{
public:
    static bool RequestPlant(SeedType theSeed);
    static bool RequestZombie(ZombieType theZombie);

    static void Update();

    static bool IsStreaming();
    static bool IsStreamingItem(bool theIsPlant, int theId);

    static void CancelAll();
    static void CancelZombies();
    static void ResetFailedRequests();
};

#endif // __SPRITESTREAMER_H__
