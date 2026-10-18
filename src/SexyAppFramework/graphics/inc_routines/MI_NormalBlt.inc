{
	uint32_t* aDestPixelsRow = ((uint32_t*) GetBits()) + (theY * mWidth) + theX;

	if ((mHasAlpha) || (mHasTrans) || (theColor != Color::White))
	{
		if (theColor == Color::White)
		{
#ifdef DIRECT_SRC_PIXELS
			for (int y = 0; y < theSrcRect.mHeight; y++)
			{
				uint32_t* aDestPixels = aDestPixelsRow;
				EACH_ROW;

				int width = theSrcRect.mWidth;
				int left = 0;
				while (left < width && (aSrcPtr[left] >> 24) == 0)
					left++;

				if (left < width)
				{
					int right = width - 1;
					while (right > left && (aSrcPtr[right] >> 24) == 0)
						right--;

					int count = right - left + 1;
					PvZ::Graphics::NeonBlit_AlphaBlend_ARGB(aDestPixels + left, aSrcPtr + left, count);
				}

				aDestPixelsRow += mWidth;
				aSrcPixelsRow += theImage->mWidth;
			}
#else
			for (int y = 0; y < theSrcRect.mHeight; y++)
			{
				uint32_t* aDestPixels = aDestPixelsRow;
				EACH_ROW;

				for (int x = 0; x < theSrcRect.mWidth; x++)
				{
					uint32_t src = NEXT_SRC_COLOR;
					uint32_t a = src >> 24;

					if (a == 0)
					{
						aDestPixels++;
					}
					else if (a == 255)
					{
						*(aDestPixels++) = src;
					}
					else
					{
						uint32_t dest = *aDestPixels;
						uint32_t aDestAlpha = dest >> 24;
						uint32_t aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;
						uint32_t blend_a = 255 * a / aNewDestAlpha;
						int oma = 256 - blend_a;

						*(aDestPixels++) = (aNewDestAlpha << 24) |
							((((dest & 0xFF00FF) * oma >> 8) + ((src & 0xFF00FF) * blend_a >> 8)) & 0xFF00FF) |
							((((dest & 0x00FF00) * oma >> 8) + ((src & 0x00FF00) * blend_a >> 8)) & 0x00FF00);
					}
				}

				aDestPixelsRow += mWidth;
				aSrcPixelsRow += theImage->mWidth;
			}
#endif
		}
		else
		{
			int ca = theColor.mAlpha;
			int cr = theColor.mRed;
			int cg = theColor.mGreen;
			int cb = theColor.mBlue;

#ifdef DIRECT_SRC_PIXELS
			uint32_t tint_color = (static_cast<uint32_t>(ca) << 24) |
			                      (static_cast<uint32_t>(cr) << 16) |
			                      (static_cast<uint32_t>(cg) << 8)  |
			                      static_cast<uint32_t>(cb);
			for (int y = 0; y < theSrcRect.mHeight; y++)
			{
				uint32_t* aDestPixels = aDestPixelsRow;
				EACH_ROW;

				int width = theSrcRect.mWidth;
				int left = 0;
				while (left < width && (aSrcPtr[left] >> 24) == 0)
					left++;

				if (left < width)
				{
					int right = width - 1;
					while (right > left && (aSrcPtr[right] >> 24) == 0)
						right--;

					int count = right - left + 1;
					PvZ::Graphics::NeonBlit_AlphaBlend_Tint_ARGB(aDestPixels + left, aSrcPtr + left, count, tint_color);
				}

				aDestPixelsRow += mWidth;
				aSrcPixelsRow += theImage->mWidth;
			}
#else
#ifdef OPTIMIZE_SOFTWARE_DRAWING
			bool performNormalBlit = true;
			if (cr == cg && cg == cb)
			{
				performNormalBlit = false;
				for (int y = 0; y < theSrcRect.mHeight; y++)
				{
					uint32_t* aDestPixels = aDestPixelsRow;

					EACH_ROW;

					for (int x = 0; x < theSrcRect.mWidth; x++)
					{
						uint32_t src = NEXT_SRC_COLOR;
						uint32_t a = ((src >> 24) * ca) / 255;

						if (a != 0)
						{
							uint32_t dest = *aDestPixels;
							uint32_t aDestAlpha = dest >> 24;
							uint32_t aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;
							uint32_t blend_a = 255 * a / aNewDestAlpha;
							int oma = 256 - blend_a;

							*(aDestPixels++) = (aNewDestAlpha << 24) |
								((((dest & 0xFF00FF) * oma >> 8) + ((((src & 0xFF00FF) * cr >> 8) & 0xFF00FF) * blend_a >> 8)) & 0xFF00FF) |
								((((dest & 0x00FF00) * oma >> 8) + ((src & 0x00FF00) * cr * blend_a >> 16)) & 0x00FF00);
						}
						else
							aDestPixels++;
					}

					aDestPixelsRow += mWidth;
					aSrcPixelsRow += theImage->mWidth;
				}
			}
			if (performNormalBlit)
#endif
			{
				for (int y = 0; y < theSrcRect.mHeight; y++)
				{
					uint32_t* aDestPixels = aDestPixelsRow;

					EACH_ROW;

					for (int x = 0; x < theSrcRect.mWidth; x++)
					{
						uint32_t src = NEXT_SRC_COLOR;
						uint32_t a = ((src >> 24) * ca) / 255;

						if (a != 0)
						{
							uint32_t dest = *aDestPixels;
							uint32_t aDestAlpha = dest >> 24;
							uint32_t aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;
							uint32_t blend_a = 255 * a / aNewDestAlpha;
							int oma = 256 - blend_a;

							*(aDestPixels++) = (aNewDestAlpha << 24) |
								((((dest & 0x0000FF) * oma) >> 8) + (((src & 0x0000FF) * blend_a * cb) >> 16) & 0x0000FF) |
								((((dest & 0x00FF00) * oma) >> 8) + (((src & 0x00FF00) * blend_a * cg) >> 16) & 0x00FF00) |
								((((dest & 0xFF0000) * oma) >> 8) + (((((src & 0xFF0000) * blend_a) >> 8) * cr) >> 8) & 0xFF0000);
						}
						else
							aDestPixels++;
					}

					aDestPixelsRow += mWidth;
					aSrcPixelsRow += theImage->mWidth;
				}
			}
#endif
		}
	}
	else
	{
		uchar* aSrcRLAlphaData = aSrcMemoryImage->GetRLAlphaData();
		uchar* aRLAlphaDataRow = aSrcRLAlphaData + (theSrcRect.mY * theImage->mWidth) + theSrcRect.mX;

		for (int y = 0; y < theSrcRect.mHeight; y++)
		{
			uint32_t* aDestPixels = aDestPixelsRow;

			EACH_ROW;

			uchar* aRLAlphaData = aRLAlphaDataRow;

			for (int aSpanLeft = theSrcRect.mWidth; aSpanLeft > 0; )
			{
				uint32_t src = READ_SRC_COLOR;
				uchar rl = *aRLAlphaData;

				if (rl > aSpanLeft)
					rl = aSpanLeft;

				int oma = 256 - (src >> 24);

				if (oma == 1) // Fully opaque
				{
					for (int i = 0; i < rl; i++)
						*aDestPixels++ = NEXT_SRC_COLOR;
				}
				else if (oma == 256) // Fully transparent
				{
					aDestPixels += rl;
					aSrcPtr += rl;
				}
				else // Partially transparent
				{
#ifdef OPTIMIZE_SOFTWARE_DRAWING
					int a = 256 - oma;

					aSrcPtr++;
					uint32_t dest = *aDestPixels;
					*(aDestPixels++) = (0xFF000000) |
						((((dest & 0xFF00FF) * oma >> 8) + ((src & 0xFF00FF) * a >> 8)) & 0xFF00FF) |
						((((dest & 0x00FF00) * oma >> 8) + ((src & 0x00FF00) * a >> 8)) & 0x00FF00);

					for (int i = 1; i < rl; i++)
					{
						uint32_t src = NEXT_SRC_COLOR;
						int a = (src >> 24);
						int oma = 256 - a;

						uint32_t dest = *aDestPixels;
						*(aDestPixels++) = (0xFF000000) |
							((((dest & 0xFF00FF) * oma >> 8) + ((src & 0xFF00FF) * a >> 8)) & 0xFF00FF) |
							((((dest & 0x00FF00) * oma >> 8) + ((src & 0x00FF00) * a >> 8)) & 0x00FF00);
					}
#else
					int a = 256 - oma;

					aSrcPtr++;
					uint32_t dest = *aDestPixels;
					*(aDestPixels++) = (0xFF000000) |
						((((dest & 0x0000FF) * oma) >> 8) + (((src & 0x0000FF) * a) >> 8) & 0x0000FF) |
						((((dest & 0x00FF00) * oma) >> 8) + (((src & 0x00FF00) * a) >> 8) & 0x00FF00) |
						((((dest & 0xFF0000) * oma) >> 8) + (((src & 0xFF0000) * a) >> 8) & 0xFF0000);

					for (int i = 1; i < rl; i++)
					{
						uint32_t src = NEXT_SRC_COLOR;
						int a = (src >> 24);
						int oma = 256 - a;

						uint32_t dest = *aDestPixels;
						*(aDestPixels++) = (0xFF000000) |
							((((dest & 0x0000FF) * oma) >> 8) + (((src & 0x0000FF) * a) >> 8) & 0x0000FF) |
							((((dest & 0x00FF00) * oma) >> 8) + (((src & 0x00FF00) * a) >> 8) & 0x00FF00) |
							((((dest & 0xFF0000) * oma) >> 8) + (((src & 0xFF0000) * a) >> 8) & 0xFF0000);
					}
#endif
				}

				aRLAlphaData += rl;
				aSpanLeft -= rl;
			}

			aDestPixelsRow += mWidth;
			aSrcPixelsRow += theImage->mWidth;
			aRLAlphaDataRow += theImage->mWidth;
		}
	}
}
