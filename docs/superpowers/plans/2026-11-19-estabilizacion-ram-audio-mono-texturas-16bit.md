# 16-Bit Dual-Texture & Audio Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved memory stabilization architecture by standardizing audio to 22.05 kHz Mono (-9 to 12 MB), fixing 16-bit software rasterization guards and straight-alpha ARGB4444 pipelines (-16 MB), reclaiming quiescent heap with glibc `malloc_trim(0)`/`mallopt`, and consolidating static memory pools (-18 MB), cutting peak combat RSS from ~78 MB down to ~50–53 MB (>30 MB safety margin below the 83 MB OOM killer) with locked 60 FPS and zero swap.

**Architecture:** Standardize `SDLSoundManager` and `OpenMptMusicInterface` to 1-channel 22,050 Hz with dynamic `openmpt_module_read_mono` binding; expand `SoftwareRenderer.cpp:673` guard to accept 16-bit textures; fix `SoftwareSurface` pixel accessors for `FMT_ARGB4444`; add `NeonBlit_Span_RGB565_to_ARGB8888` for backbuffer unpack; eliminate `SpriteCachePool` and downsize `DmaTexturePool` to 10 MB; add state-boundary `malloc_trim(0)` hooks and startup `M_MMAP_THRESHOLD`; and update `setup_converter.c` with straight-alpha ARGB4444, clamped Bayer dithering, and -3 dB audio attenuation.

**Tech Stack:** C++20, C11, ARM NEON SIMD Intrinsics, SDL2, SDL_mixer, libopenmpt (dynamic loader), CMake, Linux Framebuffer (`/dev/fb0`), Docker ARMv7 Toolchain (`miyoo-build:latest`).

**Spec:** `docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md`

## Global Constraints

- Target Platform: SigmaStar SSD202D (128 MB DDR3, 640×480 3.5" IPS display, OnionOS).
- Peak steady-state combat RSS must not exceed 53.0 MB (safety margin $\ge 30.0\text{ MB}$ below 83 MB kernel OOM ceiling).
- Absolute zero swap memory: No swapfiles or swap partitions on `/mnt/SDCARD`.
- Locked 60.0 FPS performance: Zero runtime asset conversions during gameplay; all quantization offline.
- Backbuffer `mScreenImage` remains 32-bit ARGB8888; bitmap fonts in `data/fonts` remain 32-bit ARGB8888.
- Audio standardized to 1-channel 22,050 Hz 16-bit PCM across all sound and music subsystems.

## Review Focus

1. **Straight-Alpha Composition Fidelity:** ARGB4444 textures in `SWTri.cpp` must use straight-alpha color channels without double alpha multiplication (no black rims, dark halos, or corrupted vertex tinting).
2. **Backbuffer Blit Integrity:** Opaque RGB565 backgrounds must unpack cleanly into 32-bit ARGB8888 `mScreenImage` without color channel swapping or stride distortion.
3. **16-Bit Surface Invisibility Fix:** `SoftwareRenderer.cpp:673` must accept `mBytesPerPixel == 2` so plants, zombies, and reanimations render visibly.
4. **Dynamic OpenMPT Binding Safety:** `OpenMptMusicInterface` must resolve `openmpt_module_read_mono` via `DLSYM_LOOKUP` without null dereferences or unresolved external symbol crashes.
5. **Static Pool Determinism:** `SpriteCachePool` (12 MB) must be cleanly removed and `DmaTexturePool` resized to 10 MB without leaking memory or crashing level transitions.

---

### Task 1: Global Audio Standardization to 22.05 kHz Mono

**Files:**
- Modify: `src/SexyAppFramework/sound/SDLSoundManager.cpp:33-70`
- Modify: `src/audio/OpenMptMusicInterface.hpp:60-120`
- Modify: `src/audio/OpenMptMusicInterface.cpp:90-150, 240-300`
- Test: `tests/test_openmpt_playback.cpp:200-280`

**Interfaces:**
- Consumes: `Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024)`, `DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono")`.
- Produces: `OpenMptMusicInterface::kNumChannels = 1`, `openmpt_module_read_mono_fn mFnReadMono`, `SDLSoundManager::mMixerChannels = 1`.

- [ ] **Step 1: Write/Update the failing test in `tests/test_openmpt_playback.cpp`**
  Update channel assertions and buffer dimensions from stereo to mono:
  ```cpp
  assert(OpenMptMusicInterface::kNumChannels == 1);
  constexpr size_t kChunk = OpenMptMusicInterface::kChunkFrames;
  alignas(8) int16_t buffer[kChunk * OpenMptMusicInterface::kNumChannels];
  ```

- [ ] **Step 2: Run test to verify it fails**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest cmake --build build --target test_openmpt_playback`
  Expected: FAIL (assertion on `kNumChannels == 1` fails with current `kNumChannels = 2`).

- [ ] **Step 3: Implement 22.05 kHz Mono in `SDLSoundManager.cpp`**
  In `SDLSoundManager::SDLSoundManager()`:
  ```cpp
  if (Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024))
  {
      Sexy::LogInfoLn("Failed to initialize SDL mixer");
      return;
  }
  ```

- [ ] **Step 4: Implement dynamic mono streaming in `OpenMptMusicInterface`**
  In `OpenMptMusicInterface.hpp`:
  - Set `static constexpr size_t kNumChannels = 1;`.
  - Define `typedef size_t (*openmpt_module_read_mono_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* mono);`.
  - Add `openmpt_module_read_mono_fn mFnReadMono = nullptr;`.
  In `OpenMptMusicInterface.cpp`:
  - In `TryLoadOpenMptSymbols()`: resolve `mFnReadMono = reinterpret_cast<openmpt_module_read_mono_fn>(DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono"));`.
  - In `RenderAudioChunk()`: invoke `mFnReadMono(song.openmptMod, static_cast<int32_t>(kSampleRate), frameCount, monoOutput);`.

- [ ] **Step 5: Run test to verify it passes**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest ctest --test-dir build -R test_openmpt_playback --output-on-failure`
  Expected: PASS

- [ ] **Step 6: Commit Task 1**
  ```bash
  git add src/SexyAppFramework/sound/SDLSoundManager.cpp src/audio/OpenMptMusicInterface.hpp src/audio/OpenMptMusicInterface.cpp tests/test_openmpt_playback.cpp
  git commit -m "feat(audio): standardize SDLSoundManager and OpenMPT to 22.05 kHz mono"
  ```

---

### Task 2: State-Boundary Heap Reclamation and Allocator Tuning

**Files:**
- Modify: `src/LawnApp.cpp:70-85, 200-260, 480-530`
- Modify: `src/Lawn/Widget/AlmanacDialog.cpp:55-75, 175-190`
- Modify: `src/Lawn/Board.cpp:390-405, 1490-1505`

**Interfaces:**
- Consumes: `<malloc.h>`, `malloc_trim(0)`, `mallopt(M_MMAP_THRESHOLD, 64 * 1024)`.
- Produces: Portable `TrimHeapIfLinux()` helper function quarantined strictly to state transitions.

- [ ] **Step 1: Define portable `TrimHeapIfLinux()` in `src/LawnApp.cpp`**
  ```cpp
  #if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)
  #include <malloc.h>
  static inline void TrimHeapIfLinux() {
      malloc_trim(0);
  }
  #else
  static inline void TrimHeapIfLinux() {}
  #endif
  ```

- [ ] **Step 2: Add `M_MMAP_THRESHOLD` tuning to `LawnApp::Init()`**
  In `LawnApp::Init()` (prior to resource loading):
  ```cpp
  #if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)
      mallopt(M_MMAP_THRESHOLD, 64 * 1024);
  #endif
  ```

- [ ] **Step 3: Guard state-transition trim hooks across lifecycle**
  Replace unguarded `malloc_trim(0)` calls with `TrimHeapIfLinux()` in:
  - `LawnApp::LoadingThreadProc` (loading screen end)
  - `LawnApp::KillStoreScreen` (store screen exit)
  - `LawnApp::EndLevel` (level end)
  - `AlmanacDialog::~AlmanacDialog` in `src/Lawn/Widget/AlmanacDialog.cpp`
  - `Board::DisposeBoard` in `src/Lawn/Board.cpp`

- [ ] **Step 4: Verify build compiles cleanly**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest cmake --build build --target PvZ -j$(nproc)`
  Expected: PASS with 0 errors.

- [ ] **Step 5: Commit Task 2**
  ```bash
  git add src/LawnApp.cpp src/Lawn/Widget/AlmanacDialog.cpp src/Lawn/Board.cpp
  git commit -m "feat(memory): configure glibc mmap threshold and portable heap trim hooks"
  ```

---

### Task 3: Backbuffer Unpack Kernel (`NeonBlit_Span_RGB565_to_ARGB8888`)

**Files:**
- Modify: `src/graphics/NeonBlit.hpp:30-55`
- Modify: `src/graphics/NeonBlit.cpp:465-515`

**Interfaces:**
- Consumes: 16-bit RGB565 source buffer (`const uint16_t* src`).
- Produces: `NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count)` unpacking to 32-bit ARGB8888.

- [ ] **Step 1: Declare `NeonBlit_Span_RGB565_to_ARGB8888` in `NeonBlit.hpp`**
  ```cpp
  void NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count);
  ```

- [ ] **Step 2: Implement NEON and scalar fallback kernel in `NeonBlit.cpp`**
  ```cpp
  void NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count) {
      if (!dst || !src || count == 0) return;
  #if PVZ_HAS_NEON
      while (count >= 8) {
          uint16x8_t s16 = vld1q_u16(src);
          uint16x8_t r5 = vandq_u16(vshrq_n_u16(s16, 11), vdupq_n_u16(0x1F));
          uint16x8_t g6 = vandq_u16(vshrq_n_u16(s16, 5),  vdupq_n_u16(0x3F));
          uint16x8_t b5 = vandq_u16(s16, vdupq_n_u16(0x1F));

          uint16x8_t r8 = vorrq_u16(vshlq_n_u16(r5, 3), vshrq_n_u16(r5, 2));
          uint16x8_t g8 = vorrq_u16(vshlq_n_u16(g6, 2), vshrq_n_u16(g6, 4));
          uint16x8_t b8 = vorrq_u16(vshlq_n_u16(b5, 3), vshrq_n_u16(b5, 2));
          uint16x8_t a8 = vdupq_n_u16(0xFF);

          uint8x8x4_t res;
          res.val[0] = vqmovn_u16(b8);
          res.val[1] = vqmovn_u16(g8);
          res.val[2] = vqmovn_u16(r8);
          res.val[3] = vqmovn_u16(a8);
          vst4_u8(reinterpret_cast<uint8_t*>(dst), res);

          src += 8;
          dst += 8;
          count -= 8;
      }
  #endif
      while (count > 0) {
          uint16_t c16 = *src++;
          uint32_t r = ((c16 >> 11) & 0x1F) * 255 / 31;
          uint32_t g = ((c16 >> 5) & 0x3F) * 255 / 63;
          uint32_t b = (c16 & 0x1F) * 255 / 31;
          *dst++ = 0xFF000000u | (r << 16) | (g << 8) | b;
          count--;
      }
  }
  ```

- [ ] **Step 3: Verify build compiles cleanly**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest cmake --build build --target PvZ -j$(nproc)`
  Expected: PASS

- [ ] **Step 4: Commit Task 3**
  ```bash
  git add src/graphics/NeonBlit.hpp src/graphics/NeonBlit.cpp
  git commit -m "feat(graphics): implement NeonBlit_Span_RGB565_to_ARGB8888 unpack kernel"
  ```

---

### Task 4: SoftwareRenderer 16-Bit Pipeline Fixes & Typed Dispatch

**Files:**
- Modify: `src/graphics/SoftwareRenderer.cpp:160-188, 555-590, 670-685`
- Modify: `src/SexyAppFramework/graphics/MemoryImage.h:65-75`

**Interfaces:**
- Consumes: `SoftwareSurface::mFormat`, `NeonBlit_Span_RGB565_to_ARGB8888`, `NeonBlit_Span_ARGB4444_PreMul`.
- Produces: Relaxed guard at line 673, `SoftwareSurface::GetPixel`/`SetPixel` with `FMT_ARGB4444` support, `MemoryImage::GetBits16()`.

- [ ] **Step 1: Add `GetBits16()` to `MemoryImage.h`**
  In `src/SexyAppFramework/graphics/MemoryImage.h`:
  ```cpp
  uint16_t* GetBits16() const { return reinterpret_cast<uint16_t*>(mBits.get()); }
  ```

- [ ] **Step 2: Fix `SoftwareSurface::GetPixel` and `SetPixel` in `SoftwareRenderer.cpp`**
  In `SoftwareSurface::GetPixel` (lines 164–171):
  ```cpp
  } else if (mBytesPerPixel == 2) {
      const uint16_t* row = reinterpret_cast<const uint16_t*>(mPixels + y * mPitch);
      uint16_t c16 = row[x];
      if (mFormat == PixelFormat::FMT_ARGB4444) {
          uint32_t a = (c16 >> 12) & 0x0F;
          uint32_t r = (c16 >> 8) & 0x0F;
          uint32_t g = (c16 >> 4) & 0x0F;
          uint32_t b = c16 & 0x0F;
          return ((a | (a << 4)) << 24) | ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
      } else {
          uint32_t r = ((c16 >> 11) & 0x1F) * 255 / 31;
          uint32_t g = ((c16 >> 5) & 0x3F) * 255 / 63;
          uint32_t b = (c16 & 0x1F) * 255 / 31;
          return 0xFF000000u | (r << 16) | (g << 8) | b;
      }
  }
  ```
  In `SoftwareSurface::SetPixel` (lines 180–186): branch on `mFormat == PixelFormat::FMT_ARGB4444` to pack `(a4 << 12) | (r4 << 8) | (g4 << 4) | b4`.

- [ ] **Step 3: Update `SoftwareRenderer::RasterizeTriangleSpan` guard at line 673**
  ```cpp
  if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 ||
      (texture->mBytesPerPixel != 4 && texture->mBytesPerPixel != 2) || mPhysClipRect.IsEmpty())
      return;
  ```

- [ ] **Step 4: Update `SoftwareRenderer::DrawImage` for 16-bit dispatch**
  In `SoftwareRenderer::DrawImage` fast-path (lines 560–570):
  ```cpp
  if (image->mFormat == PixelFormat::FMT_RGB565) {
      const uint16_t* src_ptr = reinterpret_cast<const uint16_t*>(image->mPixels + sy * image->mPitch) + src_offset_x;
      NeonBlit_Span_RGB565_to_ARGB8888(dst_ptr, src_ptr, clipped.w);
  } else if (image->mFormat == PixelFormat::FMT_ARGB4444) {
      const uint16_t* src_ptr = reinterpret_cast<const uint16_t*>(image->mPixels + sy * image->mPitch) + src_offset_x;
      NeonBlit_Span_ARGB4444_PreMul(dst_ptr, src_ptr, clipped.w);
  } else {
      NeonBlit_Span(dst_ptr, src_ptr, clipped.w, mode, tint);
  }
  ```

- [ ] **Step 5: Verify build compiles cleanly**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest cmake --build build --target PvZ -j$(nproc)`
  Expected: PASS

- [ ] **Step 6: Commit Task 4**
  ```bash
  git add src/graphics/SoftwareRenderer.cpp src/SexyAppFramework/graphics/MemoryImage.h
  git commit -m "fix(renderer): expand 16-bit span guard, fix surface accessors, and add typed blit dispatch"
  ```

---

### Task 5: Memory Pool Consolidation

**Files:**
- Modify: `src/graphics/SoftwareRenderer.hpp:215-225`
- Modify: `src/LawnApp.cpp:70-80, 240-250, 295-305, 395-405, 515-525, 630-640`
- Modify: `src/Lawn/Board.cpp:65-70, 290-300, 595-605, 1490-1500`

**Interfaces:**
- Consumes: `DmaTexturePool`.
- Produces: Removal of `SpriteCachePool` static 12 MB arena; downsized `DmaTexturePool` to 10 MB.

- [ ] **Step 1: Downsize `DmaTexturePool` in `SoftwareRenderer.hpp`**
  In `src/graphics/SoftwareRenderer.hpp:218`:
  ```cpp
  static constexpr size_t DEFAULT_POOL_SIZE = 10 * 1024 * 1024; // 10 MB (downsized from 16 MB for 16-bit textures)
  ```

- [ ] **Step 2: Remove `SpriteCachePool` calls in `LawnApp.cpp` and `Board.cpp`**
  Remove `#include "PvzpLib/SpriteCachePool.h"` and calls to:
  - `SpriteCachePool::FreePool();`
  - `SpriteCachePool::FreeLevelArena();`
  - `SpriteCachePool::PreloadSynchronous(this);`
  Verify that `DmaTexturePool::Instance().Reset()` is called at level cleanups.

- [ ] **Step 3: Verify build compiles cleanly**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest cmake --build build --target PvZ -j$(nproc)`
  Expected: PASS

- [ ] **Step 4: Commit Task 5**
  ```bash
  git add src/graphics/SoftwareRenderer.hpp src/LawnApp.cpp src/Lawn/Board.cpp
  git commit -m "feat(memory): eliminate SpriteCachePool and downsize DmaTexturePool to 10 MB"
  ```

---

### Task 6: Asset Converter Updates (Straight-Alpha ARGB4444, Audio -3dB, Clamped Bayer Dithering)

**Files:**
- Modify: `src/setup/setup_converter.c:50-120, 260-350`
- Test: `tests/test_setup_conversion.cpp`

**Interfaces:**
- Consumes: Raw PNG and WAV asset buffers.
- Produces: Straight-alpha ARGB4444 quantized buffers, clamped Bayer dithering, and -3 dB audio attenuation in `convert_wav_buffer`.

- [ ] **Step 1: Implement straight-alpha ARGB4444 quantizer with clamped dither**
  In `src/setup/setup_converter.c`, replace pre-multiplied formulas with straight-alpha formulas:
  ```c
  int dither = (bayer4x4[y & 3][x & 3] - 8);
  int a = clamp_val((int)src_a + dither, 0, 255) >> 4;
  int r = clamp_val((int)src_r + dither, 0, 255) >> 4;
  int g = clamp_val((int)src_g + dither, 0, 255) >> 4;
  int b = clamp_val((int)src_b + dither, 0, 255) >> 4;
  uint16_t argb4444 = (uint16_t)((a << 12) | (r << 8) | (g << 4) | b);
  ```

- [ ] **Step 2: Add -3 dB attenuation to `convert_wav_buffer`**
  In `convert_wav_buffer` (lines 310–330):
  ```c
  for (size_t i = 0; i < out_frames; ++i) {
      float s = mono_out[i] * 0.7079f; // -3 dB headroom to prevent clipping
      if (s > 1.0f) s = 1.0f;
      if (s < -1.0f) s = -1.0f;
      dst_samples[i] = (int16_t)round(s * 32767.0f);
  }
  ```

- [ ] **Step 3: Run setup conversion test to verify passing**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest ctest --test-dir build -R test_setup_conversion --output-on-failure`
  Expected: PASS

- [ ] **Step 4: Commit Task 6**
  ```bash
  git add src/setup/setup_converter.c
  git commit -m "feat(converter): implement straight-alpha ARGB4444 quantization and -3 dB audio headroom"
  ```

---

### Task 7: Full System Verification, Memory Profiling & Staging

**Files:**
- Modify: `scripts/package_release.py:40-75`
- Target: `bin/PvZ`, `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`, `ports/Games/Plants vs. Zombies/PvZ`

**Interfaces:**
- Consumes: Stripped native ARMv7 binary, package release scripts.
- Produces: Passing `ctest`, passing `package_release.py --audit-only` with $\le 53.0\text{ MB}$ RSS and $\ge 30.0\text{ MB}$ headroom.

- [ ] **Step 1: Execute full test suite via CTest**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest ctest --test-dir build --output-on-failure`
  Expected: All tests PASS.

- [ ] **Step 2: Update `package_release.py` memory budget table**
  Update the RSS expectation table in `scripts/package_release.py`:
  - Audio: 4.0 MB (reduced from 15.0 MB)
  - Textures: 12.0 MB (reduced from 32.0 MB)
  - DMA Pool: 10.0 MB (downsized from 16.0 MB)
  - Steady-state Combat RSS: 51.0 MB ($\le 53.0\text{ MB}$)
  - Headroom: 32.0 MB ($\ge 30.0\text{ MB}$)

- [ ] **Step 3: Run memory budget audit script**
  Run: `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest python3 scripts/package_release.py --audit-only`
  Expected: PASS (Audit OK, headroom $\ge 30.0\text{ MB}$).

- [ ] **Step 4: Compile stripped binary and stage across release directories**
  Run:
  ```bash
  docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest cmake --build build --target PvZ -j$(nproc)
  docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest arm-linux-gnueabihf-strip build/PvZ -o bin/PvZ
  cp bin/PvZ "packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ"
  cp bin/PvZ "ports/Games/Plants vs. Zombies/PvZ"
  ```

- [ ] **Step 5: Commit Task 7**
  ```bash
  git add scripts/package_release.py bin/PvZ "packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ" "ports/Games/Plants vs. Zombies/PvZ"
  git commit -m "chore(release): verify all tests pass, update memory audit budget to 53 MB RSS, and stage binary"
  ```
