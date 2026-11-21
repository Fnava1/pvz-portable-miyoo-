# 16-Bit Dual-Texture Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a 16-bit dual-texture rendering and asset pipeline (RGB565 for backgrounds, pre-multiplied ARGB4444 for sprites/reanimations, and preserved 32-bit ARGB8888 for fonts), cutting texture RAM by ~16 MB and dropping combat RSS to ~62 MB for >22 MB headroom on the Miyoo Mini Plus with zero swap.

**Architecture:** Extend `SoftwareSurface` and `MemoryImage` with format tagging (`PixelFormat`), update `ReanimAtlasMakeBlankMemoryImage` and `SWTri_GetTexel.inc` to sample and composite 16-bit texels via ARM NEON SIMD, implement direct RGB565 stream copying, and integrate offline 4×4 Bayer dithering and ARGB4444 pre-multiplication into `setup_converter.c`.

**Tech Stack:** C++20, C11, ARM NEON SIMD Intrinsics, CMake, Linux Framebuffer (`/dev/fb0`), Docker ARMv7 Toolchain (`miyoo-build:latest`), Python 3.

**Spec:** `docs/designs/arquitectura_texturas_16bit_estabilizacion_ram.md`

## Global Constraints

- Hardware target: SigmaStar SSD202D (128 MB RAM, 640×480 3.5" IPS display).
- Steady-state combat RSS must remain $\le 65.0\text{ MB}$ (lowered from ~78 MB).
- Permanent safety margin before kernel OOM limit (85 MB) must exceed 20 MB.
- Absolute zero swap memory: No swapfiles or swap partitions on `/mnt/SDCARD`.
- Locked 60.0 FPS native performance: No runtime texture conversions during gameplay.
- Bitmap fonts (`data/fonts`) remain strictly in 32-bit ARGB8888 for text clarity.

## Review Focus

1. **Pre-multiplied Alpha Fidelity:** Sprites in ARGB4444 must not show dark halos or perimeter discoloration around translucent antialiased edges.
2. **Gradient Smoothness:** Night and Pool backgrounds in RGB565 must not display noticeable color banding on the 3.5" display.
3. **NEON Vector Alignment:** 16-bit span blitting must handle unaligned memory boundaries gracefully without bus errors.
4. **Font Sharpness:** Dialog and Almanac typography must retain 100% legibility and contrast matching the 32-bit baseline.
5. **Dynamic Atlas Allocation Safety:** Detonating a Jalapeno against a Zamboni mid-combat must cleanly instantiate `REANIM_ZOMBIE_CHARRED_ZAMBONI` in 16-bit without approaching the OOM limit.

---

### Task 1: Surface Format Tagging in SoftwareRenderer

**Files:**
- Modify: `src/graphics/SoftwareRenderer.hpp:240-265`
- Modify: `src/graphics/SoftwareRenderer.cpp:520-645`

- [x] **Step 1: Declare `PixelFormat` enum and add format fields to `SoftwareSurface`**
  In `src/graphics/SoftwareRenderer.hpp`, add:
  ```cpp
  enum class PixelFormat : uint8_t {
      FMT_ARGB8888 = 0,
      FMT_RGB565   = 1,
      FMT_ARGB4444 = 2
  };
  ```
  In `SoftwareSurface`, add `PixelFormat mFormat = PixelFormat::FMT_ARGB8888;`. Update constructors to compute `mPitch = mWidth * mBytesPerPixel` dynamically (where `mBytesPerPixel` is 2 for `FMT_RGB565` and `FMT_ARGB4444`, and 4 for `FMT_ARGB8888`).

- [x] **Step 2: Relax assertions in `SoftwareRenderer.cpp`**
  Update lines 521, 605, and 639 in `src/graphics/SoftwareRenderer.cpp` to permit both `mBytesPerPixel == 4` and `mBytesPerPixel == 2`.

- [x] **Step 3: Verify build compiles cleanly**
  Run CMake build to verify header updates compile without syntax or typing errors.

- [x] **Step 4: Commit Task 1**
  `git commit -m "feat(graphics): add PixelFormat tagging and 16-bit surface support to SoftwareRenderer"`

---

### Task 2: Format Support and Allocation in MemoryImage and ReanimAtlas

**Files:**
- Modify: `src/SexyAppFramework/graphics/MemoryImage.h:40-70`
- Modify: `src/SexyAppFramework/graphics/MemoryImage.cpp:1060-1120`
- Modify: `src/PvzpLib/ReanimAtlas.cpp:45-65`

- [x] **Step 1: Add `PixelFormat` to `MemoryImage`**
  In `src/SexyAppFramework/graphics/MemoryImage.h`, add member `PvZ::Graphics::PixelFormat mPixelFormat = PvZ::Graphics::PixelFormat::FMT_ARGB8888;` and helper `int GetBytesPerPixel() const;`.

- [x] **Step 2: Update `MemoryImage::Create()` buffer sizing**
  In `src/SexyAppFramework/graphics/MemoryImage.cpp`, adjust memory allocation in `Create()` and `SetBits()` so that if `mPixelFormat != FMT_ARGB8888`, the buffer is sized as `(mWidth * mHeight * 2 + 7) / 4` uint32 words instead of allocating 4 bytes per pixel.

- [x] **Step 3: Update `ReanimAtlasMakeBlankMemoryImage()`**
  In `src/PvzpLib/ReanimAtlas.cpp:46-62`, configure `aImage->mPixelFormat = PvZ::Graphics::PixelFormat::FMT_ARGB4444;` and size the buffer to 16 bits per pixel (`theWidth * theHeight * 2 + 2` bytes), immediately halving atlas allocation sizes.

- [x] **Step 4: Verify build compiles cleanly**
  Run CMake build to confirm `MemoryImage` and `ReanimAtlas` changes compile.

- [x] **Step 5: Commit Task 2**
  `git commit -m "feat(reanim): configure ReanimAtlas to allocate 16-bit ARGB4444 memory images"`

---

### Task 3: 16-Bit Texel Sampling in SWTri Software Rasterizer

**Files:**
- Modify: `src/SexyAppFramework/graphics/SWTri.h:50-75`
- Modify: `src/SexyAppFramework/graphics/SWTri.cpp:485-590`
- Modify: `src/SexyAppFramework/graphics/SWTri/SWTri_GetTexel.inc:25-65`

- [x] **Step 1: Add `format` to `SWTextureInfo`**
  In `src/SexyAppFramework/graphics/SWTri.h`, add `uint8_t format;` to `struct SWTextureInfo`.

- [x] **Step 2: Populate texture format in `SWTri.cpp`**
  In `src/SexyAppFramework/graphics/SWTri.cpp:492, 580`, populate `textureInfo.format = static_cast<uint8_t>(theImage->mPixelFormat);`.

- [x] **Step 3: Update `SWTri_GetTexel.inc` for 16-bit texel sampling**
  In `src/SexyAppFramework/graphics/SWTri/SWTri_GetTexel.inc`, branch on format:
  When `textureInfo->format == 2` (`FMT_ARGB4444`):
  Read `uint16_t` from `pTexture`, expand 4-bit nibbles to 8-bit channels in registers, and apply pre-multiplied alpha composition ($\text{Dst} = \text{Src} + \text{Dst} \cdot (1 - \text{Src}_A)$).

- [x] **Step 4: Verify build compiles cleanly**
  Run CMake build to confirm rasterizer templates compile with zero warnings.

- [x] **Step 5: Commit Task 3**
  `git commit -m "feat(rasterizer): implement 16-bit ARGB4444 texel sampling and pre-multiplied blend in SWTri"`

---

### Task 4: ARM NEON 16-Bit Blitting Kernels

**Files:**
- Modify: `src/graphics/NeonBlit.hpp:20-50`
- Modify: `src/graphics/NeonBlit.cpp:430-495`

- [x] **Step 1: Declare 16-bit NEON span functions in `NeonBlit.hpp`**
  Add declarations:
  - `void NeonBlit_Span_RGB565(uint16_t* dst, const uint16_t* src, size_t count);`
  - `void NeonBlit_Span_ARGB4444_PreMul(uint32_t* dst, const uint16_t* src, size_t count);`

- [x] **Step 2: Implement `NeonBlit_Span_RGB565` in `NeonBlit.cpp`**
  Implement 64-byte streaming copy using `vld1q_u32` / `vst1q_u32` for full-screen background blits directly into the 16-bit framebuffer.

- [x] **Step 3: Implement `NeonBlit_Span_ARGB4444_PreMul` in `NeonBlit.cpp`**
  Implement vectorized 8-pixel ARGB4444 unpacking and pre-multiplied blending using `vld1_u16`, `vshrn_n_u16`, and vector arithmetic.

- [x] **Step 4: Verify build compiles cleanly**
  Run Docker ARMv7 build to verify NEON intrinsics compile cleanly.

- [x] **Step 5: Commit Task 4**
  `git commit -m "feat(neon): implement ARM NEON RGB565 stream copy and ARGB4444 pre-multiplied span kernels"`

---

### Task 5: Offline 16-Bit Quantization in Asset Converter

**Files:**
- Modify: `src/setup/setup_converter.c:115-230`

- [x] **Step 1: Implement 4×4 Bayer ordered dithering for RGB565**
  In `src/setup/setup_converter.c`, add Bayer 4×4 dithering table and convert opaque background images (`images/background*`, `images/Almanac_Paper`) to 16-bit RGB565.

- [x] **Step 2: Implement pre-multiplied ARGB4444 quantization**
  Quantize translucent sprites, reanimations, projectiles, and UI elements to 16-bit ARGB4444 with pre-multiplied alpha.

- [x] **Step 3: Exempt bitmap fonts**
  Ensure all files matching `data/fonts/*` retain uncompressed 32-bit ARGB8888 encoding.

- [x] **Step 4: Compile `setup_converter` and verify conversion**
  Compile `setup_converter` and run test conversion on assets to verify output integrity.

- [x] **Step 5: Commit Task 5**
  `git commit -m "feat(converter): add offline Bayer RGB565 dithering and pre-multiplied ARGB4444 quantization"`

---

### Task 6: Build Verification, Memory Profiling & Audit

**Files:**
- Modify: `scripts/package_release.py:38-66`
- Target: `bin/PvZ`, `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`, `ports/Games/Plants vs. Zombies/PvZ`

- [x] **Step 1: Execute full Docker ARMv7 compilation**
  Run `docker run --rm -v "c:\DEV\PVZ:/workspace" -w /workspace miyoo-build:latest cmake --build build --target PvZ -j$(nproc)` and strip output binary.

- [x] **Step 2: Stage stripped binary across packaging directories**
  Stage the compiled binary to `bin/PvZ`, `packaging/Roms/PORTS/Games/Plants vs. Zombies/PvZ`, and `ports/Games/Plants vs. Zombies/PvZ`.

- [x] **Step 3: Update `package_release.py` memory budget table**
  Adjust the texture memory budget to 12.0 MB, dropping steady-state RSS to 62.0 MB and verifying >22.0 MB safety headroom.

- [x] **Step 4: Run memory profiling audit**
  Execute `python scripts/package_release.py --audit-only` and confirm passing status.

- [x] **Step 5: Commit Task 6**
  `git commit -m "chore(release): verify 16-bit dual-texture build, update memory budget audit, and stage release"`
