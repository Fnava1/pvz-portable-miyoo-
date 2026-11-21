# Sprite Cache Architecture: Zero-Streaming & Menu Arena Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate combat stutters, memory exhaustion, and crashes during multi-mode traversals (Survival -> Zen Garden -> Zomboss Revenge) by replacing dynamic mid-combat sprite streaming with strict synchronous level-start preloading and guaranteeing zero memory usage in menus.

**Architecture:** Decommission `SpriteStreamer` runtime file I/O so that un-cached entities render via raw software skeletal reanimation with zero disk access. Harden `LawnApp::KillBoard()`, `ShowGameSelector()`, and `Board::LoadGame()` to strictly unmap and reset the 12 MB sprite arena in all menus (0 bytes allocated). Extend `Challenge::InitZombieWaves()` and `SpriteCachePool::PreloadSynchronous()` to recognize and preload all supported Dr. Zomboss minions upfront.

**Tech Stack:** C++17, SDL2, ARM NEON, Linux `mmap`/`munmap`, CMake, Docker (`miyoo-build:latest`).

**Spec:** [docs/superpowers/specs/2026-11-18-sprite-cache-zero-streaming-design.md](file:///c:/DEV/PVZ/docs/superpowers/specs/2026-11-18-sprite-cache-zero-streaming-design.md)

## Global Constraints

- **Steady-State RSS Budget:** Strict $\le$ 78 MB, hard emergency ceiling at 85 MB.
- **Zero Menu Memory:** `sArenaBase` must be `nullptr` (0 bytes allocated/mapped) in GameSelector, TitleScreen, Options, and Zen Garden.
- **Zero Combat Disk I/O:** No `fopen`, `fread`, or `fclose` allowed on the main render thread during combat ticks.
- **Synchronous Process Termination:** Any Python tool/script execution must complete synchronously or be killed immediately via `manage_task(Action='kill')`; no orphan background tasks.
- **Platform:** Miyoo Mini Plus (ARMv7 Cortex-A7, Linux, SigmaStar SSD202D, 128 MB RAM).

## Review Focus

1. **Player quits to Main Menu during active level:** `KillBoard()` must invoke `FreeLevelArena()` unconditionally even if `mBoard == nullptr`, ensuring `sArenaBase` is unmapped.
2. **Resuming a saved game (`Board::LoadGame()`):** Must call `InitLevelArena()` and `PreloadSynchronous(this)` to ensure fresh cache initialization without stale arena pointers.
3. **Dr. Zomboss spawns minions without `.spc` files (e.g. Jack-in-the-box, Zamboni, Gargantuar):** Must cleanly fall back to software skeletal reanimation with zero file open attempts or queue overhead.
4. **Levels with > 11 allowed zombie types:** Must safely fill up to `SPC_MAX_ZOMBIE_SLOTS` and let excess types render via reanimation without crashing or leaking memory.
5. **Repeated multi-mode navigation (Survival -> Zen Garden -> Boss -> Menu):** Arena unmap and re-init must be idempotent with zero virtual address leaks.

---

### Task 1: Zero-Memory Menu Lifecycle & Arena Teardown

**Files:**
- Modify: `src/LawnApp.cpp:270-305, 391-405, 510-530`
- Modify: `src/Lawn/Board.cpp:595-605`
- Modify: `src/PvzpLib/SpriteCachePool.cpp:80-100`

**Interfaces:**
- Consumes: `SpriteCachePool::FreeLevelArena()`, `SpriteStreamer::CancelAll()`, `SpriteCachePool::InitLevelArena()`, `SpriteCachePool::PreloadSynchronous(Board*)`
- Produces: `sArenaBase == nullptr` in menus; guaranteed clean cache on saved game load.

- [ ] **Step 1: Check existing teardown calls**
  Verify where `SpriteCachePool::FreeLevelArena()` is currently invoked (`src/Lawn/Board.cpp:294`) and that `LawnApp::KillBoard()` does not call it when `mBoard == nullptr`.

- [ ] **Step 2: Add unconditional arena teardown in `LawnApp`**
  In `src/LawnApp.cpp`:
  - In `LawnApp::KillBoard()`: Add unconditional calls to `SpriteStreamer::CancelAll()` and `SpriteCachePool::FreeLevelArena()` at the end of the method (after deleting `mBoard` or if `mBoard` was already null).
  - In `LawnApp::ShowGameSelector()`: Call `SpriteStreamer::CancelAll()` and `SpriteCachePool::FreeLevelArena()`.
  - In `LawnApp::MakeNewBoard()`: Ensure `SpriteStreamer::CancelAll()` and `SpriteCachePool::FreeLevelArena()` run before creating a new `Board`.

- [ ] **Step 3: Add arena init and preload in `Board::LoadGame()`**
  In `src/Lawn/Board.cpp:600`:
  - Inside `Board::LoadGame()`, immediately before returning `true`, call `SpriteCachePool::PreloadSynchronous(this)` to ensure saved games initialize the cache pool.

- [ ] **Step 4: Verify compilation of Task 1**
  Run: `docker run --rm -v "C:\DEV\PVZ:/root/workspace" -w /root/workspace miyoo-build:latest /bin/bash -c "cmake --build build -j 4"`
  Expected: Clean build without errors in `LawnApp.o`, `Board.o`, `SpriteCachePool.o`.

- [ ] **Step 5: Commit Task 1 changes**
  Run: `git add src/LawnApp.cpp src/Lawn/Board.cpp`
  Run: `git commit -m "fix(cache): enforce zero-memory arena teardown in menus and support LoadGame preload"`

---

### Task 2: Decommission Mid-Combat Dynamic Streaming

**Files:**
- Modify: `src/Lawn/Zombie.cpp:5970-5988`
- Modify: `src/Lawn/Plant.cpp:4190-4200`
- Modify: `src/PvzpLib/SpriteStreamer.cpp:30-195`

**Interfaces:**
- Consumes: `SpriteCachePool::DrawZombie()`, `SpriteCachePool::DrawPlant()`
- Produces: Complete removal of runtime SD card access on render thread; zero queuing overhead.

- [ ] **Step 1: Inspect call sites in `Zombie.cpp` and `Plant.cpp`**
  Review lines `Zombie.cpp:5980-5987` and `Plant.cpp:4193-4198` where `SpriteStreamer::RequestZombie()` and `SpriteStreamer::RequestPlant()` are invoked.

- [ ] **Step 2: Remove runtime streaming requests from `Zombie.cpp`**
  In `src/Lawn/Zombie.cpp:5981-5984`:
  Remove the `else if (SpriteCache_GetZombieEntityInfo(mZombieType) != nullptr && !SpriteCachePool::IsZombieReady(mZombieType))` branch that calls `SpriteStreamer::RequestZombie(mZombieType)`.
  When a zombie cannot use walk cache or is not ready in cache, it proceeds directly to `aBodyReanim->DrawRenderGroup(g, theBaseRenderGroup);`.

- [ ] **Step 3: Remove runtime streaming requests from `Plant.cpp`**
  In `src/Lawn/Plant.cpp:4193-4197`:
  Remove the `if (!canUseIdleCache && !SpriteCachePool::IsPlantReady(mSeedType) && SpriteCache_GetPlantEntityInfo(mSeedType) != nullptr)` block that calls `SpriteStreamer::RequestPlant(mSeedType)`.

- [ ] **Step 4: Neutralize `SpriteStreamer` runtime methods**
  In `src/PvzpLib/SpriteStreamer.cpp`:
  - `SpriteStreamer::RequestPlant(SeedType)`: Return `false` immediately.
  - `SpriteStreamer::RequestZombie(ZombieType)`: Return `false` immediately.
  - `SpriteStreamer::Update()`: Return immediately (no file `fread` or queue iteration during combat).

- [ ] **Step 5: Verify compilation of Task 2**
  Run: `docker run --rm -v "C:\DEV\PVZ:/root/workspace" -w /root/workspace miyoo-build:latest /bin/bash -c "cmake --build build -j 4"`
  Expected: Clean compilation with no undefined symbols or link errors.

- [ ] **Step 6: Commit Task 2 changes**
  Run: `git add src/Lawn/Zombie.cpp src/Lawn/Plant.cpp src/PvzpLib/SpriteStreamer.cpp`
  Run: `git commit -m "perf(streamer): decommission dynamic mid-combat streaming to prevent SD I/O frame drops"`

---

### Task 3: Exhaustive Zombie Recognition for Final Boss & Special Modes

**Files:**
- Modify: `src/Lawn/Challenge.cpp:2316-2516`
- Modify: `src/PvzpLib/SpriteCachePool.cpp:640-815`

**Interfaces:**
- Consumes: `Challenge::InitZombieWaves()`, `SpriteCachePool::PreloadSynchronous()`
- Produces: Complete preloading of Dr. Zomboss minions in `PreloadSynchronous()`.

- [ ] **Step 1: Add boss mode check to `Challenge::InitZombieWaves()`**
  In `src/Lawn/Challenge.cpp`:
  Add a branch before the default `else` in `InitZombieWaves()`:
  ```cpp
  else if (aGameMode == GAMEMODE_CHALLENGE_FINAL_BOSS || mApp->IsFinalBossLevel())
  {
      aList[ZOMBIE_TRAFFIC_CONE] = true;
      aList[ZOMBIE_PAIL] = true;
      aList[ZOMBIE_FOOTBALL] = true;
      aList[ZOMBIE_POLEVAULTER] = true;
      aList[ZOMBIE_NEWSPAPER] = true;
      aList[ZOMBIE_DOOR] = true;
      aList[ZOMBIE_BUNGEE] = true;
  }
  ```

- [ ] **Step 2: Harden `SpriteCachePool::PreloadSynchronous()`**
  In `src/PvzpLib/SpriteCachePool.cpp`:
  - In `PreloadSynchronous()`, if `theBoard->mApp->IsFinalBossLevel()`, explicitly set:
    `aZombieNeeded[ZOMBIE_TRAFFIC_CONE] = true;`
    `aZombieNeeded[ZOMBIE_PAIL] = true;`
    `aZombieNeeded[ZOMBIE_FOOTBALL] = true;`
    `aZombieNeeded[ZOMBIE_POLEVAULTER] = true;`
    `aZombieNeeded[ZOMBIE_NEWSPAPER] = true;`
    `aZombieNeeded[ZOMBIE_DOOR] = true;`
  - Ensure that only minions with existing `.spc` files (`SpriteCache_GetZombieEntityInfo(zt) != nullptr`) attempt to open cache files.
  - If `slot == nullptr` (because `SPC_MAX_ZOMBIE_SLOTS` is reached or arena memory limit is hit), log cleanly and skip without corrupting the slot array.

- [ ] **Step 3: Verify compilation of Task 3**
  Run: `docker run --rm -v "C:\DEV\PVZ:/root/workspace" -w /root/workspace miyoo-build:latest /bin/bash -c "cmake --build build -j 4"`
  Expected: Clean compilation of `Challenge.o` and `SpriteCachePool.o`.

- [ ] **Step 4: Commit Task 3 changes**
  Run: `git add src/Lawn/Challenge.cpp src/PvzpLib/SpriteCachePool.cpp`
  Run: `git commit -m "fix(boss): preload all supported Dr. Zomboss minions synchronously before combat"`

---

### Task 4: Compilation, Binary Stripping, Staging & Packaging

**Files:**
- Modify: `bin/PvZ`
- Modify: `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`
- Modify: `ports/Games/Plants vs. Zombies/PvZ`

**Interfaces:**
- Consumes: `build/PvZ`
- Produces: Stripped release ARM binary staged across all deployment trees.

- [ ] **Step 1: Execute full Docker build and strip**
  Run:
  `docker run --rm -v "C:\DEV\PVZ:/root/workspace" -w /root/workspace miyoo-build:latest /bin/bash -c "cmake --build build -j 4 && /opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-strip build/PvZ"`
  Expected: Exit code 0, binary stripped.

- [ ] **Step 2: Stage binaries to destination directories**
  Copy `build/PvZ` to:
  - `bin/PvZ`
  - `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`
  - `ports/Games/Plants vs. Zombies/PvZ`

- [ ] **Step 3: Run release packaging verification synchronously**
  Run: `python scripts/package_release.py` synchronously and verify it exits cleanly with code 0.
  Ensure task is killed/verified immediately per process termination rules.

- [ ] **Step 4: Commit release staging**
  Run: `git add bin/ packaging/ ports/`
  Run: `git commit -m "chore(release): stage hardened zero-streaming sprite cache binary"`
