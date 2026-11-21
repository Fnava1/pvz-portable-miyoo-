# Survival Intermission & Entity Inspection Memory Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the end-of-wave lag spike and prevent OOM kernel termination (`SIGKILL`) when clicking preview zombies or the Almanac/Store buttons in `SeedChooserScreen` during Survival mode.

**Architecture:** Enforce complete release of the 12 MB sprite cache pool arena (`munmap()`) during the survival stage intermission and seed chooser, and guarantee physical heap memory reclamation via `malloc_trim(0)` upon closing `AlmanacDialog` and `StoreScreen` before combat resumes.

**Tech Stack:** C++17, glibc `malloc_trim`, Linux `mmap`/`munmap`, Docker (`miyoo-build:latest`).

**Spec:** [concepts_brief.md](file:///c:/DEV/PVZ/concepts_brief.md)

## Global Constraints

- **Steady-State RSS Budget:** $\le$ 78 MB, hard emergency limit at 85 MB.
- **Zero Cache Memory during Intermission:** `sArenaBase == nullptr` throughout `SeedChooserScreen` and `InitSurvivalStage`.
- **Physical Memory Deallocation:** All modal dialog resources (`DelayLoad_Almanac`, `DelayLoad_Store`) must be unmapped and reclaimed via `malloc_trim(0)` upon dialog dismissal.
- **Platform:** Miyoo Mini Plus (SigmaStar SSD202D, 128 MB RAM, ARMv7 Linux).

---

### Task 1: Unmap Sprite Cache Arena on Survival Intermission & Seed Chooser Entry

**Files:**
- Modify: `src/Lawn/Board.cpp:1483-1505` (`Board::InitSurvivalStage`)
- Modify: `src/LawnApp.cpp:630-642` (`LawnApp::ShowSeedChooserScreen`)

**Interfaces:**
- Consumes: `SpriteCachePool::FreeLevelArena()`, `SpriteStreamer::CancelAll()`, `malloc_trim(0)`
- Produces: `sArenaBase == nullptr` and virtual memory dropped by 12 MB during the entire intermission.

- [x] **Step 1: Replace slot clearing with arena release in `Board::InitSurvivalStage()`**
- [x] **Step 2: Add arena release in `LawnApp::ShowSeedChooserScreen()`**
- [x] **Step 3: Verify build via Docker**
- [x] **Step 4: Commit Task 1 changes**

---

### Task 2: Enforce Physical Modal Asset Eviction on Almanac & Store Dismissal

**Files:**
- Modify: `src/Lawn/Widget/AlmanacDialog.cpp:115-135` (`~AlmanacDialog`)
- Modify: `src/Lawn/Widget/StoreScreen.cpp:174-193` (`~StoreScreen`)
- Modify: `src/Lawn/Widget/SeedChooserScreen.cpp:1167-1191` (`CloseSeedChooser`)

**Interfaces:**
- Consumes: `malloc_trim(0)`
- Produces: Guaranteed return of all 10–12 MB modal textures to Linux kernel before combat restarts.

- [x] **Step 1: Add `malloc_trim(0)` to `AlmanacDialog::~AlmanacDialog()`**
- [x] **Step 2: Add `malloc_trim(0)` to `StoreScreen::~StoreScreen()`**
- [x] **Step 3: Ensure heap compaction in `SeedChooserScreen::CloseSeedChooser()`**
- [x] **Step 4: Verify build via Docker**

---

### Task 3: In-Game Almanac Lazy On-Demand Texture Loading & Double-Init Elimination

**Files:**
- Modify: `src/PvzpLib/PvzpCommon.cpp` (`PvzpResourceManager::PvzpLoadNextResource`)
- Modify: `src/Resources.cpp` (`Sexy::ExtractDelayLoad_AlmanacResources`)
- Modify: `src/Lawn/Widget/AlmanacDialog.h` (`AlmanacDialog`, `EnsurePageResources`)
- Modify: `src/Lawn/Widget/AlmanacDialog.cpp` (`EnsurePageResources`, `SetPage`, `ClearPlantsAndZombies`, `Draw*`)
- Modify: `src/LawnApp.cpp` (`DoAlmanacDialog`)

**Interfaces:**
- Consumes: `LoadImage`, `DeleteImage`, `CleanSharedImages`, `malloc_trim(0)`
- Produces: In-game Almanac never holds more than 1 page backdrop in RAM (~2.5 MB vs 10.5 MB). Static backdrop on board freed while Almanac is open (+1.92 MB headroom). RSS during entity inspection capped at ~74 MB.

- [x] **Step 1: Defer loading full backdrops in `PvzpLoadNextResource()` when `mBoard != nullptr`**
- [x] **Step 2: Use non-throwing `GetImage()` in `ExtractDelayLoad_AlmanacResources`**
- [x] **Step 3: Add constructor initial page routing and `EnsurePageResources` in `AlmanacDialog`**
- [x] **Step 4: Free board `mStaticBackdrop` on Almanac open and reclaim via `malloc_trim(0)`**
- [x] **Step 5: Evict previous page backdrop and cards on page transition when in-game**
- [x] **Step 6: Free zombie reanimation definition on `ClearPlantsAndZombies()` if not allowed on board**

---

### Task 4: Build, Strip, Stage and Release Packaging

**Files:**
- Modify: `bin/PvZ`
- Modify: `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`
- Modify: `ports/Games/Plants vs. Zombies/PvZ`

**Interfaces:**
- Consumes: `build/PvZ`
- Produces: Stripped ARM binary and validated distribution zip.

- [x] **Step 1: Docker build and strip**
- [x] **Step 2: Stage binaries**
- [x] **Step 3: Run release packaging verification synchronously**
