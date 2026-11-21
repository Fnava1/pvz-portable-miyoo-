# Initial Zoom Out Review Report: 16-Bit Dual-Texture & Audio Architecture for Handheld Memory Stabilization

**Target:** `c:\DEV\PVZ\docs\designs\estabilizacion_ram_audio_mono_texturas_16bit.md` (`Lines 1–307`)  
**Stage:** 5 (`INITIAL_ZOOM_OUT`)  
**Deliverable:** `zoom_out_report.md`

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-zoom-out` | completed |
| `egm-inventive-catalyst` | completed |

---

## Executive Summary & High-Altitude Synthesis

The design document presents a strong, highly targeted strategy for fitting Plants vs. Zombies within the Miyoo Mini Plus handheld's rigid ~82–85 MB Linux kernel OOM killer boundary. By combining 16-bit texture quantization with standardized 22.05 kHz mono audio and state-boundary heap trimming, it legitimately targets a drop from ~78 MB down to ~50–53 MB RSS (>30 MB safety headroom).

However, a holistic architectural review (`egm-zoom-out`) and TRIZ contradiction analysis (`egm-inventive-catalyst`) grounded in the actual codebase revealed critical discrepancies:
1. **Double Alpha Multiplication Blocker**: Quantizing offline to pre-multiplied ARGB4444 while feeding the texels into PopCap's standard `SWDrawShape` / `SoftwareRenderer` will trigger double alpha pre-multiplication, causing dark halos and muddy sprites.
2. **Hardcoded Rasterizer Bailout Blocker**: The software rasterizer (`SoftwareRenderer.cpp:673`) currently contains `if (texture->mBytesPerPixel != 4) return;`, which silently discards every 16-bit textured quad/triangle.
3. **Linkage Mismatch Blocker**: Section 9.2 specifies statically linking `libopenmpt.a`, whereas the engine dynamically resolves symbols via `dlopen`/`dlsym`.
4. **Codebase Reality Gap**: Much of the proposed data structure changes (`PixelFormat`, `SoftwareSurface` 2-bpp support, `ReanimAtlasMakeBlankMemoryImage` ARGB4444 allocation, `SWTri_GetTexel.inc` format branching, and NEON span blitters) are **already implemented** in the repository. The real challenge is the missing integration logic and pool management.

---

## Prioritized Findings (Blockers -> Major -> Nits)

### 1. Pre-Multiplied Alpha vs Straight Alpha Pipeline Mismatch (Double Pre-Multiplication) - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-inventive-catalyst`
* **Affected Scope:** Lines 164–170 (Section 4.2), Lines 177–189 (Section 5.A), Lines 340–345 (Target File 7)
* **Discrepancy Description**: Section 4.2 dictates that `setup_converter.c` quantizes sprites and reanimations into pre-multiplied ARGB4444 ($R_4 = \frac{R_8 \cdot A_4}{15 \times 16}$). In Section 5.A, `SWTri_GetTexel.inc` unpacks these 4-bit nibbles back into 32-bit registers. However, PopCap’s software rasterizer pipeline (`SWDrawShape`, `MemoryImage::NormalBlt`, `SWTri.cpp`) treats sampled texel colors as **straight alpha** and calculates $\text{Dst} = \frac{\text{Src} \cdot \text{Src}_A + \text{Dst} \cdot (255 - \text{Src}_A)}{255}$. Because the color channels were already multiplied by alpha offline, multiplying them by alpha a second time results in **double alpha pre-multiplication**: sprites appear darkened, transparent edges develop black fringe artifacts, and additive blend modes fail.
* **Rationale & Recommendation**: 
  - *TRIZ Principle 10 (Prior Action) vs Principle 24 (Intermediary)*: Either (a) update PopCap's rasterizer formulas in `SWTri.cpp` and `MemoryImage.cpp` to check `mPixelFormat == 2` and use pure additive accumulation $\text{Dst} = \text{Src} + \frac{\text{Dst} \cdot (255 - \text{Src}_A)}{255}$, OR (b) store ARGB4444 as **straight alpha** in offline PAK assets and execute pre-multiplication exclusively inside the vectorized NEON blitter (`NeonBlit_Span_ARGB4444_PreMul`). Option (b) is far cleaner, prevents breaking non-NEON fallback paths, and keeps asset color spaces standard.

---

### 2. Software Rasterizer Hardcoded 4 bpp Bailout Drops All 16-Bit Surfaces - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 90–96 (Section 1.A), Lines 317–324 (Target File 4), `src/graphics/SoftwareRenderer.cpp:673`
* **Discrepancy Description**: Section 1.A mentions relaxing assertions in `SoftwareRenderer.cpp:521, 605, 639`. However, an audit of the actual codebase reveals that `SoftwareRenderer.cpp:673` in `RasterizeTriangleSpan` contains a rigid guard:
  ```cpp
  if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 || mPhysClipRect.IsEmpty()) return;
  ```
  Any texture surface where `mBytesPerPixel == 2` (such as ARGB4444 reanimations or RGB565 backgrounds) is immediately rejected, causing dynamic sprites and triangles to become 100% invisible. Furthermore, `SoftwareRenderer::DrawImage` (lines 560–568) blindly casts `image->mPixels` to `const uint32_t*` and invokes `NeonBlit_Span`, treating 16-bit pixel buffers as 32-bit data, which causes severe memory overreads and visual garbage.
* **Rationale & Recommendation**: Expand the scope of Target File 4. `RasterizeTriangleSpan` must permit `texture->mBytesPerPixel == 2`, and `SoftwareRenderer::DrawImage` must inspect `image->mFormat`: routing `FMT_ARGB4444` to `NeonBlit_Span_ARGB4444_PreMul` and `FMT_RGB565` to `NeonBlit_Span_RGB565`.

---

### 3. Dynamic Linkage vs Static Linkage Contradiction in `OpenMptMusicInterface` - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 126–136 (Section 2.B), Lines 303–310 (Target File 2), `src/audio/OpenMptMusicInterface.cpp:120–165`
* **Discrepancy Description**: Target File 2 states: *"Bind `openmpt_module_read_mono` from `libopenmpt.a`"*. However, `OpenMptMusicInterface.cpp` does not link statically against `libopenmpt.a`; it dynamically loads shared libraries (`libopenmpt.so.0`, `libopenmpt.so`, `libopenmpt.dll`) at runtime via `dlopen`/`LoadLibraryA` and retrieves function pointers using `dlsym`/`GetProcAddress`. If the code attempts direct static symbol binding while retaining the dynamic loading wrapper, compilation will fail, or `mFnReadMono` will remain a null pointer during dynamic initialization, triggering a `SIGSEGV` crash upon audio playback.
* **Rationale & Recommendation**: Standardize on the existing dynamic loading pattern. In `OpenMptMusicInterface.hpp`, declare:
  ```cpp
  typedef size_t (*openmpt_module_read_mono_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* mono);
  ```
  In `TryLoadOpenMptSymbols()`, resolve `mFnReadMono = reinterpret_cast<openmpt_module_read_mono_fn>(DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono"))`. Update `RenderFallbackAudio` to ensure synthetic tracker fallback also outputs single-channel mono.

---

### 4. Redundant Memory Arena Over-Allocation (`DmaTexturePool` vs `SpriteCachePool` vs Glibc Heap) - Severity: 🟡 Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Line 58 (Question 1), Lines 78–111 (Section 1), Lines 223–240 (Data Flow Diagram)
* **Discrepancy Description**: The codebase already allocates a `DmaTexturePool` of **16 MB** (`SoftwareRenderer.hpp:217`), and Open Question 1 inquires whether `SpriteCachePool` should continue operating as an independent **12 MB** ARGB4444 arena. Together, these two pools lock up **28 MB of physical RAM**. If dynamic 16-bit reanimation atlases simultaneously allocate memory from the glibc heap (`new MemoryImage()`), the game will maintain static arena reservations AND dynamic heap allocations, defeating the intended >32 MB safety margin.
* **Rationale & Recommendation**: 
  - *TRIZ Principle 5 (Merging / Consolidation)*: Consolidate the memory pools. Do not run three parallel allocation pools. Answer Question 1 definitively: either (a) eliminate `SpriteCachePool` and route dynamic 16-bit surfaces directly through `DmaTexturePool`, or (b) downsize `DmaTexturePool` from 16 MB to 8 MB to reflect the halved 16-bit texture footprint. Pinned, idle arena memory is the primary driver of OOM on 128 MB devices.

---

### 5. Spec Audit Inaccuracy: Existing Implementation Treated as Future Work - Severity: 🟡 Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 80–111 (Section 1), Lines 174–195 (Section 5), Lines 317–351 (Target Files 4, 5, 6, 7, 8)
* **Discrepancy Description**: The technical plan specifies creating `enum class PixelFormat`, modifying `SoftwareSurface`, modifying `ReanimAtlasMakeBlankMemoryImage`, adding format branches to `SWTri_GetTexel.inc`, and implementing `NeonBlit_Span_RGB565` / `NeonBlit_Span_ARGB4444_PreMul`. In reality, **all five components are already implemented** in `src/graphics/SoftwareRenderer.hpp:245`, `src/PvzpLib/ReanimAtlas.cpp:53`, `src/SexyAppFramework/graphics/SWTri/SWTri_GetTexel.inc:38-73`, and `src/graphics/NeonBlit.cpp:468-545`. However, critical defects exist in the existing code: `SoftwareSurface::GetPixel` (`SoftwareRenderer.cpp:164-171`) hardcodes 2 bpp surfaces as RGB565 and completely corrupts ARGB4444 pixel reads.
* **Rationale & Recommendation**: Update the design document to acknowledge existing merged code and pivot the focus from basic definitions to fixing the broken glue code: specifically fixing `SoftwareSurface::GetPixel` to handle `FMT_ARGB4444` unpacking and integrating the span dispatchers in `SoftwareRenderer.cpp`.

---

### 6. Missing Alpha Channel Dithering in 16-Bit Quantizer (Color vs Alpha Discrepancy) - Severity: 🟡 Major
* **Reviewer Lens:** `egm-inventive-catalyst`
* **Affected Scope:** Lines 164–170 (Section 4.2), Lines 353–360 (Target File 9)
* **Discrepancy Description**: In Section 4.1, the author wisely applies a 4×4 Bayer ordered dithering matrix to RGB565 background colors to eliminate 16-bit color banding. However, in Section 4.2, the 8-bit alpha channel is quantized to 4-bit ARGB4444 using simple bit shifting ($A_4 = A_8 \gg 4$) without dithering. Truncating alpha from 256 levels to 16 levels causes visible stairstepping, harsh contour edges, and banded gradients on soft drop shadows, particle glows, and alpha-fading sprites.
* **Rationale & Recommendation**: 
  - *TRIZ Principle 1 (Segmentation) & Principle 10 (Prior Action)*: Extend ordered dithering to the alpha channel during offline quantization in `setup_converter.c`:
    $$A_4 = \text{clamp}\left(A_8 + (M_{4\times 4}[y \bmod 4][x \bmod 4] - 8),\, 0,\, 255\right) \gg 4$$
    This resolves the physical contradiction between alpha smoothness and 4-bit storage at zero runtime CPU cost.

---

### 7. Runtime Audio Resampling Ambiguity (Question 3 Indecision) - Severity: 🟡 Major
* **Reviewer Lens:** `egm-inventive-catalyst` & `egm-zoom-out`
* **Affected Scope:** Line 60 (Question 3), Lines 214–221 (Section 6), Lines 296–302 (Target File 1)
* **Discrepancy Description**: Open Question 3 leaves unresolved whether sound effects should be downsampled offline or at runtime via SDL_mixer. Relying on runtime downmixing forces `main.pak` to store full 44.1 kHz stereo audio, wasting ~10 MB of microSD flash storage, increasing decompression CPU cycles during level transitions, and forcing SDL_mixer to allocate temporary conversion buffers on the heap right before combat starts.
* **Rationale & Recommendation**: Resolve Question 3 conclusively in the spec. Mandate **offline batch downsampling** of all sound effects to 22.05 kHz Mono 16-bit PCM during the PAK asset build pipeline (`setup_converter.c` / FFmpeg). This eliminates runtime resampling overhead, reduces PAK size, and avoids heap allocation spikes during audio loading.

---

### 8. Inaccurate RAM Savings Attribution for RGB565 vs ARGB4444 - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Line 16 (Overview)
* **Discrepancy Description**: Line 16 claims that segregating textures into RGB565 backgrounds and ARGB4444 sprites produces "~16 MB net RAM reduction". In reality, only a single 640×480 background is active in memory at any time ($640 \times 480 \times 2 = 614.4\text{ KB}$). The RAM footprint of a 614 KB background is identical whether stored in RGB565 or ARGB4444. The ~16 MB reduction is achieved entirely by halving the hundreds of dynamic sprite and reanimation atlases from 32-bit to 16-bit.
* **Rationale & Recommendation**: Clarify that RGB565 is utilized exclusively for visual color gradient fidelity (5-6-5 color precision vs 4-4-4), not for memory differentiation.

---

### 9. Glibc Heap Page Return: `malloc_trim(0)` vs `M_MMAP_THRESHOLD` - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** Lines 137–156 (Section 3), Lines 311–316 (Target File 3)
* **Discrepancy Description**: Allocations larger than 128 KB (such as full-screen surfaces and large reanimation atlases) are allocated by glibc via `mmap()` rather than the brk heap arena. When `delete` or `free()` is called on an mmap buffer, the virtual memory pages are immediately returned to the kernel via `munmap()`, making `malloc_trim(0)` a no-op for those buffers. However, dynamic glibc heuristics can grow `M_MMAP_THRESHOLD` over time, causing intermediate textures to linger in the heap.
* **Rationale & Recommendation**: Pair `malloc_trim(0)` with explicit startup allocator configuration in `LawnApp.cpp`:
  ```cpp
  mallopt(M_MMAP_THRESHOLD, 64 * 1024);
  ```
  This guarantees that all texture allocations $\ge 64\text{ KB}$ consistently bypass the brk heap arena and unmap immediately upon deletion.

---

### 10. Headphone Dual-Mono Acoustic Behavior Clarification - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-inventive-catalyst`
* **Affected Scope:** Line 49 (Non-Goals), Lines 257–266 (Alternative C)
* **Discrepancy Description**: Section "Alternative C" dismisses stereo audio because the console has a single physical speaker, stating "Headphone usage is out of scope". This wording might lead users to believe headphones will output sound in only one ear.
* **Rationale & Recommendation**: Clarify that initializing SDL_mixer with 1 channel (`Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024)`) automatically instructs the SigmaStar ALSA sound driver to duplicate the mono audio stream across both the left and right headphone DAC channels, ensuring clean, balanced dual-mono headphone playback.
