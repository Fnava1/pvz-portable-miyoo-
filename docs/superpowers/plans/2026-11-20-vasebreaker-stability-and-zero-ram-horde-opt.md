# Vasebreaker Stability, Deterministic Level Completion, and Zero-RAM Zombie Horde Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eradicate save-game restoration crashes in Vasebreaker, guarantee 100% deterministic level completion in "M is for Metal", and accelerate 20+ zombie hordes with zero additional RAM overhead.

**Architecture:** Fortify `Reanimation::TrackExists()` and `Plant::CanUseSpriteCache()` with multi-tier pointer guards and a 2-frame warmup suppression post-load; introduce a self-healing polling loop in `Challenge::ScaryPotterUpdate()` with mind-controlled exit hooks in `Zombie::CheckForBoardEdge()`; implement an ephemeral L1 stack-allocated skeletal pose ring cache for intact zombies and route unrotated quads through `SoftwareRenderer::FastBlitAxisAlignedNEON()`.

**Tech Stack:** C++17, ARMv7-A NEON SIMD, SDL2, glibc Linux (Miyoo Mini Plus SSD202D), Dockerized Miyoo Toolchain.

**Spec:** [docs/designs/estabilizacion_rompejarrones_victoria_hordas_cero_ram.md](file:///c:/DEV/PVZ/docs/designs/estabilizacion_rompejarrones_victoria_hordas_cero_ram.md)

## Global Constraints
- Maximum resident memory ceiling: <= 54.5 MB combat RSS (guaranteeing >= 28.5 MB safety headroom before 83 MB Linux OOM killer).
- Heap memory delta for zombie optimizations: Strictly 0 MB (all matrix caches must be stack-local or data segment ring buffers).
- Visual parity: 100% preservation of all animation frames, zombie accessories (helmets, doors, lights), and 4:3 aspect ratio.
- Backward compatibility: Zero schema modifications to `save/game*.dat` files.

## Review Focus
1. Deserialized reanimation handles returning null `mDefinition` during initial `Board::DrawGameObjects()` pass.
2. Mind-controlled zombies crossing the right screen edge ($X > 850$) when they are the final active entity on the lawn.
3. Breaking the last pot containing sun or a seed packet when hostile zombies have already been killed.
4. Damaged or status-affected zombies (e.g., chilled, buttered, lost head) bypassing pose deduplication to prevent corrupted bone renders.
5. Unrotated quad blit path preserving proper alpha blending and clipping boundaries.

---

### Task 1: Safe Deserialization & Reanimation Linkage Guard

**Files:**
- Modify: `src/PvzpLib/Reanimator.h:L239`
- Modify: `src/PvzpLib/Reanimator.cpp:L1386-L1392`
- Modify: `src/Lawn/Plant.h:L280-L315`
- Modify: `src/Lawn/Plant.cpp:L2773-L2827`
- Modify: `src/Lawn/System/SaveGame.cpp:L2083-L2150`

- [x] **Step 1: Fortify `Reanimation::TrackExists()` and `FindTrackIndex()` in `src/PvzpLib/Reanimator.cpp`**
  Add strict null validation: check `theTrackName != nullptr`, `mDefinition != nullptr`, `mDefinition->mTracks.tracks != nullptr`, and `mDefinition->mTracks.count > 0`.
- [x] **Step 2: Add Warmup Counter and Null Guards to `Plant::CanUseSpriteCache()`**
  In `src/Lawn/Plant.h`, add `int mWarmupCounter = 0;`.
  In `src/Lawn/Plant.cpp`, if `mWarmupCounter > 0`, decrement and return `false`.
  Ensure `aBodyReanim != nullptr`, `aBodyReanim->mDefinition != nullptr`, and multicap head reanimations are validated before calling `IsAnimPlaying()`.
- [x] **Step 3: Reconcile Plant Definitions in `SaveGame.cpp:FixBoardAfterLoad()`**
  In `src/Lawn/System/SaveGame.cpp`, iterate through `theBoard->mPlants` to set `aPlant->mWarmupCounter = 2`.
- [x] **Step 4: Verify Unit Compilation**
  Run toolchain compile check on `Reanimator.cpp`, `Plant.cpp`, and `SaveGame.cpp`.

---

### Task 2: Deterministic Level Completion in Vasebreaker / "M is for Metal"

**Files:**
- Modify: `src/Lawn/Zombie.cpp:L4661-L4668`
- Modify: `src/Lawn/Board.cpp:L401-L413`
- Modify: `src/Lawn/Challenge.h:L170-L195`
- Modify: `src/Lawn/Challenge.cpp:L3850-L3887`

- [x] **Step 1: Mind-Controlled Zombie Off-Screen Exit Hook**
  In `src/Lawn/Zombie.cpp:CheckForBoardEdge()`, when `IsWalkingBackwards() && mPosX > 850.0f`:
  Capture `bool wasMindControlled = mMindControlled;`, execute `DieNoLoot()`, and if `wasMindControlled && mApp->IsScaryPotterLevel()`, invoke `mBoard->mChallenge->ScaryPotterCheckLevelComplete()`.
- [x] **Step 2: Hostile Entity Filter in `Board::AreEnemyZombiesOnScreen()`**
  In `src/Lawn/Board.cpp:AreEnemyZombiesOnScreen()`, ensure zombies marked with `mMindControlled == true` or `IsDeadOrDying() == true` are excluded from the hostile zombie check.
- [x] **Step 3: Implement Continuous Polling `ScaryPotterCheckLevelComplete()`**
  In `src/Lawn/Challenge.h` and `src/Lawn/Challenge.cpp`, define `void ScaryPotterCheckLevelComplete()`.
  Check: `!mBoard->HasLevelAwardDropped() && !mBoard->mLevelComplete && ScaryPotterIsCompleted()`.
  If true, spawn level award at centered coordinates `(BOARD_WIDTH / 2, BOARD_HEIGHT / 2)`.
  Invoke `ScaryPotterCheckLevelComplete()` continuously from `Challenge::ScaryPotterUpdate()`.

---

### Task 3: Zero-RAM Zombie Horde Performance (Pose Deduplication & NEON Blit)

**Files:**
- Modify: `src/PvzpLib/ReanimRigEvaluator.h`
- Modify: `src/PvzpLib/ReanimRigEvaluator.cpp`
- Modify: `src/PvzpLib/Reanimator.cpp:L950-L1050`
- Modify: `src/Lawn/Zombie.cpp:L5866-L6032`
- Modify: `src/Lawn/Board.cpp:L7141-L7171`

- [x] **Step 1: Define Ephemeral Stack/Static Pose Ring Cache in `ReanimRigEvaluator`**
  Create a 4-entry per-type ring buffer indexed by `(ReanimationType, QuantizedAnimFrame, RenderTick)`.
  Provide `bool TryGetCachedPose()` and `void PutCachedPose()`. Heap allocation = 0 MB.
- [x] **Step 2: Connect Pose Deduplication in `Zombie::DrawReanim()`**
  Before full skeletal matrix evaluation for intact zombies (`GetBodyDamageIndex() == 0`, `!mMindControlled`, `mIceTrapCounter == 0`), probe `TryGetCachedPose()`. If hit, copy bone matrices directly.
- [x] **Step 3: Implement NEON Axis-Aligned Fast Blit in `Reanimation::DrawTrack()`**
  In `src/PvzpLib/Reanimator.cpp`, check if `fabsf(aMatrix.m[0][1]) < 0.001f && fabsf(aMatrix.m[1][0]) < 0.001f`.
  If axis-aligned and no color tint, dispatch to `SoftwareRenderer::FastBlitAxisAlignedNEON()`.
- [x] **Step 4: Tune Dual-Core Rig Engagement Threshold**
  In `src/Lawn/Board.cpp:ShouldEngageComputeWorker()`, adjust threshold to 10 active rigs for consistent Core 1 dispatch during heavy waves.

---

### Task 4: Regression Testing, Memory Audit, and Native Build

**Files:**
- Modify: `tests/test_release_package.py`
- Output: `bin/PvZ`
- Output: `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`

- [x] **Step 1: Run Automated Memory and Packaging Tests**
  Run `python tests/test_release_package.py` and confirm 6/6 tests pass.
- [x] **Step 2: Native Cross-Compilation in Docker**
  Execute native ARMv7 build using `/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-g++`.
- [x] **Step 3: Strip Symbols and Refresh Release Package**
  Strip `bin/PvZ` and generate refreshed `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`.
- [x] **Step 4: Refresh Trail Context Graph**
  Run `trail build` to synchronize the repository graph.
