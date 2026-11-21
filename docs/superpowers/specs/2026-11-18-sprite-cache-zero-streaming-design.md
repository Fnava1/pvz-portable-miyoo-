# Sprite Cache Architecture: Synchronous Preload & In-Combat Streaming Decommissioning

* **Author:** Antigravity & Francisco
* **Date:** 2026-11-18
* **Status:** Approved

## 1. Context and Problem Statement

Following performance testing involving multi-mode transitions (Survival -> Zen Garden -> Dr. Zomboss's Revenge / `GAMEMODE_CHALLENGE_FINAL_BOSS`), the game suffered progressive slowdown, periodic frame drops, and an out-of-memory crash. 

Root-cause analysis pinpointed several critical bottlenecks:
1. `Challenge::InitZombieWaves()` did not populate `mZombieAllowed` for `GAMEMODE_CHALLENGE_FINAL_BOSS` and Level 50, falling back to a default that only allowed `ZOMBIE_NORMAL`, `ZOMBIE_TRAFFIC_CONE`, and `ZOMBIE_PAIL`. As a result, none of the high-frequency minions spawned by Dr. Zomboss (such as Football, Polevaulter, Jack-in-the-box, Ladder, Newspaper, Screen Door) were recognized before combat.
2. `SpriteCachePool::PreloadSynchronous(theBoard)` skipped all unlisted minions during initialization.
3. Mid-combat render calls in `Zombie.cpp` (line 5981) and `Plant.cpp` (line 4194) invoked `SpriteStreamer::RequestZombie()` and `SpriteStreamer::RequestPlant()` on every active entity every frame when un-preloaded or non-cached.
4. `SpriteStreamer` opened files (`fopen`/`fread`) synchronously on the main render thread, causing FAT32 SD card I/O stalls (15-40 ms per hit).
5. When `SpriteCachePool` slots or arena memory became saturated (11 zombie slots vs. 14 potential boss spawns), `SpriteStreamer` requests failed permanently, leaking arena space because `FreeSlot` does not roll back the monotonic bump pointer, and continuously fell back to software reanimation on top of the 1,500-bone boss model, causing 100% CPU spikes and OOM termination.
6. Outside of active levels (e.g. in menus, GameSelector, Options), the 12 MB sprite cache pool arena was not reliably unmapped across all exit vectors, retaining stale allocations.

## 2. Selected Architecture: Strategy B

Under **Strategy B (Strict Synchronous Preloading with Zero Dynamic Streaming)**:
1. **Zero Runtime Disk I/O:** Dynamic mid-combat streaming via `SpriteStreamer` is retired/neutralized. No entity shall ever open or read files from disk during active combat frames.
2. **Exhaustive Preload:** All entities permitted or anticipated for a level must be identified and preloaded synchronously before gameplay begins (during level initialization or seed selection).
3. **Graceful Fallback:** Any entity not preloaded into cache simply renders via raw software skeletal reanimation (`aBodyReanim->DrawRenderGroup(...)`) without touching the SD card.
4. **Zero Cache Memory in Menus:** When in menus (GameSelector, TitleScreen, Options, etc.), the 12 MB sprite cache arena must be completely freed via `munmap()`, resetting virtual address space and physical pages to exactly 0 bytes committed.
5. **Robust Save/Load Integration:** Loading saved games via `Board::LoadGame()` must explicitly initialize a clean arena and trigger synchronous preloading.

## 3. Detailed Component Requirements

### Component 1: Zero-Memory Menu Lifecycle
- In `LawnApp::KillBoard()`, unconditionally call:
  ```cpp
  SpriteStreamer::CancelAll();
  SpriteCachePool::FreeLevelArena();
  ```
  Ensure this executes even if `mBoard == nullptr`.
- In `LawnApp::ShowGameSelector()`, call `SpriteStreamer::CancelAll()` and `SpriteCachePool::FreeLevelArena()`.
- In `LawnApp::MakeNewBoard()`, ensure any existing arena is freed before creating the new board instance.
- In `Board::LoadGame()`, invoke `SpriteCachePool::InitLevelArena()` and `SpriteCachePool::PreloadSynchronous(this)`.
- In `SpriteCachePool::FreeLevelArena()`, unmap `sArenaBase` via `munmap()`, set `sArenaBase = nullptr`, and reset offsets to 0.

### Component 2: Decommissioning Dynamic In-Combat Streaming
- In `src/Lawn/Zombie.cpp` (around line 5981): remove `SpriteStreamer::RequestZombie(mZombieType)`. If an entity is not ready in `SpriteCachePool`, draw via `aBodyReanim->DrawRenderGroup(g, theBaseRenderGroup)`.
- In `src/Lawn/Plant.cpp` (around line 4194): remove `SpriteStreamer::RequestPlant(mSeedType)`.
- In `src/PvzpLib/SpriteStreamer.cpp`:
  - `RequestPlant()` and `RequestZombie()` become immediate no-ops returning `false`.
  - `Update()` becomes an immediate no-op returning immediately.

### Component 3: Hardened Preload Recognition
- In `Challenge::InitZombieWaves()`:
  - Add explicit branch for `aGameMode == GAMEMODE_CHALLENGE_FINAL_BOSS || mApp->IsFinalBossLevel()`.
  - Mark allowed minion types: `ZOMBIE_TRAFFIC_CONE`, `ZOMBIE_PAIL`, `ZOMBIE_FOOTBALL`, `ZOMBIE_POLEVAULTER`, `ZOMBIE_NEWSPAPER`, `ZOMBIE_DOOR`, and `ZOMBIE_BUNGEE`.
- In `SpriteCachePool::PreloadSynchronous(Board* theBoard)`:
  - Check if `theBoard->mApp->IsFinalBossLevel()`, ensuring all boss minions that have cached sprites are marked in `aZombieNeeded`.
  - If `theBoard->mApp->IsBungeeBlitzLevel()`, ensure `ZOMBIE_BUNGEE` is flagged.
  - Scan existing `theBoard->mPlants` and `theBoard->mZombies` to guarantee all active entities on field are preloaded.
  - Load sprites sequentially into available slots. If slots fill up (`SPC_MAX_ZOMBIE_SLOTS`), remaining entities smoothly skip without leaking memory.

## 4. Constraints
- System RSS memory must stay strictly $\le$ 78 MB steady-state (85 MB hard ceiling).
- In menus, `sArenaBase` must be `nullptr` (0 bytes).
- Miyoo Mini Plus ARM cross-compilation toolchain and Docker environment must build cleanly with no warnings or errors.
