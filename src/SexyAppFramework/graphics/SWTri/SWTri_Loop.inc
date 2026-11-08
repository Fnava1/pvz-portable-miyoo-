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

// This file is included by SWTri.cpp and should not be built directly by the project.

	[[maybe_unused]] SWHelper::signed64	subTex = x0 - lx;
	[[maybe_unused]] unsigned int	u, v, r, g, b, a;

	#if defined(MOD_ARGB)
		a = la + static_cast<int>((da * subTex)>>16);
		r = lr + static_cast<int>((dr * subTex)>>16);
		g = lg + static_cast<int>((dg * subTex)>>16);
		b = lb + static_cast<int>((db * subTex)>>16);
	#endif

	#if defined(TEXTURED)
		u = lu + static_cast<int>((du * subTex)>>16);
		v = lv + static_cast<int>((dv * subTex)>>16);
		unsigned int current_v_int = v >> 16;
		unsigned int v_row_offset = current_v_int * tex_pitch;
	#endif

	[[maybe_unused]] PTYPE *		pix = fb + (x0>>16);
	int		width = ((x1-x0)>>16);

#if defined(TEXTURED) && !defined(LINEAR_BLEND) && !defined(MOD_ARGB)
	if (!is_tiled)
	{
		while (width >= 4)
		{
			unsigned int u0 = u >> 16;
			unsigned int u1 = (u + du) >> 16;
			unsigned int u2 = (u + du * 2) >> 16;
			unsigned int u3 = (u + du * 3) >> 16;

			unsigned int v0 = v >> 16;
			unsigned int v1 = (v + dv) >> 16;
			unsigned int v2 = (v + dv * 2) >> 16;
			unsigned int v3 = (v + dv * 3) >> 16;

			unsigned int t0 = v0 * tex_pitch + u0;
			unsigned int t1 = v1 * tex_pitch + u1;
			unsigned int t2 = v2 * tex_pitch + u2;
			unsigned int t3 = v3 * tex_pitch + u3;

			unsigned int tex0 = t0 < tex_endpos ? pTexture[t0] : 0;
			unsigned int tex1 = t1 < tex_endpos ? pTexture[t1] : 0;
			unsigned int tex2 = t2 < tex_endpos ? pTexture[t2] : 0;
			unsigned int tex3 = t3 < tex_endpos ? pTexture[t3] : 0;

			if ((tex0 >> 24) <= 8 && (tex1 >> 24) <= 8 && (tex2 >> 24) <= 8 && (tex3 >> 24) <= 8)
			{
				pix += 4;
				u += du * 4;
				v += dv * 4;
				width -= 4;
				continue;
			}

			break;
		}
	}
	else
	{
		unsigned int tiles_across = tex_pitch >> 3;
		while (width >= 4)
		{
			unsigned int u0 = u >> 16, v0 = v >> 16;
			unsigned int u1 = (u + du) >> 16, v1 = (v + dv) >> 16;
			unsigned int u2 = (u + du * 2) >> 16, v2 = (v + dv * 2) >> 16;
			unsigned int u3 = (u + du * 3) >> 16, v3 = (v + dv * 3) >> 16;

			unsigned int t0 = (((v0 >> 3) * tiles_across + (u0 >> 3)) << 6) | ((v0 & 7) << 3) | (u0 & 7);
			unsigned int t1 = (((v1 >> 3) * tiles_across + (u1 >> 3)) << 6) | ((v1 & 7) << 3) | (u1 & 7);
			unsigned int t2 = (((v2 >> 3) * tiles_across + (u2 >> 3)) << 6) | ((v2 & 7) << 3) | (u2 & 7);
			unsigned int t3 = (((v3 >> 3) * tiles_across + (u3 >> 3)) << 6) | ((v3 & 7) << 3) | (u3 & 7);

			unsigned int tex0 = t0 < tex_endpos ? pTexture[t0] : 0;
			unsigned int tex1 = t1 < tex_endpos ? pTexture[t1] : 0;
			unsigned int tex2 = t2 < tex_endpos ? pTexture[t2] : 0;
			unsigned int tex3 = t3 < tex_endpos ? pTexture[t3] : 0;

			if ((tex0 >> 24) <= 8 && (tex1 >> 24) <= 8 && (tex2 >> 24) <= 8 && (tex3 >> 24) <= 8)
			{
				pix += 4;
				u += du * 4;
				v += dv * 4;
				width -= 4;
				continue;
			}

			break;
		}
	}
#endif

	while(width-- > 0)
	{
		#include PIXEL_INCLUDE
//		if (bit_format == 0x888) PIXEL888()
//		if (bit_format == 0x565) PIXEL565()
//		if (bit_format == 0x555) PIXEL555()
//		if (bit_format == 0x8888) PIXEL8888()
		++pix;
		#if defined(MOD_ARGB)
			a += da;
			r += dr;
			g += dg;
			b += db;
		#endif

		#if defined(TEXTURED)
			u += du;
			v += dv;
		#endif
	}

	lx += ldx;
	sx += sdx;
	fb += pitch;

	#if defined (MOD_ARGB)
		la += lda;
		lr += ldr;
		lg += ldg;
		lb += ldb;
	#endif

	#if defined (TEXTURED)
		lu += ldu;
		lv += ldv;
	#endif
