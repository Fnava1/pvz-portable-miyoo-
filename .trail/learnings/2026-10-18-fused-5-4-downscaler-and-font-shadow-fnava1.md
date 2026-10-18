---
title: Fused 5:4 downscaler and high-contrast font shadow
author: Fnava1
date: 2026-10-18
branch: main
touches:
  - src/SexyAppFramework/graphics/GLInterface.cpp
  - src/SexyAppFramework/graphics/ImageFont.cpp
  - src/SexyAppFramework/graphics/ImageFont.h
  - ports/Games/Plants vs. Zombies/launch.sh
---

## Decided
- Replace 2-pass RAM-bound bilinear downscaler (PVZ_SMOOTH_SCALE=1) with a 1-pass fused 5:4 downscaler in GLInterface::Flush: rows 0, 1, 2 downscaled directly to dst without scratch memory; row 3 fused with row 4 via AverageRow800 (NEON vrhaddq_u8) in a 3.2 KB L1 scratch buffer.
- Small single-layer fonts (<= 16 pt) with light color (luminance >= 90) dynamically receive a micro drop shadow (+1, +1) in ImageFont::DrawStringEx.
- Font drop shadows are enqueued at order (anOrder + 128 - 1) = order 127, while glyph foreground runs at order 128, ensuring all shadows across the string render strictly behind all foreground glyphs.

## Tried and ruled out
- Morphological stroke dilation on atlas textures: permanently degraded curved, handwritten glyphs (FONT_BRIANNETOD16 in Welcome Back sign) by closing character apertures ('e', 'a', 'o', 'd') and creating asymmetrical blocky artifacts.
- 2-pass intermediate row buffering in DRAM (sHRows[5][640]): consumed ~4 MB DRAM read/write per frame (~240 MB/s), taking 9-11 ms per frame and causing severe frame rate drops on SigmaStar SSD202D.

## Watch out
- Do not apply drop shadows to dark text (e.g. Crazy Dave's dialog) or multi-layer fonts that already have native outline layers (e.g. CONTINUUMBOLD14OUTLINE).
- Keep L1 scratch buffer aligned and bounded (800 uint32_t = 3.2 KB) to prevent cache thrashing or stack overflows.
