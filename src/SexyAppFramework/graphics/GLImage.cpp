/*
 * Portions of this file are based on the PopCap Games Framework
 * Copyright (C) 2005-2009 PopCap Games, Inc.
 *
 * Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later AND LicenseRef-PopCap
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

#include "GLImage.h"
#include "misc/Debug.h"
#include "graphics/GLInterface.h"
#include "SexyAppBase.h"
#include "Image.h"

using namespace Sexy;

GLImage::GLImage() : MemoryImage(gSexyAppBase)
{
	mGLInterface = gSexyAppBase->mGLInterface.get();
	if (mGLInterface)
		mGLInterface->AddGLImage(this);
}

GLImage::GLImage(GLInterface *theGLInterface)
	: MemoryImage(theGLInterface ? theGLInterface->mApp : gSexyAppBase)
{
	mGLInterface = theGLInterface;
	if (mGLInterface)
		mGLInterface->AddGLImage(this);
}

GLImage::~GLImage()
{
	if (mGLInterface)
		mGLInterface->RemoveGLImage(this);
}

void GLImage::Create(int theWidth, int theHeight)
{
	MemoryImage::Create(theWidth, theHeight);
}

void GLImage::FillScanLinesWithCoverage(Span* theSpans, int theSpanCount, const Color& theColor, int theDrawMode, const uint8_t* theCoverage, int theCoverX, int theCoverY, int theCoverWidth, int theCoverHeight)
{
	if (theSpanCount == 0) return;

	int l = theSpans[0].mX, t = theSpans[0].mY;
	int r = l + theSpans[0].mWidth, b = t;
	for (int i = 1; i < theSpanCount; ++i)  // after the loop, Rect(l, t, r - l + 1, b - t + 1) is the bounding rect of all spans
	{
		l = std::min(theSpans[i].mX, l);
		r = std::max(theSpans[i].mX + theSpans[i].mWidth - 1, r);
		t = std::min(theSpans[i].mY, t);
		b = std::max(theSpans[i].mY, b);
	}
	for (int i = 0; i < theSpanCount; ++i)  // make all span coordinates relative to that rect
	{
		theSpans[i].mX -= l;
		theSpans[i].mY -= t;
	}

	MemoryImage aTempImage;
	aTempImage.Create(r-l+1, b-t+1);
	// theCoverX - l / theCoverY - t convert the coverage origin to temp-image coordinates
	aTempImage.FillScanLinesWithCoverage(theSpans, theSpanCount, theColor, theDrawMode, theCoverage, theCoverX - l, theCoverY - t, theCoverWidth, theCoverHeight);
	Blt(&aTempImage, l, t, Rect(0, 0, r-l+1, b-t+1), Color::White, theDrawMode);
	return;
}

bool GLImage::Check3D(GLImage *theImage)
{
	return true;
}

bool GLImage::Check3D(Image *theImage)
{
	GLImage *anImage = dynamic_cast<GLImage*>(theImage);
	return anImage != 0;
}

void GLImage::PurgeBits()
{
	mPurgeBits = true;

	CommitBits();
	GetBits();

	MemoryImage::PurgeBits();
}

bool GLImage::PolyFill3D(const Point theVertices[], int theNumVertices, const Rect *theClipRect, const Color &theColor, int theDrawMode, int tx, int ty)
{
#if PVZ_ENABLE_GLES
	mGLInterface->FillPoly(theVertices,theNumVertices,theClipRect,theColor,theDrawMode,tx,ty);
	return true;
#else
	return MemoryImage::PolyFill3D(theVertices, theNumVertices, theClipRect, theColor, theDrawMode, tx, ty);
#endif
}

void GLImage::FillRect(const Rect& theRect, const Color& theColor, int theDrawMode)
{
#if PVZ_ENABLE_GLES
	mGLInterface->FillRect(theRect,theColor,theDrawMode);
#else
	MemoryImage::FillRect(theRect, theColor, theDrawMode);
#endif
}

void GLImage::DrawLine(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor, int theDrawMode)
{
#if PVZ_ENABLE_GLES
	mGLInterface->DrawLine(theStartX,theStartY,theEndX,theEndY,theColor,theDrawMode);
#else
	MemoryImage::DrawLine(theStartX, theStartY, theEndX, theEndY, theColor, theDrawMode);
#endif
}

void GLImage::DrawLineAA(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor, int theDrawMode)
{
#if PVZ_ENABLE_GLES
	mGLInterface->DrawLine(theStartX,theStartY,theEndX,theEndY,theColor,theDrawMode);
#else
	MemoryImage::DrawLineAA(theStartX, theStartY, theEndX, theEndY, theColor, theDrawMode);
#endif
}

void GLImage::Blt(Image* theImage, int theX, int theY, const Rect& theSrcRect, const Color& theColor, int theDrawMode, bool linearFilter)
{
	theImage->mDrawn = true;

	DBG_ASSERTE((theColor.mRed >= 0) && (theColor.mRed <= 255));
	DBG_ASSERTE((theColor.mGreen >= 0) && (theColor.mGreen <= 255));
	DBG_ASSERTE((theColor.mBlue >= 0) && (theColor.mBlue <= 255));
	DBG_ASSERTE((theColor.mAlpha >= 0) && (theColor.mAlpha <= 255));

#if PVZ_ENABLE_GLES
	CommitBits();
	mGLInterface->Blt(theImage,theX,theY,theSrcRect,theColor,theDrawMode,linearFilter);
#else
	MemoryImage::Blt(theImage, theX, theY, theSrcRect, theColor, theDrawMode, linearFilter);
#endif
}

void GLImage::BltF(Image* theImage, float theX, float theY, const Rect& theSrcRect, const Rect &theClipRect, const Color& theColor, int theDrawMode)
{
	theImage->mDrawn = true;

#if PVZ_ENABLE_GLES
	FRect aClipRect(theClipRect.mX,theClipRect.mY,theClipRect.mWidth,theClipRect.mHeight);
	FRect aDestRect(theX,theY,theSrcRect.mWidth,theSrcRect.mHeight);

	FRect anIntersect = aDestRect.Intersection(aClipRect);
	if (anIntersect.mWidth!=aDestRect.mWidth || anIntersect.mHeight!=aDestRect.mHeight)
	{
		if (anIntersect.mWidth!=0 && anIntersect.mHeight!=0)
			mGLInterface->BltClipF(theImage,theX,theY,theSrcRect,&theClipRect,theColor,theDrawMode);
	}
	else
		mGLInterface->Blt(theImage,theX,theY,theSrcRect,theColor,theDrawMode,true);
#else
	MemoryImage::BltF(theImage, theX, theY, theSrcRect, theClipRect, theColor, theDrawMode);
#endif
}

void GLImage::BltRotated(Image* theImage, float theX, float theY, const Rect &theSrcRect, const Rect& theClipRect, const Color& theColor, int theDrawMode, double theRot, float theRotCenterX, float theRotCenterY)
{
	theImage->mDrawn = true;

#if PVZ_ENABLE_GLES
	CommitBits();
	mGLInterface->BltRotated(theImage,theX,theY,&theClipRect,theColor,theDrawMode,theRot,theRotCenterX,theRotCenterY,theSrcRect);
#else
	MemoryImage::BltRotated(theImage, theX, theY, theSrcRect, theClipRect, theColor, theDrawMode, theRot, theRotCenterX, theRotCenterY);
#endif
}

void GLImage::StretchBlt(Image* theImage, const Rect& theDestRect, const Rect& theSrcRect, const Rect& theClipRect, const Color& theColor, int theDrawMode, bool fastStretch)
{
	theImage->mDrawn = true;

#if PVZ_ENABLE_GLES
	CommitBits();
	mGLInterface->StretchBlt(theImage,theDestRect,theSrcRect,&theClipRect,theColor,theDrawMode,fastStretch);
#else
	MemoryImage::StretchBlt(theImage, theDestRect, theSrcRect, theClipRect, theColor, theDrawMode, fastStretch);
#endif
}

void GLImage::BltMatrix(Image* theImage, float x, float y, const SexyMatrix3 &theMatrix, const Rect& theClipRect, const Color& theColor, int theDrawMode, const Rect &theSrcRect, bool blend)
{
	theImage->mDrawn = true;

#if PVZ_ENABLE_GLES
	mGLInterface->BltTransformed(theImage,&theClipRect,theColor,theDrawMode,theSrcRect,theMatrix,blend,x,y,true);
#else
	MemoryImage::BltMatrix(theImage, x, y, theMatrix, theClipRect, theColor, theDrawMode, theSrcRect, blend);
#endif
}

void GLImage::BltTrianglesTex(Image *theTexture, const TriVertex theVertices[][3], int theNumTriangles, const Rect& theClipRect, const Color &theColor, int theDrawMode, float tx, float ty, bool blend)
{
	theTexture->mDrawn = true;

#if PVZ_ENABLE_GLES
	mGLInterface->DrawTrianglesTex(theVertices,theNumTriangles,theColor,theDrawMode,theTexture,tx,ty,blend);
#else
	MemoryImage::BltTrianglesTex(theTexture, theVertices, theNumTriangles, theClipRect, theColor, theDrawMode, tx, ty, blend);
#endif
}

void GLImage::BltMirror(Image* theImage, int theX, int theY, const Rect& theSrcRect, const Color& theColor, int theDrawMode, bool linearFilter)
{
	DBG_ASSERTE((theColor.mRed >= 0) && (theColor.mRed <= 255));
	DBG_ASSERTE((theColor.mGreen >= 0) && (theColor.mGreen <= 255));
	DBG_ASSERTE((theColor.mBlue >= 0) && (theColor.mBlue <= 255));
	DBG_ASSERTE((theColor.mAlpha >= 0) && (theColor.mAlpha <= 255));

#if PVZ_ENABLE_GLES
	CommitBits();
	mGLInterface->BltMirror(theImage,theX,theY,theSrcRect,theColor,theDrawMode,linearFilter);
#else
	MemoryImage::BltMirror(theImage, theX, theY, theSrcRect, theColor, theDrawMode, linearFilter);
#endif
}

void GLImage::StretchBltMirror(Image* theImage, const Rect& theDestRectOrig, const Rect& theSrcRect, const Rect& theClipRect, const Color& theColor, int theDrawMode, bool fastStretch)
{
	theImage->mDrawn = true;

#if PVZ_ENABLE_GLES
	CommitBits();
	mGLInterface->StretchBlt(theImage,theDestRectOrig,theSrcRect,&theClipRect,theColor,theDrawMode,fastStretch,true);
#else
	MemoryImage::StretchBltMirror(theImage, theDestRectOrig, theSrcRect, theClipRect, theColor, theDrawMode, fastStretch);
#endif
}
