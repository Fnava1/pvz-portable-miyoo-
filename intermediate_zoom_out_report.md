# Intermediate Zoom Out Review Report: 16-Bit Dual-Texture & Audio Architecture for Handheld Memory Stabilization

**Target:** `c:\DEV\PVZ\docs\designs\estabilizacion_ram_audio_mono_texturas_16bit.md` (`Lines 1–307`)  
**Stage:** 8 (`INTERMEDIATE_ZOOM_OUT`)  
**Deliverable:** `c:\DEV\PVZ\intermediate_zoom_out_report.md`

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-zoom-out` | completed |

---

## 1. Macro Architecture Assessment: Simplification vs. Complication

Evaluating the proposed architecture from the macro codebase perspective reveals a critical duality: while the operational concept is well-targeted at the Miyoo Mini Plus handheld's rigid ~82–85 MB Linux kernel OOM ceiling (reducing active combat RSS from ~78 MB to ~50–53 MB), the document introduces architectural fragmentation across memory arenas and rendering pipelines that complicates rather than simplifies the engine.

### Where the Architecture Simplifies the Codebase:
1. **Audio Rate Harmonization**: Unifying `SDLSoundManager` and `OpenMptMusicInterface` to a single 22.05 kHz Mono clock eliminates the existing dual-clock rate mismatch (44.1 kHz vs 22.05 kHz), eradicating real-time CPU software resampling and halving tracker mixing arithmetic on Core 0.
2. **State-Boundary Heap Reclamation**: Bounding `malloc_trim(0)` strictly to quiescent UI transitions (loading screen, store exit, almanac closure, level end) successfully cleans heap fragmentation without injecting allocator jitter or latency spikes into active combat frame loops.
3. **Offline Quantization Paradigm**: Performing color quantization (RGB565 and ARGB4444) offline during the asset PAK build eliminates runtime decoding transformations and avoids dynamic memory allocations during asset streaming.

### Where the Architecture Complicates or Fractures the Codebase:
1. **Memory Arena Triplication**: The architecture leaves unresolved whether textures reside in `DmaTexturePool` (16 MB static reservation), `SpriteCachePool` (speculative 12 MB static reservation), or the glibc dynamic heap (`new MemoryImage()`). Reserving two static arenas locks up **28 MB of physical RAM** (34% of available userland memory) regardless of actual asset load, defeating the intended >32 MB OOM safety margin.
2. **Incoherent Rendering Compositing Model**: The document claims background blitting is a "Direct 16-bit streaming copy -> FB0", yet retains 32-bit ARGB8888 bitmap fonts and presents via SDL2 / `libneonarmmiyoo.so`. If the composite backbuffer (`mScreenImage`) remains 32-bit ARGB8888, RGB565 backgrounds cannot be streamed directly without pixel expansion. If `mScreenImage` is 16-bit RGB565, 32-bit fonts cannot blend without a downsampling pass that is completely absent from the spec.
3. **Color Space Contradiction (Double Alpha Multiplication)**: Quantizing assets to pre-multiplied ARGB4444 offline while passing them to PopCap's standard `SWTri` rasterization loops (which treat inputs as straight alpha) evaluates $\text{Dst} = (\text{Src} \cdot \text{Src}_A + \text{Dst} \cdot (255 - \text{Src}_A)) / 255$, multiplying alpha a second time and causing severe black halos and darkened sprites.
4. **Codebase Reality Gap**: Key technical proposals (defining `PixelFormat`, modifying `SoftwareSurface`, `NeonBlit_Span_*`, `SWTri_GetTexel.inc` branches, and `ReanimAtlasMakeBlankMemoryImage`) are **already implemented** in the repository. Meanwhile, critical integration blockers in the current codebase—such as `SoftwareRenderer.cpp:673` dropping all non-32bpp surfaces and `SoftwareRenderer::DrawImage` casting 16-bit pixels to `uint32_t*`—are entirely unaddressed.

---

## 2. Prioritized Findings (Blockers -> Major -> Nits)

### 1. Memory Arena Triplication & Over-Allocation (`DmaTexturePool` vs `SpriteCachePool` vs Glibc Heap) - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Line 58 (Open Question 1), Lines 78–111 (Section 1), Lines 223–240 (Data Flow Diagram)
* **Discrepancy Description**: The codebase already allocates a static 16 MB `DmaTexturePool` (`SoftwareRenderer.hpp:217`), and Open Question 1 inquires whether `SpriteCachePool` should continue operating as an independent 12 MB ARGB4444 arena. Concurrently, Section 1.B (`ReanimAtlasMakeBlankMemoryImage`) allocates dynamic 16-bit surfaces from the glibc heap using `new MemoryImage()`. Maintaining two static pools locks **28 MB of physical RAM** in pinned buffers. If dynamic reanimations also allocate on the heap, the engine incurs double memory consumption, shrinking the combat safety headroom from >32 MB to <10 MB and triggering kernel OOM termination.
* **Rationale & Recommendation**: 
  - *Macro Simplification Principle*: Consolidate all texture buffers into a single memory management paradigm.
  - Close Question 1 decisively: **Eliminate `SpriteCachePool` entirely**. 
  - Downsize `DmaTexturePool` from 16 MB to **8–10 MB** (halved to match the 16-bit texture footprint).
  - Route all dynamic 16-bit reanimation atlases and temporary surfaces directly through `DmaTexturePool::Allocate()`, utilizing `Reset()` at level transitions. This guarantees a deterministic memory ceiling and reclaims 18–20 MB of physical RAM.

---

### 2. Backbuffer Format Incoherence & Direct 16-Bit Streaming Copy Contradiction - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 83–96, Lines 227–234, Lines 349–351
* **Discrepancy Description**: Section 1 and Section 6 state that opaque RGB565 backgrounds use `NeonBlit Stream Copy (Direct 16-bit, 0.32 ms) -> FB0`. However, the document never defines the pixel format of the primary application backbuffer (`mScreenImage` in `SoftwareRenderer`). In PopCap's engine, the entire frame is composited into `mScreenImage` before presentation:
  - If `mScreenImage` is 32-bit ARGB8888 (required for the preserved 32-bit bitmap fonts in lines 54 and 207), an RGB565 background cannot be direct-copied via 16-bit stream without stride mismatch and severe color corruption; it must be unpacked from RGB565 to ARGB8888.
  - If `mScreenImage` is converted to 16-bit RGB565, the document fails to explain how 32-bit ARGB8888 fonts blend into a 16-bit destination, or how `libneonarmmiyoo.so` handles presentation.
* **Rationale & Recommendation**: 
  - Define `mScreenImage` explicitly as **32-bit ARGB8888** (`SoftwareSurface` with `FMT_ARGB8888`).
  - Replace the "direct 16-bit streaming copy" with a vectorized unpacking span kernel: `NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count)`. This takes only ~0.35 ms on Cortex-A7 and maintains uniform 32-bit destination compositing across sprites, UI widgets, and crisp ARGB8888 bitmap fonts.

---

### 3. Double Alpha Pre-Multiplication Pipeline Mismatch in `SWTri` Rasterizer - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 164–170 (Section 4.2), Lines 177–189 (Section 5.A), Lines 340–345 (Target File 7)
* **Discrepancy Description**: Section 4.2 specifies quantizing sprites into pre-multiplied ARGB4444 ($R_4 = \frac{R_8 \cdot A_4}{240}$), and Section 5.A modifies `SWTri_GetTexel.inc` to unpack texels. However, in PopCap’s `SWTri.cpp`, rasterization span loops (`SWTri_DrawTriangle`, `SWTri_DrawQuad`, modulation loops) evaluate standard straight-alpha compositing:
  $$\text{Dst} = \frac{\text{Src} \cdot \text{Src}_A + \text{Dst} \cdot (255 - \text{Src}_A)}{255}$$
  Because the source color channels were already multiplied by alpha offline, multiplying them by alpha a second time in `SWTri.cpp` results in **double alpha pre-multiplication**: translucent sprite edges darken, drop shadows turn opaque black, and additive blend modes fail. Furthermore, the document fails to specify how vertex color tinting (e.g. zombie damage flash, ice slowdown) interacts with pre-multiplied colors.
* **Rationale & Recommendation**: 
  - *Macro Pipeline Coherence*: Standardize asset storage on **straight-alpha ARGB4444** in offline PAK files. Unpack straight ARGB4444 texels directly in `SWTri_GetTexel.inc` so all existing `SWTri.cpp` span loops, color modulations, and tinting routines work unmodified without mathematical distortion.
  - Limit pre-multiplied blending exclusively to dedicated, self-contained NEON blit paths (`NeonBlit_Span_ARGB4444_PreMul`) where both unpacking and destination blending occur inside a single vectorized loop.

---

### 4. Software Rasterizer Hardcoded 4 bpp Bailout & Unchecked Casts in `SoftwareRenderer` - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 90–96 (Section 1.A), Lines 317–324 (Target File 4), `src/graphics/SoftwareRenderer.cpp:560–568, 673`
* **Discrepancy Description**: Section 1.A proposes relaxing assertions in `SoftwareRenderer.cpp:521, 605, 639`. However, an audit of `SoftwareRenderer.cpp` reveals that line 673 in `RasterizeTriangleSpan` contains a hardcoded guard:
  ```cpp
  if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 || mPhysClipRect.IsEmpty()) return;
  ```
  Any texture surface with `mBytesPerPixel == 2` (such as ARGB4444 reanimations) is immediately rejected and dropped, rendering plants and zombies completely invisible! In addition, `SoftwareRenderer::DrawImage` (lines 560–568) unconditionally casts `image->mPixels` to `const uint32_t*` and executes `NeonBlit_Span`, reading 16-bit buffers as 32-bit data and causing severe out-of-bounds memory overreads.
* **Rationale & Recommendation**: Update Target File 4 in the implementation plan to:
  1. Modify line 673 to allow `texture->mBytesPerPixel == 2 || texture->mBytesPerPixel == 4`.
  2. Update `SoftwareRenderer::DrawImage` to branch on `image->mFormat`: routing `FMT_ARGB4444` to `NeonBlit_Span_ARGB4444_PreMul`, `FMT_RGB565` to `NeonBlit_Span_RGB565_to_ARGB8888`, and `FMT_ARGB8888` to `NeonBlit_Span`.

---

### 5. Linkage Conflict in `OpenMptMusicInterface` (Dynamic `dlopen` vs Static `.a`) - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 126–136 (Section 2.B), Lines 303–310 (Target File 2), `src/audio/OpenMptMusicInterface.cpp:120–165`
* **Discrepancy Description**: Target File 2 states: *"Bind `openmpt_module_read_mono` from `libopenmpt.a`"*. However, `OpenMptMusicInterface.cpp` does not link statically against `libopenmpt.a`; it dynamically loads `libopenmpt.so.0` / `libopenmpt.dll` at runtime via `dlopen`/`LoadLibraryA` and resolves symbols via `dlsym`/`GetProcAddress`. Attempting to link statically while retaining dynamic resolution introduces linker conflicts or leaves `mFnReadMono` as an uninitialized null pointer, crashing the game with `SIGSEGV` when tracker music starts.
* **Rationale & Recommendation**: Align with the existing dynamic loading pattern:
  1. In `OpenMptMusicInterface.hpp`, declare:
     ```cpp
     typedef size_t (*openmpt_module_read_mono_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* mono);
     openmpt_module_read_mono_fn mFnReadMono = nullptr;
     ```
  2. In `TryLoadOpenMptSymbols()`, resolve `mFnReadMono = reinterpret_cast<openmpt_module_read_mono_fn>(DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono"))`.
  3. Ensure `RenderFallbackAudio()` also emits 1-channel mono samples.

---

### 6. Spec Audit Inaccuracy & Corrupted 16-Bit Surface Accessors (`GetPixel` / `SetPixel`) - Severity: 🟡 Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 80–111 (Section 1), Lines 317–351 (Target Files 4, 5, 6, 7, 8), `src/graphics/SoftwareRenderer.cpp:159–187`
* **Discrepancy Description**: The design document treats `PixelFormat`, `SoftwareSurface` 2-bpp fields, `ReanimAtlasMakeBlankMemoryImage`, `SWTri_GetTexel.inc` branches, and `NeonBlit_Span_*` as work to be created. In reality, all five components already exist in the codebase. However, existing code contains critical defects: `SoftwareSurface::GetPixel` (lines 164–171) and `SetPixel` (lines 180–186) hardcode all `mBytesPerPixel == 2` buffers as RGB565. If an ARGB4444 surface is accessed via `GetPixel` (e.g. for collision detection or color sampling), the channels are completely scrambled and alpha is forced to `0xFF000000`.
* **Rationale & Recommendation**: Update the document to acknowledge existing code and redirect engineering effort to fixing `SoftwareSurface::GetPixel` and `SetPixel` to branch on `mFormat`:
  ```cpp
  if (mFormat == PixelFormat::FMT_ARGB4444) {
      uint16_t c = row[x];
      uint32_t a = ((c >> 12) & 0x0F) * 17;
      uint32_t r = ((c >> 8)  & 0x0F) * 17;
      uint32_t g = ((c >> 4)  & 0x0F) * 17;
      uint32_t b = (c         & 0x0F) * 17;
      return (a << 24) | (r << 16) | (g << 8) | b;
  }
  ```

---

### 7. Contradiction on Audio Conversion Pipeline (Open Question 3 vs NFR) - Severity: 🟡 Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Line 60 (Question 3), Line 215 (Section 6), Line 288 (NFR), Lines 296–302 (Target File 1)
* **Discrepancy Description**: Open Question 3 leaves unresolved whether sound effects should be batch-downsampled offline or downmixed at runtime via SDL_mixer. However, Section "Non-Functional Requirements" (line 288) explicitly states: *"All asset quantization must be completed offline during PAK build to eliminate SD card read-write spikes during gameplay."* If audio remains 44.1 kHz stereo in `main.pak`, loading consumes ~10 MB of extra microSD space and forces `Mix_LoadWAV` to allocate temporary resample buffers on the glibc heap right before combat starts, creating transient memory spikes that threaten the OOM limit.
* **Rationale & Recommendation**: Close Open Question 3 decisively in favor of **offline batch transcoding**. Mandate that all sound effects in the PAK asset build pipeline are pre-converted to 22.05 kHz Mono 16-bit PCM WAV. This ensures `Mix_LoadWAV` reads half the bytes directly into memory with zero runtime resampling allocations.

---

### 8. Alpha Channel Quantization Truncation & Dithering Omission - Severity: 🟡 Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 160–170 (Section 4.1 & 4.2), Lines 353–360 (Target File 9)
* **Discrepancy Description**: While Section 4.1 applies a 4×4 Bayer matrix to dither RGB565 color gradients, Section 4.2 truncates the alpha channel to 4 bits using a naive right-shift ($A_4 = A_8 \gg 4$). Truncating 8-bit alpha to 16 discrete levels strips subtle transparency gradients, producing visible stairstepping, harsh contour edges, and dirty fringing on plant drop shadows, smoke puffs, and fading projectiles.
* **Rationale & Recommendation**: Extend 4×4 Bayer ordered dithering to the alpha channel during offline quantization in `setup_converter.c`:
  $$A_4 = \text{clamp}\left(A_8 + (M_{4\times 4}[y \bmod 4][x \bmod 4] - 8),\, 0,\, 255\right) \gg 4$$
  This produces smooth perceived alpha transitions without runtime CPU overhead. In addition, correct the pre-multiplication formula divisor from 240 to standard fixed-point normalization ($/255$).

---

### 9. Toolchain Incompatibility: Unconditional `malloc_trim(0)` on Non-Glibc Toolchains - Severity: 🟡 Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 140–149 (Section 3), Lines 311–316 (Target File 3)
* **Discrepancy Description**: Section 3 guards heap trimming using `#if defined(__linux__) && !defined(__ANDROID__)`. On embedded Linux toolchains based on `musl libc` (frequently used in custom Miyoo/SigmaStar buildroots), `<malloc.h>` does not declare `malloc_trim()`, causing immediate compilation failure (`undefined reference to 'malloc_trim'`).
* **Rationale & Recommendation**: Restrict the guard specifically to GNU glibc:
  ```cpp
  #if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)
  #include <malloc.h>
  static inline void TrimHeapIfLinux() { malloc_trim(0); }
  #else
  static inline void TrimHeapIfLinux() {}
  #endif
  ```

---

### 10. Headphone Dual-Mono Acoustic Behavior & Gain Normalization - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 49–50 (Non-Goals), Lines 257–266 (Alternative C)
* **Discrepancy Description**: Alternative C dismisses headphone considerations by stating "Headphone usage is out of scope". However, the Miyoo Mini Plus features a 3.5mm stereo jack. If ALSA does not replicate mono audio across both DAC channels, audio plays exclusively in the left earphone. Furthermore, downmixing stereo tracker instruments to mono requires explicit $-3\text{ dB}$ gain normalization ($(L + R) \times 0.707$) to prevent digital clipping on loud explosions.
* **Rationale & Recommendation**: Clarify that opening SDL_mixer in 1-channel mono configures the SigmaStar ALSA driver to duplicate the mono channel across both headphone DAC channels, and specify $-3\text{ dB}$ attenuation during stereo-to-mono downmixing.

---

### 11. Ambiguous Buffer Sizing Rationale (`+ 2` vs `+ 7`) & Direct Pointer Safety - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Line 99, Line 107, Line 336
* **Discrepancy Description**: Section 1.B specifies sizing 16-bit surface allocations as `width * height * 2 + 2` bytes, whereas line 107 in `ReanimAtlasMakeBlankMemoryImage` allocates `std::make_unique<uint32_t[]>((w * h * sizeof(uint16_t) + 7) / sizeof(uint32_t))`. The document fails to explain why `+ 2` bytes are added (SIMD vector over-read protection or sentinel) or why `+ 7` uint32 rounding is used. Furthermore, accessing 16-bit data through `mBits` (`uint32_t*`) risks pointer aliasing bugs.
* **Rationale & Recommendation**: Document the trailing padding as ARM NEON vector over-read margin (protecting 8-pixel `vld1_u16` loads), standardize buffer dimensioning macros, and provide a typed accessor `uint16_t* MemoryImage::GetBits16()`.

---

### 12. Inaccurate RAM Savings Attribution for RGB565 Backgrounds - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Line 16 (Overview), Line 38 (Goals)
* **Discrepancy Description**: Line 16 claims that segregating textures into RGB565 backgrounds and ARGB4444 sprites produces "~16 MB net RAM reduction". In reality, only a single 640×480 background is active in RAM at any time ($640 \times 480 \times 2 = 614.4\text{ KB}$). The RAM footprint of a 614 KB background is identical in RGB565 and ARGB4444. The ~16 MB reduction is achieved almost entirely by halving hundreds of dynamic reanimation atlases and sprites from 32-bit to 16-bit.
* **Rationale & Recommendation**: Clarify that RGB565 is utilized for color gradient precision (5-6-5 bits vs 4-4-4 bits to avoid sky banding), not for memory differentiation.

---

## 3. Summary of Architectural Recommendations for Stage 9

1. **Unify Memory Arenas**: Eliminate `SpriteCachePool`, downsize `DmaTexturePool` to 8–10 MB, and route dynamic 16-bit reanimations through this single pool.
2. **Standardize Compositing**: Keep the backbuffer `mScreenImage` in 32-bit ARGB8888; implement `NeonBlit_Span_RGB565_to_ARGB8888` for background unpacking and keep fonts in 32-bit ARGB8888.
3. **Preserve Straight Alpha in Assets**: Store ARGB4444 as straight alpha to keep `SWTri.cpp` rasterization and color tinting mathematically sound; restrict pre-multiplied compositing to dedicated NEON span loops.
4. **Fix Existing Codebase Blockers**: Remove the hardcoded `mBytesPerPixel != 4` guard in `SoftwareRenderer.cpp:673`, fix `SoftwareRenderer::DrawImage` 16-bit pointer casts, and fix `SoftwareSurface::GetPixel` / `SetPixel` to support `FMT_ARGB4444`.
5. **Decouple OpenMPT via Dynamic Loading**: Resolve `openmpt_module_read_mono` via `dlsym` in `TryLoadOpenMptSymbols()` rather than static linkage.
6. **Mandate Offline Audio Downsampling**: Formally close Open Question 3 with offline 22.05 kHz Mono batch conversion during PAK generation.
