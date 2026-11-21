# Vasebreaker Save-Load Crash Elimination & Reanimation Hardening

* **Author:** Francisco
* **Date:** 2026-11-21

## Overview

During save restoration in Vasebreaker (Rompejarrones) on the Miyoo Mini Plus handheld, the game encounters a fatal segmentation fault (Signal 11) at small address handles like `0x2f1` (decimal 753) and unmapped addresses like `0xdb842`. 

This failure occurs because:
1. `ReanimAtlas::ReanimAtlasCreate` stores integer atlas indices into typed pointer fields (`ReanimatorTransform::mImage = (Image*)(aImageIndex + 1)`).
2. Restored entities undergoing save restoration bypass the pre-baked sprite cache due to an artificial warmup countdown (`mWarmupCounter = 2`), forcing execution down the fallback raw reanimation drawing pipeline (`Reanimation::DrawTrack`).
3. In `Reanimation::DrawTrack`, when an image pointer is null or unassigned, execution falls through to text rendering. In restored or un-sanitized entities, transient text pointers (`aTransform.mText`) contain stale numeric handles (such as `0x2f1`), which are unconditionally dereferenced by `*aTransform.mText != '\0'`, causing an instant crash.
4. Calling `ReanimAtlasCreate` or `ReanimationFreeDefinition` on already-encoded definitions treats integer handles as valid pointers, causing secondary crashes in `aImage->mWidth` and corrupting memory management inside `RemoveDynamicImage`.

This brief defines a lean, zero-RAM architectural fix that guarantees complete crash resilience on the Miyoo Mini Plus (ARMv7 Cortex-A7 @ 1.2 GHz, 128 MB RAM, hard Linux OOM ceiling at ~83 MB RSS, combat RSS $\le 54.5$ MB).

## Analysis

Seven specialized Goldfish review lenses analyzed the failure mechanism and the initial proposal. The consensus findings and contradiction resolutions are synthesized below:

### 1. Inventive Contradictions Resolved

* **Contradiction 1: Cold-Start Warmup vs. Save Restoration Stability**
  * *Conflict:* Setting `mWarmupCounter = 2` on restored plants was intended to allow animation blending, but `SpriteCachePool::PreloadSynchronous` already bakes valid idle frames into RAM before gameplay resumes. Forcing a 2-frame cache bypass on restored plants exposes execution to the unbuffered, non-cached `DrawTrack` fallback path on frame 0, triggering the crash. Furthermore, `Plant::CanUseSpriteCache()` decremented `mWarmupCounter` inside a query function called multiple times per tick (in both `Plant::Animate()` and `Plant::Draw()`), introducing impure state transitions.
  * *Resolution (Preliminary Action & Idempotency):* For plants whose sprite cache is pre-baked and ready upon load (`SpriteCachePool::IsPlantReady(aPlant->mSeedType)`), initialize `mWarmupCounter = 0`. Restored plants immediately render from pre-baked sprite sheets on frame 0, eliminating cold-start fallback execution. Move any remaining warmup decrementing strictly into `Plant::Update()`.

* **Contradiction 2: Zero-RAM In-Place Handle Encoding vs. Definition Immutability**
  * *Conflict:* Re-using `mImage` pointers to store atlas indices (`1..1000`) avoids heap allocations, but mutating global `ReanimatorDefinition` objects destroys idempotency. If an atlas is created a second time (e.g. during save loading or level restart), `ReanimAtlasCreate` dereferences `(Image*)0x2f1->mWidth`, crashing immediately. Additionally, `ReanimationFreeDefinition` passes `(Image*)0x2f1` to `RemoveDynamicImage`, corrupting the resource manager.
  * *Resolution (Self-Service & Extraction):* 
    * Add an explicit definition state flag (`bool mIsAtlasEncoded`) or check `reinterpret_cast<uintptr_t>(aImage) <= 1024`.
    * If a definition is already atlas-encoded, `ReanimAtlasCreate` immediately aborts or reloads pristine definitions from ROM/XML rather than re-encoding corrupted handles.
    * In `ReanimationFreeDefinition`, skip any image handle where `reinterpret_cast<uintptr_t>(aTransform.mImage) <= 1024` so invalid handles are never sent to `RemoveDynamicImage`.

* **Contradiction 3: Defensive Blitter Guard Proliferation vs. ARMv7 Hot-Path Performance**
  * *Conflict:* Scattering magic number checks (`> 0x10000`, `val % 4 != 0`) across innermost drawing loops (`PvzpBltMatrix`, `ReanimBltMatrix`, `DrawTrack`) treats corruption symptoms at the leaves, penalizing instruction caches on the 1.2 GHz Cortex-A7 and causing code duplication.
  * *Resolution (Boundary Enforcement & Lean Gatekeeping):* Centralize handle and pointer safety boundaries:
    * In `SaveGame.cpp` deserialization (`SyncReanimationPortable`), explicitly set `mText = nullptr` and `mFont = nullptr` on all restored transforms, because text and font pointers are runtime transients that are never valid after serialization.
    * In `Reanimation::DrawTrack` and `GetTrackMatrix`, gate font and text dereferencing with an inline boundary check: `reinterpret_cast<uintptr_t>(aTransform.mFont) >= 0x10000 && reinterpret_cast<uintptr_t>(aTransform.mText) >= 0x10000` before accessing `*aTransform.mText` or calling `mFont->StringWidth`.
    * Reuse the existing `IsValidImagePtr` helper rather than introducing disconnected ad-hoc bounds across rendering functions.

### 2. Secondary Trade-offs & Operational Diagnostics

* **Trade-off:** Silently skipping corrupt or invalid handles prevents crashes but could mask visual bugs (e.g. missing vase parts or blank plants).
* **Mitigation:** Under `PVZP_DEBUG` and via rate-limited operational tracing (`PvzpTraceWithoutSpamming`), log rejected pointer values, track indices, and reanimation types so pipeline anomalies remain observable without spamming terminal output or eating CPU cycles in release builds.
* **Auxiliary Reanimations:** Sanitize static shared reanimations in puzzle mode (such as `GridItem::sSharedSunReanim` used in transparent vases) to prevent dangling references across level reloads.

## Goals vs. Non-Goals

### In-Scope Goals

1. **Eliminate All Signal 11 Crashes in Save Loading:** Prevent pointer dereferences of atlas handles (`0x2f1`, etc.) and garbage text/font pointers during Vasebreaker and general save file hydration.
2. **Make `ReanimAtlasCreate` and `ReanimationFreeDefinition` Idempotent & Safe:** Guard against re-encoding already-encoded definitions and prevent passing integer handles to `RemoveDynamicImage`.
3. **Clean Deserialization State Invariants:** Ensure all restored reanimation transforms have clean `nullptr` font/text pointers upon load in `SaveGame.cpp`.
4. **Immediate Frame-0 Sprite Cache Dispatch:** For plants preloaded in `SpriteCachePool`, set `mWarmupCounter = 0` so frame 0 renders directly from the sprite cache without evaluating raw reanimations.
5. **Zero Dynamic RAM Growth:** Maintain strict compliance with the $\le 54.5$ MB combat RSS budget on Miyoo Mini Plus with 0 bytes of dynamic memory allocation.

### Out-of-Scope Non-Goals

1. **Save File Schema Changes:** No changes to save file TLV chunk formats or binary layout. All migrations and sanitization take place in RAM.
2. **Replacing In-Place Atlas Index Encoding:** We will not allocate heap tables or hash maps to store atlas indices; the integer handle encoding pattern remains, but is strictly isolated and validated.
3. **Rewriting Software Rasterizers:** No structural changes to `PvzpBltMatrix` or `GLImage::BltMatrix`; safety checks are applied at the transform and dispatch boundary.

## Alternatives Weighed

1. **Alternative: Scatter Handle Checks Across All Low-Level Blitters (`PvzpBltMatrix`, `GLImage::BltMatrix`, `ReanimBltMatrix`)**
   * *Rejected by author:* Littering low-level rasterization primitives with domain-specific integer handle bounds checks degrades CPU cache performance on the Cortex-A7 and masks upstream state corruption. Safety belongs at the transform interpolation and dispatch stage.
2. **Alternative: Dynamically Allocate External Atlas Index Lookup Tables**
   * *Rejected by author:* Allocating auxiliary lookup tables for each reanimation definition introduces heap fragmentation and increases combat RSS, threatening the strict 54.5 MB budget and risking Linux OOM termination.
3. **Alternative: Re-run Full Atlas Generation on Every Restored Entity in `FixBoardAfterLoad`**
   * *Rejected by author:* Definitions are globally shared across all entities of that type. Blindly calling `ReanimationCreateAtlas` per restored entity in `FixBoardAfterLoad` repeatedly triggers the double-encoding bug and spikes CPU load during loading. Definitions must be verified once at the definition level.

## Key Decisions & Staging for Next Steps

1. **Decision 1: Text and Font Pointer Boundary Check**
   * In `src/PvzpLib/Reanimator.cpp` (`DrawTrack` lines 963 and 1099, `GetTrackMatrix` line 1191), require `reinterpret_cast<uintptr_t>(aTransform.mFont) >= 0x10000 && reinterpret_cast<uintptr_t>(aTransform.mText) >= 0x10000` before evaluating `*aTransform.mText != '\0'` or invoking `mFont` methods.
2. **Decision 2: ReanimAtlas Life-Cycle Hardening**
   * In `src/PvzpLib/ReanimAtlas.cpp` (`ReanimAtlasCreate`), check if the definition is already atlas-encoded or if `aImage` handles are $\le 1024$. If so, do not re-encode.
   * In `src/PvzpLib/Reanimator.cpp` (`ReanimationFreeDefinition`), skip `anImage` if `reinterpret_cast<uintptr_t>(anImage) <= 1024` before invoking `RemoveDynamicImage`.
3. **Decision 3: Frame-0 Sprite Cache Prime in `FixBoardAfterLoad`**
   * In `src/Lawn/System/SaveGame.cpp` (`FixBoardAfterLoad`), set `aPlant->mWarmupCounter = 0` if `SpriteCachePool::IsPlantReady(aPlant->mSeedType)` is true, allowing restored plants to use the sprite cache immediately on frame 0.
   * In `src/Lawn/Plant.cpp`, remove the mutating decrement from `CanUseSpriteCache()`, making it a pure idempotent query, and decrement `mWarmupCounter` solely inside `Plant::Update()`.
4. **Decision 4: Deserialization Transform Sanitization**
   * In `src/Lawn/System/SaveGame.cpp` (`SyncReanimationPortable`), ensure restored transform instances and blend transforms have `mText = nullptr` and `mFont = nullptr`.
   * Reset `GridItem::sSharedSunReanim` upon board reset/load to prevent stale pointer references.
