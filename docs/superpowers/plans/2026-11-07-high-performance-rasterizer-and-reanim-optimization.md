# High-Performance Software Rasterizer & Reanimator Optimization Plan (Survival Endless)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Elevate Plants vs. Zombies Portable to stable 30+ FPS during dense Survival Endless waves on the Miyoo Mini Plus (Cortex-A7 @ 1.2 GHz, 128 MB RAM, software rendering) by eliminating inner-loop CPU bottlenecks in the software triangle rasterizer (`SWTri`), skeletal reanimation transforms, overlapping shadow quad rendering, and low-alpha cosmetic particle rasterization without altering memory pools or game logic.

**Architecture:** 
1. Replace 24-op scalar RGB blending with 32-bit SWAR (SIMD within a register) dual-channel arithmetic (`(R+B)` and `G`) in [`src/SexyAppFramework/graphics/SWTri/SWTri_Pixel8888.inc`](file:///c:/DEV/PVZ/src/SexyAppFramework/graphics/SWTri/SWTri_Pixel8888.inc), cutting the inner pixel rasterization cost by >50% for all rotated/transformed sprites.
2. Fast-path zero-skew skeletal transforms (`skewX == 0 && skewY == 0`) and replace double-precision `cos()`/`sin()` with hardware single-precision `sincosf()` in [`src/PvzpLib/Reanimator.cpp:MatrixFromTransform`](file:///c:/DEV/PVZ/src/PvzpLib/Reanimator.cpp#L613-L627), saving thousands of transcendental calculations per frame.
3. Spatially deduplicate redundant zombie shadow rasterization in [`src/Lawn/Zombie.cpp:DrawShadow`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp#L9394-L9590) during dense waves, eliminating dozens of overlapping transparent quad passes over identical grass tiles.
4. Dynamically prune invisible/faint cosmetic secondary particles (alpha $\le 20$) under dense horde conditions in [`src/PvzpLib/PvzpParticle.cpp:DrawParticle`](file:///c:/DEV/PVZ/src/PvzpLib/PvzpParticle.cpp#L1018-L1053) while keeping `MAX_PARTICLES_SIZE = 900` strictly untouched to avoid memory crashes.

**Tech Stack:** C++17, ARMv7-A Cortex-A7 NEON/VFPv4, OnionOS/Linux, Docker cross-compilation toolchain (`aemiii91/miyoomini-toolchain`), Trail repo graph.

**Spec:** High-Performance Engine Optimizations for Survival Endless on Miyoo Mini Plus (audit findings 2026-11-07).

## Global Constraints
- Target hardware: SigmaStar SSD202D dual-core Cortex-A7 @ 1.2 GHz, 128 MB total RAM, Miyoo Mini Plus (OnionOS).
- Render pipeline: 100% software rasterization (`PVZ_ENABLE_GLES=0`), 32-bit ARGB8888 frame buffer.
- Memory budget: Steady-state memory footprint $\le 78.0$ MB RAM.
- `MAX_PARTICLES_SIZE`: Must remain strictly 900 (do NOT decrease particle memory pool to avoid SIGSEGV in `DataArrayGetID`).
- Main thread presentation: Must remain synchronous on the main thread (do NOT offload presentation to secondary threads or Core 1).
- Tick loop: Clamped accumulator cadence without catch-up spirals.
- Background task rules: Always execute Python scripts synchronously or terminate tasks immediately (`manage_task(Action='kill')`); no orphaned background processes.

## Review Focus
- High-alpha and low-alpha blending in `SWTri_Pixel8888.inc` must produce identical visual output without color channel bleeding or arithmetic overflow between R and B.
- Linear blend and non-linear blend paths in `SWTri` must both correctly resolve under SWAR arithmetic.
- Skeletal reanimation matrices must remain mathematically exact for walking, idle, dying, and attacking animations across all zombie and plant archetypes.
- Spatial shadow deduplication must only prune visually redundant overlapping shadows in the same row during horde conditions, without clipping isolated zombies or bosses.
- Particle culling must only suppress faint secondary sub-particles (alpha $\le 20$) when zombie counts are high, leaving all cob cannon, cherry bomb, doom-shroom, and melon impact explosions fully intact.

---

### Task 1: SWAR 32-Bit Dual-Channel Blending in Software Triangle Rasterizer

**Files:**
- Modify: [`src/SexyAppFramework/graphics/SWTri/SWTri_Pixel8888.inc:37-88`](file:///c:/DEV/PVZ/src/SexyAppFramework/graphics/SWTri/SWTri_Pixel8888.inc#L37-L88)
- Test: Local build / Docker verification & visual comparison

**Context:**
The software triangle rasterizer is invoked for all rotated, scaled, or skewed sprites (zombie limbs, walking animations, melon projectiles, cob cannon shells, particles). The inner loop in [`SWTri_Pixel8888.inc`](file:///c:/DEV/PVZ/src/SexyAppFramework/graphics/SWTri/SWTri_Pixel8888.inc) currently performs 6 scalar multiplications, 9 bitwise AND operations, and 6 shifts per pixel. For an opaque background (`da >= 254`), which represents over 95% of drawn pixels on the game lawn, SWAR parallel arithmetic blends Red and Blue simultaneously in a single 32-bit register and Green in a second register.

- [ ] **Step 1.1: Implement SWAR blending for the opaque background path (`da >= 254`)**
In [`src/SexyAppFramework/graphics/SWTri/SWTri_Pixel8888.inc`](file:///c:/DEV/PVZ/src/SexyAppFramework/graphics/SWTri/SWTri_Pixel8888.inc):
Replace the scalar per-channel calculations in `if (da >= 254)`:
```cpp
#if !defined(LINEAR_BLEND)
					if (da >= 254)
					{
						unsigned int inv_a = 256 - alpha;
						unsigned int rb = (((tex & 0x00FF00FF) * alpha + (p & 0x00FF00FF) * inv_a) >> 8) & 0x00FF00FF;
						unsigned int g  = (((tex & 0x0000FF00) * alpha + (p & 0x0000FF00) * inv_a) >> 8) & 0x0000FF00;
						*pix = 0xFF000000 | rb | g;
					}
#else
					if (da >= 254)
					{
						unsigned int inv_a = 256 - alpha;
						unsigned int rb = (((tex & 0x00FF00FF) << 8) + (p & 0x00FF00FF) * inv_a) >> 8;
						unsigned int g  = (((tex & 0x0000FF00) << 8) + (p & 0x0000FF00) * inv_a) >> 8;
						*pix = 0xFF000000 | (rb & 0x00FF00FF) | (g & 0x0000FF00);
					}
#endif
```

- [ ] **Step 1.2: Verify math against overflow**
Confirm that `(0x00FF00FF * 255) + (0x00FF00FF * 256)` does not overflow the 32-bit boundary and that the Blue channel product (bits 0..15) never carries into the Red channel (bits 16..31).
Max Blue sum = $255 \times 256 = 65280 = \text{0xFF00} \le \text{0xFFFF}$. Overflow is strictly impossible.

- [ ] **Step 1.3: Verify translucent destination fallback**
Ensure the `da < 254` branch (used when rendering onto transparent offscreen surfaces) retains its existing exact alpha composition logic without regression.

---

### Task 2: Fast-Path Zero-Skew Early-Out & Single-Precision `sincosf` in Skeletal Reanimation

**Files:**
- Modify: [`src/PvzpLib/Reanimator.cpp:613-627`](file:///c:/DEV/PVZ/src/PvzpLib/Reanimator.cpp#L613-L627)
- Test: Reanimation transform unit test / cross-compilation

**Context:**
In dense Survival Endless horde waves, the game updates up to 2,000 skeletal tracks per frame. [`Reanimation::MatrixFromTransform`](file:///c:/DEV/PVZ/src/PvzpLib/Reanimator.cpp#L613-L627) is called for every active track. Over 40% of tracks have zero skew (`mSkewX == 0.0f && mSkewY == 0.0f`). Currently, the engine computes 4 double-precision 64-bit transcendental trigonometric functions (`cos()` and `sin()`) for each track, burning millions of CPU cycles on Cortex-A7.

- [ ] **Step 2.1: Add zero-skew fast-path check**
In [`src/PvzpLib/Reanimator.cpp:MatrixFromTransform`](file:///c:/DEV/PVZ/src/PvzpLib/Reanimator.cpp#L613-L627), check if `theTransform.mSkewX == 0.0f && theTransform.mSkewY == 0.0f`:
```cpp
void Reanimation::MatrixFromTransform(const ReanimatorTransform& theTransform, SexyMatrix3& theMatrix)
{
	if (theTransform.mSkewX == 0.0f && theTransform.mSkewY == 0.0f)
	{
		theMatrix.m00 = theTransform.mScaleX;
		theMatrix.m10 = 0.0f;
		theMatrix.m20 = 0.0f;
		theMatrix.m01 = 0.0f;
		theMatrix.m11 = theTransform.mScaleY;
		theMatrix.m21 = 0.0f;
		theMatrix.m02 = theTransform.mTransX;
		theMatrix.m12 = theTransform.mTransY;
		theMatrix.m22 = 1.0f;
		return;
	}
...
```

- [ ] **Step 2.2: Replace double-precision trigonometric calls with hardware `sincosf`**
For tracks with non-zero skews:
```cpp
	float aSkewX = -DEG_TO_RAD(theTransform.mSkewX);
	float aSkewY = -DEG_TO_RAD(theTransform.mSkewY);

	float sinX, cosX, sinY, cosY;
#if defined(__GNUC__) || defined(__arm__) || defined(__aarch64__)
	sincosf(aSkewX, &sinX, &cosX);
	sincosf(aSkewY, &sinY, &cosY);
#else
	sinX = sinf(aSkewX); cosX = cosf(aSkewX);
	sinY = sinf(aSkewY); cosY = cosf(aSkewY);
#endif

	theMatrix.m00 = cosX * theTransform.mScaleX;
	theMatrix.m10 = -sinX * theTransform.mScaleX;
	theMatrix.m20 = 0.0f;
	theMatrix.m01 = sinY * theTransform.mScaleY;
	theMatrix.m11 = cosY * theTransform.mScaleY;
	theMatrix.m21 = 0.0f;
	theMatrix.m02 = theTransform.mTransX;
	theMatrix.m12 = theTransform.mTransY;
	theMatrix.m22 = 1.0f;
```

---

### Task 3: Spatial Zombie Shadow Deduplication in Dense Combat Rows

**Files:**
- Modify: [`src/Lawn/Zombie.cpp:9394-9590`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp#L9394-L9590)
- Modify: [`src/Lawn/Board.h`](file:///c:/DEV/PVZ/src/Lawn/Board.h) or static frame-cache in `Zombie.cpp`
- Test: Visual check in dense horde scenarios

**Context:**
In Survival Endless, when 20–30 zombies cluster in a single lane, each zombie issues an independent translucent shadow quad via `PvzpBltMatrix` $\rightarrow$ `SWTri`. Visually, stacking 10+ black transparent oval shadows on top of each other saturates to solid black within 2–3 layers; drawing 25 overlapping shadows produces 25 redundant software rasterization passes on the exact same 50-pixel grass patch.

- [ ] **Step 3.1: Add per-frame row shadow tracking**
Maintain a per-frame cache of shadow horizontal coordinates rendered per row (`mRowLastShadowX[MAX_GRID_SIZE_Y]` and `mShadowFrameCounter`).
- [ ] **Step 3.2: Prune redundant overlapping shadows during horde stress**
In [`Zombie::DrawShadow`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp#L9394-L9590):
If `mBoard != nullptr && mBoard->mZombies.mSize > 15`:
Check the zombie's row and horizontal position `aShadowOffsetX`.
If another zombie in the same row has already drawn a shadow within $|X - X_{prev}| \le 22.0\text{f}$ in the current frame, skip drawing the duplicate shadow quad.
Ensure special entities (Zomboss, Bungee, Pogo jumping) bypass deduplication.

---

### Task 4: Dynamic Cosmetic Particle Pruning Under Horde Stress

**Files:**
- Modify: [`src/PvzpLib/PvzpParticle.cpp:1018-1053`](file:///c:/DEV/PVZ/src/PvzpLib/PvzpParticle.cpp#L1018-L1053)
- Test: Verification with massive cob cannon / winter melon barrages

**Context:**
During dense Survival Endless waves, multiple cob cannon impacts and winter melon splashes generate dozens of fading secondary particles (smoke puffs, faint sparkles, residual ice mist). The engine currently renders every particle until its alpha drops below 5 (`aAlpha <= 4`). Faint particles ($5 \le \text{alpha} \le 20$) are barely visible against the combat lawn but require full triangle rasterization.
Crucially: `MAX_PARTICLES_SIZE` must remain at 900 (as established in commit `5e1050a` to prevent `DataArrayGetID` null-pointer crashes). Pruning is strictly visual rendering culling in `DrawParticle`.

- [ ] **Step 4.1: Dynamic alpha culling threshold in `PvzpParticleEmitter::DrawParticle`**
In [`src/PvzpLib/PvzpParticle.cpp:DrawParticle`](file:///c:/DEV/PVZ/src/PvzpLib/PvzpParticle.cpp#L1018-L1053):
Check zombie count and board activity:
```cpp
		int aAlpha = std::clamp(FloatRoundToInt(aParams.mAlpha), 0, 255);
		int aMinAlpha = 4;
		if (gLawnApp && gLawnApp->mBoard && gLawnApp->mBoard->mZombies.mSize > 20)
		{
			aMinAlpha = 20;
		}
		if (aAlpha <= aMinAlpha)
			return;
```
This drops the long-tail fading particles early during heavy combat, freeing up the software rasterizer without altering any memory allocation structures or primary explosion effects.

---

### Task 5: Compilation, Package Deployment & Trail Sync

**Files:**
- Modify: `bin/PvZ` (cross-compiled ARMv7 executable)
- Modify: `packaging/PvZ_MiyooMiniPlus/RApp/PvZ/PvZ`
- Modify: `ports/PvZ/PvZ`
- Modify: `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`

- [ ] **Step 5.1: Cross-compile ARMv7 binary in Docker**
Run CMake configure and build in `aemiii91/miyoomini-toolchain`:
```bash
docker run --rm -v C:\DEV\PVZ:/work -w /work/build-docker-arm aemiii91/miyoomini-toolchain sh -c "make -j4"
```
- [ ] **Step 5.2: Strip binary and update distribution locations**
Strip debug symbols with `arm-linux-gnueabihf-strip` and copy to `bin/PvZ`, `packaging/PvZ_MiyooMiniPlus/RApp/PvZ/PvZ`, and `ports/PvZ/PvZ`.
- [ ] **Step 5.3: Execute automated lifecycle test suite**
Run `tests/test_launch_script.sh` in the Docker container to ensure all 71 tests pass without regression.
- [ ] **Step 5.4: Package release zip synchronously**
Run `python scripts/package_release.py` synchronously to generate `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`.
- [ ] **Step 5.5: Update Trail graph**
Run `trail build` to ensure all modified symbols, files, and spans are fully updated in the repository context graph.
