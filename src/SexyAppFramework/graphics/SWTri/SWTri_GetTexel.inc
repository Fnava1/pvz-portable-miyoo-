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

	unsigned int tex;
	#if !defined(LINEAR_BLEND)
	{
		unsigned int v_int = v >> 16;
		if (__builtin_expect(v_int != current_v_int, 0))
		{
			current_v_int = v_int;
			v_row_offset = v_int * tex_pitch;
		}
		unsigned int t_pos = v_row_offset + ((u) >> 16);

		if (__builtin_expect(tex_format == 2, 0))
		{
			const uint16_t* pTex16 = reinterpret_cast<const uint16_t*>(pTexture);
			if (pTex16 && t_pos < tex_endpos)
			{
				uint16_t c16 = pTex16[t_pos];
				uint32_t a = (c16 >> 12) & 0x0F;
				uint32_t r = (c16 >> 8)  & 0x0F;
				uint32_t g = (c16 >> 4)  & 0x0F;
				uint32_t b = c16         & 0x0F;
				tex = ((a | (a << 4)) << 24) | ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
			}
			else
			{
				tex = 0;
			}
		}
		else if (__builtin_expect(tex_format == 1, 0))
		{
			const uint16_t* pTex16 = reinterpret_cast<const uint16_t*>(pTexture);
			if (pTex16 && t_pos < tex_endpos)
			{
				uint16_t c16 = pTex16[t_pos];
				uint32_t r = (c16 >> 11) & 0x1F;
				uint32_t g = (c16 >> 5)  & 0x3F;
				uint32_t b = c16         & 0x1F;
				r = (r * 255 + 15) / 31;
				g = (g * 255 + 31) / 63;
				b = (b * 255 + 15) / 31;
				tex = 0xFF000000 | (r << 16) | (g << 8) | b;
			}
			else
			{
				tex = 0;
			}
		}
		else if (__builtin_expect(!is_tiled, 1))
		{
			tex = (pTexture && t_pos < tex_endpos) ? pTexture[t_pos] : 0;
		}
		else
		{
			unsigned int u_int = (u) >> 16;
			unsigned int tiles_across = tex_pitch >> 3;
			unsigned int tiled_pos = (((v_int >> 3) * tiles_across + (u_int >> 3)) << 6) | ((v_int & 7) << 3) | (u_int & 7);
			tex = (pTexture && tiled_pos < tex_endpos) ? pTexture[tiled_pos] : 0;
		}
	}
	#else
	{
		int umid = u-0x8000;
		int vmid = v-0x8000;
		int umidfloor = FixedFloor(umid);
		int vmidfloor = FixedFloor(vmid);

		unsigned int	t_pos = (vmidfloor>>16)*tex_pitch + (umidfloor>>16);

		unsigned int t00, t10, t01, t11;
		if (__builtin_expect(tex_format == 2, 0))
		{
			const uint16_t* pTex16 = reinterpret_cast<const uint16_t*>(pTexture);
			auto unpack4444 = [](uint16_t c16) -> uint32_t {
				uint32_t a = (c16 >> 12) & 0x0F;
				uint32_t r = (c16 >> 8)  & 0x0F;
				uint32_t g = (c16 >> 4)  & 0x0F;
				uint32_t b = c16         & 0x0F;
				return ((a | (a << 4)) << 24) | ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
			};
			t00 = (pTex16 && t_pos < tex_endpos) ? unpack4444(pTex16[t_pos]) : 0;
			t10 = (pTex16 && t_pos + 1 < tex_endpos) ? unpack4444(pTex16[t_pos + 1]) : 0;
			t01 = (pTex16 && t_pos + tex_pitch < tex_endpos) ? unpack4444(pTex16[t_pos + tex_pitch]) : 0;
			t11 = (pTex16 && t_pos + tex_pitch + 1 < tex_endpos) ? unpack4444(pTex16[t_pos + tex_pitch + 1]) : 0;
		}
		else if (__builtin_expect(tex_format == 1, 0))
		{
			const uint16_t* pTex16 = reinterpret_cast<const uint16_t*>(pTexture);
			auto unpack565 = [](uint16_t c16) -> uint32_t {
				uint32_t r = (c16 >> 11) & 0x1F;
				uint32_t g = (c16 >> 5)  & 0x3F;
				uint32_t b = c16         & 0x1F;
				r = (r * 255 + 15) / 31;
				g = (g * 255 + 31) / 63;
				b = (b * 255 + 15) / 31;
				return 0xFF000000 | (r << 16) | (g << 8) | b;
			};
			t00 = (pTex16 && t_pos < tex_endpos) ? unpack565(pTex16[t_pos]) : 0;
			t10 = (pTex16 && t_pos + 1 < tex_endpos) ? unpack565(pTex16[t_pos + 1]) : 0;
			t01 = (pTex16 && t_pos + tex_pitch < tex_endpos) ? unpack565(pTex16[t_pos + tex_pitch]) : 0;
			t11 = (pTex16 && t_pos + tex_pitch + 1 < tex_endpos) ? unpack565(pTex16[t_pos + tex_pitch + 1]) : 0;
		}
		else
		{
			t00 = (pTexture && t_pos < tex_endpos) ? pTexture[t_pos] : 0;
			t10 = (pTexture && t_pos + 1 < tex_endpos) ? pTexture[t_pos + 1] : 0;
			t01 = (pTexture && t_pos + tex_pitch < tex_endpos) ? pTexture[t_pos + tex_pitch] : 0;
			t11 = (pTexture && t_pos + tex_pitch + 1 < tex_endpos) ? pTexture[t_pos + tex_pitch + 1] : 0;
		}

		unsigned int aUFactor = ((umid-umidfloor) & 0xFFFE) + 1; // aUFactor needs to be between 1 and 0xFFFF to avoid overflow
		unsigned int aVFactor = ((vmid-vmidfloor) & 0xFFFE) + 1; // ditto for aVFactor
		unsigned int a00 = ((t00 >> 24) * ((ulong) ((0x10000  - aUFactor) * (0x10000  - aVFactor)) >> 16)) >> 16;
		unsigned int a10 = ((t10 >> 24) * ((ulong) ((           aUFactor) * (0x10000  - aVFactor)) >> 16)) >> 16;
		unsigned int a01 = ((t01 >> 24) * ((ulong) ((0x10000  - aUFactor) * (           aVFactor)) >> 16)) >> 16;
		unsigned int a11 = ((t11 >> 24) * ((ulong) ((           aUFactor) * (           aVFactor)) >> 16)) >> 16;
		unsigned int r = (((t00&0x00FF0000)*a00 + (t10&0x00FF0000)*a10 + (t01&0x00FF0000)*a01 + (t11&0x00FF0000)*a11)>>8)&0xFF0000;
		unsigned int g = (((t00&0x0000FF00)*a00 + (t10&0x0000FF00)*a10 + (t01&0x0000FF00)*a01 + (t11&0x0000FF00)*a11)>>8)&0x00FF00;
		unsigned int b = (((t00&0x000000FF)*a00 + (t10&0x000000FF)*a10 + (t01&0x000000FF)*a01 + (t11&0x000000FF)*a11)>>8)&0x0000FF;
		unsigned int a = ((a00 + a10 + a01 + a11)<<24)&0xFF000000;

		tex = a|r|g|b;
	}
	#endif

	unsigned int	alpha;
	#if defined (TEX_ALPHA)
		alpha = tex>>24;
	#else
		alpha = 0xFF;
	#endif
