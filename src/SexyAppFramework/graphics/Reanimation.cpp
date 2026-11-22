#include "SexyAppFramework/graphics/Reanimation.h"
#include "PvzpLib/ReanimAtlas.h"
#include "PvzpLib/PvzpCommon.h"
#include "SexyAppFramework/graphics/Font.h"
#include "SexyAppFramework/graphics/Image.h"
#include <algorithm>
#include <cmath>
#include <cstring>

void Reanimation::CalculateModelMatrix(int theTrackIndex, float outM[2][3], float* outA, uint32_t* outC)
{
	if (theTrackIndex < 0 || !mDefinition || !mTrackInstances || theTrackIndex >= mDefinition->mTracks.count)
	{
		if (outM)
		{
			outM[0][0] = 1.0f; outM[0][1] = 0.0f; outM[0][2] = 0.0f;
			outM[1][0] = 0.0f; outM[1][1] = 1.0f; outM[1][2] = 0.0f;
		}
		if (outA) *outA = 0.0f;
		if (outC) *outC = 0;
		return;
	}

	ReanimatorTransform aTransform;
	ReanimatorTrackInstance* aTrackInstance = &mTrackInstances[theTrackIndex];
	if (!GetCurrentTransform(theTrackIndex, &aTransform, nullptr))
	{
		if (outM)
		{
			outM[0][0] = 1.0f; outM[0][1] = 0.0f; outM[0][2] = 0.0f;
			outM[1][0] = 0.0f; outM[1][1] = 1.0f; outM[1][2] = 0.0f;
		}
		if (outA) *outA = 0.0f;
		if (outC) *outC = 0;
		return;
	}

	if (outA)
	{
		*outA = aTransform.mAlpha;
	}

	Color aColor = aTrackInstance->mTrackColor;
	if (!aTrackInstance->mIgnoreColorOverride)
	{
		aColor = ColorsMultiply(aColor, mColorOverride);
	}
	int aImageAlpha = std::clamp(FloatRoundToInt(aTransform.mAlpha * aColor.mAlpha), 0, 255);
	aColor.mAlpha = aImageAlpha;
	if (outC)
	{
		*outC = aColor.ToInt();
	}

	Image* aImage = aTransform.mImage;
	ReanimAtlasImage* aAtlasImage = nullptr;
	Image* aPivotImage = aImage;
	ReanimAtlasImage* aPivotAtlasImage = nullptr;

	if (mDefinition->mReanimAtlas != nullptr && aPivotImage != nullptr)
	{
		aPivotAtlasImage = mDefinition->mReanimAtlas->GetEncodedReanimAtlas(aPivotImage);
		if (aPivotAtlasImage == nullptr && reinterpret_cast<uintptr_t>(aPivotImage) <= 1000)
			aPivotImage = nullptr;
	}

	if (mDefinition->mReanimAtlas != nullptr && aImage != nullptr)
	{
		aAtlasImage = mDefinition->mReanimAtlas->GetEncodedReanimAtlas(aImage);
		if (aTrackInstance->mImageOverride != nullptr)
		{
			aImage = aTrackInstance->mImageOverride;
			aAtlasImage = nullptr;
		}
		else if (aAtlasImage == nullptr && reinterpret_cast<uintptr_t>(aImage) <= 1000)
		{
			aImage = nullptr;
		}
	}
	if (aTrackInstance->mImageOverride != nullptr)
	{
		aImage = aTrackInstance->mImageOverride;
	}

	if (reinterpret_cast<uintptr_t>(aPivotImage) <= 10000)
		aPivotImage = nullptr;
	if (reinterpret_cast<uintptr_t>(aImage) <= 10000)
		aImage = nullptr;

	SexyMatrix3 aMatrix;
	MatrixFromTransform(aTransform, aMatrix);

	float aPivotX = 0.0f;
	float aPivotY = 0.0f;
	if (aPivotAtlasImage != nullptr)
	{
		aPivotX = aPivotAtlasImage->mWidth * 0.5f;
		aPivotY = aPivotAtlasImage->mHeight * 0.5f;
	}
	else if (aPivotImage != nullptr)
	{
		int aCelWidth = aPivotImage->GetCelWidth();
		int aCelHeight = aPivotImage->GetCelHeight();
		aPivotX = aCelWidth * 0.5f;
		aPivotY = aCelHeight * 0.5f;
	}
	else if (aImage != nullptr)
	{
		int aCelWidth = aImage->GetCelWidth();
		int aCelHeight = aImage->GetCelHeight();
		aPivotX = aCelWidth * 0.5f;
		aPivotY = aCelHeight * 0.5f;
	}
	else if (aTransform.mFont != nullptr && aTransform.mText != nullptr && *aTransform.mText != '\0')
	{
		int aWidth = aTransform.mFont->StringWidth(aTransform.mText);
		aPivotX = -aWidth * 0.5f;
		aPivotY = aTransform.mFont->mAscent;
	}

	if (aPivotX != 0.0f || aPivotY != 0.0f)
	{
		aMatrix.m02 += aMatrix.m00 * aPivotX + aMatrix.m01 * aPivotY;
		aMatrix.m12 += aMatrix.m10 * aPivotX + aMatrix.m11 * aPivotY;
	}

	if (__builtin_expect(mHasOverlayMatrix, 0))
	{
		SexyMatrix3Multiply(aMatrix, mOverlayMatrix, aMatrix);
	}

	if (outM)
	{
		outM[0][0] = aMatrix.m00;
		outM[0][1] = aMatrix.m01;
		outM[0][2] = aMatrix.m02;
		outM[1][0] = aMatrix.m10;
		outM[1][1] = aMatrix.m11;
		outM[1][2] = aMatrix.m12;
	}
}
