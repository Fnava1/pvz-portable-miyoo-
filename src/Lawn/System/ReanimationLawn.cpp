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

#include "../Plant.h"
#include "../Zombie.h"
#include "../../LawnApp.h"
#include "ReanimationLawn.h"
#include "../../PvzpLib/PvzpDebug.h"
#include "graphics/Color.h"
#include "../../PvzpLib/Reanimator.h"
#include "graphics/MemoryImage.h"
#include "../../PvzpLib/EffectSystem.h"
#include "../../Resources.h"
#include <algorithm>
#include <cmath>
#if defined(__linux__) && !defined(__ANDROID__)
#include <malloc.h>
#endif

void ReanimatorCache::UpdateReanimationForVariation(Reanimation* theReanim, DrawVariation theDrawVariation)
{
	if (theDrawVariation >= DrawVariation::VARIATION_MARIGOLD_WHITE && theDrawVariation <= DrawVariation::VARIATION_MARIGOLD_LIGHT_GREEN)
	{
		size_t aVariationIndex = static_cast<size_t>(theDrawVariation) - static_cast<size_t>(DrawVariation::VARIATION_MARIGOLD_WHITE);
		Color MARIGOLD_VARIATIONS[] = {
			Color(255, 255, 255),
			Color(230, 30, 195),
			Color(250, 125, 5),
			Color(255, 145, 215),
			Color(160, 255, 245),
			Color(230, 30, 30),
			Color(5, 130, 255),
			Color(195, 55, 235),
			Color(235, 210, 255),
			Color(255, 245, 55),
			Color(180, 255, 105)
		};

		PVZP_ASSERT(aVariationIndex >= 0 && aVariationIndex < LENGTH(MARIGOLD_VARIATIONS));
		theReanim->GetTrackInstanceByName("Marigold_petals")->mTrackColor = MARIGOLD_VARIATIONS[aVariationIndex];
	}
	else
	{
		switch (theDrawVariation)
		{
		case DrawVariation::VARIATION_IMITATER:
			theReanim->mFilterEffect = FilterEffect::FILTER_EFFECT_WASHED_OUT;
			break;
		case DrawVariation::VARIATION_IMITATER_LESS:
			theReanim->mFilterEffect = FilterEffect::FILTER_EFFECT_LESS_WASHED_OUT;
			break;
		case DrawVariation::VARIATION_ZEN_GARDEN:
			theReanim->SetFramesForLayer("anim_zengarden");
			break;
		case DrawVariation::VARIATION_ZEN_GARDEN_WATER:
			theReanim->SetFramesForLayer("anim_waterplants");
			break;
		case DrawVariation::VARIATION_AQUARIUM:
			theReanim->SetFramesForLayer("anim_idle_aquarium");
			break;
		case DrawVariation::VARIATION_SPROUT_NO_FLOWER:
			theReanim->SetFramesForLayer("anim_idle_noflower");
			break;
		default:
			PVZP_ASSERT(false);
			break;
		}
	}
}

void ReanimatorCache::DrawReanimatorFrame(Graphics* g, float thePosX, float thePosY, ReanimationType theReanimationType, const char* theTrackName, DrawVariation theDrawVariation)
{
	Reanimation aReanim;
	aReanim.ReanimationInitializeType(thePosX, thePosY, theReanimationType);

	if (theTrackName != nullptr && aReanim.TrackExists(theTrackName))
	{
		aReanim.SetFramesForLayer(theTrackName);
	}
	if (theReanimationType == ReanimationType::REANIM_KERNELPULT)
	{
		aReanim.AssignRenderGroupToTrack("Cornpult_butter", RENDER_GROUP_HIDDEN);
	}
	else if (theReanimationType == ReanimationType::REANIM_SUNFLOWER)
	{
		aReanim.mAnimTime = 0.15f;
	}
	aReanim.AssignRenderGroupToTrack("anim_waterline", RENDER_GROUP_HIDDEN);

	if (g->GetColorizeImages())
	{
		aReanim.mColorOverride = g->GetColor();
	}
	aReanim.OverrideScale(g->mScaleX, g->mScaleY);

	if (theDrawVariation != DrawVariation::VARIATION_NORMAL)
	{
		UpdateReanimationForVariation(&aReanim, theDrawVariation);
	}

	aReanim.Draw(g);
}

std::unique_ptr<MemoryImage> ReanimatorCache::MakeBlankMemoryImage(int theWidth, int theHeight)
{
	auto aImage = std::make_unique<MemoryImage>();

	int aBitsCount = theWidth * theHeight;
	aImage->mBits = std::make_unique<uint32_t[]>(aBitsCount + 1);
	aImage->mWidth = theWidth;
	aImage->mHeight = theHeight;
	aImage->mHasTrans = true;
	aImage->mHasAlpha = true;
	memset(aImage->mBits.get(), 0, aBitsCount * 4);
	aImage->mBits[aBitsCount] = Sexy::MEMORYCHECK_ID;
	return aImage;
}

void ReanimatorCache::GetPlantImageSize(SeedType theSeedType, int& theOffsetX, int& theOffsetY, int& theWidth, int& theHeight)
{
	theOffsetX = -20;
	theOffsetY = -20;
	theWidth = 120;
	theHeight = 120;

	if (theSeedType == SeedType::SEED_TALLNUT)
	{
		theOffsetY = -40;
		theHeight += 40;
	}
	else if (theSeedType == SeedType::SEED_MELONPULT || theSeedType == SeedType::SEED_WINTERMELON)
	{
		theOffsetX = -40;
		theWidth += 40;
	}
	else if (theSeedType == SeedType::SEED_COBCANNON)
	{
		theWidth += 80;
	}
	else if (theSeedType == SeedType::SEED_THREEPEATER)
	{
		theOffsetX = -35;
		theOffsetY = -25;
		theWidth = 150;
		theHeight = 140;
	}
}

std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedMowerFrame(LawnMowerType theMowerType)
{
	std::unique_ptr<MemoryImage> aImage;

	switch (theMowerType)
	{
	case LawnMowerType::LAWNMOWER_LAWN:
	{
		aImage = MakeBlankMemoryImage(90, 100);
		Graphics aMemoryGraphics(aImage.get());
		aMemoryGraphics.SetLinearBlend(true);
		aMemoryGraphics.mScaleX = 0.85f;
		aMemoryGraphics.mScaleY = 0.85f;
		DrawReanimatorFrame(&aMemoryGraphics, 10.0f, 0.0f, ReanimationType::REANIM_LAWNMOWER, "anim_normal", DrawVariation::VARIATION_NORMAL);
		break;
	}
	case LawnMowerType::LAWNMOWER_POOL:
	{
		aImage = MakeBlankMemoryImage(90, 100);
		Graphics aMemoryGraphics(aImage.get());
		aMemoryGraphics.SetLinearBlend(true);
		aMemoryGraphics.mScaleX = 0.8f;
		aMemoryGraphics.mScaleY = 0.8f;
		DrawReanimatorFrame(&aMemoryGraphics, 10.0f, 25.0f, ReanimationType::REANIM_POOL_CLEANER, nullptr, DrawVariation::VARIATION_NORMAL);
		break;
	}
	case LawnMowerType::LAWNMOWER_ROOF:
	{
		aImage = MakeBlankMemoryImage(90, 100);
		Graphics aMemoryGraphics(aImage.get());
		aMemoryGraphics.SetLinearBlend(true);
		aMemoryGraphics.mScaleX = 0.85f;
		aMemoryGraphics.mScaleY = 0.85f;
		DrawReanimatorFrame(&aMemoryGraphics, 10.0f, 0.0f, ReanimationType::REANIM_ROOF_CLEANER, nullptr, DrawVariation::VARIATION_NORMAL);
		break;
	}
	case LawnMowerType::LAWNMOWER_SUPER_MOWER:
	{
		aImage = MakeBlankMemoryImage(90, 100);
		Graphics aMemoryGraphics(aImage.get());
		aMemoryGraphics.SetLinearBlend(true);
		aMemoryGraphics.mScaleX = 0.85f;
		aMemoryGraphics.mScaleY = 0.85f;
		DrawReanimatorFrame(&aMemoryGraphics, 10.0f, 0.0f, ReanimationType::REANIM_LAWNMOWER, "anim_tricked", DrawVariation::VARIATION_NORMAL);
		break;
	}
	default:
		PVZP_ASSERT(false);
		break;
	}

	return aImage;
}

std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedPlantFrame(SeedType theSeedType, DrawVariation theDrawVariation)
{
	int aOffsetX, aOffsetY, aWidth, aHeight;
	GetPlantImageSize(theSeedType, aOffsetX, aOffsetY, aWidth, aHeight);
	auto aMemoryImage = MakeBlankMemoryImage(aWidth, aHeight);
	Graphics aMemoryGraphics(aMemoryImage.get());
	aMemoryGraphics.SetLinearBlend(true);

	const PlantDefinition& aPlantDef = GetPlantDefinition(theSeedType);
	//PVZP_ASSERT(aPlantDef.mReanimationType != ReanimationType::REANIM_NONE);

	if (theSeedType == SeedType::SEED_POTATOMINE)
	{
		aMemoryGraphics.mScaleX = 0.85f;
		aMemoryGraphics.mScaleY = 0.85f;
		DrawReanimatorFrame(&aMemoryGraphics, -static_cast<int>(aOffsetX - 12.0f), -static_cast<int>(aOffsetY - 12.0f), aPlantDef.mReanimationType, "anim_armed", theDrawVariation);
	}
	else if (theSeedType == SeedType::SEED_INSTANT_COFFEE)
	{
		aMemoryGraphics.mScaleX = 0.8f;
		aMemoryGraphics.mScaleY = 0.8f;
		DrawReanimatorFrame(&aMemoryGraphics, -static_cast<int>(aOffsetX - 12.0f), -static_cast<int>(aOffsetY - 12.0f), aPlantDef.mReanimationType, "anim_idle", theDrawVariation);
	}
	else if (theSeedType == SeedType::SEED_EXPLODE_O_NUT)
	{
		aMemoryGraphics.SetColorizeImages(true);
		aMemoryGraphics.SetColor(Color(255, 64, 64));
		DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_idle", theDrawVariation);
	}
	else
	{
		DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_idle", theDrawVariation);

		if (theSeedType == SeedType::SEED_PEASHOOTER || theSeedType == SeedType::SEED_SNOWPEA || theSeedType == SeedType::SEED_REPEATER ||
			theSeedType == SeedType::SEED_LEFTPEATER || theSeedType == SeedType::SEED_GATLINGPEA)
		{
			DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_head_idle", theDrawVariation);
		}
		else if (theSeedType == SeedType::SEED_SPLITPEA)
		{
			DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_head_idle", theDrawVariation);
			DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_splitpea_idle", theDrawVariation);
		}
		else if (theSeedType == SeedType::SEED_THREEPEATER)
		{
			DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_head_idle1", theDrawVariation);
			DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_head_idle3", theDrawVariation);
			DrawReanimatorFrame(&aMemoryGraphics, -aOffsetX, -aOffsetY, aPlantDef.mReanimationType, "anim_head_idle2", theDrawVariation);
		}
	}

	return aMemoryImage;
}

std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedZombieFrame(ZombieType theZombieType)
{
	auto aMemoryImage = MakeBlankMemoryImage(200, 210);
	Graphics aMemoryGraphics(aMemoryImage.get());
	aMemoryGraphics.SetLinearBlend(true);

	ZombieType aUseZombieType = theZombieType;
	if (theZombieType == ZombieType::ZOMBIE_CACHED_POLEVAULTER_WITH_POLE)
	{
		aUseZombieType = ZombieType::ZOMBIE_POLEVAULTER;
	}
	const ZombieDefinition& aZombieDef = GetZombieDefinition(aUseZombieType);
	PVZP_ASSERT(aZombieDef.mReanimationType != ReanimationType::REANIM_NONE);

	float aPosX = 40.0f, aPosY = 40.0f;
	if (aZombieDef.mReanimationType == ReanimationType::REANIM_ZOMBIE)
	{
		Reanimation aReanim;
		aReanim.ReanimationInitializeType(aPosX, aPosY, aZombieDef.mReanimationType);
		aReanim.SetFramesForLayer("anim_idle");
		Zombie::SetupReanimLayers(&aReanim, aUseZombieType);

		if (theZombieType == ZombieType::ZOMBIE_DOOR)
			aReanim.AssignRenderGroupToTrack("anim_screendoor", RENDER_GROUP_NORMAL);
		else if (theZombieType == ZombieType::ZOMBIE_FLAG)
		{
			Reanimation aReanimFlag;
			aReanimFlag.ReanimationInitializeType(aPosX, aPosY, ReanimationType::REANIM_FLAG);
			aReanimFlag.SetFramesForLayer("Zombie_flag");
			aReanimFlag.Draw(&aMemoryGraphics);
		}
		aReanim.Draw(&aMemoryGraphics);
	}
	else if (aZombieDef.mReanimationType == ReanimationType::REANIM_BOSS)
	{
		Reanimation aReanim;
		aReanim.ReanimationInitializeType(-524.0f, -88.0f, aZombieDef.mReanimationType);
		aReanim.SetFramesForLayer("anim_head_idle");
		Reanimation aReanimDriver;
		aReanimDriver.ReanimationInitializeType(46.0f, 22.0f, ReanimationType::REANIM_BOSS_DRIVER);
		aReanimDriver.SetFramesForLayer("anim_idle");

		aReanim.Draw(&aMemoryGraphics);
		aReanimDriver.Draw(&aMemoryGraphics);
		aReanim.AssignRenderGroupToTrack("boss_body1", RENDER_GROUP_HIDDEN);
		aReanim.AssignRenderGroupToTrack("boss_neck", RENDER_GROUP_HIDDEN);
		aReanim.AssignRenderGroupToTrack("boss_head2", RENDER_GROUP_HIDDEN);
		aReanim.Draw(&aMemoryGraphics);
	}
	else
	{
		const char* aTrackName = "anim_idle";
		if (theZombieType == ZombieType::ZOMBIE_POGO)
		{
			aTrackName = "anim_pogo";
		}
		else if (theZombieType == ZombieType::ZOMBIE_CACHED_POLEVAULTER_WITH_POLE)
		{
			aTrackName = "anim_idle";
		}
		else if (theZombieType == ZombieType::ZOMBIE_POLEVAULTER)
		{
			aTrackName = "anim_walk";
		}
		else if (theZombieType == ZombieType::ZOMBIE_GARGANTUAR)
		{
			aPosY = 60.0f;
		}

		DrawReanimatorFrame(&aMemoryGraphics, aPosX, aPosY, aZombieDef.mReanimationType, aTrackName, DrawVariation::VARIATION_NORMAL);
	}
	return aMemoryImage;
}

ReanimatorCache::~ReanimatorCache()
{
	ReanimatorCacheDispose();
}

void ReanimatorCache::ReanimatorCacheInitialize()
{
	mApp = (LawnApp*)gSexyAppBase;
	PrecachePumpkinSurfaces();
}

void ReanimatorCache::ClearLevelTransientCaches()
{
	for (int i = 0; i < ZombieType::NUM_CACHED_ZOMBIE_TYPES; i++)
	{
		mZombieWalkFrames[i].clear();
		mZombieWalkFrames[i].shrink_to_fit();
	}
}

void ReanimatorCache::ReanimatorCacheDispose()
{
	while (mImageVariationList.mSize != 0)
	{
		ReanimCacheImageVariation aImageVariation = mImageVariationList.RemoveHead();
		if (aImageVariation.mImage != nullptr)
			delete aImageVariation.mImage;
	}
	for (int i = 0; i < SeedType::NUM_SEED_TYPES; i++)
	{
		mPlantImages[i].reset();
		mPlantIdleFrames[i].clear();
	}
	for (int i = 0; i < LawnMowerType::NUM_MOWER_TYPES; i++)
	{
		mLawnMowers[i].reset();
	}
	for (int i = 0; i < ZombieType::NUM_CACHED_ZOMBIE_TYPES; i++)
	{
		mZombieImages[i].reset();
		mZombieWalkFrames[i].clear();
	}
	mPumpkinBackImage.reset();
	for (int i = 0; i < 3; i++)
	{
		mPumpkinFrontImages[i].reset();
	}
}

void ReanimatorCache::PrecacheAll()
{
	for (int i = 0; i < SeedType::NUM_SEED_TYPES; i++)
	{
		SeedType aSeed = static_cast<SeedType>(i);
		const PlantDefinition& aDef = GetPlantDefinition(aSeed);
		if (aDef.mReanimationType != ReanimationType::REANIM_NONE)
		{
			ReanimatorEnsureDefinitionLoaded(aDef.mReanimationType, true);
			if (mPlantImages[aSeed] == nullptr)
			{
				mPlantImages[aSeed] = MakeCachedPlantFrame(aSeed, DrawVariation::VARIATION_NORMAL);
			}
		}
	}

	for (int i = 0; i < ZombieType::NUM_CACHED_ZOMBIE_TYPES; i++)
	{
		ZombieType aZombie = static_cast<ZombieType>(i);
		if (aZombie == ZombieType::ZOMBIE_INVALID)
			continue;
		ZombieType aUseZombieType = (aZombie == ZombieType::ZOMBIE_CACHED_POLEVAULTER_WITH_POLE) ? ZombieType::ZOMBIE_POLEVAULTER : aZombie;
		const ZombieDefinition& aDef = GetZombieDefinition(aUseZombieType);
		if (aDef.mReanimationType != ReanimationType::REANIM_NONE)
		{
			ReanimatorEnsureDefinitionLoaded(aDef.mReanimationType, true);
			if (mZombieImages[aZombie] == nullptr)
			{
				mZombieImages[aZombie] = MakeCachedZombieFrame(aZombie);
			}
		}
	}
}


void ReanimatorCache::DrawCachedPlant(Graphics* g, float thePosX, float thePosY, SeedType theSeedType, DrawVariation theDrawVariation)
{
	PVZP_ASSERT(theSeedType >= 0 && theSeedType < SeedType::NUM_SEED_TYPES);

	MemoryImage* aImage = nullptr;
	if (theDrawVariation != DrawVariation::VARIATION_NORMAL)
	{
		for (PvzpListNode<ReanimCacheImageVariation>* aNode = mImageVariationList.mHead; aNode != nullptr; aNode = aNode->mNext)
		{
			ReanimCacheImageVariation& aImageVariation = aNode->mValue;
			if (aImageVariation.mSeedType == theSeedType && aImageVariation.mDrawVariation == theDrawVariation)
			{
				aImage = aImageVariation.mImage;
				break;
			}
		}

		if (aImage == nullptr)
		{
			ReanimCacheImageVariation aNewImageVariation;
			aNewImageVariation.mSeedType = theSeedType;
			aNewImageVariation.mDrawVariation = theDrawVariation;
			aNewImageVariation.mImage = MakeCachedPlantFrame(theSeedType, theDrawVariation).release();
			aImage = aNewImageVariation.mImage;
			mImageVariationList.AddHead(aNewImageVariation);
		}
	}
	else
	{
		aImage = mPlantImages[theSeedType].get();
		if (aImage == nullptr)
		{
			mPlantImages[theSeedType] = MakeCachedPlantFrame(theSeedType, DrawVariation::VARIATION_NORMAL);
			aImage = mPlantImages[theSeedType].get();
		}
	}

	int aOffsetX, aOffsetY, aWidth, aHeight;
	GetPlantImageSize(theSeedType, aOffsetX, aOffsetY, aWidth, aHeight);
	if (!mApp->Is3DAccelerated() && g->mScaleX == 1.0f && g->mScaleY == 1.0f)
	{
		g->DrawImage(aImage, thePosX + aOffsetX, thePosY + aOffsetY);
	}
	else
	{
		PvzpDrawImageScaledF(g, aImage, thePosX + (aOffsetX * g->mScaleX), thePosY + (aOffsetY * g->mScaleY), g->mScaleX, g->mScaleY);
	}
}

void ReanimatorCache::DrawCachedMower(Graphics* g, float thePosX, float thePosY, LawnMowerType theMowerType)
{
	PVZP_ASSERT(theMowerType >= 0 && theMowerType < LawnMowerType::NUM_MOWER_TYPES);

	if (mLawnMowers[theMowerType] == nullptr)
		mLawnMowers[theMowerType] = MakeCachedMowerFrame(theMowerType);
	PvzpDrawImageScaledF(g, mLawnMowers[theMowerType].get(), thePosX - 20.0f, thePosY, g->mScaleX, g->mScaleY);
}

void ReanimatorCache::DrawCachedZombie(Graphics* g, float thePosX, float thePosY, ZombieType theZombieType)
{
	PVZP_ASSERT(theZombieType >= 0 && theZombieType < ZombieType::NUM_CACHED_ZOMBIE_TYPES);

	if (mZombieImages[theZombieType] == nullptr)
		mZombieImages[theZombieType] = MakeCachedZombieFrame(theZombieType);

	PvzpDrawImageScaledF(g, mZombieImages[theZombieType].get(), thePosX, thePosY, g->mScaleX, g->mScaleY);
}

bool ReanimatorCache::HasCachedIdle(SeedType theSeedType)
{
	switch (theSeedType)
	{
	case SeedType::SEED_SUNFLOWER:
	case SeedType::SEED_TWINSUNFLOWER:
		return true;
	default:
		return false;
	}
}

void ReanimatorCache::PrecachePlantIdle(SeedType theSeedType)
{
	if (theSeedType < 0 || theSeedType >= SeedType::NUM_SEED_TYPES)
		return;

	const PlantDefinition& aDef = GetPlantDefinition(theSeedType);
	if (aDef.mReanimationType == ReanimationType::REANIM_NONE)
		return;

	ReanimatorEnsureDefinitionLoaded(aDef.mReanimationType, true);

	auto& frames = mPlantIdleFrames[theSeedType];
	frames.clear();
	frames.reserve(NUM_CACHED_IDLE_FRAMES);

	for (int i = 0; i < NUM_CACHED_IDLE_FRAMES; i++)
	{
		float animTime = (float)i / (float)NUM_CACHED_IDLE_FRAMES;
		frames.push_back(MakeCachedPlantIdleFrame(theSeedType, animTime));
	}
}

std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedPlantIdleFrame(SeedType theSeedType, float theAnimTime)
{
	int aOffsetX, aOffsetY, aWidth, aHeight;
	GetPlantImageSize(theSeedType, aOffsetX, aOffsetY, aWidth, aHeight);
	auto aMemoryImage = MakeBlankMemoryImage(aWidth, aHeight);
	Graphics aMemoryGraphics(aMemoryImage.get());
	aMemoryGraphics.SetLinearBlend(true);

	const PlantDefinition& aPlantDef = GetPlantDefinition(theSeedType);
	if (aPlantDef.mReanimationType == ReanimationType::REANIM_NONE)
		return aMemoryImage;

	ReanimatorEnsureDefinitionLoaded(aPlantDef.mReanimationType, true);

	std::vector<ReanimationID> aHeadReanimIDs;

	{
		Reanimation aBodyReanim;
		aBodyReanim.ReanimationInitializeType(-aOffsetX, -aOffsetY, aPlantDef.mReanimationType);
		aBodyReanim.mIsAttachment = true;
		if (aBodyReanim.TrackExists("anim_idle"))
		{
			aBodyReanim.SetFramesForLayer("anim_idle");
		}
		aBodyReanim.mLoopType = ReanimLoopType::REANIM_LOOP;
		aBodyReanim.mAnimRate = 0.0f;
		aBodyReanim.mAnimTime = theAnimTime;

		if (theSeedType == SeedType::SEED_THREEPEATER)
		{
			Reanimation* aHead1 = mApp->AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_THREEPEATER);
			aHead1->mLoopType = ReanimLoopType::REANIM_LOOP;
			aHead1->mAnimRate = aBodyReanim.mAnimRate;
			aHead1->mAnimTime = theAnimTime;
			aHead1->SetFramesForLayer("anim_head_idle1");
			aHead1->AttachToAnotherReanimation(&aBodyReanim, "anim_head1");
			aHead1->Update();
			aHeadReanimIDs.push_back(mApp->ReanimationGetID(aHead1));

			Reanimation* aHead2 = mApp->AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_THREEPEATER);
			aHead2->mLoopType = ReanimLoopType::REANIM_LOOP;
			aHead2->mAnimRate = aBodyReanim.mAnimRate;
			aHead2->mAnimTime = theAnimTime;
			aHead2->SetFramesForLayer("anim_head_idle2");
			aHead2->AttachToAnotherReanimation(&aBodyReanim, "anim_head2");
			aHead2->Update();
			aHeadReanimIDs.push_back(mApp->ReanimationGetID(aHead2));

			Reanimation* aHead3 = mApp->AddReanimation(0.0f, 0.0f, 0, ReanimationType::REANIM_THREEPEATER);
			aHead3->mLoopType = ReanimLoopType::REANIM_LOOP;
			aHead3->mAnimRate = aBodyReanim.mAnimRate;
			aHead3->mAnimTime = theAnimTime;
			aHead3->SetFramesForLayer("anim_head_idle3");
			aHead3->AttachToAnotherReanimation(&aBodyReanim, "anim_head3");
			aHead3->Update();
			aHeadReanimIDs.push_back(mApp->ReanimationGetID(aHead3));
		}
		else if (theSeedType == SeedType::SEED_PEASHOOTER ||
		         theSeedType == SeedType::SEED_REPEATER ||
		         theSeedType == SeedType::SEED_SNOWPEA ||
		         theSeedType == SeedType::SEED_GATLINGPEA ||
		         theSeedType == SeedType::SEED_LEFTPEATER)
		{
			Reanimation* aHead = mApp->AddReanimation(0.0f, 0.0f, 0, aPlantDef.mReanimationType);
			aHead->mLoopType = ReanimLoopType::REANIM_LOOP;
			aHead->mAnimRate = aBodyReanim.mAnimRate;
			aHead->mAnimTime = theAnimTime;
			aHead->SetFramesForLayer("anim_head_idle");
			const char* aTrackName = aBodyReanim.TrackExists("anim_stem") ? "anim_stem" : "anim_idle";
			aHead->AttachToAnotherReanimation(&aBodyReanim, aTrackName);
			aHead->Update();
			aHeadReanimIDs.push_back(mApp->ReanimationGetID(aHead));
		}
		else if (theSeedType == SeedType::SEED_SPLITPEA)
		{
			Reanimation* aHead1 = mApp->AddReanimation(0.0f, 0.0f, 0, aPlantDef.mReanimationType);
			aHead1->mLoopType = ReanimLoopType::REANIM_LOOP;
			aHead1->mAnimRate = aBodyReanim.mAnimRate;
			aHead1->mAnimTime = theAnimTime;
			aHead1->SetFramesForLayer("anim_head_idle");
			aHead1->AttachToAnotherReanimation(&aBodyReanim, "anim_idle");
			aHead1->Update();
			aHeadReanimIDs.push_back(mApp->ReanimationGetID(aHead1));

			Reanimation* aHead2 = mApp->AddReanimation(0.0f, 0.0f, 0, aPlantDef.mReanimationType);
			aHead2->mLoopType = ReanimLoopType::REANIM_LOOP;
			aHead2->mAnimRate = aBodyReanim.mAnimRate;
			aHead2->mAnimTime = theAnimTime;
			aHead2->SetFramesForLayer("anim_splitpea_idle");
			aHead2->AttachToAnotherReanimation(&aBodyReanim, "anim_idle");
			aHead2->Update();
			aHeadReanimIDs.push_back(mApp->ReanimationGetID(aHead2));
		}

		aBodyReanim.Update();
		aBodyReanim.Draw(&aMemoryGraphics);

		for (ReanimationID anId : aHeadReanimIDs)
		{
			mApp->RemoveReanimation(anId);
		}
	}

	if (mApp->mEffectSystem)
	{
		mApp->mEffectSystem->ProcessDeleteQueue();
	}

	return aMemoryImage;
}

void ReanimatorCache::DrawCachedPlantIdle(Graphics* g, float thePosX, float thePosY, SeedType theSeedType, float theAnimTime)
{
	if (theSeedType < 0 || theSeedType >= SeedType::NUM_SEED_TYPES)
		return;

	if (mPlantIdleFrames[theSeedType].empty())
	{
		PrecachePlantIdle(theSeedType);
	}

	const auto& frames = mPlantIdleFrames[theSeedType];
	if (frames.empty())
		return;

	float normTime = theAnimTime - std::floor(theAnimTime);
	int frameIndex = std::clamp((int)(normTime * NUM_CACHED_IDLE_FRAMES), 0, NUM_CACHED_IDLE_FRAMES - 1);
	MemoryImage* aImage = frames[frameIndex].get();
	if (!aImage)
		return;

	int aOffsetX, aOffsetY, aWidth, aHeight;
	GetPlantImageSize(theSeedType, aOffsetX, aOffsetY, aWidth, aHeight);
	if (!mApp->Is3DAccelerated() && g->mScaleX == 1.0f && g->mScaleY == 1.0f)
	{
		g->DrawImage(aImage, thePosX + aOffsetX, thePosY + aOffsetY);
	}
	else
	{
		PvzpDrawImageScaledF(g, aImage, thePosX + (aOffsetX * g->mScaleX), thePosY + (aOffsetY * g->mScaleY), g->mScaleX, g->mScaleY);
	}
}

bool ReanimatorCache::HasCachedZombieWalk(ZombieType theZombieType)
{
	switch (theZombieType)
	{
	case ZombieType::ZOMBIE_NORMAL:
	case ZombieType::ZOMBIE_FLAG:
	case ZombieType::ZOMBIE_TRAFFIC_CONE:
	case ZombieType::ZOMBIE_PAIL:
	case ZombieType::ZOMBIE_DOOR:
		return true;
	default:
		return false;
	}
}

void ReanimatorCache::PrecacheZombieWalk(ZombieType theZombieType)
{
	if (theZombieType < 0 || theZombieType >= ZombieType::NUM_CACHED_ZOMBIE_TYPES)
		return;

	auto& frames = mZombieWalkFrames[theZombieType];
	frames.clear();
	frames.reserve(NUM_CACHED_ZOMBIE_WALK_FRAMES);

	for (int i = 0; i < NUM_CACHED_ZOMBIE_WALK_FRAMES; i++)
	{
		float animTime = (float)i / (float)NUM_CACHED_ZOMBIE_WALK_FRAMES;
		frames.push_back(MakeCachedZombieWalkFrame(theZombieType, animTime));
	}
}

std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedZombieWalkFrame(ZombieType theZombieType, float theAnimTime)
{
	int aWidth = 180;
	int aHeight = 200;
	float aBaseX = 60.0f;
	float aBaseY = 60.0f;

	auto aMemoryImage = MakeBlankMemoryImage(aWidth, aHeight);
	Graphics aMemoryGraphics(aMemoryImage.get());
	aMemoryGraphics.SetLinearBlend(true);

	ZombieType aUseZombieType = (theZombieType == ZombieType::ZOMBIE_CACHED_POLEVAULTER_WITH_POLE) ? ZombieType::ZOMBIE_POLEVAULTER : theZombieType;
	if (aUseZombieType < 0 || aUseZombieType >= ZombieType::NUM_ZOMBIE_TYPES)
		return aMemoryImage;

	const ZombieDefinition& aZombieDef = GetZombieDefinition(aUseZombieType);
	if (aZombieDef.mReanimationType == ReanimationType::REANIM_NONE)
		return aMemoryImage;

	ReanimatorEnsureDefinitionLoaded(aZombieDef.mReanimationType, true);

	Reanimation aReanim;
	aReanim.ReanimationInitializeType(aBaseX, aBaseY, aZombieDef.mReanimationType);
	if (aReanim.TrackExists("anim_walk"))
	{
		aReanim.SetFramesForLayer("anim_walk");
	}
	else if (aReanim.TrackExists("anim_idle"))
	{
		aReanim.SetFramesForLayer("anim_idle");
	}

	Zombie::SetupReanimLayers(&aReanim, aUseZombieType);

	if (aUseZombieType == ZombieType::ZOMBIE_DOOR)
	{
		aReanim.AssignRenderGroupToTrack("anim_screendoor", RENDER_GROUP_NORMAL);
	}

	aReanim.mAnimRate = 0.0f;
	aReanim.mAnimTime = theAnimTime;
	aReanim.Update();
	aReanim.Draw(&aMemoryGraphics);

	if (aUseZombieType == ZombieType::ZOMBIE_FLAG)
	{
		ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_FLAG, true);
		Reanimation aReanimFlag;
		aReanimFlag.ReanimationInitializeType(aBaseX, aBaseY, ReanimationType::REANIM_FLAG);
		if (aReanimFlag.TrackExists("Zombie_flag"))
		{
			aReanimFlag.SetFramesForLayer("Zombie_flag");
		}
		aReanimFlag.mAnimRate = 0.0f;
		aReanimFlag.mAnimTime = theAnimTime;
		aReanimFlag.Update();
		aReanimFlag.Draw(&aMemoryGraphics);
	}

	return aMemoryImage;
}

void ReanimatorCache::DrawCachedZombieWalk(Graphics* g, const ZombieDrawPosition& theDrawPos, ZombieType theZombieType, float theAnimTime)
{
	if (theZombieType < 0 || theZombieType >= ZombieType::NUM_CACHED_ZOMBIE_TYPES)
		return;

	if (mZombieWalkFrames[theZombieType].empty())
	{
		PrecacheZombieWalk(theZombieType);
	}

	const auto& frames = mZombieWalkFrames[theZombieType];
	if (frames.empty())
		return;

	float normTime = theAnimTime - std::floor(theAnimTime);
	int frameIndex = std::clamp((int)(normTime * NUM_CACHED_ZOMBIE_WALK_FRAMES), 0, NUM_CACHED_ZOMBIE_WALK_FRAMES - 1);
	MemoryImage* aImage = frames[frameIndex].get();
	if (!aImage)
		return;

	float aBaseX = 60.0f;
	float aBaseY = 60.0f;

	float drawX = theDrawPos.mImageOffsetX + 15.0f - aBaseX;
	float drawY = theDrawPos.mImageOffsetY + theDrawPos.mBodyY - 8.0f - aBaseY;

	if (!mApp->Is3DAccelerated() && g->mScaleX == 1.0f && g->mScaleY == 1.0f)
	{
		g->DrawImage(aImage, (int)drawX, (int)drawY);
	}
	else
	{
		PvzpDrawImageScaledF(g, aImage, drawX * g->mScaleX, drawY * g->mScaleY, g->mScaleX, g->mScaleY);
	}
}

void ReanimatorCache::PrecachePumpkinSurfaces()
{
	ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_PUMPKIN, true);

	// Back image (120x120)
	mPumpkinBackImage = MakeBlankMemoryImage(120, 120);
	Graphics gBack(mPumpkinBackImage.get());
	gBack.SetLinearBlend(true);
	Reanimation aBackReanim;
	aBackReanim.ReanimationInitializeType(20.0f, 20.0f, ReanimationType::REANIM_PUMPKIN);
	aBackReanim.AssignRenderGroupToTrack("Pumpkin_back", 1);
	aBackReanim.DrawRenderGroup(&gBack, 1);

	// Front images (3 states, 120x120)
	// State 0 (intact)
	mPumpkinFrontImages[0] = MakeBlankMemoryImage(120, 120);
	Graphics gFront0(mPumpkinFrontImages[0].get());
	gFront0.SetLinearBlend(true);
	Reanimation aFrontReanim0;
	aFrontReanim0.ReanimationInitializeType(20.0f, 20.0f, ReanimationType::REANIM_PUMPKIN);
	aFrontReanim0.AssignRenderGroupToTrack("Pumpkin_back", 1);
	aFrontReanim0.DrawRenderGroup(&gFront0, 0);

	// State 1 (damage1)
	mPumpkinFrontImages[1] = MakeBlankMemoryImage(120, 120);
	Graphics gFront1(mPumpkinFrontImages[1].get());
	gFront1.SetLinearBlend(true);
	Reanimation aFrontReanim1;
	aFrontReanim1.ReanimationInitializeType(20.0f, 20.0f, ReanimationType::REANIM_PUMPKIN);
	aFrontReanim1.AssignRenderGroupToTrack("Pumpkin_back", 1);
	aFrontReanim1.SetImageOverride("Pumpkin_front", IMAGE_REANIM_PUMPKIN_DAMAGE1);
	aFrontReanim1.DrawRenderGroup(&gFront1, 0);

	// State 2 (damage3)
	mPumpkinFrontImages[2] = MakeBlankMemoryImage(120, 120);
	Graphics gFront2(mPumpkinFrontImages[2].get());
	gFront2.SetLinearBlend(true);
	Reanimation aFrontReanim2;
	aFrontReanim2.ReanimationInitializeType(20.0f, 20.0f, ReanimationType::REANIM_PUMPKIN);
	aFrontReanim2.AssignRenderGroupToTrack("Pumpkin_back", 1);
	aFrontReanim2.SetImageOverride("Pumpkin_front", IMAGE_REANIM_PUMPKIN_DAMAGE3);
	aFrontReanim2.DrawRenderGroup(&gFront2, 0);
}

void ReanimatorCache::DrawCachedPumpkinBack(Graphics* g, float thePosX, float thePosY)
{
	if (mPumpkinBackImage)
	{
		g->DrawImage(mPumpkinBackImage.get(), thePosX - 20.0f, thePosY - 20.0f);
	}
}

void ReanimatorCache::DrawCachedPumpkinFront(Graphics* g, float thePosX, float thePosY, int theDamageState)
{
	if (theDamageState < 0)
		theDamageState = 0;
	else if (theDamageState > 2)
		theDamageState = 2;

	if (mPumpkinFrontImages[theDamageState])
	{
		g->DrawImage(mPumpkinFrontImages[theDamageState].get(), thePosX - 20.0f, thePosY - 20.0f);
	}
}

