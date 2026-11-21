# Bounded 16-Bit Plant Cache & Reanim Rasterizer Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore locked 60.0 FPS nominal speed on the Miyoo Mini Plus by reintroducing a 3.5 MB in-memory ARGB4444 plant idle cache and an axis-aligned NEON Reanim fast-path, keeping steady-state combat RSS $\le 54.5\text{ MB}$.

**Architecture:** Single-writer architecture on CPU Core 0 using a 3.5 MB slab sub-allocated from `DmaTexturePool`. Plant idle frames are baked dynamically in RAM during level load transitions (`InitLevel`, `InitSurvivalStage`, `PuzzleNextStageClear`, `LoadGame`) with zero disk `.spc` dependencies. Non-rotated Reanim bone tracks bypass software triangle rasterization via direct SIMD blits.

**Tech Stack:** C++20, ARMv7 hard-float, ARM NEON SIMD, SDL2, Linux `statm` telemetry, CMake, Docker toolchain.

**Spec:** `docs/designs/optimizacion_cache_plantas_16bit_reanim_neon.md`

## Global Constraints

* Target platform: Miyoo Mini Plus / OnionOS (SigmaStar SSD202D dual-core Cortex-A7 @ 1.2 GHz, 128 MB RAM, 640×480 framebuffer).
* Linux kernel OOM killer ceiling: 83.0 MB RSS.
* Steady-state combat RSS must remain $\le 54.5\text{ MB}$ ($\ge 28.5\text{ MB}$ safety margin below 83.0 MB).
* Zero `.spc` sprite files required on disk; 100% clean-room Bring-Your-Own-Data (BYOD) compliant.
* Zero multi-threaded software rasterization to the framebuffer; Core 0 retains 100% of framebuffer blitting.
* Strict 100% visual fidelity: shooting heads, damaged accessories, strobe lights, and freeze overlays must match original PC GOTY presentation.

## Review Focus

1. **Clean-Room In-RAM Baking Failure:** If a plant definition fails to load or bake, the system must not crash or leave uninitialized memory; it must fall back cleanly to standard Reanimation rendering.
2. **Shooting / Accessory State Leaks:** A plant shooting a pea, taking bite damage, or sleeping must never render a cached idle sprite; `CanUseSpriteCache()` must return `false` on the exact frame the state changes.
3. **Buffer Overrun in Bounded Slab:** Bounded slab allocation (3.5 MB) must strictly reject allocations exceeding the budget without crashing or heap spilling.
4. **Non-Rotated Fast-Path False Positives:** Tracks with small non-zero rotations (shear or angle) must not be treated as axis-aligned; the epsilon guard must strictly reject $\lvert m01 \rvert \ge 1\times 10^{-4}$ or $\lvert m10 \rvert \ge 1\times 10^{-4}$.
5. **Level Teardown Pointer Dangling:** Exiting to title or restarting must reset `sArenaBase` and slot ready flags so subsequent levels do not read stale memory.

---

### Task 1: Bounded Arena & Dynamic In-RAM Baker in `SpriteCachePool`

**Files:**
- Modify: `src/PvzpLib/SpriteCachePool.h`
- Modify: `src/PvzpLib/SpriteCachePool.cpp`

- [x] **Step 1: Update pool constants and structures in `SpriteCachePool.h`**
  - Set `POOL_MAX_BUDGET = 3670016` (3.5 MB).
  - Eliminate `ZOMBIE_ZONE_SIZE` and set `SPC_MAX_ZOMBIE_SLOTS = 0` (or disable zombie slots).
  - Declare `static bool BakePlantInRam(SeedType theSeed, ReanimatorDefinition* theDef);`.
  - Remove all disk path references (`SPC_CACHE_DIR`).

- [x] **Step 2: Re-implement `InitLevelArena()` and `FreeLevelArena()` in `SpriteCachePool.cpp`**
  - In `InitLevelArena()`: Allocate a 3.5 MB buffer using `PvZ::Graphics::DmaTexturePool::Instance().Allocate(POOL_MAX_BUDGET, 64)`.
  - In `FreeLevelArena()`: Reset offsets (`sPlantArenaOffset = 0`, `sTotalAllocatedBytes = 0`, `sArenaBase = nullptr`) and mark all slots `mId = -1`, `mIsReady = false`.

- [x] **Step 3: Implement `BakePlantInRam()` in `SpriteCachePool.cpp`**
  - Iterate 24 keyframes ($t = 0.0f \dots \frac{23}{24.0f}$) of `anim_idle`.
  - Render frames to a temporary scratch surface, calculate non-zero alpha bounding box, quantize to ARGB4444, and store in `slot->mPixelData`.
  - Mark `slot->mIsReady = true`.

- [x] **Step 4: Update `PreloadSynchronous(Board* theBoard)`**
  - Loop through `theBoard->mSeedBank->mSeedPackets`.
  - For each active seed type, call `BakePlantInRam()` directly from the in-memory `ReanimatorDefinition`.
  - Remove all `fopen` / `fread` calls to `.spc` files on disk.

- [x] **Step 5: Verify build compiles cleanly in Docker**
  - Run `docker run --rm -v "c:/DEV/PVZ:/root/workspace" -w /root/workspace/build miyoo-build:latest cmake --build . --target PvZ -j4`.

---

### Task 2: Plant Kinematics Bypass & Draw Hookup in `Plant.cpp`

**Files:**
- Modify: `src/Lawn/Plant.cpp`
- Modify: `src/Lawn/Plant.h`

- [x] **Step 1: Verify and strengthen `Plant::CanUseSpriteCache()`**
  - Ensure guards return `false` if `mShootingCounter > 0`, `mRecentlyEatenCountdown > 0`, `mEatenFlashCountdown > 0`, `mIsAsleep`, `mState == PlantState::STATE_DOINGSPECIAL`, or `!aBodyReanim->IsAnimPlaying("anim_idle")`.
  - For multi-head plants (Threepeater, Split Pea) and single-head shooters, confirm head reanimations are verified in idle loops.

- [x] **Step 2: Hook kinematics bypass in `Plant::Animate()`**
  - When `CanUseSpriteCache()` evaluates `true`, set `skipPlantBoneUpdate = true` and call `aBodyReanim->UpdateAnimationTimeOnly()`.
  - Confirm that 300–500 trigonometric and matrix operations per frame are bypassed for idle plants.

- [x] **Step 3: Wire sprite drawing in `Plant::Draw()`**
  - When `CanUseSpriteCache()` evaluates `true` and `SpriteCachePool::IsPlantReady(mSeedType)` evaluates `true`, call `SpriteCachePool::DrawPlant(g, aOffsetX, aOffsetY, mSeedType, aBodyReanim->mAnimTime)`.
  - If false or on cache miss, fall back cleanly to `aBodyReanim->Draw(g)`.

- [x] **Step 4: Verify build compiles cleanly in Docker**
  - Run `docker run --rm -v "c:/DEV/PVZ:/root/workspace" -w /root/workspace/build miyoo-build:latest cmake --build . --target PvZ -j4`.

---

### Task 3: Axis-Aligned NEON Fast-Path in `Reanimator.cpp` & `SoftwareRenderer`

**Files:**
- Modify: `src/graphics/SoftwareRenderer.hpp`
- Modify: `src/graphics/SoftwareRenderer.cpp`
- Modify: `src/PvzpLib/Reanimator.cpp`

- [x] **Step 1: Add axis-aligned blit primitive in `SoftwareRenderer`**
  - Implement `DrawImageAxisAligned_NEON(SoftwareSurface* src, const Rect& dst_rect, const Rect& src_rect, const Color& tint)`.
  - Route non-rotated ARGB4444 spans directly to `NeonBlit_Span_ARGB4444_PreMul` or `NeonBlit_AlphaBlend_ARGB`.

- [x] **Step 2: Implement rotation detection in `Reanimator::DrawTrack()`**
  - Inspect matrix transform: check if $\lvert m01 \rvert < 1\times 10^{-4}$ and $\lvert m10 \rvert < 1\times 10^{-4}$.
  - When non-rotated, compute destination rectangle $(x, y, w, h)$ directly from matrix scaling/translation.
  - Bypass `MemoryImage::BltTrianglesTex()`, `SWHelper::SWDrawShape()`, and Sutherland-Hodgman polygon clipping.

- [x] **Step 3: Verify build compiles cleanly in Docker**
  - Run `docker run --rm -v "c:/DEV/PVZ:/root/workspace" -w /root/workspace/build miyoo-build:latest cmake --build . --target PvZ -j4`.

---

### Task 4: Board Lifecycle Hooks, Memory Polling & Puzzle Streak Support

**Files:**
- Modify: `src/Lawn/Board.cpp`
- Modify: `src/Lawn/Challenge.cpp`

- [x] **Step 1: Wire lifecycle in `Board::InitLevel()` and `Board::InitSurvivalStage()`**
  - Call `SpriteCachePool::InitLevelArena()`.
  - Call `SpriteCachePool::PreloadSynchronous(this)`.

- [x] **Step 2: Wire cleanup in `Board::DisposeBoard()`**
  - Call `SpriteCachePool::FreeLevelArena()`.

- [x] **Step 3: Add periodic RSS telemetry guardrail in `Board::Update()`**
  - Every 60 frames, read `/proc/self/statm` on Linux.
  - If RSS exceeds 65.0 MB, set an engine flag to disable new cache allocations to prevent kernel OOM killer invocation.

- [x] **Step 4: Update `Challenge::PuzzleNextStageClear()`**
  - In Vasebreaker streak transitions, ensure `SpriteCachePool::ClearPlantSlots()` and `SpriteCachePool::PreloadSynchronous(mBoard)` are called.

- [x] **Step 5: Verify build compiles cleanly in Docker**
  - Run `docker run --rm -v "c:/DEV/PVZ:/root/workspace" -w /root/workspace/build miyoo-build:latest cmake --build . --target PvZ -j4`.

---

### Task 5: Release Package Test Assertions & Memory Audit Verification

**Files:**
- Modify: `tests/test_release_package.py`
- Modify: `scripts/package_release.py`

- [x] **Step 1: Update static memory assertions in `tests/test_release_package.py`**
  - Allocate 3.5 MB for `sprite_cache`.
  - Assert steady-state combat RSS $\le 54.5\text{ MB}$.
  - Assert safety headroom $\ge 28.5\text{ MB}$ below 83.0 MB.
  - Assert clean-room compliance: zero `.spc` files bundled.

- [x] **Step 2: Execute release package tests**
  - Run `python tests/test_release_package.py` and confirm 6/6 tests pass.

---

### Task 6: Native ARMv7 Compilation, Binary Stripping & Package Staging

**Files:**
- Modify: `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`
- Modify: staged binaries in `bin/` and `dist/`

- [x] **Step 1: Full native release compile**
  - Build `PvZ` target via Docker container with `-j$(nproc)`.

- [x] **Step 2: Strip symbols**
  - Run `/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip build/PvZ`.

- [x] **Step 3: Package release distribution**
  - Run `python scripts/package_release.py`.
  - Verify zip integrity and file size.
