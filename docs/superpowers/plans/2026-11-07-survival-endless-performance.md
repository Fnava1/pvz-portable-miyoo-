# Survival Endless Performance Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate performance degradation, frame stutter, and CPU starvation in *Plants vs. Zombies* during high-density waves (Survival Endless Wave 3+) on the Miyoo Mini Plus (SigmaStar SSD202D dual-core Cortex-A7 @ 1.2-1.8 GHz) without breaking logical 800×600 coordinates, animation rigs, or combat physics.

**Architecture:**
1. **Dual-Core Asynchronous Presentation Worker (Core 1):** Offload the fused 5:4 NEON downscaling and SDL texture presentation to a dedicated worker thread pinned to Core 1 (`CPU_SET(1)`), using a double-buffered ping-pong mechanism (`mScreenImage` buffers) synchronized via lightweight POSIX condition variables.
2. **Adaptive Reanimator Skeletal LOD & Viewport Culling:** Throttle stationary idle plant rig evaluations to 30 Hz (interleaved by row/column) when entity count exceeds threshold ($N > 40$), and cull all 30-track skeletal matrix evaluations for zombies outside visible screen bounds ($x > 820$).
3. **Fillrate & Particle Storm Governor:** Pace multi-directional Gloom-shroom gas cloud particle bursts during intense waves and cull redundant overlapping drop-shadows in dense zombie clusters to prevent DDR3 memory bus saturation.
4. **ARM NEON 2D Affine Matrix Vectorization:** Vectorize `SexyMatrix3Multiply` using ARM NEON intrinsics, taking advantage of 2D affine invariant properties ($Z=[0, 0, 1]$) to reduce matrix arithmetic from 27 multiplications to 8 multiplications with SIMD parallelization.

**Tech Stack:** C++17, POSIX Threads (`pthread`), ARMv7-A NEON SIMD (`arm_neon.h`), SDL2, PopCap SexyAppFramework / PvZ-Portable.

---

## Global Constraints

- Preserve clean-room 800×600 logical coordinate space. Never alter game simulation bounds, entity hitboxes, or projectile physics.
- The 128 MB RAM budget must remain respected: total userland footprint must not exceed 75 MB (leaving $\ge 15$ MB headroom below the 85 MB critical threshold).
- Thread safety: Core 0 is the single writer to game simulation state; Core 1 operates strictly as a consumer of completed frame buffers.
- Linux and OnionOS signal resilience: All worker threads must terminate cleanly during shutdown (`gShutdownRequested`, `SIGINT`, `SIGTERM`, `CloseRequestAsync`).

## Review Focus

1. **Race Conditions in Framebuffer Ping-Pong:** Ensure Core 0 cannot begin writing to a buffer currently being downscaled by Core 1.
2. **Visual Continuity in Plant Animations:** Ensure 30 Hz skeletal interleaving on stationary plants produces zero noticeable jitter or detached limbs.
3. **Zombie Spawning Integrity:** Ensure zombies culled off-screen ($x > 820$) still advance their linear movement and take damage if hit by lobbed projectiles (cabbage, melon).
4. **Combat Hitbox Accuracy:** Confirm that reducing Gloom-shroom cloud particle emissions does not reduce damage frequency or radius in `Plant::Fire()`.
5. **Clean Exit & Signal Handling:** Verify that pressing Menu, SELECT+START, or closing clamshell cleanly shuts down the Core 1 worker thread without hanging or leaving orphan processes.

---

## Tasks

### Task 1: Vectorización Analítica NEON de Transformaciones 2D (`SexyMatrix3Multiply`)

**Files:**
- Modify: `src/PvzpLib/PvzpCommon.cpp:1003-1025`
- Test: Unit test matrix multiplication equivalence in `tests/test_matrix_neon.cpp`

- [ ] **Step 1: Write verification test for 2D affine matrix multiplication**
  Create `tests/test_matrix_neon.cpp` comparing scalar $3\times3$ vs 2D affine NEON implementation for translation, rotation, and shear matrices.
- [ ] **Step 2: Implement 2D Affine Matrix Optimization in `PvzpCommon.cpp`**
  Optimize `SexyMatrix3Multiply` to exploit invariant 3rd row `[0, 0, 1]`:
  $$m_{00} = l_{00}r_{00} + l_{01}r_{10},\quad m_{01} = l_{00}r_{01} + l_{01}r_{11},\quad m_{02} = l_{00}r_{02} + l_{01}r_{12} + l_{02}$$
  $$m_{10} = l_{10}r_{00} + l_{11}r_{10},\quad m_{11} = l_{10}r_{01} + l_{11}r_{11},\quad m_{12} = l_{10}r_{02} + l_{11}r_{12} + l_{12}$$
  Vectorize with ARM NEON (`float32x4_t`) when `__ARM_NEON` is defined.
- [ ] **Step 3: Verify bit-exact/float-precision results and measure benchmark speedup**
  Ensure results match within $1\times 10^{-5}$ epsilon.

---

### Task 2: LOD Esquelético Adaptativo y Culling de Zombies Fuera de Pantalla

**Files:**
- Modify: `src/Lawn/Zombie.cpp:6309-6335` (Early viewport culling in `Zombie::Draw`)
- Modify: `src/Lawn/Plant.cpp:2758-2815` (Adaptive plant skeleton interleaving)
- Modify: `src/PvzpLib/Reanimator.cpp:965-989` (Matrix reuse / LOD state check)

- [ ] **Step 1: Implement screen-space viewport culling in `Zombie::Draw`**
  Check `float aScreenX = mPosX + mBoard->mX;`
  If $aScreenX > \text{BOARD\_WIDTH} + 80.0\text{f}$ or $aScreenX < -150.0\text{f}$, skip `DrawReanim()` and shadow drawing.
- [ ] **Step 2: Implement Interleaved Plant Animation LOD in `Plant::UpdateReanim`**
  When `mBoard->mZombies.mSize > 40` and plant is stationary in `anim_idle` (Sunflowers, Pumpkins, Repeaters, Spikerocks):
  Update bone rig at 30 Hz (alternating odd/even frames based on `(mPlantCol + mRow + mBoardUpdateCounter) % 2 == 0`).
  Advance animation time by $2\times$ delta time on active frames to preserve rhythm, skipping skeletal matrix evaluation on inactive frames.
- [ ] **Step 3: Verify visual smoothness in Survival Endless wave**
  Confirm no limb detachment or animation stuttering occurs.

---

### Task 3: Gobernador de Partículas y Sombras para Alivio de Fillrate

**Files:**
- Modify: `src/Lawn/Plant.cpp:3236-3247` (Gloom-shroom cloud emission pacing)
- Modify: `src/Lawn/Board.cpp:6910-6925` (Dense horde shadow culling)
- Modify: `src/PvzpLib/PvzpParticle.cpp:1018-1055` (Active particle thresholding)

- [ ] **Step 1: Implement Gloom-shroom cloud emission governor in `Plant.cpp`**
  When total active particles $> 60$ or during Survival Endless waves with $> 30$ zombies:
  Pace `PARTICLE_GLOOMCLOUD` bursts from 4 bursts (counters 136, 108, 80, 52) to 2 bursts (136 and 80), keeping `Plant::Fire()` damage intact across all 4 hit cycles.
- [ ] **Step 2: Implement Horde Drop-Shadow Culling in `Board.cpp`**
  In `Board::Draw()`, when a lawn row has $> 6$ zombies closely packed ($\Delta x < 35\text{ px}$), draw shadow only for the lead and rear zombies, skipping redundant overlapping ellipses.
- [ ] **Step 3: Measure fillrate and frame time reduction**
  Confirm massive drop in pixel overdraw during Gloom-shroom multi-plant firing.

---

### Task 4: Arquitectura Multihilo Asíncrona en Core 1 (Double-Buffered Worker)

**Files:**
- Modify: `src/SexyAppFramework/graphics/GLInterface.h` (Double-buffer declarations & worker thread primitives)
- Modify: `src/SexyAppFramework/graphics/GLInterface.cpp` (Worker thread startup, ping-pong swap, and asynchronous `Flush()`)
- Modify: `src/main_miyoo.cpp` (Lifecycle synchronization on shutdown)

- [ ] **Step 1: Declare Double-Buffer and Worker Synchronization Primitives in `GLInterface.h`**
  Add secondary `MemoryImage` / pixel buffer `mBackBits[800 * 600]`, `pthread_t mFlipWorkerThread`, `pthread_mutex_t mFlipMutex`, `pthread_cond_t mFlipCond`, `std::atomic<bool> mFlipWorkerRunning`, and `std::atomic<bool> mFrameReady`.
- [ ] **Step 2: Implement `FlipWorkerThreadFunc` pinned to Core 1 in `GLInterface.cpp`**
  Pin thread to Core 1 (`CPU_SET(1)`).
  Wait on `pthread_cond_wait`.
  When signalled, execute `DownscaleRow800To640` / `AverageAndDownscaleRow800To640` directly into `SDL_LockTexture`, followed by `SDL_UnlockTexture`, `SDL_RenderCopy`, and `SDL_RenderPresent`.
- [ ] **Step 3: Update `GLInterface::Flush()` on Core 0**
  Instead of blocking Core 0 for 5-7 ms, Core 0 swaps the front/back buffer pointer, marks frame ready, signals `pthread_cond_signal(&mFlipCond)`, and returns immediately ($< 0.01\text{ ms}$).
- [ ] **Step 4: Implement Graceful Shutdown & Frame Synchronization**
  Ensure destructor joins `mFlipWorkerThread` cleanly. Verify no deadlocks on exit, sleep, or menu button press.

---

### Task 5: Compilación Cruzada ARMv7, Empaquetado y Verificación en Banco de Pruebas

**Files:**
- Target Binaries: `bin/PvZ`, `packaging/.../PvZ`, `ports/.../PvZ`
- Release Zip: `dist/PvZ_MiyooMiniPlus_v1.1.0.zip`
- Automated Tests: `tests/test_launch_script.sh`

- [ ] **Step 1: Compile via Docker ARMv7 Toolchain**
  Execute Docker build with `aemiii91/miyoomini-toolchain`, `-O3`, NEON SIMD, and verify zero compiler warnings.
- [ ] **Step 2: Strip and Verify Binary Sizes**
  Ensure binary size remains within $\approx 5.76\text{ MB}$.
- [ ] **Step 3: Run Full Automated Regression Test Suite**
  Run `tests/test_launch_script.sh` (71/71 tests).
- [ ] **Step 4: Update Documentation and Trail Graph**
  Update `c:\DEV\WIKI\registro_conocimiento_pvz_miyoo.md` and refresh Trail graph (`trail build`).
