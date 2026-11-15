#ifndef __SPRITECACHEBAKER_H__
#define __SPRITECACHEBAKER_H__

#include "SpriteCacheDef.h"

class LawnApp;

class SpriteCacheBaker
{
public:
    static bool IsCacheValid();
    static bool BakeAll(LawnApp* theApp);
    static bool BakePlant(LawnApp* theApp, SeedType theSeed, const char* theFileName);
    static bool BakeZombie(LawnApp* theApp, ZombieType theZombie, const char* theFileName);
};

#endif // __SPRITECACHEBAKER_H__
