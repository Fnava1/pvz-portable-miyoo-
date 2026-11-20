# Goldfish Annotated Review Report

**Target:** `docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md` (`full file`)  
**Status:** resolved  
**Date:** 2026-11-19  

---

## Prioritized Findings & AI Suggestions

### 1. Pre-Multiplied Alpha vs Straight Alpha Pipeline Mismatch [`egm-inventive-catalyst` / `egm-design`] - Severity: Blocker
*   **Affected Scope:** [Lines 164–170, 177–189, 340–345](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L164-L189) (Sections 4.2, 5.A, Target File 7)
*   **Discrepancy Description:** `setup_converter.c` quantizes sprites into pre-multiplied ARGB4444, and `SWTri_GetTexel.inc` unpacks them into registers. However, in PopCap’s `SWTri.cpp`, standard span rasterization loops compute $\text{Dst} = (\text{Src} \cdot \text{Src}_A + \text{Dst} \cdot (255 - \text{Src}_A)) / 255$. Because the source color was already multiplied by alpha offline, multiplying it by alpha a second time darkens translucent sprites, turns drop shadows opaque black, and corrupts additive blend modes.
*   **Rationale:** Double alpha multiplication destroys visual fidelity and breaks PopCap vertex color tinting (damage flash, frozen tints).
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Storing ARGB4444 as straight-alpha in offline PAK files ensures all existing `SWTri.cpp` span loops, color modulations, and tinting routines work unmodified without mathematical distortion. Pre-multiplied compositing should be restricted exclusively to dedicated, self-contained NEON blit paths.
*   ***Proposed Edit:***
    *   Location: Section 4.2 (lines 164–170), Section 5.A (lines 177–189), and Target File 7 (lines 340–345).
    *   Change: Mandate that offline PAK assets store straight-alpha ARGB4444 ($R_4 = (R_8 \times 15 + 128) / 255$, $A_4 = (A_8 \times 15 + 128) / 255$). Unpack straight ARGB4444 in `SWTri_GetTexel.inc`. Restrict pre-multiplied accumulation exclusively to dedicated NEON span loops (`NeonBlit_Span_ARGB4444_PreMul`).

---

### 2. Backbuffer Format Incoherence & Direct 16-Bit Copy Contradiction [`egm-comprehension` / `egm-design`] - Severity: Blocker
*   **Affected Scope:** [Lines 80–96, 227–234, 349–351](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L80-L96) (Sections 1.A, 6, Target File 8)
*   **Discrepancy Description:** The spec states that opaque RGB565 backgrounds use `NeonBlit Stream Copy (Direct 16-bit, 0.32 ms) -> FB0`, yet retains 32-bit ARGB8888 bitmap fonts and presents via SDL2 / `libneonarmmiyoo.so`. In SexyAppFramework, the entire frame is composited into `mScreenImage` before presentation. If `mScreenImage` is 32-bit ARGB8888, RGB565 backgrounds cannot be streamed directly without stride mismatch and color corruption. If `mScreenImage` is 16-bit RGB565, 32-bit fonts cannot blend without an unspecified downsampling pass.
*   **Rationale:** Omitting the concrete pixel format of `mScreenImage` introduces an impossible compositing contradiction between background blits and font rendering.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Keeping `mScreenImage` in 32-bit ARGB8888 allows zero-overhead blending for crisp ARGB8888 fonts and UI widgets. Backgrounds are unpacked from RGB565 to ARGB8888 using a dedicated vector kernel in only ~0.35 ms on Cortex-A7.
*   ***Proposed Edit:***
    *   Location: Section 1 (lines 80–96), Section 6 (lines 227–234), and Target File 8 (lines 349–351).
    *   Change: Explicitly define `mScreenImage` as 32-bit ARGB8888 (`SoftwareSurface` with `FMT_ARGB8888`). Replace the "direct 16-bit streaming copy" with `NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count)`.

---

### 3. Software Rasterizer Hardcoded 4 bpp Bailout & Unchecked Casts [`egm-zoom-out` / `egm-implementation-readiness`] - Severity: Blocker
*   **Affected Scope:** [Lines 90–96, 317–324](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L90-L96) (Section 1.A, Target File 4, `SoftwareRenderer.cpp:560–568, 673`)
*   **Discrepancy Description:** `SoftwareRenderer.cpp:673` in `RasterizeTriangleSpan` contains a rigid guard:
    `if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 || mPhysClipRect.IsEmpty()) return;`
    Any surface where `mBytesPerPixel == 2` (such as ARGB4444 reanimations) is immediately rejected, rendering plants and zombies completely invisible. Furthermore, `SoftwareRenderer::DrawImage` (lines 560–568) blindly casts `image->mPixels` to `const uint32_t*` and executes 32-bit blits, causing out-of-bounds memory overreads on 16-bit buffers.
*   **Rationale:** The game will fail to render any 16-bit textured quad or triangle without modifying line 673 and providing typed pointer dispatch in `DrawImage`.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Fixing line 673 and branching on `mFormat` in `DrawImage` completes the core 16-bit rendering pipeline.
*   ***Proposed Edit:***
    *   Location: Section 1.A (lines 90–96) and Target File 4 (lines 317–324).
    *   Change: Instruct updating `SoftwareRenderer.cpp:673` to allow `(texture->mBytesPerPixel == 2 || texture->mBytesPerPixel == 4)`. In `DrawImage` fast-path and scaling loops, branch on `image->mFormat`: routing `FMT_ARGB4444` to `NeonBlit_Span_ARGB4444_PreMul`, `FMT_RGB565` to `NeonBlit_Span_RGB565_to_ARGB8888`, and `FMT_ARGB8888` to `NeonBlit_Span`.

---

### 4. Dynamic dlsym OpenMPT Mono Binding vs Static Linkage [`egm-implementation-readiness` / `egm-codebase-details`] - Severity: Blocker
*   **Affected Scope:** [Lines 126–136, 303–310](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L126-L136) (Section 2.B, Target File 2)
*   **Discrepancy Description:** Target File 2 directs the implementer to link `openmpt_module_read_mono` from `libopenmpt.a`. However, `OpenMptMusicInterface.cpp` dynamically loads shared libraries (`libopenmpt.so.0`, `libopenmpt.so`) at runtime via `dlopen`/`LoadLibraryA` and resolves symbols via `dlsym`. Static linkage breaks portable packaging and leaves `mFnReadMono` as an uninitialized null pointer, causing an immediate `SIGSEGV` crash when tracker music plays.
*   **Rationale:** Dynamic loading architecture is already established in `OpenMptMusicInterface`; violating it breaks compilation and runtime stability.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Aligning with the existing dynamic loader pattern requires minimal code and guarantees crash-free symbol resolution.
*   ***Proposed Edit:***
    *   Location: Section 2.B (lines 126–136) and Target File 2 (lines 303–310).
    *   Change: In `OpenMptMusicInterface.hpp`, declare `typedef size_t (*openmpt_module_read_mono_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* mono);` and member `openmpt_module_read_mono_fn mFnReadMono = nullptr;`. In `TryLoadOpenMptSymbols()`, resolve via `mFnReadMono = reinterpret_cast<openmpt_module_read_mono_fn>(DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono"))`. In `RenderAudioChunk()`, call `mFnReadMono(song.openmptMod, static_cast<int32_t>(kSampleRate), frameCount, monoOutput)`.

---

### 5. Memory Arena Triplication (`DmaTexturePool` vs `SpriteCachePool` vs Glibc Heap) [`egm-zoom-out` / `egm-comprehension`] - Severity: Blocker
*   **Affected Scope:** [Line 58, Lines 78–111, 223–240](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L58) (Open Question 1, Section 1, Data Flow Diagram)
*   **Discrepancy Description:** The codebase already allocates a static 16 MB `DmaTexturePool` (`SoftwareRenderer.hpp:217`), and Open Question 1 inquires whether `SpriteCachePool` should continue operating as an independent 12 MB ARGB4444 arena. Together, these two static pools lock up **28 MB of physical RAM** (34% of available userland memory). If dynamic 16-bit reanimations also allocate on the glibc heap, memory is triplicated, shrinking combat safety headroom from >32 MB to <10 MB and triggering kernel OOM termination.
*   **Rationale:** Static pool reservation without consolidation defeats the entire purpose of 16-bit texture memory reduction.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Consolidating into a single downsized DMA pool reclaims 18–20 MB of static memory and enforces a deterministic memory ceiling.
*   ***Proposed Edit:***
    *   Location: Open Question 1 (line 58), Section 1 (lines 78–111), and Data Flow Diagram (lines 223–240).
    *   Change: Close Open Question 1 decisively: Eliminate `SpriteCachePool` completely. Downsize `DmaTexturePool` from 16 MB to 8–10 MB. Route all dynamic 16-bit reanimation atlases through `DmaTexturePool::Allocate()`, utilizing `Reset()` at level transitions.

---

### 6. Redundant Specification of Pre-existing Code in Implementation Plan [`egm-codebase-details` / `egm-implementation-readiness`] - Severity: Major
*   **Affected Scope:** [Lines 325–351](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L325-L351) (Target Files 5, 6, 7, 8)
*   **Discrepancy Description:** The implementation plan instructs the implementer to write data structures that already exist in the repository (`MemoryImage.h:70` has `mPixelFormat`; `ReanimAtlas.cpp:53-62` already allocates 16-bit buffers; `SWTri_GetTexel.inc:38-50` already has ARGB4444 branches; `NeonBlit.cpp:468-545` already implements the span kernels). However, critical integration code is broken: `SoftwareSurface::GetPixel` (lines 164–171) hardcodes all 2-bpp surfaces as RGB565, corrupting ARGB4444 pixel reads.
*   **Rationale:** Misleads developers into re-implementing existing code while leaving broken surface accessors unaddressed.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Acknowledging existing merged code focuses developer effort on fixing broken surface accessors.
*   ***Proposed Edit:***
    *   Location: Target Files 4, 5, 6, 7, 8 (lines 317–351).
    *   Change: Retitle Sections 5, 6, 7, 8 as "Verification of Existing Implementation". Add explicit instructions to fix `SoftwareSurface::GetPixel` and `SetPixel` in `SoftwareRenderer.cpp:164–186` to branch on `mFormat == PixelFormat::FMT_ARGB4444`.

---

### 7. Contradiction on Audio Conversion Pipeline (Open Question 3 vs NFR) [`egm-structural` / `egm-comprehension`] - Severity: Major
*   **Affected Scope:** [Line 60, Lines 215, 288, 296–302](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L60) (Question 3, Section 6, NFR, Target File 1)
*   **Discrepancy Description:** Open Question 3 leaves unresolved whether sound effects should be batch-downsampled offline or downmixed at runtime via SDL_mixer. Relying on runtime downmixing wastes 10 MB on the microSD card and forces `Mix_LoadWAV` to allocate temporary resample buffers on the glibc heap right before combat starts, creating transient memory spikes that threaten the OOM limit.
*   **Rationale:** Runtime conversion violates Non-Functional Requirements (line 288) forbidding runtime allocation spikes.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Pre-converting audio in the PAK build pipeline guarantees zero runtime conversion latency and minimal RAM usage.
*   ***Proposed Edit:***
    *   Location: Open Question 3 (line 60), Section 2.A (lines 115–125), and Target File 1 (lines 296–302).
    *   Change: Close Open Question 3 decisively in favor of offline batch transcoding. Mandate that all sound effects in the PAK asset build pipeline (`setup_converter.c` / FFmpeg) are pre-converted to 22.05 kHz Mono 16-bit PCM WAV.

---

### 8. Toolchain Incompatibility with `malloc_trim(0)` on Non-Glibc Toolchains [`egm-design` / `egm-structural`] - Severity: Major
*   **Affected Scope:** [Lines 140–149, 311–316](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L140-L149) (Section 3, Target File 3)
*   **Discrepancy Description:** Guarding heap trimming with `#if defined(__linux__) && !defined(__ANDROID__)` fails on embedded Linux toolchains based on `musl libc`, where `<malloc.h>` does not declare `malloc_trim()`, causing immediate compilation failure.
*   **Rationale:** Breaks portability on embedded Linux toolchains that do not use GNU glibc.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Adding `defined(__GLIBC__)` makes the heap trimming call 100% portable across all toolchains.
*   ***Proposed Edit:***
    *   Location: Section 3 (lines 140–149) and Target File 3 (lines 311–316).
    *   Change: Restrict the preprocessor guard to `#if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)`.

---

### 9. Incomplete Bayer Dithering & Alpha Channel Truncation in Quantizer [`egm-inventive-catalyst` / `egm-style`] - Severity: Major
*   **Affected Scope:** [Lines 160–170, 353–360](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L160-L170) (Section 4, Target File 9)
*   **Discrepancy Description:** Section 4.1 defined matrix $M$ but omitted the algorithmic scaling and clamping formula for RGB565. Section 4.2 truncated alpha via `A8 >> 4` without dithering, causing visible stairstepping on drop shadows and particle fades. The pre-multiplication formula divisor was 240 instead of 255.
*   **Rationale:** Quantization formulas must be mathematically complete and avoid visual contour banding.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Applying ordered dithering to alpha and clamping values produces smooth transitions without CPU cost.
*   ***Proposed Edit:***
    *   Location: Section 4.1 & 4.2 (lines 160–170) and Target File 9 (lines 353–360).
    *   Change: Provide explicit formulas for RGB and alpha dithering: $A_4 = \text{clamp}(A_8 + (M[y\%4][x\%4] - 8), 0, 255) \gg 4$. Correct the channel scaling divisor to standard fixed-point normalization ($/255$).

---

### 10. Invalid File Path for AlmanacDialog and Unit Test Regressions [`egm-implementation-readiness`] - Severity: Major
*   **Affected Scope:** [Lines 311–315, 361–375](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L311-L315) (Target File 3, Verification Plan)
*   **Discrepancy Description:** 
    1. Line 311 specifies `src/Lawn/AlmanacDialog.cpp`. This file does not exist; the actual path is `src/Lawn/Widget/AlmanacDialog.cpp`.
    2. Changing `OpenMptMusicInterface::kNumChannels` from 2 to 1 will cause assertion failures in `tests/test_openmpt_playback.cpp` (lines 208, 271 assume `kChunk * 2` stereo frames).
    3. Verification Plan lacks automated execution via `ctest`.
*   **Rationale:** Prevents build breaks and test suite regressions.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Justification:** Correct file paths and test assertions are mandatory for automated CI builds.
*   ***Proposed Edit:***
    *   Location: Target File 3 (lines 311–315) and Verification Plan (lines 361–375).
    *   Change: Update path to `src/Lawn/Widget/AlmanacDialog.cpp`. Add instructions to update `tests/test_openmpt_playback.cpp` assertions for mono (`kChunk * 1`) and mandate `ctest --output-on-failure`.

---

### 11. Audio Downmix Gain & Headphone Dual-Mono Clarification [`egm-design` / `egm-style`] - Severity: Nit
*   **Affected Scope:** [Lines 49–50, 115–125, 257–266](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L49-L50) (Non-Goals, Section 2.A, Alternative C)
*   **Discrepancy Description:** Handphone users could experience one-ear audio if the driver doesn't duplicate channels, and loud explosions could clip without gain attenuation.
*   **Rationale:** Clarifies physical DAC behavior and prevents digital clipping.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Proposed Edit:*** Clarify that 1-channel mono in SDL_mixer instructs ALSA to duplicate to both headphone DAC channels, and specify $-3\text{ dB}$ attenuation ($(L+R) \times 0.707$) during downmixing.

---

### 12. Buffer Sizing Padding Clarification & Direct Pointer Safety [`egm-style` / `egm-comprehension`] - Severity: Nit
*   **Affected Scope:** [Line 99, Line 107, Line 336](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L99) (Section 1.B, Target File 6)
*   **Discrepancy Description:** Unexplained `+ 2` padding byte allocation and direct `mBits` pointer casts.
*   **Rationale:** Clarifies SIMD over-read protection.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Proposed Edit:*** Document trailing `+ 2` bytes as ARM NEON vector over-read margin (protecting 8-pixel `vld1_u16` loads) and provide typed accessor `uint16_t* MemoryImage::GetBits16()`.

---

### 13. Glibc Mmap Threshold Configuration [`egm-zoom-out`] - Severity: Nit
*   **Affected Scope:** [Lines 137–156, 311–316](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L137-L156) (Section 3, Target File 3)
*   **Discrepancy Description:** Dynamic glibc heuristics can grow `M_MMAP_THRESHOLD` over time, causing intermediate textures to linger in the heap.
*   **Rationale:** Ensures large texture buffers unmap immediately upon deletion.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Proposed Edit:*** Add `mallopt(M_MMAP_THRESHOLD, 64 * 1024)` to `LawnApp.cpp` startup.

---

### 14. RAM Savings Attribution Clarification [`egm-zoom-out`] - Severity: Nit
*   **Affected Scope:** [Line 16, Line 38](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md#L16) (Overview, Goals)
*   **Discrepancy Description:** Attributing ~16 MB savings to RGB565 backgrounds when only one 614 KB background is active at a time.
*   **Rationale:** Precision in documentation.
*   ***AI Suggestion:*** `PROPOSE FIX`
*   ***Proposed Edit:*** Clarify that RGB565 is used for color gradient fidelity, while the ~16 MB savings stem from halving hundreds of dynamic reanimation atlases and sprites.
