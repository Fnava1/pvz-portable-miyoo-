# 🌻 Plants vs. Zombies GOTY Edition — Miyoo Mini & Miyoo Mini Plus Port (OnionOS)

[![License: MIT / Clean-Room BYOD](https://img.shields.io/badge/License-MIT%20%2F%20BYOD-green.svg)](#legal--clean-room-architecture)
[![Platform: Miyoo Mini & Plus](https://img.shields.io/badge/Platform-Miyoo%20Mini%20%26%20Plus%20%28OnionOS%29-blue.svg)](#hardware-profile--performance-target)
[![Architecture: ARMv7 Cortex-A7](https://img.shields.io/badge/Arch-ARMv7--A%20%28Hard%20Float%20%2B%20NEON%29-orange.svg)](#subsystem-architecture)
[![Memory: <= 85 MB](https://img.shields.io/badge/RAM-78%20MB%20%2F%2085%20MB%20Cap-brightgreen.svg)](#memory-budget--profiling-matrix)

A native, high-performance port of PopCap's classic **Plants vs. Zombies: Game of the Year Edition** for the **Miyoo Mini (v1/v2/v3/v4)** and **Miyoo Mini Plus** retro handheld consoles running **OnionOS**.

Rendered at the display's native **640×480 (4:3)** resolution at a fluid **60 FPS**, complete with authentic tracker music reproduction, custom handheld ergonomics (discrete 9×5 grid snapping, automatic sun hover-vacuum, dedicated Zen Garden L/R tool cycling), and full lifecycle power protection with dynamic model-aware overclocking (1800 MHz on MM+, safe 1400 MHz on MM).

---

## 📑 Table of Contents
1. [Key Features](#key-features)
2. [Hardware Profile & Performance Target](#hardware-profile--performance-target)
3. [Subsystem Architecture](#subsystem-architecture)
4. [Handheld Control Scheme](#handheld-control-scheme)
5. [Legal & Clean-Room Architecture](#legal--clean-room-architecture)
6. [Installation Guide](#installation-guide)
   - [Method A: Fast PC Preparation (Recommended)](#method-a-fast-pc-preparation-recommended)
   - [Method B: Autonomous On-Device Installation](#method-b-autonomous-on-device-installation)
7. [Memory Budget & Profiling Matrix](#memory-budget--profiling-matrix)
8. [Building from Source](#building-from-source)
9. [Directory Structure (OnionOS Pure Ports)](#directory-structure-onionos-pure-ports)
10. [Credits & Acknowledgments](#credits--acknowledgments)

---

## 🌟 Key Features

- **Universal Miyoo Mini Family Support:** Out-of-the-box support for both **Miyoo Mini Plus** and **Miyoo Mini (v1/v2/v3/v4)** with automatic hardware detection.
- **Native 640×480 Resolution (1:1 Pixel Match):** Perfectly tailored to the 2.8" / 3.5" IPS LCD display without letterboxing or uneven scaling.
- **NEON-Accelerated Software Rasterizer:** Completely removes OpenGL ES 2.0; utilizes ARMv7 NEON SIMD blitting and a 16 MB contiguous DMA texture pool (`SDL_MMIYOO_TEXTURE_POOL=1`).
- **Hybrid Ergonomic Controls:**
  - **Lawn Battle:** Discrete 9×5 grid snapping on D-pad with instant L/R seed packet cycling.
  - **Hover Sun Vacuum:** Hovering the cursor over falling or produced suns and coins automatically collects them—no frantic clicking required.
  - **Zen Garden:** Dedicated **L / R** triggers cycle available top-shelf tools (Watering Can, Fertilizer, Bug Spray, Phonograph, Chocolate, Glove, Money Sign, Wheelbarrow, Tree Food); **A** applies the tool on the selected plant; **B** drops/cancels.
  - **Dave's Shop:** Direct shelf-and-slot navigation.
  - **Minigames & Precision:** Smooth velocity-accelerated contextual virtual cursor for games requiring freeform aiming (*Whack-a-Zombie*, *Zombiquarium*).
- **Faithful Low-Latency Tracker Audio:** Replaces proprietary BASS with `libopenmpt` and `SDL-Mixer-X`, decoding Laura Shigihara's original `.mo3` modules into low-latency 1,024-sample audio streams (~46 ms latency) at 22,050 Hz with an 8 MB LRU sound effect cache.
- **Dynamic Overclocking & Power Resiliency:**
  - Automatic model detection: **1800 MHz** for Miyoo Mini Plus (AXP223 PMIC) and a safe **1400 MHz** ceiling for Miyoo Mini non-Plus (preventing undervoltage PMIC kernel panics).
  - Signal traps (`EXIT`, `INT`, `TERM`, `HUP`) with unconditional `sync` and governor restoration to `ondemand 1200` to eliminate FAT32 SD card corruption.
  - Direct hardware framebuffer restoration (`ResetFramebuffer()` via `ioctl(FBIOPAN_DISPLAY)`) completely eliminating post-game black screens.
- **Dual Installation Workflow:** 3-second automated PC builder (`Launcher.py`) or autonomous console conversion (`setup_miyoo`) with on-screen `/dev/fb0` progress tracking.

---

## ⚙️ Hardware Profile & Performance Target

| Component | Specification | Port Engineering Strategy |
|---|---|---|
| **SoC** | SigmaStar SSD202D | Dual ARM Cortex-A7 @ 1.2 GHz base; dynamic OC: **1800 MHz** (Plus) / **1400 MHz** (non-Plus) |
| **SIMD / FPU** | ARMv7-A NEON + VFPv4 | Vectorized Reanim matrix transforms and alpha blending (`-mfpu=neon-vfpv4`) |
| **RAM** | 128 MB DDR3 SiP | **~80–95 MB usable** userland; engine restricted to **<= 78 MB** steady-state |
| **GPU** | None (No 3D / OpenGL) | Custom 2D Software Rasterizer with 16 MB DMA pool on `/dev/fb0` |
| **Display** | 2.8" / 3.5" IPS LCD (640×480, 4:3) | Native 1:1 match; 0.8× texture scaling with 800×600 logical coordinate parity |
| **Audio** | SigmaStar DAC (`MI_AO`) | `killall -9 audioserver` on launch; clean daemon restoration on exit; 1024-sample buffer |
| **Storage** | microSD (FAT32 / exFAT) | Consolidated `main_mmp.pak` container for single-seek streaming I/O |

---

## 🏗️ Subsystem Architecture

```
+-------------------------------------------------------------------+
|                  Plants vs. Zombies Engine Core                   |
+-------------------------------------------------------------------+
|      Input Subsystem       |  Audio Subsystem  | Graphics Engine  |
| - 9x5 Grid Snapping        | - libopenmpt MO3  | - 0.8x Viewport  |
| - Sun Hover Auto-Vacuum    | - 22.05 kHz Stream| - NEON Software  |
| - L/R Seed Tray Cycle      | - 8 MB LRU Cache  |   Rasterizer     |
| - Contextual Smooth Cursor | - Channel Steal   | - 16 MB DMA Pool |
+----------------------------+-------------------+------------------+
|                          SDL2 Platform Layer                      |
+-------------------------------------------------------------------+
|                   Linux Kernel 4.9.84 (OnionOS)                   |
|   /dev/fb0 Framebuffer   |   ALSA / MI_AO Audio   |  cpuclock 1700|
+-------------------------------------------------------------------+
```

### 1. Viewport & Coordinate Space
The PC original operates at 800×600 logical coordinates. Rather than modifying hundreds of game XMLs, Reanim skeletal tracks, TodParticleSystem definitions, and collision boxes, this port preserves the **800×600 logical coordinate space** internally and applies a **0.8× viewport scale matrix** (`640 / 800 = 0.8`) during final software blitting. Textures are pre-scaled to 80% inside `main_mmp.pak`, ensuring 1:1 texel-to-pixel presentation on the 640×480 screen without runtime resizing overhead.

### 2. Audio Pipeline
The original Windows BASS library is replaced by `libopenmpt` integrated with `SDL-Mixer-X`:
- Tracker modules (`.mo3`) are streamed in 2,048-frame chunks directly into the SDL audio hook, consuming <= 2 MB of RAM.
- SFX files are resampled to 22,050 Hz and upmixed from mono to stereo to prevent pitch doubling on Miyoo's audio hardware.
- An 8 MB LRU cache enforces memory limits while channel-stealing ensures essential plant and zombie sounds are never dropped.

### 3. Display Reset & Black Screen Prevention
OnionOS uses `/dev/fb0` with panning double-buffering. Custom SDL video drivers can leave display offsets non-zero when closing. Upon exit or signal interception, `ResetFramebuffer()` queries `FBIOGET_VSCREENINFO`, resets `xoffset` and `yoffset` to `0`, and calls `ioctl(fb, FBIOPAN_DISPLAY, &vinfo)`, returning the user seamlessly to the OnionOS MainUI every time.

---

## 🎮 Handheld Control Scheme
 
| Button | Garden Battle Mode | Menus & Dave's Shop | Zen Garden | Minigames (*Whack-a-Zombie*, etc.) |
|---|---|---|---|---|
| **D-Pad** | Discrete 9×5 Grid Snapping (Tile-to-Tile) | Navigate Items / Shelves | Move Virtual Cursor / Target Pot | Smooth Contextual Virtual Cursor |
| **Cursor Hover** | **Auto-Collect Suns & Coins** | — | — | — |
| **L / R** | Cycle Seed Packets Prev / Next | Tab Pages Left / Right | **Cycle Zen Garden Tools** (Water, Fertilizer, Spray, Music, Chocolate, Glove, Money, Cart, Tree Food) | Cycle Available Items |
| **A** | Plant Selected Seed / Confirm | Select / Purchase | **Equip / Apply Tool to Plant** | Hit / Whack / Select |
| **B** | Cancel Seed Selection / Deselect | Back / Close Menu | **Cancel / Drop Held Tool (`ClearCursor`)** | Deselect / Back |
| **X** | **Quick Shovel Toggle** (or Slot Lever) | — | Secondary Action | Special Action |
| **Y** | Inspect Tile / Cancel | — | Cancel / Drop Tool | — |
| **Start** | In-Game Pause & Options Menu | Pause | Pause Menu | Pause Menu |
| **Select** | Fast Forward / Toggle Cursor Mode | — | Fast Forward / Toggle Cursor Mode | — |
| **MENU** | **Universal Pause / Exit (ESC)** | Exit / Return to OS | Universal Pause / Exit (ESC) | Universal Pause / Exit (ESC) |

> 💡 **Tip:** In lawn battles, you don't need to press a button to grab sun! Simply guide your cursor across falling or grounded sun tokens and they will be vacuumed instantly into your bank.
>
> 🌸 **Zen Garden Tip:** Press **L** or **R** at any time to instantly equip the Watering Can or cycle through your tools. Press **A** over any plant to care for it, and press **B** to instantly holster your tool!

---

## ⚖️ Legal & Clean-Room Architecture

This repository strictly complies with the **Bring Your Own Data (BYOD)** legal distribution model:
1. **0% Proprietary Assets:** The repository contains zero copyrighted PopCap graphics, animations, sounds, music, or `.pak` game archives.
2. **0 Windows Binaries:** No Windows `.exe` files are tracked in Git, preventing antivirus false positives and Microsoft SmartScreen blocks.
3. **User Ownership:** All game data is supplied directly by the user from their legitimately purchased copy of *Plants vs. Zombies GOTY* (Steam, EA App, Origin, or PopCap CD).

---

## 💾 Installation Guide

### Method A: Fast PC Preparation (Recommended)
*Prepares and optimizes assets on your computer in ~3 seconds using Python standard library.*

1. **Clone or Download:**
   ```bash
   git clone https://github.com/Francisco/PVZ.git
   cd PVZ
   ```
2. **Launch the Graphical Builder:**
   ```bash
   python Launcher.py
   ```
   *(Or run CLI: `python tools/build_release.py`)*
3. **Select Your Game Source:**
   - The launcher will automatically search for your Steam or EA App installation.
   - Alternatively, click **Browse** and select your `Plants vs. Zombies` directory containing `main.pak`.
4. **Build & Copy:**
   - Select your target folder (or your microSD card directly).
   - Click **Build Package**.
   - Copy the generated `Roms` folder to the root of your microSD card.

---

### Method B: Autonomous On-Device Installation
*No PC setup tools required—drag and drop your raw game file straight to your SD card.*

1. **Download Release Package:**
   Download `PvZ_MiyooMiniPlus_v1.0.0.zip` from the [Releases](https://github.com/Francisco/PVZ/releases) page.
2. **Extract to SD Card:**
   Extract the ZIP archive directly to the root of your microSD card. It will stage:
   ```text
   /mnt/SDCARD/Roms/PORTS/Games/Plants vs. Zombies/
   /mnt/SDCARD/Roms/PORTS/Shortcuts/Strategy/Plants vs. Zombies.port
   /mnt/SDCARD/Roms/PORTS/Imgs/Plants vs. Zombies.png
   ```
3. **Copy PC Asset File:**
   Locate `main.pak` in your Steam installation (`.../steamapps/common/Plants Vs Zombies/main.pak`) and copy it into:
   ```text
   /mnt/SDCARD/Roms/PORTS/Games/Plants vs. Zombies/main.pak
   ```
4. **Boot & Play:**
   - Insert the microSD card into your Miyoo Mini or Miyoo Mini Plus.
   - In OnionOS, open **Ports -> Strategy -> Plants vs. Zombies**.
   - On first launch, the autonomous installer (`setup_miyoo`) will display an on-screen progress bar on the LCD, downscale textures and resample audio into `main_mmp.pak`, back up your original file to `main.pak.bak`, delete `setup_miyoo` to free storage, and immediately launch into the game!

---

## 📊 Memory Budget & Profiling Matrix

The Miyoo Mini Plus features a 128 MB DDR3 SiP SoC with ~80–95 MB usable userland memory under OnionOS. This port was designed to strictly adhere to an **<= 85 MB hard ceiling**, maintaining a **<= 78 MB steady-state footprint** during peak zombie waves:

```
+--------------------------------------------------------------------------+
|                  OnionOS Usable RAM: ~95 MB Maximum                      |
|  +--------------------------------------------------------------------+  |
|  |             Hard Safety Ceiling: <= 85.0 MB (OOM Safe)             |  |
|  |  +----------------------------------------------------+  +-------+ |  |
|  |  |      Peak Steady-State Footprint: <= 78.0 MB       |  | Safety| |  |
|  |  |  +-----------------------------+  +--------------+ |  | Margin| |  |
|  |  |  | Base Steady-State: 57.0 MB  |  | Wave Surge:  | |  | 7.0 MB | |  |
|  |  |  | (Textures, Audio, Engine)   |  | 21.0 MB      | |  +-------+ |  |
|  |  |  +-----------------------------+  +--------------+ |            |  |
|  |  +----------------------------------------------------+            |  |
|  +--------------------------------------------------------------------+  |
+--------------------------------------------------------------------------+
```

### Static Subsystem Budget Allocation

| Subsystem | Budget | Description | Status |
|---|---|---|---|
| **Textures & Surfaces** | **24.0 MB** | 0.8× downscaled RGBA surfaces + 16 MB contiguous DMA texture pool | **PASS** |
| **Reanim Skeletal Tracks** | **8.0 MB** | Pre-parsed skeletal bone hierarchies and transformation matrices | **PASS** |
| **Tracker Music Stream** | **2.0 MB** | libopenmpt ring-buffer & 2,048-sample audio streaming buffer | **PASS** |
| **Sound Effects (SFX)** | **8.0 MB** | 22.05 kHz stereo SoundManager LRU cache with channel stealing | **PASS** |
| **Logic & Framebuffer** | **15.0 MB** | Game logic state, TodParticleSystem, projectiles, `/dev/fb0` double-buffer | **PASS** |
| **Base Steady-State** | **57.0 MB** | Normal lawn wave execution | **PASS** |
| **Peak Wave Surge Headroom**| **21.0 MB** | Dense zombie horde bursts, particle emitters, lawnmower blits | **PASS** |
| **Max Steady-State Target**| **78.0 MB** | Strict steady-state ceiling | **PASS** |
| **Platform Hard Ceiling** | **85.0 MB** | Strict OOM threshold (10 MB below kernel limit) | **PASS** |

Run the memory audit directly:
```bash
python scripts/package_release.py --audit-only
```

---

## 🛠️ Building from Source

Compilation uses the official Miyoo Mini Docker cross-compilation toolchain.

### Prerequisites
- Docker Desktop (or Linux Docker daemon)
- Python 3.8+
- Git

### 1. Compile ARM Targets via Docker
```bash
bash scripts/docker_build.sh
```
This builds all ARMv7 targets using the `aemiii91/miyoomini-toolchain:latest` container:
- `build/PvZ` (Main game executable)
- `build/setup_miyoo` (Autonomous on-device converter)
- Unit & regression test targets (`test_arm`, `test_viewport_scale`, `test_openmpt_playback`, `test_input_mapping`, `test_main_teardown`)

### 2. Run Test Suite
```bash
# Python regression and packaging tests
python -m unittest discover tests

# Docker QEMU ARM tests
docker run --rm -v "$(pwd):/root/workspace" -w /root/workspace/build \
  -e QEMU_LD_PREFIX=/opt/miyoomini-toolchain/usr/arm-linux-gnueabihf/sysroot \
  aemiii91/miyoomini-toolchain:latest bash -c "ctest --output-on-failure"
```

### 3. Generate Release Package
```bash
python scripts/package_release.py --version 1.0.0
```
This generates the Pure Ports tree in `dist/Roms/PORTS/` and produces `dist/PvZ_MiyooMiniPlus_v1.0.0.zip`.

---

## 📂 Directory Structure (OnionOS Pure Ports)

The release ZIP adheres strictly to the **Pure Ports Collection** standard:

```text
microSD Root
└── Roms/
    └── PORTS/
        ├── Imgs/
        │   └── Plants vs. Zombies.png          # Boxart preview for OnionOS Ports UI
        ├── Shortcuts/
        │   └── Strategy/
        │       └── Plants vs. Zombies.port     # Pure Port shortcut launcher (0755)
        └── Games/
            └── Plants vs. Zombies/
                ├── PvZ                         # ARMv7 Cortex-A7 engine executable (0755)
                ├── setup_miyoo                 # On-device autonomous setup converter (0755)
                ├── launch.sh                   # Environment launch script & traps (0755)
                ├── launcher.sh                 # Fallback launcher alias (0755)
                ├── HOW_TO_INSTALL.txt          # Clean-room BYOD user documentation
                ├── libs/                       # Local shared libraries
                ├── properties/                 # Game configuration properties
                └── userdata/                   # Player profiles and game save data
```

---

## 👥 Credits & Acknowledgments

- **PopCap Games:** Creators of the original masterpiece *Plants vs. Zombies*.
- **Laura Shigihara:** Composer of the iconic Plants vs. Zombies soundtrack.
- **[wszqkzqk/PvZ-Portable](https://github.com/wszqkzqk/PvZ-Portable):** Modern multiplatform C++ source foundation.
- **OnionOS Community:** Outstanding operating system, tools, and developer support for the Miyoo Mini Plus.
- **libopenmpt & SDL-Mixer-X Teams:** High-accuracy tracker music decoding and audio mixing engines.
- **Zuma Deluxe Miyoo Port Engineering Anthology:** Architectural reference and hardware resiliency guidelines.

---

<p align="center">
  <b>Plants vs. Zombies for Miyoo Mini Plus is an independent open-source fan project.</b><br>
  Plants vs. Zombies is a trademark of Electronic Arts Inc. / PopCap Games.
</p>
