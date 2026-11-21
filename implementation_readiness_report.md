# Implementation Readiness Report: 16-Bit Dual-Texture & Audio Architecture for Handheld Memory Stabilization

**Target:** `c:\DEV\PVZ\docs\designs\estabilizacion_ram_audio_mono_texturas_16bit.md` (`lines 292..307`, extended context `292..375`)  
**Stage:** 9 (`IMPLEMENTATION_READINESS_TEST`)  
**Deliverable:** `c:\DEV\PVZ\implementation_readiness_report.md`

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-implementation-readiness` | completed |
| `egm-codebase-details` | completed |
| `egm-style` | completed |

## Executive Summary
The implementation plan outlines a viable architectural direction for handheld RAM stabilization, but in its current form it is **NOT implementation-ready**. It contains critical technical blockers: it erroneously directs the implementer to bind OpenMPT from a static library (`libopenmpt.a`) despite the codebase using runtime dynamic resolution (`dlopen`/`dlsym`), fails to provide the concrete sampling and blitting logic required for 16-bit textures in `SoftwareRenderer` while citing non-existent assertion lines, re-specifies substantial components that already exist in the codebase, points to an invalid file path for `AlmanacDialog`, and omits unit test updates that would cause CI failures in `test_openmpt_playback`.

---

## Prioritized Findings (Blockers -> Major -> Nits)

### 1. Dynamic dlsym Resolution vs Static Library Binding in OpenMptMusicInterface - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** Lines 303–310 (Section 2: `src/audio/OpenMptMusicInterface.hpp` & `OpenMptMusicInterface.cpp`)
* **Discrepancy Description**: 
  Line 307 mandates: *"Bind `openmpt_module_read_mono` from `libopenmpt.a`."*
  However, `OpenMptMusicInterface` does not statically link to OpenMPT; it dynamically resolves symbols at runtime via `dlopen`/`dlsym` (`DLSYM_LOOKUP`) across shared libraries (`libopenmpt.so.0`, `libopenmpt.so`, `./libs/libopenmpt.so`). The plan lacks actionable instructions for the dynamic loader pipeline:
  1. No typedef for `openmpt_module_read_mono_fn`.
  2. No instruction to replace member `openmpt_module_read_interleaved_stereo_fn mFnReadStereo` with `openmpt_module_read_mono_fn mFnReadMono`.
  3. No instruction to update `TryLoadOpenMptSymbols()` to call `DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono")`.
  4. No instruction to update symbol validation: `if (mFnCreateFromMemory && mFnDestroy && mFnReadMono && ...)`.
  5. No concrete dispatch update in `RenderAudioChunk`: `mFnReadMono(song.openmptMod, static_cast<int32_t>(kSampleRate), frameCount, monoOutput)`.
* **Rationale & Recommendation**: 
  Attempting to statically link against `libopenmpt.a` will fail or break runtime shared library portability. Update the implementation plan with explicit dynamic resolution steps and typedefs.

---

### 2. Under-specified SoftwareRenderer 16-Bit Texture Pipeline & 4bpp Guard Fixes - Severity: 🔴 Blocker
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** Lines 317–324 (Section 4: `src/graphics/SoftwareRenderer.hpp` & `SoftwareRenderer.cpp`)
* **Discrepancy Description**: 
  The plan instructs: *"Relax assertions requiring `mBytesPerPixel == 4` to also accept `mBytesPerPixel == 2`"* (referencing non-existent assertions at `SoftwareRenderer.cpp:521, 605, 639`).
  In reality:
  1. The actual hardcoded guard is in `RasterizeTriangleSpan` at `SoftwareRenderer.cpp:673`:
     `if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || texture->mBytesPerPixel != 4 || mPhysClipRect.IsEmpty()) return;`
  2. Merely relaxing this guard without providing sampling and dispatch logic breaks rendering:
     - `SoftwareRenderer::DrawImage` (fast-path, lines 560–568) hardcodes 4-byte pointer arithmetic:
       `const uint32_t* src_ptr = reinterpret_cast<const uint32_t*>(image->mPixels + sy * image->mPitch) + src_offset_x;`
       For 16-bit textures (`mBytesPerPixel == 2`), this skips pixels and causes buffer overreads.
     - `SoftwareSurface::GetPixel` (lines 159–173) assumes any 2-byte surface is `RGB565`, discarding the alpha channel and corrupting `ARGB4444` sprites.
     - `DrawImage` (scaling path, lines 583–589) indexes `src_row[sx]` as `const uint32_t*`.
* **Rationale & Recommendation**: 
  Leaving the 16-bit blit and sampling dispatch unspecified forces the implementer to design the graphics pipeline. The plan must provide explicit instructions to:
  - Update line 673 to `(texture->mBytesPerPixel == 4 || texture->mBytesPerPixel == 2)`.
  - In `DrawImage` fast-path: Branch on `image->mFormat`. If `FMT_ARGB4444` and target is 32-bit, invoke `NeonBlit_Span_ARGB4444_PreMul`. If `FMT_RGB565` and target is 16-bit, invoke `NeonBlit_Span_RGB565`.
  - In `SoftwareSurface::GetPixel`: Add format branch for `PixelFormat::FMT_ARGB4444` expanding 4-bit nibbles to 8-bit channels `(A4<<4)|A4`.

---

### 3. Redundant Specification of Pre-existing Code - Severity: 🟡 Major
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** Lines 325–351 (Sections 5, 6, 7, 8)
* **Discrepancy Description**: 
  The plan instructs the implementer to write code that is already implemented in the codebase:
  - **Section 5**: `MemoryImage.h:70` already defines `uint8_t mPixelFormat = 0;` and `GetBytesPerPixel()`.
  - **Section 6**: `src/PvzpLib/ReanimAtlas.cpp:53-62` already sets `aImage->mPixelFormat = 2; // FMT_ARGB4444` and allocates 16-bit backing buffers via `aWordsNeeded`.
  - **Section 7**: `SWTri_GetTexel.inc:38-50` already contains the `if (__builtin_expect(tex_format == 2, 0))` block unpacking ARGB4444.
  - **Section 8**: `src/graphics/NeonBlit.hpp` and `NeonBlit.cpp:468-545` already define and implement `NeonBlit_Span_RGB565` and `NeonBlit_Span_ARGB4444_PreMul`.
* **Rationale & Recommendation**: 
  Listing already-implemented logic as pending tasks misleads the implementer. Retitle these sections as *"Verification of Existing Implementation"* and refocus instructions strictly on unlinked call sites.

---

### 4. Invalid File Path for AlmanacDialog and Unaddressed SoundManager - Severity: 🟡 Major
* **Reviewer Lens:** `egm-codebase-details`, `egm-implementation-readiness`
* **Affected Scope:** Lines 311–315 (Section 3) and Lines 296–302 (Section 1)
* **Discrepancy Description**: 
  1. Line 311 specifies modifying `src/Lawn/AlmanacDialog.cpp`. This file does not exist; the actual path is `src/Lawn/Widget/AlmanacDialog.cpp`.
  2. Line 299 targets `SDLSoundManager.cpp` for `Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024)`, but overlooks `SoundManager::Initialize` in `src/audio/OpenMptMusicInterface.cpp:693`, which calls `Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 1024)`.
* **Rationale & Recommendation**: 
  Correct the path to `src/Lawn/Widget/AlmanacDialog.cpp`. Clarify whether `PvZ::Audio::SoundManager` in `OpenMptMusicInterface.cpp` requires updating or clarify its role relative to `SDLSoundManager`.

---

### 5. Omission of Automated Test Suite Execution and test_openmpt_playback Regression - Severity: 🟡 Major
* **Reviewer Lens:** `egm-implementation-readiness`
* **Affected Scope:** Lines 361–375 (Verification Plan)
* **Discrepancy Description**: 
  1. The Verification Plan relies exclusively on manual checks and Docker/hardware runtime, omitting CMake's test suite (`ctest`).
  2. Changing `OpenMptMusicInterface::kNumChannels` from 2 to 1 will cause immediate assertion failures in `tests/test_openmpt_playback.cpp` (lines 208, 271 assume `kChunk * 2` stereo frames).
  3. No unit tests are prescribed in `tests/test_viewport_scale.cpp` to validate 16-bit texture decoding or rasterization.
* **Rationale & Recommendation**: 
  Update the Verification Plan to:
  - Update `tests/test_openmpt_playback.cpp` buffer assertions for mono (`kChunk * 1`).
  - Add a unit test in `tests/test_viewport_scale.cpp` verifying `SoftwareRenderer` rendering `FMT_ARGB4444` and `FMT_RGB565` textures.
  - Require executing `ctest --output-on-failure`.

---

### 6. Hallucinated Identifiers in Architectural Example - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-codebase-details`, `egm-style`
* **Affected Scope:** Line 133 (Section 1.2)
* **Discrepancy Description**: 
  Snippet `size_t count = mFnReadMono(mMod, mSampleRate, framesToRender, static_cast<int16_t*>(mBuffer));` references non-existent variables `mMod`, `mSampleRate`, `framesToRender`, and `mBuffer`.
* **Rationale & Recommendation**: 
  Update example to reflect actual class members:
  `size_t readFrames = mFnReadMono(song.openmptMod, static_cast<int32_t>(kSampleRate), frameCount, out);`.

---

### 7. Obsolete Parameter Nomenclature in RenderAudioChunk - Severity: 🟢 Nit
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** Line 308 (Section 2)
* **Discrepancy Description**: 
  `RenderAudioChunk(int16_t* interleavedStereoOutput, size_t frameCount)` retains stereo parameter naming despite mono conversion.
* **Rationale & Recommendation**: 
  Rename parameter to `int16_t* monoOutput` in header and implementation for consistency.

---

## Conclusion & Readiness Verdict
* **Stage 9 Verdict:** **REJECTED (REQUIRES REVISION)**
* The document cannot proceed to implementation until the OpenMPT dynamic binding, SoftwareRenderer 16-bit blitting path, existing codebase status, file paths, and test regressions are resolved in the specification.
