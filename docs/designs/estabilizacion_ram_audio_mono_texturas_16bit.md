# 16-Bit Dual-Texture & Audio Architecture for Handheld Memory Stabilization

*   **Author:** Fnava1
*   **Status:** Approved
*   **Date:** 2026-11-19

--------------------------------------------------------------------------------

## Overview

Plants vs. Zombies on the Miyoo Mini Plus handheld console (SigmaStar SSD202D SoC, 128 MB RAM, 640×480 3.5" IPS display) suffers from recurring out-of-memory (OOM) crashes during intense combat waves, level transitions, and combinatorial entity interactions (such as a Jalapeno detonating against a Zamboni). Under the unoptimized baseline (32-bit ARGB8888 textures and 44.1 kHz stereo uncompressed PCM audio), active combat consumes ~78 MB Resident Set Size (RSS) against a rigid Linux kernel OOM killer ceiling of ~82–85 MB, leaving a precarious operating margin of less than 5 MB.

Because platform safety policies strictly prohibit swapfiles on the microSD card due to flash degradation, filesystem corruption, and severe frame-dropping I/O latency, memory stability must be achieved through userland engine architecture.

This specification establishes a unified memory stabilization architecture combining:
1.  **16-Bit Dual-Texture Architecture**: Segregating textures by visual properties—storing opaque backgrounds in 16-bit RGB565 (for smooth gradient fidelity) and reanimation atlases/sprites in 16-bit straight-alpha ARGB4444 (halving hundreds of active entity textures to achieve a ~16 MB net RAM reduction), while preserving bitmap fonts in 32-bit ARGB8888.
2.  **Global 22.05 kHz Mono Audio Standardization**: Synchronizing `SDLSoundManager` and `OpenMptMusicInterface` to 1-channel 22,050 Hz 16-bit audio, eliminating real-time software resampling overhead and cutting uncompressed PCM chunk memory by 75% (~9 to 12 MB net RAM reduction).
3.  **State-Boundary Heap Reclamation**: Bounding `malloc_trim(0)` strictly to quiescent UI transitions (title screen load, store exit, almanac close, and stage clearance) and setting `mallopt(M_MMAP_THRESHOLD, 64 * 1024)` to reclaim fragmented glibc heap pages without in-combat frame drops.
4.  **Static Pool Consolidation**: Eliminating `SpriteCachePool` (reclaiming 12 MB) and downsizing `DmaTexturePool` from 16 MB to 8–10 MB for dynamic 16-bit reanimations, freeing ~18 MB of locked static userland memory.

Together, these measures reduce peak combat RSS from ~78 MB down to **~50–53 MB**, establishing a permanent **>32 MB safety margin** (>38% of userland memory) below the kernel OOM killer threshold with zero swap dependence and locked 60 FPS performance.

## The Problem

1. **The 4 MB Combat Cliff:**
   The game engine stores textures as 32-bit ARGB8888 (`MemoryImage::mBits`, 4 bytes/pixel) consuming ~32 MB, and holds sound effects as 44.1 kHz 16-bit Stereo uncompressed PCM consuming ~12–16 MB. Steady-state combat reaches ~78 MB RSS. With the Miyoo Mini Plus kernel OOM killer terminating userland processes at 82–85 MB, any burst allocation (such as dynamic compilation of a charred zombie atlas or sound buffer allocations) causes immediate process termination (`SIGKILL`).

2. **Audio Subsystem Mismatch & Redundancy:**
   `SDLSoundManager.cpp` calls `Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 2048)` while `OpenMptMusicInterface.cpp` streams tracker music at 22,050 Hz Stereo. This forces continuous CPU resampling for whichever subsystem initializes second. Furthermore, the Miyoo Mini and Mini Plus feature a single physical mono speaker; maintaining two stereo channels doubles audio memory consumption and bus traffic to produce identical physical acoustic vibrations.

3. **Inviability of MicroSD Swap Memory:**
   Platform rules strictly forbid swap on microSD storage. MicroSD random writes induce high I/O wait latency (dropping frame rates from 60 FPS down to 5–10 FPS) and rapidly wear NAND flash blocks, causing FAT32/exFAT filesystem corruption. The game must execute 100% in physical RAM.

4. **Heap Fragmentation & Dynamic Heap Retention:**
   Navigating between the Suburban Almanac, Crazy Dave's Twiddydinkies, and combat stages allocates and frees temporary texture pages and animation models. Without periodic heap coalescing and a tuned mmap threshold, memory fragmentation keeps dirty virtual memory pages pinned in userland RAM, eroding the OOM margin over long play sessions.

## Goals

*   **Halve Texture Memory Footprint:** Reduce total texture RAM consumption from ~32 MB to ~16 MB by converting opaque backgrounds to RGB565 (2 bytes/pixel for gradient quality) and halving hundreds of dynamic reanimation atlases and sprites to ARGB4444 (2 bytes/pixel).
*   **Reduce Audio RAM by 75%:** Standardize audio output to 22.05 kHz Mono across both `SDLSoundManager` and `OpenMptMusicInterface`, lowering sound effect and music RAM from ~15 MB to ~4 MB (~9–12 MB net reduction).
*   **Expand OOM Safety Headroom to >32 MB:** Lower peak combat RSS from ~78 MB down to ~50–53 MB, establishing a permanent >32 MB safety buffer below the 82–85 MB kernel OOM limit.
*   **Consolidate Memory Pools:** Eliminate `SpriteCachePool` (12 MB) and downsize `DmaTexturePool` to 8–10 MB, preventing static arena triplication and locking in deterministic allocation bounds.
*   **Eliminate Audio Driver Resampling Overhead:** Synchronize the sound manager and music interface to an identical 22,050 Hz sampling clock, eliminating CPU-bound software sample conversion.
*   **Zero Swap Dependency:** Guarantee 100% in-RAM execution without mounting or touching swapfiles on the microSD card.
*   **Maintain Native 60 FPS Performance:** Exploit 16-bit memory bus bandwidth reductions and ARM NEON SIMD vectorized blitters to sustain locked 60.0 FPS frame delivery on the dual-core Cortex-A7 CPU.
*   **Quarantine Allocator Latency:** Restrict `malloc_trim(0)` strictly to state transitions and configure `M_MMAP_THRESHOLD`, eliminating allocator-induced frame stutter during gameplay.

## Non-Goals

*   **No MicroSD Swap Memory:** Never create, mount, or fallback to disk-backed swapfiles or virtual partitions.
*   **No Stereo Audio or Spatial Panning:** Directional audio calculations across lawn columns are intentionally sacrificed to maximize memory preservation for the console's single speaker. For 3.5mm headphone output, the ALSA driver duplicates the single mono channel across both DAC channels with $-3\text{ dB}$ attenuation ($(L+R) \times 0.707$) to avoid clipping.
*   **No 8-Bit Paletted Sprites:** Sprites will not be converted to 8-bit indexed color due to the absence of NEON vector gather instructions on ARM Cortex-A7, preventing severe frame rate drops.
*   **No Direct `/dev/fb0` Bypass:** Will not replace the SDL2 `libneonarmmiyoo.so` presentation backend with manual scanline row assembly, preserving VSync and clean OnionOS exit handling.
*   **No In-Combat Heap Trimming:** `malloc_trim(0)` will never execute within active tick or draw loops.
*   **No Resolution Downscaling:** Do not reduce the native 640×480 framebuffer resolution or render at sub-native scales.
*   **No Font Quantization:** Bitmap fonts (`data/fonts`) remain in 32-bit ARGB8888 to ensure maximum text sharpness on the 3.5" display.

## Resolved Architectural Decisions

*   **Decision 1 (SpriteCachePool vs DmaTexturePool)**: Decisively resolved. `SpriteCachePool` (12 MB) is completely eliminated. `DmaTexturePool` is downsized from 16 MB to 8–10 MB. All dynamic 16-bit reanimation atlases are routed through `DmaTexturePool::Allocate()` and cleared with `Reset()` during level transitions.
*   **Decision 2 (Modal Dialog Backdrops)**: Modal dialog backdrops (Almanac pages, Store backdrop) share the opaque RGB565 format unpacked to the 32-bit backbuffer via `NeonBlit_Span_RGB565_to_ARGB8888`.
*   **Decision 3 (Audio Downsampling Strategy)**: Decisively resolved in favor of offline batch transcoding. All sound effects in `main.pak` are pre-converted offline to 22.05 kHz Mono 16-bit PCM WAV (with $-3\text{ dB}$ headroom) to prevent runtime glibc heap resample allocations during combat loading.

## Critical User Journeys (CUJs)

### CUJ 1: Explosive Combat on Zamboni (Jalapeno / Doom-shroom)
*   **Given**: Active Survival: Endless combat wave on Pool or Roof with multiple active zombies and heavy projectile volume, operating at ~51 MB RSS.
*   **When**: The player plants a Jalapeno, which detonates on a Zamboni, dynamically triggering `REANIM_ZOMBIE_CHARRED_ZAMBONI` and instantiating explosion sound channels.
*   **Then**: The newly allocated 16-bit atlas consumes ~1.0 MB from `DmaTexturePool` instead of ~2.5 MB from heap, and mono sound channels allocate 44.1 KB/s instead of 176.4 KB/s. Process RSS peaks at ~52.5 MB, remaining >30 MB below the 83 MB OOM ceiling. The explosion and charred animation render at 60 FPS without crashing or stuttering.

### CUJ 2: Survival Stage Intermission & Almanac Inspection
*   **Given**: Completion of a Survival stage with board state preserved in memory (~45 MB RSS with 16-bit assets and mono audio).
*   **When**: The player enters seed selection and opens the Suburban Almanac to inspect a zombie.
*   **Then**: The Almanac loads its 16-bit backdrop and zombie preview atlases. Total RSS reaches ~54 MB. Upon closing, the temporary assets are freed and trimmed with `malloc_trim(0)`. Heap fragmentation is purged, and combat restarts seamlessly without memory creep.

--------------------------------------------------------------------------------

## Technical Plan (Architecture)

### 1. Architectural Overview & Data Structures

To support multiple pixel formats cleanly without code duplication, the engine introduces explicit format tagging across the graphics pipeline:

```cpp
enum class PixelFormat : uint8_t {
    FMT_ARGB8888 = 0, // 32-bit (4 bytes/px): Preserved for fonts & unquantized buffers
    FMT_RGB565   = 1, // 16-bit (2 bytes/px): Opaque backgrounds
    FMT_ARGB4444 = 2  // 16-bit (2 bytes/px): Sprites & reanimations (straight-alpha)
};
```

#### A. Backbuffer & `SoftwareSurface` Architecture (`SoftwareRenderer.hpp`, `SoftwareRenderer.cpp`)
*   **Backbuffer Format (`mScreenImage`)**: The primary render target `mScreenImage` is explicitly maintained as **32-bit ARGB8888** (`SoftwareSurface` with `FMT_ARGB8888`). This ensures 32-bit bitmap glyphs and UI widgets blend with subpixel fidelity and zero overhead.
*   **Background Blitting**: Opaque RGB565 backgrounds are converted into `mScreenImage` using the ARM NEON kernel `NeonBlit_Span_RGB565_to_ARGB8888` (~0.35 ms per 640×480 frame on Cortex-A7), eliminating stride and bit-depth mismatch.
*   **Rasterizer Guard Updates (`SoftwareRenderer.cpp:673`)**:
    Update the `RasterizeTriangleSpan` guard:
    ```cpp
    // Allow both 16-bit (2 bpp) and 32-bit (4 bpp) textures onto 32-bit render target
    if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 ||
        (texture->mBytesPerPixel != 4 && texture->mBytesPerPixel != 2) || mPhysClipRect.IsEmpty())
        return;
    ```
*   **Typed Dispatch in `SoftwareRenderer::DrawImage`**:
    Replace unconditional `(const uint32_t*)image->mPixels` casts with branches on `image->mFormat`:
    *   `FMT_ARGB4444`: Dispatched to `NeonBlit_Span_ARGB4444_PreMul`.
    *   `FMT_RGB565`: Dispatched to `NeonBlit_Span_RGB565_to_ARGB8888`.
    *   `FMT_ARGB8888`: Dispatched to standard 32-bit `NeonBlit_Span`.
*   **Surface Pixel Accessors**:
    Update `SoftwareSurface::GetPixel` and `SetPixel` in `SoftwareRenderer.cpp:164–186` to unpack `FMT_ARGB4444`:
    ```cpp
    case PixelFormat::FMT_ARGB4444: {
        uint16_t val = reinterpret_cast<const uint16_t*>(mPixels)[y * mWidth + x];
        uint32_t a = (val >> 12) & 0x0F;
        uint32_t r = (val >> 8) & 0x0F;
        uint32_t g = (val >> 4) & 0x0F;
        uint32_t b = val & 0x0F;
        return ((a | (a << 4)) << 24) | ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
    }
    ```

#### B. `MemoryImage` & Dynamic Allocation Safety
*   `MemoryImage` stores pixel data in a format-tagged buffer.
*   **NEON Vector Padding**: For 16-bit surfaces (`FMT_RGB565` and `FMT_ARGB4444`), buffer allocation is sized as `width * height * 2 + 2` bytes. The trailing `+ 2` bytes provide padding against 8-pixel `vld1_u16` ARM NEON vector over-read margins on odd line counts.
*   Provide a typed 16-bit pixel accessor:
    ```cpp
    uint16_t* MemoryImage::GetBits16() const {
        return reinterpret_cast<uint16_t*>(mBits.get());
    }
    ```
*   In `ReanimAtlas.cpp:46` (`ReanimAtlasMakeBlankMemoryImage`), allocate `mBits` directly as 16-bit ARGB4444 from `DmaTexturePool`:
    ```cpp
    MemoryImage* ReanimAtlasMakeBlankMemoryImage(int theWidth, int theHeight) {
        MemoryImage* aImage = new MemoryImage();
        aImage->mWidth = theWidth;
        aImage->mHeight = theHeight;
        aImage->mPixelFormat = PixelFormat::FMT_ARGB4444;
        aImage->mBits = std::make_unique<uint32_t[]>((theWidth * theHeight * sizeof(uint16_t) + 7) / sizeof(uint32_t));
        return aImage;
    }
    ```

#### C. Memory Pool Consolidation
*   **Elimination of `SpriteCachePool`**: Remove the redundant 12 MB static arena.
*   **Downsizing `DmaTexturePool`**: Shrink `DmaTexturePool` (`SoftwareRenderer.hpp:217`) from 16 MB to **8–10 MB**.
*   All dynamic 16-bit reanimation buffers allocate through `DmaTexturePool`, freeing ~18 MB of permanent static RAM reservations.

### 2. Audio Subsystem Standardization (22.05 kHz Mono)

#### A. Audio Specification Harmonization (`SDLSoundManager.cpp`)
*   Change `SDLSoundManager::SDLSoundManager()` initialization call:
    ```cpp
    // Standardize to 22050 Hz Mono 16-bit with 1024-frame buffer (46.4 ms latency)
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024))
    {
        Sexy::LogInfoLn("Failed to initialize SDL mixer");
        return;
    }
    ```
*   Update `Mix_QuerySpec` tracking variables: `mMixerChannels` receives `1`, halving pitch-shifter inner loops in `SDLSoundInstance.cpp:152`.
*   **Headphone Dual-Mono Routing & Gain Normalization**:
    In SDL_mixer / ALSA, single-channel mono is duplicated to both Left and Right DAC channels on the 3.5mm headphone jack. Offline asset downsampling applies a $-3\text{ dB}$ attenuation factor ($(L+R) \times 0.707$) during conversion to prevent acoustic clipping during multi-explosion waves.

#### B. OpenMPT Dynamic Mono Streaming (`OpenMptMusicInterface.cpp`)
*   Update channel constant in `OpenMptMusicInterface.hpp`:
    ```cpp
    static constexpr size_t kNumChannels = 1;
    typedef size_t (*openmpt_module_read_mono_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* mono);
    ```
*   In `OpenMptMusicInterface` class declaration, add member:
    ```cpp
    openmpt_module_read_mono_fn mFnReadMono = nullptr;
    ```
*   In `OpenMptMusicInterface::TryLoadOpenMptSymbols()`, resolve dynamically:
    ```cpp
    mFnReadMono = reinterpret_cast<openmpt_module_read_mono_fn>(DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono"));
    if (!mFnReadMono) {
        Sexy::LogInfoLn("Failed to bind openmpt_module_read_mono symbol");
    }
    ```
*   In `RenderAudioChunk()`, execute tracker mixing in mono:
    ```cpp
    size_t count = mFnReadMono(song.openmptMod, static_cast<int32_t>(kSampleRate), frameCount, monoOutput);
    ```
*   Eliminates stereo channel interleaving and cuts tracker mixing arithmetic on CPU Core 0 by 50%.

### 3. State-Boundary Heap Reclamation & Allocator Tuning

Insert guarded `malloc_trim(0)` calls at quiescent execution boundaries, guarded with glibc checks to guarantee portability across musl and bionic toolchains:
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

*   **Heap Mmap Threshold**: In `LawnApp::Init()`, set `mallopt(M_MMAP_THRESHOLD, 64 * 1024)` to ensure temporary buffers >64 KB are returned immediately to the OS upon `delete[]` rather than lingering in the glibc brk arena.
*   **State-Boundary Hooks**:
    1.  `LawnApp::LoadingThreadProc` (immediately upon loading completion).
    2.  `LawnApp::KillStoreScreen` (after disposing `DelayLoad_Store`).
    3.  `AlmanacDialog::~AlmanacDialog` in `src/Lawn/Widget/AlmanacDialog.cpp` (after freeing `DelayLoad_Almanac`).
    4.  `LawnApp::EndLevel` and `Board::DisposeBoard` (after entity teardown).

### 4. Offline Asset Converter Pipeline (`src/setup/setup_converter.c`)

1.  **Opaque Background Encoding (RGB565 with 4×4 Bayer Dithering):**
    *   Input textures matching `images/background*` or full-screen panels are converted to RGB565.
    *   Apply a 4×4 Bayer ordered dithering matrix to avoid banding on color gradients:
        $$M = \begin{bmatrix} 0 & 8 & 2 & 10 \\ 12 & 4 & 14 & 6 \\ 3 & 11 & 1 & 9 \\ 15 & 7 & 13 & 5 \end{bmatrix}$$
    *   Algorithmic dithering and clamping per pixel $(x, y)$:
        $$R_5 = \text{clamp}\left(R_8 + \left(\frac{M[y\%4][x\%4] - 8}{2}\right), 0, 255\right) \gg 3$$
        $$G_6 = \text{clamp}\left(G_8 + \left(\frac{M[y\%4][x\%4] - 8}{4}\right), 0, 255\right) \gg 2$$
        $$B_5 = \text{clamp}\left(B_8 + \left(\frac{M[y\%4][x\%4] - 8}{2}\right), 0, 255\right) \gg 3$$
    *   Encode into 16-bit word: `(R_5 << 11) | (G_6 << 5) | B_5`.

2.  **Sprite & Reanimation Encoding (Straight-Alpha ARGB4444 with Dithered Alpha):**
    *   To prevent double alpha pre-multiplication in standard `SWTri.cpp` rasterization loops, sprites and reanimations are encoded offline with **straight-alpha** color channels:
        $$A_4 = \text{clamp}\left(A_8 + (M[y\%4][x\%4] - 8), 0, 255\right) \gg 4$$
        $$R_4 = \text{clamp}\left(R_8 + (M[y\%4][x\%4] - 8), 0, 255\right) \gg 4$$
        $$G_4 = \text{clamp}\left(G_8 + (M[y\%4][x\%4] - 8), 0, 255\right) \gg 4$$
        $$B_4 = \text{clamp}\left(B_8 + (M[y\%4][x\%4] - 8), 0, 255\right) \gg 4$$
    *   Pack into 16-bit word: `(A_4 << 12) | (R_4 << 8) | (G_4 << 4) | B_4`.
    *   Pre-multiplied accumulation is strictly isolated to dedicated NEON blit kernels (`NeonBlit_Span_ARGB4444_PreMul`).

3.  **Offline Batch Audio Transcoding:**
    *   All WAV and sound assets in `main.pak` are transcoded offline to 22,050 Hz Mono 16-bit PCM WAV with $-3\text{ dB}$ attenuation using FFmpeg:
        `ffmpeg -i input.wav -ac 1 -ar 22050 -sample_fmt s16 -af "volume=-3dB" output.wav`

4.  **Font Preservation:**
    *   Assets in `data/fonts` are strictly preserved in 32-bit ARGB8888.

### 5. Software Rasterizer & NEON Blitting (`SWTri` & `NeonBlit.cpp`)

#### A. Triangle & Quad Texel Sampling (`SWTri_GetTexel.inc`)
*   Inspect `textureInfo->format`. When sampling from `FMT_ARGB4444` (straight-alpha):
    ```cpp
    const uint16_t* pTexture16 = reinterpret_cast<const uint16_t*>(textureInfo->pTexture);
    uint16_t tex16 = pTexture16[t_pos];
    unsigned int a = (tex16 >> 12) & 0x0F;
    unsigned int r = (tex16 >> 8)  & 0x0F;
    unsigned int g = (tex16 >> 4)  & 0x0F;
    unsigned int b = tex16         & 0x0F;
    tex = ((a | (a << 4)) << 24) | ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
    ```
*   `SWTri.cpp` standard span loops perform unmodified straight-alpha composition:
    $$\text{Dst} = \frac{\text{Src} \cdot \text{Src}_A + \text{Dst} \cdot (255 - \text{Src}_A)}{255}$$

#### B. Vectorized ARM NEON Span Kernels (`NeonBlit.cpp`)
*   `NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count)`:
    *   Loads 8 RGB565 pixels (16 bytes) into a 128-bit register using `vld1_u16`.
    *   Unpacks 5-bit and 6-bit channels to 8-bit channels using `vshrn` and `vshl`, setting alpha to 0xFF.
    *   Stores 8 32-bit ARGB8888 pixels into `mScreenImage` in ~0.35 ms per frame.
*   `NeonBlit_Span_ARGB4444_PreMul(uint32_t* dst, const uint16_t* src, size_t count)`:
    *   Loads 8 ARGB4444 texels into vector register.
    *   Expands 4-bit nibbles and pre-multiplies color channels inside registers.
    *   Performs SIMD pre-multiplied blend across 8 pixels simultaneously in <0.28 ms per frame.

### 6. Architectural Data Flow Diagram

```mermaid
flowchart TD
    subgraph BuildTime ["Offline Asset Build (setup_converter.c)"]
        RawAssets["Raw 32-Bit Assets (PNG)"]
        FontFilter{"Asset Type?"}
        RawAssets --> FontFilter
        
        FontFilter -->|Backgrounds| DitherBayer["Bayer 4x4 Ordered Dither<br/>Encode RGB565 (2B/px)"]
        FontFilter -->|Sprites & Reanim| StraightQuant["Straight-Alpha Quantization<br/>Encode ARGB4444 (2B/px)"]
        FontFilter -->|Bitmap Fonts| Preserve32["Preserve ARGB8888<br/>(4B/px, <800 KB)"]
        
        DitherBayer --> PakFile["main_mmp.pak"]
        StraightQuant --> PakFile
        Preserve32 --> PakFile

        RawAudio["Raw Audio (WAV/OGG)"] -->|FFmpeg -ac 1 -ar 22050| MonoWav["22.05 kHz Mono 16-Bit WAV<br/>(-3 dB Headroom)"]
        MonoWav --> PakFile
    end

    subgraph AudioEngine ["Audio Subsystem (22.05 kHz Mono)"]
        PakFile -->|Mix_LoadWAV_RW| SfxChunks["PCM Chunks in RAM<br/>(22.05 kHz Mono, ~4 MB)"]
        MusicTracker["Mo3 / XM Tracker Music"] -->|openmpt_module_read_mono<br/>(Dynamic dlsym)| MusicStream["Streaming Buffer<br/>(1024 frames, ~2 KB)"]
        
        SfxChunks --> MixCallback["SDL_mixer Audio Callback"]
        MusicStream --> MixCallback
        MixCallback --> Speaker["Console Mono DAC / 3.5mm Jack<br/>(Dual-Mono Duplicated)"]
    end

    subgraph RuntimeEngine ["PvZ Graphics Engine (Miyoo Mini Plus)"]
        PakFile -->|DMA Allocate| DmaPool["DmaTexturePool (8-10 MB)<br/>(Reanimation Atlases)"]
        DmaPool --> Surfaces["SoftwareSurface / MemoryImage"]
        
        Surfaces -->|RGB565| BgBlit["NeonBlit_Span_RGB565_to_ARGB8888<br/>(0.35 ms Unpack to Backbuffer)"]
        Surfaces -->|ARGB4444| TexRaster["SWTri / NeonBlit SIMD<br/>(Straight Unpack & PreMul Blit)"]
        Surfaces -->|ARGB8888| FontRaster["Glyph Rasterizer<br/>(Pixel-Sharp Text)"]
        
        BgBlit --> Backbuffer["mScreenImage<br/>(32-bit ARGB8888 SoftwareSurface)"]
        TexRaster --> Backbuffer
        FontRaster --> Backbuffer
        Backbuffer --> FB0["SDL2 / libneonarmmiyoo.so -> /dev/fb0"]
    end
    
    subgraph MemoryReclamation ["Total Memory Stabilization"]
        FB0 -.-> RSS["Peak Combat RSS: ~51 MB<br/>(Safety Buffer: >32 MB below OOM Ceil)"]
        SfxChunks -.-> RSS
        TrimHook["malloc_trim(0) at State Transitions<br/>mallopt(M_MMAP_THRESHOLD, 64KB)"] -.->|Purge Dirty Heap| RSS
    end
```

--------------------------------------------------------------------------------

## Alternatives Considered

### Alternative A: 8-Bit Paletted / Indexed Sprites
*   **Pros**: Saves 1 additional byte per pixel on dynamic sprites.
*   **Cons**: Cortex-A7 (ARMv7-A) lacks SIMD vector gather instructions (`vgather`). The `vtbl` instruction is limited to 32 bytes (32 colors), whereas a 256-color palette requires 512–1024 bytes. Looking up colors requires scalar loops, dropping frame rates from 60 FPS to 15–20 FPS during heavy combat.
*   **Reason for Rejection**: Rejected by Author. Severe CPU performance collapse makes 8-bit paletted textures unviable for dynamic reanimations with affine transformations.

### Alternative B: Direct `/dev/fb0` Scanline Assembly
*   **Pros**: Bypasses intermediate SDL display surfaces.
*   **Cons**: PvZ already renders into cached RAM buffers (`mScreenImage`); writing directly to `/dev/fb0` bypasses `libneonarmmiyoo.so`, breaking VSync, causing screen tearing, and inducing black-screen lockups on game exit.
*   **Reason for Rejection**: Rejected by Author. SDL2 + `libneonarmmiyoo.so` provides stable, tear-free presentation with clean exit routines.

### Alternative C: 22.05 kHz Stereo Audio
*   **Pros**: Preserves horizontal spatial stereo panning.
*   **Cons**: Console hardware features a single physical mono speaker transducer. Stereo separation is physically impossible in handheld play; maintaining 2 channels consumes ~4 MB additional RAM and doubles bus traffic for imperceptible acoustic benefit.
*   **Reason for Rejection**: Rejected by Author. Dual-mono routing handles headphones cleanly; mono audio maximizes physical RAM reduction.

### Alternative D: MicroSD Swapfile / ZRAM Buffer
*   **Pros**: Absorbs dynamic memory spikes without modifying texture or audio data structures.
*   **Cons**: Induces massive I/O wait latency spikes (frame rates drop from 60 FPS to 5–10 FPS); causes NAND flash wear leading to microSD card corruption.
*   **Reason for Rejection**: Rejected by Author. Platform engineering rules strictly prohibit swap on all console projects to safeguard storage integrity and eliminate frame stutter.

### Alternative E: Downscaling Assets to 50% Resolution (0.4x Scale)
*   **Pros**: Cuts texture memory by 75%.
*   **Cons**: Results in severe visual blurriness, blurry edges, and illegible text on the 640×480 3.5" display.
*   **Reason for Rejection**: Degrades visual fidelity unacceptably. 16-bit color quantization delivers the required memory reduction while preserving native 640×480 spatial pixel sharpness.

--------------------------------------------------------------------------------

## Non-Functional Requirements

### Performance & Framerate
*   The game must sustain locked 60.0 FPS frame delivery during intense combat waves on the dual-core Cortex-A7 CPU without frame dropping.
*   ARM NEON vectorized kernels must execute ARGB4444 span decoding and pre-multiplied blending within <0.30 ms per full frame.
*   `NeonBlit_Span_RGB565_to_ARGB8888` must unpack the full 640×480 background surface in $\le 0.35\text{ ms}$.
*   Audio callback execution must consume <2% CPU Core 0 time per tick at 22,050 Hz Mono.

### Memory & Stability
*   Peak combat Resident Set Size (RSS) must not exceed 53.0 MB under maximum entity load (Survival: Endless flags 20+).
*   Zero swap memory engagement: The Linux kernel must never invoke memory compaction, page-cache reclaim onto disk, or swap paging.
*   A permanent safety margin of $\ge 30.0\text{ MB}$ must be preserved below the 83 MB kernel OOM killer threshold.

### Storage & Longevity
*   Zero swapfiles or swap partitions on `/mnt/SDCARD`.
*   All asset quantization and audio downmixing must be completed offline during PAK build to eliminate SD card read-write spikes and heap allocations during gameplay.

--------------------------------------------------------------------------------

## Detailed Implementation Plan

### Target Files to Create/Modify

#### 1. `src/SexyAppFramework/sound/SDLSoundManager.cpp` (Modify)
*   **Type / Scope**: C++ Source (Audio Engine)
*   **Proposed Changes**:
    *   Update `Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024)` in constructor.
    *   Ensure `mMixerChannels = 1` and `mMixerFreq = 22050`.
*   **Rationale**: Configures SDL_mixer to decode all sound effects into 22.05 kHz Mono PCM, cutting audio chunk RAM by 75% (~9–12 MB savings).

#### 2. `src/audio/OpenMptMusicInterface.hpp` & `OpenMptMusicInterface.cpp` (Modify)
*   **Type / Scope**: C++ Header & Implementation (Tracker Music)
*   **Proposed Changes**:
    *   Set `static constexpr size_t kNumChannels = 1;`.
    *   Declare `typedef size_t (*openmpt_module_read_mono_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* mono);` and member `openmpt_module_read_mono_fn mFnReadMono = nullptr;`.
    *   In `TryLoadOpenMptSymbols()`, dynamically resolve `mFnReadMono` via `DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_mono")`.
    *   In `RenderAudioChunk()`, call `mFnReadMono` into `monoOutput`.
    *   Update `SdlMusicCallback` frame length calculations for 1-channel mono streaming.
*   **Rationale**: Unifies tracker music output with the sound manager clock and avoids undefined symbols when `libopenmpt.so` is loaded dynamically.

#### 3. `src/LawnApp.cpp`, `src/Lawn/Board.cpp`, & `src/Lawn/Widget/AlmanacDialog.cpp` (Modify)
*   **Type / Scope**: C++ Game Logic
*   **Proposed Changes**:
    *   In `LawnApp.cpp`, configure `mallopt(M_MMAP_THRESHOLD, 64 * 1024)` during startup.
    *   Insert guarded `TrimHeapIfLinux()` (`malloc_trim(0)`) protected with `#if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)` at loading completion in `LoadingThreadProc`, after store exit in `KillStoreScreen`, after level completion in `EndLevel`, and in `AlmanacDialog::~AlmanacDialog` (`src/Lawn/Widget/AlmanacDialog.cpp`).
*   **Rationale**: Reclaims fragmented heap pages without incurring in-combat frame drops or breaking non-glibc toolchains.

#### 4. `src/graphics/SoftwareRenderer.hpp` & `SoftwareRenderer.cpp` (Modify & Fix)
*   **Type / Scope**: C++ Rendering Engine
*   **Proposed Changes**:
    *   Define `enum class PixelFormat : uint8_t { FMT_ARGB8888, FMT_RGB565, FMT_ARGB4444 };`.
    *   Add `PixelFormat mFormat` and dynamic `mBytesPerPixel` to `SoftwareSurface`.
    *   Update line 673 guard: `if (!mCurrentTarget || !texture || mCurrentTarget->mBytesPerPixel != 4 || (texture->mBytesPerPixel != 4 && texture->mBytesPerPixel != 2) || mPhysClipRect.IsEmpty()) return;`.
    *   Fix `SoftwareSurface::GetPixel` and `SetPixel` (lines 164–186) to unpack `FMT_ARGB4444` nibbles rather than assuming RGB565.
    *   In `SoftwareRenderer::DrawImage`, branch on `image->mFormat` to dispatch to `NeonBlit_Span_ARGB4444_PreMul` or `NeonBlit_Span_RGB565_to_ARGB8888`.
    *   Eliminate `SpriteCachePool` (reclaiming 12 MB) and downsize `DmaTexturePool` to 8–10 MB.
*   **Rationale**: Fixes the 16-bit texture invisibility bug and prevents memory arena triplication.

#### 5. `src/SexyAppFramework/graphics/MemoryImage.h` & `MemoryImage.cpp` (Verification of Existing Implementation)
*   **Type / Scope**: C++ Image Abstraction
*   **Proposed Changes**:
    *   Verify existing `PixelFormat mPixelFormat` in `MemoryImage.h:70`.
    *   Add typed 16-bit accessor: `uint16_t* GetBits16() const;`.
*   **Rationale**: Confirms existing format tagging and provides type-safe buffer access.

#### 6. `src/PvzpLib/ReanimAtlas.cpp` (Verification of Existing Implementation)
*   **Type / Scope**: C++ Reanimation Subsystem
*   **Proposed Changes**:
    *   Verify existing 16-bit buffer allocation in `ReanimAtlasMakeBlankMemoryImage`.
    *   Document `+ 2` padding bytes as ARM NEON vector over-read protection.
*   **Rationale**: Ensures 16-bit reanimations allocate with SIMD over-read safety.

#### 7. `src/SexyAppFramework/graphics/SWTri/SWTri_GetTexel.inc` & `SWTri.h` (Verification & Fix)
*   **Type / Scope**: C++ Software Rasterizer
*   **Proposed Changes**:
    *   Confirm existing `FMT_ARGB4444` straight-alpha unpacking in `SWTri_GetTexel.inc`.
    *   Retain standard straight-alpha rasterization formulas in `SWTri.cpp` to prevent double alpha multiplication and preserve color tinting.
*   **Rationale**: Provides high-performance software rasterization of 16-bit textures without visual darkening or black halos.

#### 8. `src/graphics/NeonBlit.hpp` & `src/graphics/NeonBlit.cpp` (Verification & Kernels)
*   **Type / Scope**: ARM NEON Optimization
*   **Proposed Changes**:
    *   Implement `NeonBlit_Span_RGB565_to_ARGB8888(uint32_t* dst, const uint16_t* src, size_t count)` to unpack background spans into the 32-bit `mScreenImage`.
    *   Verify `NeonBlit_Span_ARGB4444_PreMul` using NEON 128-bit vector instructions (`vld1_u16`, `vshrn_n_u16`, `vshlq_n_u16`) to unpack and composite 8 pixels simultaneously.
*   **Rationale**: Accelerates 16-bit compositing on Cortex-A7 to maintain locked 60.0 FPS.

#### 9. `src/setup/setup_converter.c` (Modify)
*   **Type / Scope**: C Offline Asset Converter
*   **Proposed Changes**:
    *   Add 4×4 Bayer matrix ordered dithering with full color range clamping when writing RGB565 background assets.
    *   Add straight-alpha ARGB4444 quantization with dithered alpha channels for sprites, reanimations, and UI elements.
    *   Exempt `data/fonts` assets to preserve crisp 32-bit typography.
*   **Rationale**: Pre-processes all assets into optimal 16-bit payloads offline, eliminating runtime conversion latency and RAM spikes.

### Verification Plan

*   **Compilation / Build**:
    *   Compile the native ARMv7 binary via Docker container (`miyoo-build:latest`) using `cmake --build build --target PvZ -j$(nproc)`.
    *   Verify clean build with zero warnings or linker errors.
*   **Automated Test Suite**:
    *   Update `tests/test_openmpt_playback.cpp` assertions for mono (`kChunk * 1` frame counts at lines 208 and 271).
    *   Execute automated test suite via `ctest --output-on-failure`.
*   **Audio Driver Synchronization Verification**:
    *   Verify SDL_mixer queries confirm `22050 Hz`, `AUDIO_S16SYS`, `channels = 1`.
    *   Verify tracker music and sound effects play back concurrently without audible pitch shifts, underruns, or clicks.
*   **Static Memory Audit**:
    *   Confirm steady-state combat RSS $\le 53.0\text{ MB}$ and safety headroom $\ge 30.0\text{ MB}$ below kernel ceiling.
*   **Runtime Stress Verification (Hardware & Docker)**:
    *   Execute Survival: Endless Level 5-10 with multiple active zombies and verify with `getrusage` / `/proc/self/statm` that RSS remains between 48 and 53 MB.
    *   Trigger Jalapeno detonation against Zamboni (`REANIM_ZOMBIE_CHARRED_ZAMBONI`) to confirm zero OOM crashes and immediate atlas allocation within the expanded >30 MB margin.
    *   Verify that `malloc_trim(0)` execution during state transitions incurs 0 ms delay during active gameplay.

--------------------------------------------------------------------------------

*Drafted with EGM*
