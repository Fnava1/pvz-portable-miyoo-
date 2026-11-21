# Consolidated Review Report: 16-Bit Dual-Texture & Audio Architecture for Handheld Memory Stabilization

**Target Document:** [estabilizacion_ram_audio_mono_texturas_16bit.md](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md)  
**Execution Mode:** Uninterrupted Full Flow (Stages 5, 6, 7, 8, 9)  
**Date:** 2026-11-19  
**Review Synthesis Author:** Intermediate Swarm Compiler (EGM Protocol)

---

## 1. Review Stages Executed & Reports Generated

| Stage | Name | Key Reviewer Personas | Generated Report Artifact | Verdict |
| :--- | :--- | :--- | :--- | :--- |
| **Stage 5** | Initial Zoom Out Review | `egm-zoom-out`, `egm-inventive-catalyst` | [zoom_out_report.md](file:///c:/DEV/PVZ/zoom_out_report.md) | Requires Revision |
| **Stage 6** | Comprehension Test | `egm-comprehension` | [comprehension_report.md](file:///c:/DEV/PVZ/comprehension_report.md) | Requires Revision |
| **Stage 7** | Critic Review | `egm-design`, `egm-structural`, `egm-style` | [critic_report.md](file:///c:/DEV/PVZ/critic_report.md) | Requires Revision |
| **Stage 8** | Intermediate Zoom Out Review | `egm-zoom-out` | [intermediate_zoom_out_report.md](file:///c:/DEV/PVZ/intermediate_zoom_out_report.md) | Requires Revision |
| **Stage 9** | Implementation Readiness Test | `egm-implementation-readiness`, `egm-codebase-details`, `egm-style` | [implementation_readiness_report.md](file:///c:/DEV/PVZ/implementation_readiness_report.md) | **REJECTED (Requires Revision)** |

---

## 2. Executive Synthesis & Architectural Verdict

The core operational objective—stabilizing Plants vs. Zombies within the Miyoo Mini Plus handheld's rigid ~82–85 MB Linux kernel OOM killer boundary by lowering steady-state combat RSS from ~78 MB down to ~50–53 MB (>30 MB safety headroom)—is **unanimously validated** across all review lenses.

However, the design document in its current form **CANNOT proceed directly to implementation**. A multi-lens code-level audit against the actual repository revealed critical technical blockers:
1. **Compositing Color Space Contradiction**: Pre-multiplying ARGB4444 assets offline while passing them to PopCap's existing `SWTri.cpp` rasterization loops will trigger double alpha pre-multiplication, corrupting translucency and producing black halos around sprites.
2. **Backbuffer Format Incoherence**: The document specifies a direct 16-bit streaming copy of RGB565 backgrounds while simultaneously retaining 32-bit ARGB8888 bitmap fonts, without declaring the composite format of `mScreenImage`.
3. **Software Renderer 4bpp Bailout**: `SoftwareRenderer.cpp:673` contains a hardcoded `texture->mBytesPerPixel != 4` guard that silently drops all 16-bit textured surfaces.
4. **Linkage Model Conflict**: `OpenMptMusicInterface` uses dynamic runtime symbol resolution (`dlopen`/`dlsym`), but the implementation plan erroneously prescribes static library linkage (`libopenmpt.a`).
5. **Memory Arena Over-Allocation**: The spec leaves `SpriteCachePool` (12 MB) and `DmaTexturePool` (16 MB) running in parallel with dynamic heap allocations, locking up 28 MB of static memory and undermining the OOM safety margin.

---

## 3. Prioritized Actionable Findings

### 🔴 Blocker 1: Double Alpha Pre-Multiplication Pipeline Mismatch
* **Sources:** Stage 5 (#1), Stage 7 (#2), Stage 8 (#3)
* **Affected Scope:** Lines 164–170 (Section 4.2), Lines 177–189 (Section 5.A), Lines 340–345 (Target File 7)
* **Problem:** `setup_converter.c` quantizes sprites into pre-multiplied ARGB4444, and `SWTri_GetTexel.inc` unpacks them into registers. However, in PopCap’s `SWTri.cpp`, standard span rasterization loops compute $\text{Dst} = (\text{Src} \cdot \text{Src}_A + \text{Dst} \cdot (255 - \text{Src}_A)) / 255$. Because the source color was already multiplied by alpha offline, multiplying it by alpha a second time darkens translucent sprites, turns drop shadows opaque black, and corrupts additive blend modes.
* **Consolidated Resolution:**
  1. Store ARGB4444 assets as **straight-alpha** in offline PAK files.
  2. Unpack straight ARGB4444 in `SWTri_GetTexel.inc` so all existing `SWTri.cpp` span loops, color modulations, and tinting routines work unmodified without mathematical distortion.
  3. Restrict pre-multiplied compositing exclusively to dedicated, self-contained NEON blit paths (`NeonBlit_Span_ARGB4444_PreMul`) where both unpacking and destination blending occur inside a single vectorized loop.

---

### 🔴 Blocker 2: Backbuffer Format Incoherence & Direct 16-Bit Copy Contradiction
* **Sources:** Stage 6 (#1), Stage 7 (#1), Stage 8 (#2)
* **Affected Scope:** Lines 80–96, Lines 227–234, Lines 349–351
* **Problem:** The spec states that opaque RGB565 backgrounds use `NeonBlit Stream Copy (Direct 16-bit, 0.32 ms) -> FB0`, yet retains 32-bit ARGB8888 bitmap fonts and presents via SDL2 / `libneonarmmiyoo.so`. In SexyAppFramework, the entire frame is composited into `mScreenImage` before presentation. If `mScreenImage` is 32-bit ARGB8888, RGB565 backgrounds cannot be streamed directly without stride mismatch and color corruption. If `mScreenImage` is 16-bit RGB565, 32-bit fonts cannot blend without an unspecified downsampling pass.
* **Consolidated Resolution:**
  1. Explicitly define `mScreenImage` as **32-bit ARGB8888** (`SoftwareSurface` with `FMT_ARGB8888`).
  2. Replace the "direct 16-bit streaming copy" with a vectorized unpacking span kernel: `NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count)`. This executes in ~0.35 ms on Cortex-A7 and maintains uniform 32-bit destination compositing across sprites, UI widgets, and crisp ARGB8888 bitmap fonts.

---

### 🔴 Blocker 3: Software Rasterizer Hardcoded 4 bpp Bailout & Unchecked Casts
* **Sources:** Stage 5 (#2), Stage 8 (#4), Stage 9 (#2)
* **Affected Scope:** Lines 90–96 (Section 1.A), Lines 317–324 (Target File 4), `src/graphics/SoftwareRenderer.cpp:560–568, 673`
* **Problem:** `SoftwareRenderer.cpp:673` in `RasterizeTriangleSpan` contains a rigid guard:
  `if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 || mPhysClipRect.IsEmpty()) return;`
  Any surface where `mBytesPerPixel == 2` (such as ARGB4444 reanimations) is immediately rejected, rendering plants and zombies completely invisible. Furthermore, `SoftwareRenderer::DrawImage` (lines 560–568) blindly casts `image->mPixels` to `const uint32_t*` and executes 32-bit blits, causing out-of-bounds memory overreads on 16-bit buffers.
* **Consolidated Resolution:**
  1. Update `SoftwareRenderer.cpp:673` to allow `(texture->mBytesPerPixel == 2 || texture->mBytesPerPixel == 4)`.
  2. Update `SoftwareRenderer::DrawImage` to branch on `image->mFormat`: routing `FMT_ARGB4444` to `NeonBlit_Span_ARGB4444_PreMul`, `FMT_RGB565` to `NeonBlit_Span_RGB565_to_ARGB8888`, and `FMT_ARGB8888` to `NeonBlit_Span`.

---

### 🔴 Blocker 4: Linkage Conflict in `OpenMptMusicInterface` (Dynamic `dlopen` vs Static `.a`)
* **Sources:** Stage 5 (#3), Stage 8 (#5), Stage 9 (#1)
* **Affected Scope:** Lines 126–136 (Section 2.B), Lines 303–310 (Target File 2), `src/audio/OpenMptMusicInterface.cpp:120–165`
* **Problem:** Target File 2 directs the implementer to link `openmpt_module_read_mono` from `libopenmpt.a`. However, `OpenMptMusicInterface.cpp` dynamically loads shared libraries (`libopenmpt.so.0`, `libopenmpt.so`) at runtime via `dlopen`/`LoadLibraryA` and resolves symbols via `dlsym`. Static linkage breaks portable packaging and leaves `mFnReadMono` as an uninitialized null pointer, causing an immediate `SIGSEGV` crash when tracker music plays.
* **Consolidated Resolution:**
  1. In `OpenMptMusicInterface.hpp`, declare:
     ```cpp
     typedef size_t (*openmpt_module_read_mono_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* mono);
     openmpt_module_read_mono_fn mFnReadMono = nullptr;
     ```
  2. In `TryLoadOpenMptSymbols()`, resolve `mFnReadMono = reinterpret_cast<openmpt_module_read_mono_fn>(DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono"))`.
  3. In `RenderAudioChunk()`, call `mFnReadMono(song.openmptMod, static_cast<int32_t>(kSampleRate), frameCount, monoOutput)`.
  4. Ensure `RenderFallbackAudio()` also outputs single-channel mono samples.

---

### 🔴 Blocker 5: Memory Arena Triplication (`DmaTexturePool` vs `SpriteCachePool` vs Glibc Heap)
* **Sources:** Stage 6 (#2), Stage 5 (#4), Stage 8 (#1)
* **Affected Scope:** Line 58 (Question 1), Lines 78–111 (Section 1), Lines 223–240 (Data Flow Diagram)
* **Problem:** The codebase already allocates a static 16 MB `DmaTexturePool` (`SoftwareRenderer.hpp:217`), and Open Question 1 inquires whether `SpriteCachePool` should continue operating as an independent 12 MB ARGB4444 arena. Together, these two static pools lock up **28 MB of physical RAM** (34% of available userland memory). If dynamic 16-bit reanimations also allocate on the glibc heap, memory is triplicated, shrinking combat safety headroom from >32 MB to <10 MB and triggering kernel OOM termination.
* **Consolidated Resolution:**
  1. Close Open Question 1 decisively: **Eliminate `SpriteCachePool` entirely**.
  2. Downsize `DmaTexturePool` from 16 MB to **8–10 MB** (halved to match the 16-bit texture footprint).
  3. Route all dynamic 16-bit reanimation atlases through `DmaTexturePool::Allocate()`, utilizing `Reset()` at level transitions to enforce a deterministic memory ceiling.

---

### 🟡 Major 1: Redundant Specification of Pre-existing Code in Implementation Plan
* **Sources:** Stage 5 (#5), Stage 8 (#6), Stage 9 (#3)
* **Affected Scope:** Lines 325–351 (Sections 5, 6, 7, 8)
* **Problem:** The implementation plan instructs the implementer to write data structures that already exist in the repository (`MemoryImage.h:70` has `mPixelFormat`; `ReanimAtlas.cpp:53-62` already allocates 16-bit buffers; `SWTri_GetTexel.inc:38-50` already has ARGB4444 branches; `NeonBlit.cpp:468-545` already implements the span kernels). However, critical integration code is broken: `SoftwareSurface::GetPixel` (lines 164–171) hardcodes all 2-bpp surfaces as RGB565, corrupting ARGB4444 pixel reads.
* **Consolidated Resolution:** Retitle Sections 5, 6, 7, 8 as *"Verification of Existing Implementation"* and refocus instructions strictly on fixing `SoftwareSurface::GetPixel` and `SetPixel` to branch on `mFormat`.

---

### 🟡 Major 2: Contradiction on Audio Conversion (Open Question 3 vs NFR)
* **Sources:** Stage 6 (#6), Stage 7 (#5), Stage 8 (#7)
* **Affected Scope:** Line 60 (Question 3), Line 215 (Section 6), Line 288 (NFR), Lines 296–302 (Target File 1)
* **Problem:** Open Question 3 leaves unresolved whether sound effects should be batch-downsampled offline or downmixed at runtime via SDL_mixer. Relying on runtime downmixing wastes 10 MB on the microSD card and forces `Mix_LoadWAV` to allocate temporary resample buffers on the glibc heap right before combat starts, creating transient memory spikes that threaten the OOM limit.
* **Consolidated Resolution:** Close Open Question 3 decisively in favor of **offline batch transcoding**. Mandate that all sound effects in the PAK asset build pipeline (`setup_converter.c` / FFmpeg) are pre-converted to 22.05 kHz Mono 16-bit PCM WAV.

---

### 🟡 Major 3: Toolchain Incompatibility with `malloc_trim(0)` on Non-Glibc Toolchains
* **Sources:** Stage 7 (#3), Stage 8 (#9)
* **Affected Scope:** Lines 140–149 (Section 3), Lines 311–316 (Target File 3)
* **Problem:** Guarding heap trimming with `#if defined(__linux__) && !defined(__ANDROID__)` fails on embedded Linux toolchains based on `musl libc`, where `<malloc.h>` does not declare `malloc_trim()`, causing immediate compilation failure.
* **Consolidated Resolution:** Guard strictly with GNU glibc:
  ```cpp
  #if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)
  #include <malloc.h>
  static inline void TrimHeapIfLinux() { malloc_trim(0); }
  #else
  static inline void TrimHeapIfLinux() {}
  #endif
  ```

---

### 🟡 Major 4: Incomplete Bayer Dithering & Alpha Channel Truncation in Quantizer
* **Sources:** Stage 6 (#4, #5), Stage 7 (#7), Stage 8 (#8)
* **Affected Scope:** Lines 160–170 (Section 4.1 & 4.2), Lines 353–360 (Target File 9)
* **Problem:** Section 4.1 defined matrix $M$ but omitted the algorithmic scaling and clamping formula for RGB565. Section 4.2 truncated alpha via `A8 >> 4` without dithering, causing visible stairstepping on drop shadows and particle fades. The pre-multiplication formula divisor was 240 instead of 255.
* **Consolidated Resolution:** Provide explicit Bayer dithering formulas for both RGB color channels and the alpha channel with color clamping to `[0, 255]`, and standard fixed-point normalization ($/255$).

---

### 🟡 Major 5: Invalid File Path for AlmanacDialog and Unit Test Regressions
* **Sources:** Stage 9 (#4, #5)
* **Affected Scope:** Lines 311–315 (Section 3) and Lines 361–375 (Verification Plan)
* **Problem:** 
  1. Line 311 specifies `src/Lawn/AlmanacDialog.cpp`. This file does not exist; the actual path is `src/Lawn/Widget/AlmanacDialog.cpp`.
  2. Changing `OpenMptMusicInterface::kNumChannels` from 2 to 1 will cause assertion failures in `tests/test_openmpt_playback.cpp` (lines 208, 271 assume `kChunk * 2` stereo frames).
  3. Verification Plan lacks automated execution via `ctest`.
* **Consolidated Resolution:** Correct the path to `src/Lawn/Widget/AlmanacDialog.cpp`, update `test_openmpt_playback.cpp` assertions for mono (`kChunk * 1`), and mandate `ctest --output-on-failure`.

---

### 🟢 Nits & Quality-of-Life Polish
1. **Audio Downmix Gain & Headphone Balancing:** Specify $-3\text{ dB}$ attenuation ($(L + R) \times 0.707$) during stereo-to-mono downmixing to prevent digital clipping, and clarify that SDL_mixer replicates mono across both left/right DAC channels for 3.5mm headphones.
2. **Buffer Sizing Rationale & Typed Accessors:** Document trailing `+ 2` bytes as ARM NEON vector over-read margin (protecting 8-pixel `vld1_u16` loads), standardize buffer dimensioning macros, and provide a typed accessor `uint16_t* MemoryImage::GetBits16()`.
3. **Glibc Mmap Threshold:** Add `mallopt(M_MMAP_THRESHOLD, 64 * 1024)` in `LawnApp.cpp` to ensure allocations $\ge 64\text{ KB}$ immediately unmap upon deletion.
4. **RAM Savings Attribution:** Clarify that RGB565 is used for color gradient fidelity (preventing sky banding), while the ~16 MB savings stem from halving hundreds of dynamic reanimation atlases and sprites.
5. **Parameter Nomenclature:** Rename parameter in `RenderAudioChunk(int16_t* monoOutput, size_t frameCount)`.

---

## 4. Traceability Matrix

| Consolidated Action Item | Raw Findings Mapped | Severity | Resolution Status |
| :--- | :--- | :--- | :--- |
| **Store Straight ARGB4444 in Assets** | Stage 5 (#1), Stage 7 (#2), Stage 8 (#3) | 🔴 Blocker | Pending doc remediation |
| **Declare 32-bit Backbuffer & Unpack Kernel** | Stage 6 (#1), Stage 7 (#1), Stage 8 (#2) | 🔴 Blocker | Pending doc remediation |
| **Unblock SoftwareRenderer 16-Bit Guards** | Stage 5 (#2), Stage 8 (#4), Stage 9 (#2) | 🔴 Blocker | Pending doc remediation |
| **Dynamic dlsym OpenMPT Mono Binding** | Stage 5 (#3), Stage 8 (#5), Stage 9 (#1) | 🔴 Blocker | Pending doc remediation |
| **Eliminate SpriteCachePool & Consolidate DMA** | Stage 6 (#2), Stage 5 (#4), Stage 8 (#1) | 🔴 Blocker | Pending doc remediation |
| **Refocus Plan on Existing Code & GetPixel Fix** | Stage 5 (#5), Stage 8 (#6), Stage 9 (#3) | 🟡 Major | Pending doc remediation |
| **Mandate Offline Audio Downsampling** | Stage 6 (#6), Stage 7 (#5), Stage 8 (#7) | 🟡 Major | Pending doc remediation |
| **Restrict malloc_trim to __GLIBC__** | Stage 7 (#3), Stage 8 (#9) | 🟡 Major | Pending doc remediation |
| **Specify Bayer Alpha Dithering** | Stage 6 (#4, #5), Stage 7 (#7), Stage 8 (#8) | 🟡 Major | Pending doc remediation |
| **Fix AlmanacDialog Path & Update Unit Tests** | Stage 9 (#4, #5) | 🟡 Major | Pending doc remediation |
| **Polish Nits (Headphones, Mmap, Padding)** | Stage 6 (#3, #7-9), Stage 7 (#4, #6, #8, #9), Stage 8 (#10-12), Stage 9 (#6, #7) | 🟢 Nit | Pending doc remediation |

---

## 5. Next Steps & Recommended Action

The 5-stage uninterrupted review is complete and the consolidated report is finalized. To make this design fully implementation-ready, proceed to **Automated Remediation (`/egm-resolve`)** to apply the 5 Blockers and 5 Majors directly to `docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md`.
