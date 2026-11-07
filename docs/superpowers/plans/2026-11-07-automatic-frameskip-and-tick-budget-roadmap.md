# Roadmap & Implementation Plan: Automatic Frameskip & Decoupled Tick Budget for PvZ Portable

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate slow-motion gameplay in 1.0x and stutter in heavy horde waves (Survival Endless) on Miyoo Mini Plus by decoupling the game logic tick rate from the display render rate, normalizing logic simulation to 100 ticks/sec, and introducing an adaptive 0/1 render frameskip active strictly during combat.

**Architecture:**
1. **Simulation Normalization:** Remove the aggressive 15 ms clamp (`mUpdateFTimeAcc > aFrameFTime * 1.5 -> 0.0`) in [`src/SexyAppFramework/SexyAppBase.cpp`](file:///c:/DEV/PVZ/src/SexyAppFramework/SexyAppBase.cpp#L2856) during active gameplay. Allow up to 2 logic ticks (20 ms) per render cycle when wall-clock time demands it, bounded by a strict 30.0 ms debt cap to prevent game loop runaway.
2. **Dynamic 0/1 Render Frameskip:** If a render cycle takes $\ge 34\text{ ms}$ ($\le 29\text{ FPS}$) during combat, skip drawing the subsequent frame (`DrawDirtyStuff`), reducing frame rendering load by ~25 ms instantly while maintaining on-time 100 Hz logic simulation. Enforce a strict maximum of 1 consecutive skip so visual refresh never drops below 30 FPS.
3. **Gameplay Isolation Boundary:** Both the multi-tick logic budget and adaptive frameskip are active **exclusively** when `LawnApp::IsInActiveGameplay()` is true (`mGameScene == SCENE_PLAYING && mBoard && !mBoard->mPaused && GetDialogCount() == 0`). TitleScreen, loading threads, menus, almanac, store, and intro cutscenes remain 100% untouched.

**Tech Stack:** C++17, ARMv7-A Cortex-A7 NEON/VFPv4, OnionOS/Linux, SDL2.

---

## Global Constraints & Safety Anti-Regressions
- **No Death Spirals:** Never use an unbounded `while` catch-up loop. Logic update budget per frame is strictly capped at 2 ticks maximum.
- **No Title Screen / Loading Thread Interference:** TitleScreen, resource decompression, and game initialization must never use frameskip or multi-tick accumulation.
- **Strict Frameskip Ceiling:** Maximum consecutive skipped render frames = 1. The cadence during heavy stress can never exceed `Draw -> Skip -> Draw -> Skip` (guaranteed $\ge 30\text{ FPS}$ visual updates).
- **Zero Memory Allocation Changes:** `MAX_PARTICLES_SIZE` remains strictly 900. No memory pool adjustments.
- **Clean Process Termination:** All Python packaging and test tools run synchronously without leaving background tasks.

---

### Task 1: Gameplay Isolation Query & Tick Accumulator Budget Normalization

**Files:**
- Modify: [`src/SexyAppFramework/SexyAppBase.h`](file:///c:/DEV/PVZ/src/SexyAppFramework/SexyAppBase.h)
- Modify: [`src/LawnApp.h`](file:///c:/DEV/PVZ/src/LawnApp.h)
- Modify: [`src/LawnApp.cpp`](file:///c:/DEV/PVZ/src/LawnApp.cpp)
- Modify: [`src/SexyAppFramework/SexyAppBase.cpp:2845-2860`](file:///c:/DEV/PVZ/src/SexyAppFramework/SexyAppBase.cpp#L2845-L2860)

- [ ] **Step 1.1: Add `IsInActiveGameplay()` query method**
In `SexyAppBase.h`, add virtual method:
```cpp
virtual bool IsInActiveGameplay() { return false; }
```
In `LawnApp.h` and `LawnApp.cpp`, override:
```cpp
bool LawnApp::IsInActiveGameplay()
{
    return mGameScene == GameScenes::SCENE_PLAYING && mBoard && !mBoard->mPaused && GetDialogCount() == 0;
}
```

- [ ] **Step 1.2: Decouple accumulator clamp in `SexyAppBase::Process`**
Replace the destructive 15 ms wipe with a dual-mode safety clamp:
```cpp
if (IsInActiveGameplay())
{
    // In active gameplay: permit accumulator debt up to 30ms (3 ticks), clamped to prevent runaway.
    if (mUpdateFTimeAcc > 30.0)
        mUpdateFTimeAcc = 30.0;
}
else
{
    // In menus, loading screens, and dialogs: preserve strict 1.5x clamp to protect background loading threads
    if (mUpdateFTimeAcc > aFrameFTime * 1.5)
        mUpdateFTimeAcc = 0.0;
}
```

---

### Task 2: Adaptive 0/1 Render Frameskip Engine in `SexyAppBase`

**Files:**
- Modify: [`src/SexyAppFramework/SexyAppBase.h`](file:///c:/DEV/PVZ/src/SexyAppFramework/SexyAppBase.h)
- Modify: [`src/SexyAppFramework/SexyAppBase.cpp:2750-2875`](file:///c:/DEV/PVZ/src/SexyAppFramework/SexyAppBase.cpp#L2750-L2875)

- [ ] **Step 2.1: Add frameskip state variables in `SexyAppBase.h`**
```cpp
int  mConsecutiveFrameskips{0};
bool mSkipNextDraw{false};
uint32_t mLastFrameDuration{0};
```

- [ ] **Step 2.2: Evaluate render skip condition before `DrawDirtyStuff`**
In `SexyAppBase::Process`, track frame execution time:
If `IsInActiveGameplay()` and `mLastFrameDuration >= 34` (cycle took $> 34\text{ ms}$, meaning rendering is dragging below 30 FPS), and `mConsecutiveFrameskips == 0`:
Set `mSkipNextDraw = true`.

- [ ] **Step 2.3: Bypass drawing when `mSkipNextDraw` is active**
Around line 2868 in `SexyAppBase.cpp`:
```cpp
if (mHasPendingDraw)
{
    if (mSkipNextDraw && mConsecutiveFrameskips == 0)
    {
        mHasPendingDraw = false;
        mSkipNextDraw = false;
        mConsecutiveFrameskips = 1;
        // Rendering skipped: saves ~25ms of CPU instantly
    }
    else
    {
        DrawDirtyStuff();
        mConsecutiveFrameskips = 0;
        mSkipNextDraw = false;
    }
}
```

---

### Task 3: Background Zombie Skeletal LOD Interleaving for Survival Endless

**Files:**
- Modify: [`src/Lawn/Board.cpp:UpdateGameObjects`](file:///c:/DEV/PVZ/src/Lawn/Board.cpp#L5442)
- Modify: [`src/Lawn/Zombie.cpp:Update`](file:///c:/DEV/PVZ/src/Lawn/Zombie.cpp)

- [ ] **Step 3.1: Interleave skeletal Reanimator updates in dense waves**
When `mZombies.mSize > 25`:
Basic walking zombies (`ZOMBIE_NORMAL`, `ZOMBIE_TRAFFIC_CONE`, `ZOMBIE_PAIL`, `ZOMBIE_DOOR`) in the background swarm update their skeletal animation tracks on alternating ticks based on `(mAppCounter + mZombieID) & 1`.
All combat hitboxes, eating logic, projectile collisions, and health updates continue ticking on every frame.
Cuts skeletal matrix transformation cost by ~45% in 40+ zombie hordes.

---

### Task 4: Verification, Safety Audits & Release Packaging

- [ ] **Step 4.1: Cross-compile ARMv7 binary with Docker**
Run `make -j4` in `aemiii91/miyoomini-toolchain`.
- [ ] **Step 4.2: Strip binary and deploy**
Deploy stripped 5.6 MB binary to `bin/PvZ`, `packaging/...`, and `ports/...`.
- [ ] **Step 4.3: Verify TitleScreen & OnionOS boot sequence**
Verify loading thread progression, title screen bar animation, and almanac caching.
- [ ] **Step 4.4: Run automated integration tests**
Run `tests/test_launch_script.sh` (all 71 tests must pass).
- [ ] **Step 4.5: Synchronous release packaging & Trail sync**
Run `python scripts/package_release.py` synchronously, verify RAM $\le 78.0$ MB, and run `trail build`.
