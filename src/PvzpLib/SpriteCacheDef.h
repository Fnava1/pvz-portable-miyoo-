#ifndef __SPRITECACHEDEF_H__
#define __SPRITECACHEDEF_H__

#include <cstdint>
#include <string>
#include "../ConstEnums.h"

#define SPC_MAGIC 0x53504331 // 'S', 'P', 'C', '1'
#define SPC_VERSION 2
#define SPC_FORMAT_ARGB4444 1
#define SPC_TOTAL_FRAMES 24

#define SPC_MAX_PLANT_SLOTS 12
#define SPC_MAX_ZOMBIE_SLOTS 11

#define SPC_CACHE_DIR "cached_sprites"
#define SPC_VERSION_FILE "cached_sprites/.version"

#pragma pack(push, 1)
struct SpcHeader
{
    uint32_t mMagic;          // SPC_MAGIC (0x53504331)
    uint16_t mVersion;        // SPC_VERSION (1)
    uint16_t mFormat;         // SPC_FORMAT_ARGB4444 (1)
    uint16_t mWidth;          // Width of each frame in pixels
    uint16_t mHeight;         // Height of each frame in pixels
    int16_t  mOffsetX;        // Draw offset X relative to entity base
    int16_t  mOffsetY;        // Draw offset Y relative to entity base
    uint16_t mFrameCount;     // Number of frames (24)
    uint32_t mFrameSizeBytes; // mWidth * mHeight * 2
    uint32_t mTotalDataBytes; // mFrameSizeBytes * mFrameCount
    uint8_t  mReserved[6];    // 32-byte header alignment
};
#pragma pack(pop)

static_assert(sizeof(SpcHeader) == 32, "SpcHeader must be exactly 32 bytes");

inline uint16_t ARGB8888_To_ARGB4444(uint32_t c)
{
    uint32_t a = (c >> 28) & 0x0F;
    uint32_t r = (c >> 20) & 0x0F;
    uint32_t g = (c >> 12) & 0x0F;
    uint32_t b = (c >> 4)  & 0x0F;
    return (uint16_t)((a << 12) | (r << 8) | (g << 4) | b);
}

inline uint32_t ARGB4444_To_ARGB8888(uint16_t c)
{
    uint32_t a = (c >> 12) & 0x0F;
    uint32_t r = (c >> 8)  & 0x0F;
    uint32_t g = (c >> 4)  & 0x0F;
    uint32_t b = c         & 0x0F;
    return ((a | (a << 4)) << 24) |
           ((r | (r << 4)) << 16) |
           ((g | (g << 4)) << 8)  |
           (b | (b << 4));
}

struct SpriteCacheEntityInfo
{
    int         mId;
    const char* mFileName;
    int         mWidth;
    int         mHeight;
    int         mOffsetX;
    int         mOffsetY;
};

inline const SpriteCacheEntityInfo gPlantCacheEntities[] = {
    { SeedType::SEED_SUNFLOWER,     "plant_sunflower.spc",     120, 120, -20, -20 },
    { SeedType::SEED_TWINSUNFLOWER, "plant_twinsunflower.spc", 120, 120, -20, -20 },
    { SeedType::SEED_PEASHOOTER,    "plant_peashooter.spc",    120, 120, -20, -20 },
    { SeedType::SEED_REPEATER,      "plant_repeater.spc",      120, 120, -20, -20 },
    { SeedType::SEED_GATLINGPEA,    "plant_gatlingpea.spc",    120, 120, -20, -20 },
    { SeedType::SEED_SNOWPEA,       "plant_snowpea.spc",       120, 120, -20, -20 },
    { SeedType::SEED_WALLNUT,       "plant_wallnut.spc",       120, 120, -20, -20 },
    { SeedType::SEED_TALLNUT,       "plant_tallnut.spc",       120, 160, -20, -40 },
    { SeedType::SEED_PUFFSHROOM,    "plant_puffshroom.spc",    120, 120, -20, -20 },
    { SeedType::SEED_FUMESHROOM,    "plant_fumeshroom.spc",    120, 120, -20, -20 },
    { SeedType::SEED_THREEPEATER,   "plant_threepeater.spc",   150, 140, -35, -25 },
};
constexpr size_t gNumPlantCacheEntities = sizeof(gPlantCacheEntities) / sizeof(gPlantCacheEntities[0]);

inline const SpriteCacheEntityInfo gZombieCacheEntities[] = {
    { ZombieType::ZOMBIE_NORMAL,       "zombie_normal.spc",       140, 170, 0, 0 },
    { ZombieType::ZOMBIE_FLAG,         "zombie_flag.spc",         140, 170, 0, 0 },
    { ZombieType::ZOMBIE_TRAFFIC_CONE, "zombie_cone.spc",         140, 170, 0, 0 },
    { ZombieType::ZOMBIE_PAIL,         "zombie_pail.spc",         140, 170, 0, 0 },
    { ZombieType::ZOMBIE_DOOR,         "zombie_door.spc",         140, 170, 0, 0 },
    { ZombieType::ZOMBIE_FOOTBALL,     "zombie_football.spc",     150, 170, 0, 0 },
    { ZombieType::ZOMBIE_NEWSPAPER,    "zombie_newspaper.spc",    140, 170, 0, 0 },
    { ZombieType::ZOMBIE_DUCKY_TUBE,   "zombie_ducky_tube.spc",   140, 170, 0, 0 },
    { ZombieType::ZOMBIE_SNORKEL,      "zombie_snorkel.spc",      140, 170, 0, 0 },
    { ZombieType::ZOMBIE_POLEVAULTER,  "zombie_polevaulter.spc",  160, 170, 0, 0 },
};
constexpr size_t gNumZombieCacheEntities = sizeof(gZombieCacheEntities) / sizeof(gZombieCacheEntities[0]);

inline const SpriteCacheEntityInfo* SpriteCache_GetPlantEntityInfo(SeedType theSeed)
{
    for (size_t i = 0; i < gNumPlantCacheEntities; i++)
    {
        if (gPlantCacheEntities[i].mId == theSeed)
            return &gPlantCacheEntities[i];
    }
    return nullptr;
}

inline const SpriteCacheEntityInfo* SpriteCache_GetZombieEntityInfo(ZombieType theZombie)
{
    for (size_t i = 0; i < gNumZombieCacheEntities; i++)
    {
        if (gZombieCacheEntities[i].mId == theZombie)
            return &gZombieCacheEntities[i];
    }
    return nullptr;
}

#endif // __SPRITECACHEDEF_H__
