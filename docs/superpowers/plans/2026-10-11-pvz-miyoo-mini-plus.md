# Plants vs. Zombies GOTY (Miyoo Mini Plus) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port *Plants vs. Zombies: GOTY Edition* natively to the Miyoo Mini Plus (OnionOS) at 640×480 with $\le 85$ MB RAM usage, hybrid handheld controls, 1700 MHz overclock, and an intelligent two-tier asset preparation pipeline (fast PC builder + autonomous self-destructing console installer).

**Architecture:** Fork and adapt `wszqkzqk/PvZ-Portable` (C++20/SDL2); eliminate OpenGL in favor of an SDL2 software rasterizer with DMA texture pooling (`SDL_MMIYOO_TEXTURE_POOL`) and NEON-accelerated transforms; keep internal game coordinates at 800×600 while scaling viewport to 640×480 ($0.8\times$ global matrix) to match 80% downscaled textures; integrate `libopenmpt` for `.mo3` tracker music; implement hybrid D-pad controls (grid-snapping in lawn, direct list in shop, contextual smooth cursor in Zen Garden/minigames).

**Tech Stack:** C++20, ARM Cortex-A7 toolchain (`arm-linux-gnueabihf`), CMake, Ninja, SDL2 (Miyoo fork), SDL-Mixer-X, `libopenmpt`, ARM NEON SIMD intrinsics, Python 3 (`tkinter`), `stb_image` / `stb_image_resize2`.

**Spec:** [`docs/superpowers/specs/2026-10-11-pvz-miyoo-mini-plus-design.md`](file:///c:/DEV/PVZ/docs/superpowers/specs/2026-10-11-pvz-miyoo-mini-plus-design.md)

---

## Global Constraints

- **Platform Target:** Miyoo Mini Plus only (SigmaStar SSD202D, Dual ARM Cortex-A7 @ 1.2 GHz base, overclocked to 1700 MHz).
- **RAM Ceiling:** Usable memory strictly restricted to $\le 85\text{–}95$ MB (out of 128 MB SiP).
- **Display Output:** 3.5" IPS LCD, 640×480 @ 60 Hz (4:3 aspect ratio).
- **No 3D GPU:** 100% software rasterization and 2D blitting; zero OpenGL/GLES runtime calls.
- **Repository Clean-Room:** 0% copyrighted assets, 0 Windows `.exe` files tracked in Git.
- **OnionOS Layout:** Strict conformance to `Roms/PORTS/` Pure Ports standard.
- **Overclock Standard:** 1700 MHz base via `/mnt/SDCARD/.tmp_update/bin/cpuclock 1700`.
- **Signal & Teardown Safety:** `trap cleanup EXIT INT TERM HUP` with `sync` and `ResetFramebuffer()` after `SDL_Quit()`.

---

## Review Focus

1. **Out-of-Memory (OOM) Killer on Dense Waves:** When Survival/Fog levels spawn 50+ zombies and high projectile density, resident memory must not exceed 85 MB; verified via memory tracking allocator.
2. **Reanim Skeletal De-synchronization under 0.8× Scale:** If global viewport scale is misapplied, body parts (arms, heads) detach from zombie rigs; verified with test render comparing bone offset transformations.
3. **Black Screen on Exit via Menu or Emergency Combo:** Exiting the game must never leave `/dev/fb0` panned or display driver unmapped; verified with signal trap and `FBIOPAN_DISPLAY` reset.
4. **FAT32 Power-Cut Resilience during Asset Conversion:** If the user shuts down the device during `setup_miyoo` execution, no user assets are lost and `.installed` is not created; verified by transactional staging (`.tmp` / atomic rename).
5. **Physical Menu Key Ignored:** Pressing the central MENU key on Miyoo Mini Plus emits `SDLK_HOME` (102) or `SDLK_RALT` (100); verified by event loop intercept translating both to `KEYCODE_ESCAPE`.

---

## Task Decomposition

### Task 1: Scaffolding, Toolchain Configuration & Docker Build Environment

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/miyoomini.toolchain.cmake`
- Create: `scripts/docker_build.sh`
- Create: `.gitignore`

**Interfaces:**
- Consumes: `aemiii91/miyoomini-toolchain:latest` Docker image.
- Produces: Working cross-compilation pipeline emitting ARMv7 hard-float ELFs with `-mcpu=cortex-a7 -mfpu=neon-vfpv4 -O3`.

- [x] **Step 1: Create `.gitignore` adhering to distribution anti-virus rules**
  Write `.gitignore` containing `/build/`, `/dist/`, `*.exe`, `*.pak`, `*.dat`, `*.o`, `*.a`, `*.so*`, and explicit tracking exception for shell scripts and CMake.
- [x] **Step 2: Create CMake toolchain file `cmake/miyoomini.toolchain.cmake`**
  Set `CMAKE_SYSTEM_NAME Linux`, `CMAKE_SYSTEM_PROCESSOR arm`, compiler `arm-linux-gnueabihf-gcc`/`g++`, with flags `-mcpu=cortex-a7 -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -O3 -fomit-frame-pointer -ffast-math`.
- [x] **Step 3: Create top-level `CMakeLists.txt` scaffolding**
  Define project `PvZ_Miyoo` (C++20, C11). Add target for engine core, setup utility, and test suite. Provide option `ENABLE_GLES=OFF`.
- [x] **Step 4: Create Docker build runner `scripts/docker_build.sh`**
  Script mounts workspace into `/root/workspace` in `aemiii91/miyoomini-toolchain` and invokes `cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/miyoomini.toolchain.cmake && cmake --build build`.
- [x] **Step 5: Verify cross-compilation of dummy ARM target**
  Run dummy test build through Docker; verify generated ELF with `file build/test_arm` reports `ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), dynamically linked`.
- [x] **Step 6: Commit**
  `git commit -m "chore: setup Miyoo Mini Plus CMake toolchain and Docker build harness"`

---

### Task 2: PC Asset Pipeline & Release Builder (`Launcher.py` & `build_release.py`)

**Files:**
- Create: `tools/pak_lib.py`
- Create: `tools/optimize_assets.py`
- Create: `tools/build_release.py`
- Create: `Launcher.py`
- Create: `tests/test_pak_lib.py`

**Interfaces:**
- Consumes: PC Steam GOTY directory containing `main.pak` and `properties/`.
- Produces: Optimized `main_mmp.pak` with 80% downscaled textures (bilinear/lanczos), 22.05 kHz mono audio, and staging tree in `Roms/PORTS/...`.

- [x] **Step 1: Write test for PopCap PAK container unpack/pack (`tests/test_pak_lib.py`)**
  Test byte-exact roundtrip packing and unpacking with PopCap XOR 0xF7 record encryption.
- [x] **Step 2: Implement `tools/pak_lib.py`**
  Implement PopCap PAK parser and writer supporting single-pass streaming, XOR 0xF7 record decryption, and linear seek offsets.
- [x] **Step 3: Implement `tools/optimize_assets.py`**
  Implement asset transformation: downscale all PNG/JPEG textures by $0.8\times$ (preserving aspect and alpha channels); convert WAV SFX to 22,050 Hz mono 16-bit PCM; pack output directly into `main_mmp.pak`.
- [x] **Step 4: Implement `tools/build_release.py` with safe staging**
  Staging in `tempfile.mkdtemp()`, non-destructive `copy_tree_merge()`, POSIX permission normalization (`0755` for scripts/binaries), and creation of `.installed` marker.
- [x] **Step 5: Implement `Launcher.py` Tkinter UI**
  Zero-dependency Python GUI with Steam installation auto-discovery, progress bar, and "Export to SD Card" button.
- [x] **Step 6: Verify PAK optimization test passes**
  Run: `python -m pytest tests/test_pak_lib.py -v` -> PASS.
- [x] **Step 7: Commit**
  `git commit -m "feat(tools): add PC release builder, Tkinter launcher, and PAK optimizer"`

---

### Task 3: Autonomous On-Device Asset Converter & Self-Destruction (`setup_miyoo`)

**Files:**
- Create: `src/setup/setup_miyoo.c`
- Create: `src/setup/fb_progress.c`
- Create: `src/setup/fb_progress.h`
- Create: `src/setup/stb_impl.c`
- Test: `tests/test_setup_conversion.cpp`

**Interfaces:**
- Consumes: Raw `main.pak` placed on SD card.
- Produces: `main_mmp.pak`, renames `main.pak` to `main.pak.bak`, touches `.installed`, and deletes itself (`setup_miyoo`) upon success.

- [x] **Step 1: Write test for single-file streaming PAK processor (`tests/test_setup_conversion.cpp`)**
  Assert that conversion processes sequentially without buffering all files in memory simultaneously (peak memory $\le 10$ MB).
- [x] **Step 2: Implement framebuffer UI `src/setup/fb_progress.c`**
  Open `/dev/fb0`, query `FBIOGET_VSCREENINFO`, render clean progress bar and status text ("Optimizando Plants vs. Zombies...") in 16-bit RGB565 / 32-bit XRGB8888.
- [x] **Step 3: Implement sequential image downscaler in `src/setup/setup_miyoo.c`**
  Integrate `stb_image`, `stb_image_resize2`, and lightweight PAK streamer. Scale images to $80\%$, resample WAVs to 22.05 kHz mono, write atomic `main_mmp.pak.tmp`.
- [x] **Step 4: Implement transactional finalize and self-destruction**
  Rename `main_mmp.pak.tmp` $\to$ `main_mmp.pak`, rename `main.pak` $\to$ `main.pak.bak`, touch `.installed`, sync filesystem, and unlink `setup_miyoo`.
- [x] **Step 5: Verify setup tool builds for ARM and passes unit tests**
  Compile via Docker toolchain and run test suite.
- [x] **Step 6: Commit**
  `git commit -m "feat(setup): implement autonomous on-device installer with fb0 progress and self-cleanup"`

---

### Task 4: Software Rasterizer & Viewport Scaling Backend in Engine

**Files:**
- Create: `src/graphics/SoftwareRenderer.hpp`
- Create: `src/graphics/SoftwareRenderer.cpp`
- Create: `src/graphics/NeonBlit.hpp`
- Create: `src/graphics/NeonBlit.cpp`
- Test: `tests/test_viewport_scale.cpp`

**Interfaces:**
- Consumes: SexyAppBase draw calls (quads, triangles, textured blits, Reanim transforms).
- Produces: 640×480 framebuffer rendering via SDL2 Software Surface with DMA texture pool.

- [x] **Step 1: Write test for 0.8× coordinate matrix projection (`tests/test_viewport_scale.cpp`)**
  Test that an 800×600 quad coordinate $(x=400, y=300, w=100, h=100)$ maps exactly to $(320, 240, 80, 80)$ on the 640×480 surface.
- [x] **Step 2: Implement `src/graphics/SoftwareRenderer.cpp`**
  Provide `SoftwareRenderer` implementing SexyApp Graphics interface using `SDL_Surface` blitting and direct pixel access. Bind pre-allocated 16 MB DMA pool (`SDL_MMIYOO_TEXTURE_POOL=1`).
- [x] **Step 3: Implement NEON-accelerated alpha blending (`src/graphics/NeonBlit.cpp`)**
  Implement vector alpha blend: process 8 pixels per iteration using ARM NEON registers without crossing into VFP/integer banks.
- [x] **Step 4: Implement global viewport scaling in camera pipeline**
  Apply $0.8\times$ affine scale matrix to all vertex submissions; keep game logic, Reanim bone hierarchy, and TodParticleSystem coordinates at original 800×600.
- [x] **Step 5: Verify test passes**
  Run: `build/tests/test_viewport_scale` -> PASS.
- [x] **Step 6: Commit**
  `git commit -m "feat(gfx): implement SDL2 software rasterizer with NEON blit and 0.8x viewport projection"`

---

### Task 5: Audio Engine Integration (`libopenmpt` & SDL-Mixer-X)

**Files:**
- Create: `src/audio/OpenMptMusicInterface.hpp`
- Create: `src/audio/OpenMptMusicInterface.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_openmpt_playback.cpp`

**Interfaces:**
- Consumes: PopCap Laura Shigihara soundtrack files (`.mo3`, `.it`).
- Produces: Low-latency streaming stereo audio to SDL-Mixer-X / ALSA.

- [x] **Step 1: Write test for OpenMPT stream decoding (`tests/test_openmpt_playback.cpp`)**
  Verify opening and streaming sample frames from a tracker music module without allocating more than 2 MB buffer.
- [x] **Step 2: Implement `src/audio/OpenMptMusicInterface.cpp`**
  Implement PopCap `MusicInterface` using `openmpt::module`. Stream in chunks of 2,048 samples; handle music volume, loop points, and song transitions.
- [x] **Step 3: Integrate low-overhead SFX loading in SDL-Mixer-X**
  Load downsampled 22.05 kHz WAV sound effects lazily or on-demand to maintain audio buffer footprint under 8 MB RAM.
- [x] **Step 4: Verify test passes**
  Run: `build/tests/test_openmpt_playback` -> PASS.
- [x] **Step 5: Commit**
  `git commit -m "feat(audio): integrate libopenmpt tracker streamer and SDL-Mixer-X sound manager"`

---

### Task 6: Hybrid Input Controller Subsystem

**Files:**
- Create: `src/input/HandheldInput.hpp`
- Create: `src/input/HandheldInput.cpp`
- Test: `tests/test_input_mapping.cpp`

**Interfaces:**
- Consumes: SDL2 Controller / Keyboard events (`SDLK_UP`, `SDLK_DOWN`, `SDLK_HOME`, buttons).
- Produces: Hybrid interaction events (9×5 grid navigation, hover sun auto-collect, L/R packet selection, direct shop list navigation, contextual smooth cursor).

- [ ] **Step 1: Write unit test for hybrid navigation modes (`tests/test_input_mapping.cpp`)**
  Assert grid coordinates increment discretely in lawn mode $(0..8, 0..4)$; assert hovering cursor over sun object fires `CollectSun()`; assert `SDLK_HOME` and `SDLK_RALT` trigger `KEYCODE_ESCAPE`.
- [ ] **Step 2: Implement 9×5 Grid Snapping and Hover Sun Auto-Collect**
  In garden battle state: D-pad jumps directly between cell centers. Check collision bounds between current cursor position and active falling suns/coins; trigger collection immediately on overlap.
- [ ] **Step 3: Implement Seed Packet Cycle and Action Buttons**
  L/R triggers shift active packet index `(mSelectedSeed ± 1) % mNumSeeds`. Button A plants/confirms; Button B drops selection; Button X toggles shovel tool.
- [ ] **Step 4: Implement Shop and Contextual Smooth Cursor Modes**
  In Dave's shop: translate D-pad into direct list selection. In Zen Garden, Zombatar, and free-aim minigames (*Whack-a-Zombie*): switch to floating virtual cursor with acceleration curve.
- [ ] **Step 5: Map physical Menu key**
  Translate `SDLK_HOME` (scancode 102) and `SDLK_RALT` (scancode 100) to PopCap `KEYCODE_ESCAPE`.
- [ ] **Step 6: Verify input tests pass**
  Run: `build/tests/test_input_mapping` -> PASS.
- [ ] **Step 7: Commit**
  `git commit -m "feat(input): implement hybrid grid-snapping, sun hover-collect, and contextual cursor"`

---

### Task 7: OnionOS Packaging, Lifecycle Traps & Power Resiliency

**Files:**
- Create: `ports/Plants vs. Zombies.port`
- Create: `ports/Games/Plants vs. Zombies/launch.sh`
- Create: `src/main_miyoo.cpp` (teardown and framebuffer reset)
- Test: `tests/test_launch_script.sh`

**Interfaces:**
- Consumes: Compiled ARM binaries (`PvZ`, `setup_miyoo`), shared libraries, assets.
- Produces: Ready-to-copy OnionOS directory structure with signal traps and overclock.

- [ ] **Step 1: Write integration test for launch script traps (`tests/test_launch_script.sh`)**
  Verify `launch.sh` syntax, signal trap traps `EXIT`, `INT`, `TERM`, `HUP`, exports `sync`, and restores CPU governor.
- [ ] **Step 2: Implement `ports/Games/Plants vs. Zombies/launch.sh`**
  Add detection: if `main_mmp.pak` exists, delete `setup_miyoo` if present. If only raw `main.pak` exists, run `./setup_miyoo`. Apply safe overclock `cpuclock 1700`, kill `audioserver`, export `SDL_MMIYOO_TEXTURE_POOL=1`. Wrap in `trap cleanup`.
- [ ] **Step 3: Implement clean teardown and `ResetFramebuffer()` in `src/main_miyoo.cpp`**
  Unwind main loop cleanly on quit; call `ResetFramebuffer()` using `ioctl(fb, FBIOPAN_DISPLAY, &vinfo)` after `SDL_Quit()` to restore display offsets to $(0,0)$.
- [ ] **Step 4: Create OnionOS shortcut `ports/Plants vs. Zombies.port`**
  Create pure port launcher script resolving paths dynamically relative to `Roms/PORTS/Games/Plants vs. Zombies/`.
- [ ] **Step 5: Verify launch script passes linting and unit test**
  Run: `bash -n ports/Games/Plants\ vs.\ Zombies/launch.sh` and run test.
- [ ] **Step 6: Commit**
  `git commit -m "feat(onion): add OnionOS launch scripts, lifecycle traps, and clean fb reset"`

---

### Task 8: End-to-End Build, Memory Profiling & Release Packaging

**Files:**
- Create: `scripts/package_release.py`
- Modify: `README.md`

**Interfaces:**
- Consumes: Complete project repository and cross-compiled binaries.
- Produces: Distributable release ZIP `PvZ_MiyooMiniPlus_v1.0.0.zip` ready for SD card drag-and-drop.

- [ ] **Step 1: Execute full Docker cross-compilation**
  Run: `bash scripts/docker_build.sh` -> Produces `build/PvZ` and `build/setup_miyoo`.
- [ ] **Step 2: Assemble release tree structure**
  Run: `python scripts/package_release.py` -> Populates `dist/Roms/PORTS/...`.
- [ ] **Step 3: Run memory profiling simulation**
  Verify with memory logger that steady-state level execution allocates $\le 78$ MB heap/resident memory.
- [ ] **Step 4: Update README.md with installation guide**
  Document Steam PC automated setup (`Launcher.py`) and autonomous console setup.
- [ ] **Step 5: Commit**
  `git commit -m "release: finalize end-to-end packaging and documentation for Miyoo Mini Plus"`
