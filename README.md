# Plants vs. Zombies — Miyoo Mini Plus Port

[![License: MIT / Clean-Room BYOD](https://img.shields.io/badge/License-MIT%20%2F%20BYOD-green.svg)](#legal--clean-room-architecture)
[![Platform: Miyoo Mini & Plus](https://img.shields.io/badge/Platform-Miyoo%20Mini%20%26%20Plus%20%28OnionOS%29-blue.svg)](#hardware-profile--performance-target)
[![Architecture: ARMv7 Cortex-A7](https://img.shields.io/badge/Arch-ARMv7--A%20%28Hard%20Float%20%2B%20NEON%29-orange.svg)](#subsystem-architecture)

A port of **Plants vs. Zombies: Game of the Year Edition** for the **Miyoo Mini & Plus** running **OnionOS** based on Pvz-Portable decomp.
---
**Bugs:**
- random crashes after some levels, currently working on it.
- buttons stop working on Zombatar
---

## Features

- **Hybrid Ergonomic Controls:**
  - **Lawn Battle:** Discrete 9×5 grid snapping on D-pad with L/R seed packet cycling.
  - **Hover Sun Vacuum:** Hovering the cursor over falling or produced suns and coins automatically collects them.
  - **Zen Garden:** Dedicated **L / R** triggers cycle available top-shelf tools (Watering Can, Fertilizer, Bug Spray, Phonograph, Chocolate, Glove, Money Sign, Wheelbarrow, Tree Food); **A** applies the tool on the selected plant; **B** drops/cancels.
  - **Dave's Shop:** Direct shelf-and-slot navigation.
- **Dynamic Overclocking & Power Resiliency:**
  - Automatic model detection: **1800 MHz** for Miyoo Mini Plus (AXP223 PMIC) and a safe **1400 MHz** ceiling for Miyoo Mini non-Plus (preventing undervoltage PMIC kernel panics).
  - Signal traps (`EXIT`, `INT`, `TERM`, `HUP`) with unconditional `sync` and governor restoration to `ondemand 1200` to eliminate FAT32 SD card corruption.
  - Direct hardware framebuffer restoration (`ResetFramebuffer()` via `ioctl(FBIOPAN_DISPLAY)`) completely eliminating post-game black screens.
- **Dual Installation Workflow:** 3-second automated PC builder (`Launcher.py`) or autonomous console conversion (`setup_miyoo`) with on-screen `/dev/fb0` progress tracking.

---

## Control Scheme
 
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

> **Tip:** In lawn battles, simply guide your cursor across falling or grounded sun tokens and they will be vacuumed instantly into your bank.
>
> **Zen Garden Tip:** Press **L** or **R** at any time to instantly equip the Watering Can or cycle through your tools. Press **A** over any plant to care for it, and press **B** to instantly holster your tool!

---

## Clean-Room Architecture

This repository strictly complies with the **Bring Your Own Data (BYOD)** legal distribution model:
1. **0% Proprietary Assets:** The repository contains zero copyrighted PopCap graphics, animations, sounds, music, or `.pak` game archives.
2. **0 Windows Binaries:** No Windows `.exe` files are tracked in Git, preventing antivirus false positives and Microsoft SmartScreen blocks.
3. **User Ownership:** All game data is supplied directly by the user from their legitimately purchased copy of *Plants vs. Zombies GOTY* (Steam, EA App, Origin, or PopCap CD).

---

## Installation Guide

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
   Download `PvZ_MiyooMiniPlus_v1.0.1.zip` from the [Releases](https://github.com/Francisco/PVZ/releases) page.
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
   - On first launch, the autonomous installer (`setup_miyoo`) will display an on-screen progress bar on the LCD, downscale textures and resample audio into `main_mmp.pak`, back up your original file to `main.pak.bak`, delete `setup_miyoo` to free storage, and immediately launch into the game! (**The first launch GUI visuals are bugged right now, this does not affect the functionality and I'll fix it later**)

---

## Building from Source

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
python scripts/package_release.py --version 1.0.1
```
This generates the Pure Ports tree in `dist/Roms/PORTS/` and produces `dist/PvZ_MiyooMiniPlus_v1.0.1.zip`.

---

## Credits & Acknowledgments

- **PopCap Games:** Creators of the original masterpiece *Plants vs. Zombies*.
- **Laura Shigihara:** Composer of the iconic Plants vs. Zombies soundtrack.
- **[wszqkzqk/PvZ-Portable](https://github.com/wszqkzqk/PvZ-Portable):** Modern multiplatform C++ source foundation.
- **OnionOS Community:** Outstanding operating system, tools, and developer support for the Miyoo Mini Plus.
- **libopenmpt & SDL-Mixer-X Teams:** High-accuracy tracker music decoding and audio mixing engines.
- **Zuma Deluxe Miyoo Port Engineering Anthology:** Architectural reference and hardware resiliency guidelines.
