# Comprehension Test Report: 16-Bit Dual-Texture & Audio Architecture for Handheld Memory Stabilization

**Document:** [estabilizacion_ram_audio_mono_texturas_16bit.md](file:///c:/DEV/PVZ/docs/designs/estabilizacion_ram_audio_mono_texturas_16bit.md)  
**Evaluated Scope:** Feature Specification (Lines 1–291), Detailed Implementation Plan (Lines 292–307)  
**Agent:** Comprehension Goldfish (Strict Read-Only Audit)

---

## Feature Specification Paraphrase

### What this proposes
The document proposes a comprehensive userland memory stabilization architecture for Plants vs. Zombies running on the Miyoo Mini Plus (128 MB RAM, ~82–85 MB kernel OOM ceiling) to reduce peak combat memory from ~78 MB down to ~50–53 MB without relying on swap storage. This reduction is achieved across three pillars: splitting textures into a dual 16-bit pipeline (RGB565 with Bayer dithering for opaque backgrounds and pre-multiplied ARGB4444 for dynamic reanimations/sprites, while keeping bitmap fonts in 32-bit ARGB8888), standardizing all audio playback (sound effects and tracker music) to a unified 22.05 kHz 16-bit mono stream, and triggering `malloc_trim(0)` at clean state transitions to reclaim fragmented heap memory. The plan relies on offline asset quantization via `setup_converter.c` and vectorized ARM NEON assembly kernels (`NeonBlit`) to sustain locked 60 FPS performance.

### How the existing system works, per this document
According to the document, the current engine stores all textures as uncompressed 32-bit ARGB8888 (`MemoryImage::mBits` at 4 bytes per pixel) consuming ~32 MB, and holds sound effects in memory as 44.1 kHz 16-bit stereo uncompressed PCM consuming ~12–16 MB. The audio subsystem suffers from an internal rate and channel mismatch: `SDLSoundManager` runs at 44.1 kHz stereo while `OpenMptMusicInterface` runs tracker music at 22.05 kHz stereo, forcing real-time CPU resampling while redundantly feeding two stereo channels to the console's single physical mono speaker. Furthermore, navigating between menus, the shop, and the Suburban Almanac causes glibc heap fragmentation that pins dirty pages in RAM, pushing steady-state combat to ~78 MB Resident Set Size (RSS) and causing immediate `SIGKILL` termination by the Linux kernel OOM killer during burst entity or sound allocations.

---

## Implementation Plan Paraphrase

### What this proposes
The audited implementation scope (Lines 292–307) defines the concrete code modifications required to standardize the audio subsystem to 22.05 kHz mono. Specifically, it updates `SDLSoundManager.cpp` to call `Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024)` and synchronize channel/frequency tracking variables (`mMixerChannels = 1`, `mMixerFreq = 22050`), and modifies `OpenMptMusicInterface.hpp` / `OpenMptMusicInterface.cpp` to set `kNumChannels = 1` and link `openmpt_module_read_mono` from `libopenmpt.a`. This work is designed to cut audio PCM chunk memory by 75% (~9–12 MB net reduction) and eliminate tracker channel mixing overhead on Core 0.

### How the existing system works, per this document
Currently, `SDLSoundManager.cpp` initializes SDL_mixer with `Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 2048)`, which decodes sound effect chunks into 44.1 kHz stereo buffers in RAM and executes dual-channel pitch-shifting inner loops. Concurrently, `OpenMptMusicInterface.cpp` renders tracker music across two interleaved channels using stereo module read functions, creating mismatch with the sound manager and consuming additional CPU cycles and memory bandwidth.

---

## Audit Findings & Comprehension Gaps

### 1. Inconsistent Render Target Format and Compositing Pipeline
*   **Severity**: 🔴 Blocker
*   **Affected Scope**: Lines 83–96, 186–195, 227–234, 253–254, 321–322, 349–351.
*   **Discrepancy Description & Rationale**:
    The document explains that source textures will be segregated into RGB565 (backgrounds), ARGB4444 (sprites), and ARGB8888 (fonts). However, it never defines the pixel format of the primary backbuffer / render target (`mScreenImage` or active `SoftwareSurface`).
    Crucially, the document presents contradictory compositing paths:
    1. In Line 191 and Line 350, the ARGB4444 blitter function signature is `NeonBlit_Span_ARGB4444_PreMul(uint32_t* dst, const uint16_t* src, size_t count)`, where `dst` is explicitly 32-bit (`uint32_t*`).
    2. In Line 227 and Line 349, background rendering is specified as `NeonBlit_Span_RGB565` performing a "direct 16-bit streaming copy of background spans". A 16-bit streaming copy requires a 16-bit (`uint16_t*`) destination buffer.
    If the backbuffer is 32-bit ARGB8888, a direct 16-bit copy of RGB565 background spans is impossible without unpack/conversion (otherwise stride and color channels are corrupted). If the backbuffer is 16-bit RGB565, `NeonBlit_Span_ARGB4444_PreMul` cannot write to a `uint32_t* dst`, and ARGB8888 fonts cannot be rasterized without a 32-to-16-bit downsampling step that is never specified. A new engineer cannot build the rendering pipeline from this description.

### 2. Undefined `SpriteCachePool` and Contradiction with Baseline Memory Model
*   **Severity**: 🔴 Blocker
*   **Affected Scope**: Line 58 (Open Questions: Question 1).
*   **Discrepancy Description & Rationale**:
    Line 58 asks: *"Does the existing `SpriteCachePool` require format adjustments, or should it continue operating as an independent 12 MB ARGB4444 arena alongside the 16-bit texture pipeline?"*
    `SpriteCachePool` is never defined, described, or referenced anywhere else in the document. Furthermore, this introduces a direct contradiction with the core problem statement:
    - Lines 24–26 assert that baseline textures consume ~32 MB because all textures are stored in 32-bit ARGB8888.
    - If a 12 MB ARGB4444 pool already exists, why was it described as unoptimized 32-bit? Is that 12 MB included in the 32 MB texture baseline, or is it an additional allocation? If ARGB4444 sprites already exist in a dedicated arena, what portion of the engine is actually being converted? Without defining `SpriteCachePool`, the baseline memory breakdown and the projected ~16 MB savings cannot be verified.

### 3. Unexplained Buffer Allocation Formulas (`+ 2` vs `+ 7`)
*   **Severity**: 🟡 Major
*   **Affected Scope**: Line 99 and Line 107 (also Line 336).
*   **Discrepancy Description & Rationale**:
    In Section 1.B (Line 99), the document specifies: *"For 16-bit surfaces (`FMT_RGB565` and `FMT_ARGB4444`), buffer allocation is sized as `width * height * 2 + 2` bytes."*
    In Line 107, `ReanimAtlasMakeBlankMemoryImage` allocates backing memory as:
    `std::make_unique<uint32_t[]>((theWidth * theHeight * sizeof(uint16_t) + 7) / sizeof(uint32_t))` uint32 words.
    Neither formula is explained:
    - Why does Line 99 add `+ 2` bytes? (Is it a sentinel, null terminator, or alignment padding?)
    - In Line 107, since `sizeof(uint16_t) == 2` and `sizeof(uint32_t) == 4`, ceiling integer division to `uint32_t` would normally use `+ 3` (i.e. `+ sizeof(uint32_t) - 1`), not `+ 7`. Adding 7 bytes before dividing by 4 corresponds to rounding up to an 8-byte boundary in bytes, but dividing by 4 yields uint32 words, leaving the alignment rationale unclear and inconsistent with Line 99.

### 4. Incomplete Bayer Matrix Dithering Specification
*   **Severity**: 🟡 Major
*   **Affected Scope**: Lines 160–163, Line 356.
*   **Discrepancy Description & Rationale**:
    Section 4.1 defines the 4×4 Bayer matrix $M$ and the bit-packing formula `((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)`, but completely omits the algorithmic transformation connecting $M$ to $r, g, b$:
    - It does not specify how matrix values are normalized, scaled, or added to the 8-bit color components before shifting.
    - It does not specify color clamping (e.g., handling values that exceed 255 after dither addition).
    An implementer cannot write the dither kernel in `setup_converter.c` without unstated domain assumptions.

### 5. Ambiguous Denominator and Rounding in ARGB4444 Pre-multiplication Formula
*   **Severity**: 🟡 Major
*   **Affected Scope**: Lines 166–169.
*   **Discrepancy Description & Rationale**:
    The sprite quantization formula in Section 4.2 defines:
    $$A_4 = A_8 \gg 4$$
    $$R_4 = \frac{R_8 \cdot A_4}{15 \times 16} \quad (\text{Pre-multiplied})$$
    The divisor is $15 \times 16 = 240$.
    For fully opaque white ($R_8 = 255, A_4 = 15$), $\frac{255 \times 15}{240} = 15.9375$. The document does not specify whether this value is truncated, rounded to nearest, or clamped. Furthermore, standard 8-to-4-bit pre-multiplication typically divides by 255 (e.g. $(R_8 \cdot A_8 + 127) / 255$ shifted). The document does not provide rationale for why 240 is selected or how integer rounding should be implemented.

### 6. Contradiction on Audio Conversion Pipeline (Offline Build vs Runtime Downmixing)
*   **Severity**: 🟡 Major
*   **Affected Scope**: Line 60 (Question 3), Line 215, Line 288, Line 301.
*   **Discrepancy Description & Rationale**:
    Line 60 poses Open Question 3: *"Should sound effects in `main.pak` be batch-downsampled offline via FFmpeg during release packaging, or rely on SDL_mixer's internal runtime downmixing upon loading?"*
    However, the specification elsewhere treats this as settled in conflicting ways:
    - Line 301 states that `Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024)` in `SDLSoundManager.cpp` will decode all sound effects into 22.05 kHz mono PCM in RAM at runtime.
    - Line 288 (Non-Functional Requirements) states: *"All asset quantization must be completed offline during PAK build to eliminate SD card read-write spikes during gameplay."*
    If audio files remain 44.1 kHz stereo inside `main.pak`, runtime loading requires CPU conversion and spikes transient memory during decompression. If audio is converted offline, `setup_converter.c` or release scripts lack any audio processing specifications.

### 7. Phantom Reference to `ImageLib`
*   **Severity**: ⚪ Nit
*   **Affected Scope**: Line 97 (Section heading "B. `MemoryImage` & `ImageLib` Storage Representation").
*   **Discrepancy Description & Rationale**:
    The section header explicitly cites `ImageLib`, but the body of Section 1.B (lines 97–110) never mentions `ImageLib`. The document should either clarify what modifications `ImageLib` requires for 16-bit loading/decoding or remove `ImageLib` from the heading.

### 8. Discrepancy Between `main.pak` and `main_mmp.pak`
*   **Severity**: ⚪ Nit
*   **Affected Scope**: Line 60, Line 209, Line 224, Line 288.
*   **Discrepancy Description & Rationale**:
    Lines 60 and 288 refer to `main.pak`, whereas the Mermaid architecture diagram (Lines 209 and 224) references `main_mmp.pak`. It is unclear whether the build pipeline creates an alternate package file for the Miyoo Mini Plus (`main_mmp.pak`) or replaces `main.pak`.

### 9. Unexplained Symbols and Internal Handles
*   **Severity**: ⚪ Nit
*   **Affected Scope**: Line 133, Lines 153–154, Line 307.
*   **Discrepancy Description & Rationale**:
    Several symbols are introduced without definition:
    - `mFnReadMono` (Line 133) is used in a snippet without explaining if it is an existing member variable, a wrapper function, or dynamically loaded via `dlsym`.
    - `DelayLoad_Store` and `DelayLoad_Almanac` (Lines 153–154) are referenced without explaining whether they are resource group IDs, smart pointers, or dialog member objects.
