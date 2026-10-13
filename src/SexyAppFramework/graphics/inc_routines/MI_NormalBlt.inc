{
	uint32_t* aDestPixelsRow = ((uint32_t*) GetBits()) + (theY * mWidth) + theX;

	if ((mHasAlpha) || (mHasTrans) || (theColor != Color::White))
	{
		if (theColor == Color::White)
		{
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

						if (aDestAlpha == 255)
						{
							int oma = 256 - a;
							*(aDestPixels++) = 0xFF000000 |
								((((dest & 0xFF00FF) * oma >> 8) + ((src & 0xFF00FF) * a >> 8)) & 0xFF00FF) |
								((((dest & 0x00FF00) * oma >> 8) + ((src & 0x00FF00) * a >> 8)) & 0x00FF00);
						}
						else if (aDestAlpha == 0)
						{
							*(aDestPixels++) = src;
						}
						else
						{
							uint32_t aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;
							uint32_t blend_a = (255 * a) / aNewDestAlpha;
							int oma = 256 - blend_a;
							*(aDestPixels++) = (aNewDestAlpha << 24) |
								((((dest & 0xFF00FF) * oma >> 8) + ((src & 0xFF00FF) * blend_a >> 8)) & 0xFF00FF) |
								((((dest & 0x00FF00) * oma >> 8) + ((src & 0x00FF00) * blend_a >> 8)) & 0x00FF00);
						}
					}
				}

				aDestPixelsRow += mWidth;
				aSrcPixelsRow += theImage->mWidth;
			}
		}
		else
		{
			int ca = theColor.mAlpha;
			int cr = theColor.mRed;
			int cg = theColor.mGreen;
			int cb = theColor.mBlue;

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

						if (a == 0)
						{
							aDestPixels++;
						}
						else
						{
							uint32_t dest = *aDestPixels;
							uint32_t aDestAlpha = dest >> 24;
							uint32_t c_rb = (((src & 0xFF00FF) * cr >> 8) & 0xFF00FF);
							uint32_t c_g  = (((src & 0x00FF00) * cr >> 8) & 0x00FF00);

							if (aDestAlpha == 255)
							{
								int oma = 256 - a;
								*(aDestPixels++) = 0xFF000000 |
									((((dest & 0xFF00FF) * oma >> 8) + ((c_rb * a) >> 8)) & 0xFF00FF) |
									((((dest & 0x00FF00) * oma >> 8) + ((c_g * a) >> 8)) & 0x00FF00);
							}
							else if (aDestAlpha == 0)
							{
								*(aDestPixels++) = (a << 24) | c_rb | c_g;
							}
							else
							{
								uint32_t aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;
								uint32_t blend_a = (255 * a) / aNewDestAlpha;
								int oma = 256 - blend_a;
								*(aDestPixels++) = (aNewDestAlpha << 24) |
									((((dest & 0xFF00FF) * oma >> 8) + ((c_rb * blend_a) >> 8)) & 0xFF00FF) |
									((((dest & 0x00FF00) * oma >> 8) + ((c_g * blend_a) >> 8)) & 0x00FF00);
							}
						}
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

						if (a == 0)
						{
							aDestPixels++;
						}
						else
						{
							uint32_t dest = *aDestPixels;
							uint32_t aDestAlpha = dest >> 24;
							uint32_t cb_val = (((src & 0x0000FF) * cb) >> 8) & 0x0000FF;
							uint32_t cg_val = (((src & 0x00FF00) * cg) >> 8) & 0x00FF00;
							uint32_t cr_val = (((src & 0xFF0000) * cr) >> 8) & 0xFF0000;

							if (aDestAlpha == 255)
							{
								int oma = 256 - a;
								*(aDestPixels++) = 0xFF000000 |
									((((dest & 0x0000FF) * oma) >> 8) + (((cb_val * a) >> 8) & 0x0000FF)) |
									((((dest & 0x00FF00) * oma) >> 8) + (((cg_val * a) >> 8) & 0x00FF00)) |
									((((dest & 0xFF0000) * oma) >> 8) + (((cr_val * a) >> 8) & 0xFF0000));
							}
							else if (aDestAlpha == 0)
							{
								*(aDestPixels++) = (a << 24) | cr_val | cg_val | cb_val;
							}
							else
							{
								uint32_t aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;
								uint32_t blend_a = (255 * a) / aNewDestAlpha;
								int oma = 256 - blend_a;
								*(aDestPixels++) = (aNewDestAlpha << 24) |
									((((dest & 0x0000FF) * oma) >> 8) + (((cb_val * blend_a) >> 8) & 0x0000FF)) |
									((((dest & 0x00FF00) * oma) >> 8) + (((cg_val * blend_a) >> 8) & 0x00FF00)) |
									((((dest & 0xFF0000) * oma) >> 8) + (((cr_val * blend_a) >> 8) & 0xFF0000));
							}
						}
					}

					aDestPixelsRow += mWidth;
					aSrcPixelsRow += theImage->mWidth;
				}
			}
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
