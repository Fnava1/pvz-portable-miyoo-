/*
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of PvZ-Portable.
 *
 * PvZ-Portable is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PvZ-Portable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with PvZ-Portable. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __REANIMATORCACHE_H__
#define __REANIMATORCACHE_H__

#include "../../ConstEnums.h"
#include "../../PvzpLib/PvzpList.h"
#include <array>
#include <vector>
#include <memory>
#include "graphics/MemoryImage.h"

namespace Sexy
{
	class Graphics;
};
using namespace Sexy;

class LawnApp;
class ZombieDrawPosition;
class ReanimCacheImageVariation
{
public:
	SeedType                mSeedType;
	DrawVariation           mDrawVariation;
	MemoryImage*            mImage;
};
typedef PvzpList<ReanimCacheImageVariation> ImageVariationList;

class Reanimation;
class ReanimatorCache
{
public:
	static constexpr int NUM_CACHED_IDLE_FRAMES = 16;
	std::array<std::unique_ptr<MemoryImage>, SeedType::NUM_SEED_TYPES> mPlantImages;
	std::array<std::vector<std::unique_ptr<MemoryImage>>, SeedType::NUM_SEED_TYPES> mPlantIdleFrames;
	ImageVariationList      mImageVariationList;
	std::array<std::unique_ptr<MemoryImage>, LawnMowerType::NUM_MOWER_TYPES> mLawnMowers;
	std::array<std::unique_ptr<MemoryImage>, ZombieType::NUM_CACHED_ZOMBIE_TYPES> mZombieImages;
	static constexpr int NUM_CACHED_ZOMBIE_WALK_FRAMES = 12;
	std::array<std::vector<std::unique_ptr<MemoryImage>>, ZombieType::NUM_CACHED_ZOMBIE_TYPES> mZombieWalkFrames;
	std::unique_ptr<MemoryImage> mPumpkinBackImage;
	std::unique_ptr<MemoryImage> mPumpkinFrontImages[3]; // 0: Intact, 1: Damage1, 2: Damage3
	LawnApp*                mApp;

public:
	~ReanimatorCache();

	void                    ReanimatorCacheInitialize();
	void                    ReanimatorCacheDispose();
	void                    ClearLevelTransientCaches();
	void                    PrecacheAll();
	void                    PrecachePumpkinSurfaces();
	void                    DrawCachedPumpkinBack(Graphics* g, float thePosX, float thePosY);
	void                    DrawCachedPumpkinFront(Graphics* g, float thePosX, float thePosY, int theDamageState);
	bool                    HasCachedIdle(SeedType theSeedType);
	void                    PrecachePlantIdle(SeedType theSeedType);
	void                    DrawCachedPlant(Graphics* g, float thePosX, float thePosY, SeedType theSeedType, DrawVariation theDrawVariation);
	void                    DrawCachedPlantIdle(Graphics* g, float thePosX, float thePosY, SeedType theSeedType, float theAnimTime);
	void                    DrawCachedMower(Graphics* g, float thePosX, float thePosY, LawnMowerType theMowerType);
	void                    DrawCachedZombie(Graphics* g, float thePosX, float thePosY, ZombieType theZombieType);
	bool                    HasCachedZombieWalk(ZombieType theZombieType);
	void                    PrecacheZombieWalk(ZombieType theZombieType);
	std::unique_ptr<MemoryImage> MakeCachedZombieWalkFrame(ZombieType theZombieType, float theAnimTime);
	void                    DrawCachedZombieWalk(Graphics* g, const ZombieDrawPosition& theDrawPos, ZombieType theZombieType, float theAnimTime);
	std::unique_ptr<MemoryImage> MakeBlankMemoryImage(int theWidth, int theHeight);
	std::unique_ptr<MemoryImage> MakeCachedPlantFrame(SeedType theSeedType, DrawVariation theDrawVariation);
	std::unique_ptr<MemoryImage> MakeCachedPlantIdleFrame(SeedType theSeedType, float theAnimTime);
	std::unique_ptr<MemoryImage> MakeCachedMowerFrame(LawnMowerType theMowerType);
	std::unique_ptr<MemoryImage> MakeCachedZombieFrame(ZombieType theZombieType);
	void         GetPlantImageSize(SeedType theSeedType, int& theOffsetX, int& theOffsetY, int& theWidth, int& theHeight);
	void                    DrawReanimatorFrame(Graphics* g, float thePosX, float thePosY, ReanimationType theReanimationType, const char* theTrackName, DrawVariation theDrawVariation);
	void                    UpdateReanimationForVariation(Reanimation* theReanim, DrawVariation theDrawVariation);
};

#endif
