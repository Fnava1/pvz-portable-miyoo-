---
title: Model-specific CPU clock detection and sleep-resume restoration
author: Fnava1
date: 2026-10-24
branch: main
touches:
  - src/main_miyoo.cpp
  - src/main_miyoo.hpp
  - ports/Games/Plants vs. Zombies/launch.sh
  - packaging/Roms/PORTS/Games/Plants vs. Zombies/launch.sh
  - scripts/package_release.py
  - tools/build_release.py
---

## Decided
- Replace single-interface wlan0 detection with multi-tier detection in launch scripts: check /tmp/deviceModel (354 = Plus), /customer/app/axp_test (factory Plus NAND), I2C AXP223 probe, cmdline, and wlan0, exporting TARGET_CPU_MHZ (1800 for MM+, 1400 for MM).
- Integrate monotonic sleep-resume detection into HardwareWatchdogThreadFunc in src/main_miyoo.cpp: when monotonic diff exceeds 1200 ms between 50 ms poll ticks, safely restore CPU governor and model-specific clock via direct POSIX descriptors (< 15 µs overhead, zero fork/system).
- Ensure model-specific clocks (1800 MHz for Plus, 1400 MHz for non-Plus) are strictly honored during both initial launch and sleep-resume restoration.

## Tried and ruled out
- Core 1 multithreaded downscaling / presentation worker (Phase 5 MiyooWorker): downscaler already takes only 1.2 ms on Core 0; moving it to Core 1 introduces inter-core futex overhead and starves the SDL/OpenMPT audio mixer on Core 1, causing audio popping.
- Adaptive particle culling in PvzpParticleHolder: bottle-neck profiling showed lag in horde scenes was caused by bilinear filtering in SWTri and floating-point math in FastStretchBlt (both resolved in b29a0d2), not particle count. Dropping particles risks breaking gameplay indicators.
- Using system() or fork() to re-invoke cpuclock from C++: forks duplicate process page tables and cause latency spikes or OOM triggers on embedded Linux.

## Watch out
- Never hardcode 1800 MHz across all hardware: standard Miyoo Mini (v1-v4 non-Plus) will suffer PMIC undervoltage kernel panics if clocked above 1400-1500 MHz. Always distinguish hardware model.
- Keep sysfs file descriptors short-lived and non-blocking in the watchdog thread.
