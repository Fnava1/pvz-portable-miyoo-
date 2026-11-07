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

#include "SexyMatrix.h"
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#include <math.h>

using namespace Sexy;

SexyMatrix3::SexyMatrix3()
{
}

void SexyMatrix3::ZeroMatrix()
{
	m00 = m01 = m02 =
	m10 = m11 = m12 =
	m20 = m21 = m22 = 0;
}

void SexyMatrix3::LoadIdentity()
{
	m01	= m02 = m10 = m12 = m20 = m21 = 0;
	m00 = m11 = m22 = 1;
}

SexyMatrix3 SexyMatrix3::operator*(const SexyMatrix3 &theMat) const
{
	SexyMatrix3 aResult;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
	// In 2D affine graphics, the 3rd row is strictly [0, 0, 1].
	// Compute 2x2 rotation/scale/skew block in parallel using 128-bit NEON vector registers:
	// row0: [m00, m01] = l00 * [r00, r01] + l01 * [r10, r11]
	// row1: [m10, m11] = l10 * [r00, r01] + l11 * [r10, r11]
	float32x4_t r_row01 = { theMat.m00, theMat.m01, theMat.m00, theMat.m01 };
	float32x4_t r_row10 = { theMat.m10, theMat.m11, theMat.m10, theMat.m11 };
	float32x4_t l_col0 = { m00, m00, m10, m10 };
	float32x4_t l_col1 = { m01, m01, m11, m11 };

	float32x4_t rot_block = vmlaq_f32(vmulq_f32(l_col0, r_row01), l_col1, r_row10);

	aResult.m00 = vgetq_lane_f32(rot_block, 0);
	aResult.m01 = vgetq_lane_f32(rot_block, 1);
	aResult.m10 = vgetq_lane_f32(rot_block, 2);
	aResult.m11 = vgetq_lane_f32(rot_block, 3);

	// Translation: [m02, m12] = [l00, l10] * r02 + [l01, l11] * r12 + [l02, l12]
	aResult.m02 = m00 * theMat.m02 + m01 * theMat.m12 + m02;
	aResult.m12 = m10 * theMat.m02 + m11 * theMat.m12 + m12;
#else
	// Analytical 2D affine reduction: 8 multiplications and 4 additions vs 27 mults / 18 adds
	aResult.m00 = m00 * theMat.m00 + m01 * theMat.m10;
	aResult.m01 = m00 * theMat.m01 + m01 * theMat.m11;
	aResult.m02 = m00 * theMat.m02 + m01 * theMat.m12 + m02;
	aResult.m10 = m10 * theMat.m00 + m11 * theMat.m10;
	aResult.m11 = m10 * theMat.m01 + m11 * theMat.m11;
	aResult.m12 = m10 * theMat.m02 + m11 * theMat.m12 + m12;
#endif
	aResult.m20 = 0.0f;
	aResult.m21 = 0.0f;
	aResult.m22 = 1.0f;

	return aResult;
}

SexyVector2 SexyMatrix3::operator*(const SexyVector2 &theVec) const
{
	return SexyVector2(
		m00*theVec.x + m01*theVec.y + m02,
		m10*theVec.x + m11*theVec.y + m12);
}

SexyVector3 SexyMatrix3::operator*(const SexyVector3 &theVec) const
{
	return SexyVector3(
		m00*theVec.x + m01*theVec.y + m02*theVec.z,
		m10*theVec.x + m11*theVec.y + m12*theVec.z,
		m20*theVec.x + m21*theVec.y + m22*theVec.z);
}

const SexyMatrix3& SexyMatrix3::operator*=(const SexyMatrix3 &theMat)
{
	return operator=(operator*(theMat));
}

SexyTransform2D::SexyTransform2D()
{
	LoadIdentity();
}

SexyTransform2D::SexyTransform2D(bool loadIdentity)
{
	if (loadIdentity)
		LoadIdentity();
}

SexyTransform2D::SexyTransform2D(const SexyMatrix3& theMatrix) : SexyMatrix3(theMatrix)
{
}

const SexyTransform2D& SexyTransform2D::operator=(const SexyMatrix3 &theMat)
{
	SexyMatrix3::operator=(theMat);
	return *this;
}

void SexyTransform2D::Translate(float tx, float ty)
{
	SexyMatrix3 aMat;
	aMat.LoadIdentity();
	aMat.m02 = tx;
	aMat.m12 = ty;
	aMat.m22 = 1;

	*this = aMat * (*this);
}

void SexyTransform2D::RotateRad(float rot)
{
	SexyMatrix3 aMat;
	aMat.LoadIdentity();

	float sinRot = -sinf(rot);
	float cosRot = cosf(rot);

	aMat.m00 = cosRot;
	aMat.m01 = -sinRot;
	aMat.m10 = sinRot;
	aMat.m11 = cosRot;

	*this = aMat * (*this);
}

void SexyTransform2D::RotateDeg(float rot)
{
	RotateRad(3.1415926535897932384626433832795028841971f * rot / 180.0f);
}

void SexyTransform2D::Scale(float sx, float sy)
{
	SexyMatrix3 aMat;
	aMat.LoadIdentity();
	aMat.m00 = sx;
	aMat.m11 = sy;

	*this = aMat * (*this);
}

Transform::Transform() : mMatrix(false)
{
	Reset();
}

void Transform::Reset()
{
	mNeedCalcMatrix = true;
	mComplex = false;
	mTransX1 = mTransY1 = 0;
	mTransX2 = mTransY2 = 0;
	mScaleX = mScaleY = 1;
	mRot = 0;
	mHaveRot = false;
	mHaveScale = false;
}

void Transform::Translate(float tx, float ty)
{
	if (!mComplex)
	{
		mNeedCalcMatrix = true;
		if (mHaveRot || mHaveScale)
		{
			mTransX2 += tx;
			mTransY2 += ty;
		}
		else
		{
			mTransX1 += tx;
			mTransY1 += ty;
		}
	}
	else
		mMatrix.Translate(tx,ty);
}

void Transform::RotateRad(float rot)
{
	if (!mComplex)
	{
		if (mHaveScale)
		{
			MakeComplex();
			mMatrix.RotateRad(rot);
		}
		else
		{
			mNeedCalcMatrix = true;
			mHaveRot = true;
			mRot += rot;
		}
	}
	else
		mMatrix.RotateRad(rot);
}

void Transform::RotateDeg(float rot)
{
	Transform::RotateRad(3.1415926535897932384626433832795028841971f * rot / 180.0f);
}

void Transform::Scale(float sx, float sy)
{
	if (!mComplex)
	{
		if (mHaveRot || mTransX1!=0 || mTransY1!=0 || (sx<0 && mScaleX*sx!=-1) || sy<0)
		{
			MakeComplex();
			mMatrix.Scale(sx,sy);
		}
		else
		{
			mNeedCalcMatrix = true;
			mHaveScale = true;
			mScaleX *= sx;
			mScaleY *= sy;
		}
	}
	else
		mMatrix.Scale(sx,sy);
}

void Transform::MakeComplex()
{
	if (!mComplex)
	{
		mComplex = true;
		CalcMatrix();
	}
}

void Transform::CalcMatrix() const
{
	if (mNeedCalcMatrix)
	{
		mNeedCalcMatrix = false;

		mMatrix.LoadIdentity();
		mMatrix.m02 = mTransX1;
		mMatrix.m12 = mTransY1;
		mMatrix.m22 = 1;

		if (mHaveScale)
		{
			mMatrix.m00 = mScaleX;
			mMatrix.m11 = mScaleY;
		}
		else if (mHaveRot)
			mMatrix.RotateRad(mRot);

		if (mTransX2!=0 || mTransY2!=0)
			mMatrix.Translate(mTransX2,mTransY2);
	}

}

const SexyTransform2D& Transform::GetMatrix() const
{
	CalcMatrix();
	return mMatrix;
}
