# Critic Review Report: 16-Bit Dual-Texture & Audio Architecture for Handheld Memory Stabilization

**Target:** `c:\DEV\PVZ\docs\designs\estabilizacion_ram_audio_mono_texturas_16bit.md` (`lines 1-291`)  
**Stage:** 7 (`CRITIC_REVIEW`)  
**Deliverable:** `critic_report.md`

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-design` | completed |
| `egm-structural` | completed |
| `egm-style` | completed |

## Severity Summary

- 🔴 **Blocker**: 3
- 🟡 **Major**: 3
- 🟢 **Nit**: 3
- **Total Findings**: 9

---

## Prioritized Findings (Blockers -> Major -> Nits)

### 1. Undefined Backbuffer Format (`mScreenImage`) & Invalid 16-Bit Direct Blit Assumption - Severity: Blocker
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** Lines 80–95, 225–234
* **Discrepancy Description**: Section 1 and Section 6 state that opaque RGB565 backgrounds use `NeonBlit Stream Copy (Direct 16-bit, 0.32 ms) -> FB0`. However, the document never defines the pixel format of the primary application backbuffer (`mScreenImage`). In PopCap's SexyAppFramework architecture, the entire scene (backgrounds, plants, zombies, particles, and UI widgets) is rendered into `mScreenImage` before presentation. If `mScreenImage` remains 32-bit ARGB8888 (which the document implies by retaining 32-bit bitmap fonts in lines 54 and 207), an RGB565 background cannot be direct-copied via 16-bit stream; it must be unpacked from RGB565 to ARGB8888 during the blit. Conversely, if `mScreenImage` is converted to 16-bit RGB565, the document fails to explain how 32-bit ARGB8888 fonts blend into a 16-bit backbuffer, or how `libneonarmmiyoo.so` is configured for a 16-bit destination.
* **Rationale & Recommendation**: Omitting the concrete pixel format of the composite backbuffer creates an architectural contradiction between background blitting and font rendering.  
  *Recommendation*: Explicitly declare the format of `mScreenImage`. If `mScreenImage` remains 32-bit ARGB8888, replace the "direct 16-bit stream copy" with a NEON unpacking blitter (`NeonBlit_Span_RGB565_to_ARGB8888`). If `mScreenImage` is 16-bit RGB565, specify the destination format conversion across `SoftwareRenderer`, font compositing routines, and the SDL2 presentation surface.

---

### 2. Broken Pre-Multiplied Alpha Compositing Pipeline in `SWTri` Rasterizer - Severity: Blocker
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** Lines 164–170, 176–189
* **Discrepancy Description**: Section 4 specifies that sprites and reanimation atlases are encoded as pre-multiplied ARGB4444 ($R_4 = (R_8 \cdot A_4) / 240$), and Section 5 proposes handling this by modifying `SWTri_GetTexel.inc` to unpack texels and compute $\text{Dst} = \text{Src} + \text{Dst} \cdot (1 - \text{Src}_A)$. However, in SexyAppFramework, `SWTri_GetTexel.inc` only handles texture sampling; destination compositing is performed across dozens of rasterization span loops in `SWTri.cpp` (`SWTri_DrawTriangle`, `SWTri_DrawQuad`, modulation loops), which evaluate standard straight-alpha formulas: $\text{Dst} = (\text{Src} \cdot \text{Src}_A + \text{Dst} \cdot (255 - \text{Src}_A)) / 255$. If `SWTri_GetTexel.inc` returns an already pre-multiplied color without modifying the span compositors, the rasterizer will multiply by alpha a second time, resulting in severe black halos, darkened edges, and corrupted opacity on all sprites.
* **Rationale & Recommendation**: Modifying `SWTri_GetTexel.inc` alone is insufficient and introduces double alpha modulation.  
  *Recommendation*: Either maintain non-premultiplied (straight) ARGB4444 storage so standard `SWTri` span compositors function unmodified, OR explicitly update all destination blending loops in `SWTri.cpp` to use the pre-multiplied formulation ($\text{Dst} = \text{Src} + (\text{Dst} \cdot (255 - \text{Src}_A)) / 255$) and avoid multiplying source color channels by alpha during modulation.

---

### 3. Toolchain Incompatibility: Unconditional `malloc_trim(0)` on Non-Glibc Embedded Toolchains - Severity: Blocker
* **Reviewer Lens:** `egm-design` / `egm-structural`
* **Affected Scope:** Lines 140–149
* **Discrepancy Description**: The proposed heap reclamation wrapper in Section 3 uses `#if defined(__linux__) && !defined(__ANDROID__)` to include `<malloc.h>` and call `malloc_trim(0)`. On embedded Linux distributions for retro handhelds (such as Buildroot or musl-based toolchains frequently utilized on SigmaStar SSD202D / Miyoo devices), `malloc_trim()` is not implemented in the C standard library. In `musl libc`, `<malloc.h>` does not declare `malloc_trim`, causing an immediate compilation error (`undefined reference to 'malloc_trim'`).
* **Rationale & Recommendation**: Guarding solely on `__linux__` breaks portability on embedded Linux toolchains that do not use GNU glibc.  
  *Recommendation*: Guard the implementation strictly with `#if defined(__GLIBC__)` in addition to `__linux__`:
  ```cpp
  #if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)
  #include <malloc.h>
  static inline void TrimHeapIfLinux() { malloc_trim(0); }
  #else
  static inline void TrimHeapIfLinux() {}
  #endif
  ```

---

### 4. Audio Downmix Distortion & Headphone Output Neglect - Severity: Major
* **Reviewer Lens:** `egm-design` / `egm-style`
* **Affected Scope:** Lines 49–50, 115–125, 257–260
* **Discrepancy Description**: The audio specification reduces all mixer channels to 1-channel mono and asserts that headphone usage is "out of scope" because the console has a single speaker transducer. However:
  1. The Miyoo Mini Plus features a 3.5mm stereo headphone jack. Under standard ALSA/OSS audio drivers on the SSD202D, opening a 1-channel mono stream without driver-level channel duplication outputs audio exclusively to the left earphone, causing a broken user experience.
  2. The document omits the downmixing gain law when converting multi-channel audio to mono. Simply summing channels ($L + R$) causes digital clipping and distortion on loud combat effects, while taking a single channel drops 50% of stereo-panned instruments.
* **Rationale & Recommendation**: Handheld games must support headphone output gracefully, and mono downmixing requires defined gain staging.  
  *Recommendation*: Specify standard mono gain normalization ($-3\text{ dB}$ or $-6\text{ dB}$ attenuation: $(L + R) \times 0.707$ or $(L + R) / 2$) during downmix. Ensure SDL audio initialization or ALSA configuration enables dual-mono replication across DAC channels 0 and 1 so 3.5mm headphone playback remains balanced.

---

### 5. Contradiction Between Open Question 3 and Non-Functional Requirements - Severity: Major
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** Lines 60–61, 288
* **Discrepancy Description**: The document exhibits an internal contradiction regarding audio asset processing. Open Question 3 (lines 60–61) asks: *"Should sound effects in `main.pak` be batch-downsampled offline via FFmpeg during release packaging, or rely on SDL_mixer's internal runtime downmixing upon loading?"*. However, Section "Non-Functional Requirements" (line 288) explicitly mandates: *"All asset quantization must be completed offline during PAK build to eliminate SD card read-write spikes during gameplay."* If audio conversion were deferred to runtime loading, `Mix_LoadWAV` would dynamically allocate memory for sample-rate conversion buffers, inducing peak RAM spikes during level loading that threaten the OOM safety ceiling.
* **Rationale & Recommendation**: An open question cannot remain pending when the system's non-functional requirements and stability budget forbid runtime conversion spikes.  
  *Recommendation*: Close Open Question 3 decisively in favor of offline FFmpeg batch downsampling during PAK generation, harmonizing the architecture with line 288.

---

### 6. Memory Sizing & Buffer Type Mismatch in `MemoryImage::mBits` - Severity: Major
* **Reviewer Lens:** `egm-structural` / `egm-design`
* **Affected Scope:** Lines 98–110
* **Discrepancy Description**: Section 1.B proposes modifying `MemoryImage` to support 16-bit storage, but retains `mBits` as `std::unique_ptr<uint32_t[]>`. In PopCap's SexyAppFramework, code across `Graphics.cpp`, `MemoryImage.cpp`, and image filters accesses `mBits` directly as 32-bit pixel words (`uint32_t*`). Storing 16-bit texels in a `uint32_t[]` array without introducing dedicated 16-bit accessors (`GetBits16()`, `mBits16`) or type safety risks pointer aliasing bugs, incorrect stride increments in rasterizers, and buffer overrun vulnerabilities.
* **Rationale & Recommendation**: Abstracting pixel formats requires explicit typed accessors to prevent stride and indexing errors.  
  *Recommendation*: Refactor `MemoryImage` to use a typed union or `uint8_t*` / `void*` backing buffer with explicit typed accessors (`GetBits32()` vs `GetBits16()`), and assert that pixel access matches `mPixelFormat`.

---

### 7. Flawed Pre-multiplied Quantization Formula ($/240$) and Alpha Precision Loss - Severity: Nit
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** Lines 166–169
* **Discrepancy Description**: The pre-multiplication formula given for ARGB4444 quantization ($R_4 = \frac{R_8 \cdot A_4}{15 \times 16}$) divides by 240 instead of 255. Because full scale in 8-bit is 255, dividing by 240 introduces a systematic ~6.25% brightening error and roundoff distortion across all midtones. Furthermore, shifting 8-bit alpha right by 4 bits ($A_4 = A_8 \gg 4$) truncates any alpha values below 16 down to 0, completely stripping delicate antialiased edge fringes.
* **Rationale & Recommendation**: Quantization formulas must be mathematically sound.  
  *Recommendation*: Use standard fixed-point rounding: $A_4 = (A_8 + 8) / 17$ (or $(A_8 \times 15 + 128) / 255$), and calculate pre-multiplied channels using $R_4 = (R_8 \cdot A_8 + 128) / (255 \times 17)$.

---

### 8. Unexplained `+ 2` Padding Byte Allocation in Specification - Severity: Nit
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** Line 99, Line 107
* **Discrepancy Description**: Line 99 states that 16-bit surface allocations are sized as `width * height * 2 + 2 bytes`, but fails to explain the rationale for the trailing `+ 2` bytes (e.g., sentinel pixel, SIMD boundary padding, or alignment). Meanwhile, line 107 sizes the allocation using `((theWidth * theHeight * sizeof(uint16_t) + 7) / sizeof(uint32_t))`. The disparity between `+ 2 bytes` and `+ 7` uint32 rounding creates ambiguity for developers implementing the memory allocators.
* **Rationale & Recommendation**: Buffer sizing logic must be consistent and documented.  
  *Recommendation*: Explain the necessity of padding bytes explicitly (e.g., SIMD vector tail over-read protection) and harmonize the sizing formula between Section 1.B and line 107.

---

### 9. Inconsistent LaTeX Math Mode Usage and Heading Notation Inconsistencies - Severity: Nit
* **Reviewer Lens:** `egm-style` / `egm-structural`
* **Affected Scope:** Lines 157, 188, 284
* **Discrepancy Description**: The document exhibits formatting and typographical inconsistencies:
  1. Section headers inconsistently include file paths: Section 4 includes `(src/setup/setup_converter.c)` while Section 1, 2, and 3 follow different conventions.
  2. The document mixes LaTeX math mode (`$\ge 30.0\text{ MB}$` in line 284, `$$...$$` in lines 162, 188) with standard Markdown notation (`>32 MB` in line 20), creating rendering issues in plain-text or standard Markdown viewers.
* **Rationale & Recommendation**: Uniform Markdown formatting ensures consistent presentation across all viewing environments.  
  *Recommendation*: Standardize section headers to conceptual names and replace inline LaTeX math expressions with standard ASCII typography (e.g., `>= 30.0 MB`).
