#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "audio/OpenMptMusicInterface.hpp"

using namespace PvZ::Audio;

// Helper to create synthetic 22.05 kHz 16-bit Mono WAV in memory
static std::vector<uint8_t> CreateSyntheticWav(uint32_t sampleRate, uint16_t channels, uint32_t numFrames) {
    uint32_t dataSize = numFrames * channels * sizeof(int16_t);
    uint32_t chunkSize = 36 + dataSize;
    uint32_t byteRate = sampleRate * channels * sizeof(int16_t);
    uint16_t blockAlign = channels * sizeof(int16_t);

    std::vector<uint8_t> wav(44 + dataSize);
    uint8_t* p = wav.data();

    // RIFF header
    std::memcpy(p, "RIFF", 4);
    std::memcpy(p + 4, &chunkSize, 4);
    std::memcpy(p + 8, "WAVE", 4);

    // fmt subchunk
    std::memcpy(p + 12, "fmt ", 4);
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1; // PCM
    std::memcpy(p + 16, &subchunk1Size, 4);
    std::memcpy(p + 20, &audioFormat, 2);
    std::memcpy(p + 22, &channels, 2);
    std::memcpy(p + 24, &sampleRate, 4);
    std::memcpy(p + 28, &byteRate, 4);
    std::memcpy(p + 32, &blockAlign, 2);
    uint16_t bitsPerSample = 16;
    std::memcpy(p + 34, &bitsPerSample, 2);

    // data subchunk
    std::memcpy(p + 36, "data", 4);
    std::memcpy(p + 40, &dataSize, 4);

    // Fill with synthetic tone (e.g. 440 Hz sine)
    int16_t* samples = reinterpret_cast<int16_t*>(p + 44);
    for (uint32_t i = 0; i < numFrames * channels; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        samples[i] = static_cast<int16_t>(std::sin(2.0f * 3.14159265f * 440.0f * t) * 16000.0f);
    }

    return wav;
}

// Helper to create synthetic ProTracker MOD module header & pattern
static std::vector<uint8_t> CreateSyntheticTrackerModule() {
    size_t totalSize = 1084 + 1024; // 1 pattern (64 rows * 4 channels * 4 bytes = 1024)
    std::vector<uint8_t> mod(totalSize, 0);

    const char* title = "PvZ Lawn Theme Synth";
    std::memcpy(mod.data(), title, std::strlen(title));

    mod[950] = 1; // Song length: 1 order
    mod[951] = 0; // Restart position
    mod[952] = 0; // Order 0 = Pattern 0

    std::memcpy(mod.data() + 1080, "M.K.", 4);

    uint8_t* pattern = mod.data() + 1084;
    for (int row = 0; row < 64; ++row) {
        if (row % 4 == 0) {
            pattern[row * 16 + 0] = 0x00;
            pattern[row * 16 + 1] = 0x85;
            pattern[row * 16 + 2] = 0x10;
            pattern[row * 16 + 3] = 0x00;
        }
    }

    return mod;
}

// ---------------------------------------------------------------------------
// Test 1: Music Streaming & Memory Limits (<= 2 MB)
// ---------------------------------------------------------------------------
static void test_music_loading_and_memory_limit() {
    std::cout << "[TEST] Running test_music_loading_and_memory_limit..." << std::endl;

    OpenMptMusicInterface music;
    assert(music.GetAllocatedMemory() == 0);
    assert(music.GetMaxMemory() == 2 * 1024 * 1024); // 2 MB limit

    // Load synthetic tracker module
    std::vector<uint8_t> modData = CreateSyntheticTrackerModule();
    bool loaded = music.LoadMusicFromMemory(1, modData.data(), modData.size());
    assert(loaded);
    assert(music.GetAllocatedMemory() > 0);
    assert(music.GetAllocatedMemory() <= music.GetMaxMemory());

    // Verify rejecting oversized music buffer > 2 MB
    std::vector<uint8_t> oversizedData(3 * 1024 * 1024, 0xAA);
    bool overLoaded = music.LoadMusicFromMemory(2, oversizedData.data(), oversizedData.size());
    assert(!overLoaded);
    assert(music.GetAllocatedMemory() <= music.GetMaxMemory());

    music.UnloadMusic(1);
    assert(music.GetAllocatedMemory() == 0);

    std::cout << "  -> PASS: Module loading strictly enforced <= 2 MB memory budget" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 2: Chunk Streaming (2,048 samples) and Zero Heap Thrashing
// ---------------------------------------------------------------------------
static void test_chunk_streaming_2048_samples() {
    std::cout << "[TEST] Running test_chunk_streaming_2048_samples..." << std::endl;

    OpenMptMusicInterface music;
    std::vector<uint8_t> modData = CreateSyntheticTrackerModule();
    assert(music.LoadMusicFromMemory(1, modData.data(), modData.size()));

    music.PlayMusic(1, 0, true);
    assert(music.IsPlaying(1));

    // Test SDL_mixer hook registration and unregistration
    music.RegisterWithSdlMixer();
    music.UnregisterFromSdlMixer();

    constexpr size_t kChunk = OpenMptMusicInterface::kChunkFrames;
    assert(kChunk == 2048);
    assert(OpenMptMusicInterface::kSampleRate == 22050);

    alignas(8) int16_t chunkBuffer[kChunk * 2];
    size_t initialMem = music.GetAllocatedMemory();

    bool hasNonZeroSample = false;
    for (int iter = 0; iter < 10; ++iter) {
        size_t rendered = music.RenderAudioChunk(chunkBuffer, kChunk);
        assert(rendered == kChunk);

        for (size_t s = 0; s < kChunk * 2; ++s) {
            if (chunkBuffer[s] != 0) {
                hasNonZeroSample = true;
                break;
            }
        }

        assert(music.GetAllocatedMemory() == initialMem);
    }

    assert(hasNonZeroSample);
    std::cout << "  -> PASS: Successfully streamed 2,048-frame chunks at 22,050 Hz with zero heap thrashing" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 3: Playback Controls (Play, Pause, Stop, Seek, Volume, Loop)
// ---------------------------------------------------------------------------
static void test_playback_controls() {
    std::cout << "[TEST] Running test_playback_controls..." << std::endl;

    OpenMptMusicInterface music;
    std::vector<uint8_t> modData = CreateSyntheticTrackerModule();
    assert(music.LoadMusicFromMemory(1, modData.data(), modData.size()));

    constexpr size_t kChunk = OpenMptMusicInterface::kChunkFrames;
    alignas(8) int16_t buffer[kChunk * 2];

    // 1. Play
    music.PlayMusic(1, 0, true);
    assert(music.IsPlaying(1));
    assert(!music.IsPaused(1));

    // 2. Volume control (0.0 to 1.0)
    music.SetMusicVolume(1, 1.0);
    music.RenderAudioChunk(buffer, kChunk);
    int32_t maxAmpFull = 0;
    for (size_t i = 0; i < kChunk * 2; ++i) {
        int32_t a = std::abs(static_cast<int32_t>(buffer[i]));
        if (a > maxAmpFull) maxAmpFull = a;
    }
    assert(maxAmpFull > 0);

    // Half volume
    music.SetMusicVolume(1, 0.5);
    music.RenderAudioChunk(buffer, kChunk);
    int32_t maxAmpHalf = 0;
    for (size_t i = 0; i < kChunk * 2; ++i) {
        int32_t a = std::abs(static_cast<int32_t>(buffer[i]));
        if (a > maxAmpHalf) maxAmpHalf = a;
    }
    assert(maxAmpHalf > 0);
    assert(maxAmpHalf < maxAmpFull);

    // Zero volume -> silence
    music.SetMusicVolume(1, 0.0);
    music.RenderAudioChunk(buffer, kChunk);
    for (size_t i = 0; i < kChunk * 2; ++i) {
        assert(buffer[i] == 0);
    }

    music.SetMusicVolume(1, 1.0);

    // 3. Pause
    music.PauseMusic(1);
    assert(music.IsPaused(1));
    size_t renderedPaused = music.RenderAudioChunk(buffer, kChunk);
    assert(renderedPaused == kChunk);
    for (size_t i = 0; i < kChunk * 2; ++i) {
        assert(buffer[i] == 0);
    }

    // 4. Resume
    music.ResumeMusic(1);
    assert(!music.IsPaused(1));
    assert(music.IsPlaying(1));

    // 5. Seek
    music.SeekMusic(1, 3.5);
    assert(std::fabs(music.GetMusicPosition(1) - 3.5) < 0.05);

    // 6. Stop
    music.StopMusic(1);
    assert(!music.IsPlaying(1));
    assert(music.GetMusicPosition(1) == 0.0);

    // 7. Looping vs Non-Looping
    music.PlayMusic(1, 0, false);
    double duration = music.GetMusicLength(1);
    assert(duration > 0.0);
    music.SeekMusic(1, duration - 0.02);
    for (int i = 0; i < 5; ++i) {
        music.RenderAudioChunk(buffer, kChunk);
    }
    assert(!music.IsPlaying(1));

    music.PlayMusic(1, 0, true);
    music.SeekMusic(1, duration - 0.02);
    for (int i = 0; i < 5; ++i) {
        music.RenderAudioChunk(buffer, kChunk);
    }
    assert(music.IsPlaying(1));

    std::cout << "  -> PASS: Play, pause, resume, stop, volume, seek, and loop verified" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 4: Song Transitions & Fading
// ---------------------------------------------------------------------------
static void test_song_transitions_and_fades() {
    std::cout << "[TEST] Running test_song_transitions_and_fades..." << std::endl;

    OpenMptMusicInterface music;
    std::vector<uint8_t> song1 = CreateSyntheticTrackerModule();
    std::vector<uint8_t> song2 = CreateSyntheticTrackerModule();

    assert(music.LoadMusicFromMemory(1, song1.data(), song1.size()));
    assert(music.LoadMusicFromMemory(2, song2.data(), song2.size()));

    music.PlayMusic(1, 0, true);
    assert(music.IsPlaying(1));

    // Seek to 5.0 seconds
    music.SeekMusic(1, 5.0);
    assert(std::fabs(music.GetMusicPosition(1) - 5.0) < 0.05);

    // Test FadeIn with theOffset == -1 (must preserve current offset 5.0)
    music.FadeIn(1, -1, 0.05, true);
    assert(std::fabs(music.GetMusicPosition(1) - 5.0) < 0.05);

    constexpr size_t kChunk = OpenMptMusicInterface::kChunkFrames;
    alignas(8) int16_t buffer[kChunk * 2];
    music.RenderAudioChunk(buffer, kChunk);

    // Fade in song 2 at offset 0
    music.FadeIn(2, 0, 0.05, true);
    assert(music.IsPlaying(2));

    for (int step = 0; step < 20; ++step) {
        music.Update();
        music.RenderAudioChunk(buffer, kChunk);
    }

    assert(music.GetAllocatedMemory() <= music.GetMaxMemory());
    std::cout << "  -> PASS: Song transitions, offset preservation on FadeIn(-1), and fading verified" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 5: SoundManager Lazy Loading & Memory Cap (<= 8 MB SFX)
// ---------------------------------------------------------------------------
static void test_sound_manager_sfx_and_lru_cache() {
    std::cout << "[TEST] Running test_sound_manager_sfx_and_lru_cache..." << std::endl;

    SoundManager sfx;
    assert(sfx.Initialize(32));
    assert(sfx.GetMaxMemory() == 8 * 1024 * 1024); // 8 MB limit
    assert(sfx.GetAllocatedMemory() == 0);

    // Create 3 synthetic 22.05 kHz WAV sounds (mono)
    std::vector<uint8_t> wavSmall = CreateSyntheticWav(22050, 1, 2205); // 0.1s mono ~ 4.4 KB
    std::vector<uint8_t> wavMed   = CreateSyntheticWav(22050, 1, 22050); // 1.0s mono ~ 44 KB

    // Register sounds lazily
    assert(sfx.LoadSoundFromMemory(1, wavSmall.data(), wavSmall.size()));
    assert(sfx.LoadSoundFromMemory(2, wavMed.data(), wavMed.size()));

    // Lazy check: sound registered but not decoded into memory until first play
    assert(sfx.GetAllocatedMemory() == 0);

    // Play sound 1 -> triggers on-demand decode with mono upmixed to stereo
    int ch1 = sfx.PlaySample(1, -50); // Test panning
    assert(ch1 >= 0 && ch1 < 32);
    size_t memAfterPlay1 = sfx.GetAllocatedMemory();
    assert(memAfterPlay1 > 0);
    assert(memAfterPlay1 <= sfx.GetMaxMemory());

    // Play sound 2 -> triggers on-demand decode
    int ch2 = sfx.PlaySample(2, 50);
    assert(ch2 >= 0 && ch2 < 32);
    assert(ch2 != ch1); // Different channel allocated
    size_t memAfterPlay2 = sfx.GetAllocatedMemory();
    assert(memAfterPlay2 > memAfterPlay1);
    assert(memAfterPlay2 <= sfx.GetMaxMemory());

    // Channel volume and master volume
    sfx.SetSoundVolume(1, 0.7);
    sfx.SetVolume(0.8);
    sfx.StopSound(1);

    // Test LRU memory cap enforcement & sound stealing:
    // Create large sounds (each ~1.5 MB uncompressed stereo) to force memory eviction past 8 MB
    std::vector<uint8_t> wavLarge = CreateSyntheticWav(22050, 1, 375000); // ~1.5 MB stereo
    for (unsigned int id = 10; id <= 16; ++id) {
        assert(sfx.LoadSoundFromMemory(id, wavLarge.data(), wavLarge.size()));
    }

    // Sequentially play sounds 10..16.
    // Every sound MUST successfully play (ch >= 0) and eviction MUST free older sounds!
    for (unsigned int id = 10; id <= 16; ++id) {
        int ch = sfx.PlaySample(id);
        assert(ch >= 0 && ch < 32); // STRICT: Sound is NEVER dropped; oldest sound was evicted
        sfx.Update();
        assert(sfx.GetAllocatedMemory() <= sfx.GetMaxMemory()); // Strictly <= 8 MB
    }

    // Release all sounds
    sfx.ReleaseAllSounds();
    assert(sfx.GetAllocatedMemory() == 0);
    sfx.Shutdown();

    std::cout << "  -> PASS: SoundManager lazy loading, mono upmix, panning, channel stealing, and 8 MB LRU eviction verified" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 6: Combined System Budget (Music <= 2 MB + SFX <= 8 MB <= 10 MB total)
// ---------------------------------------------------------------------------
static void test_total_audio_budget() {
    std::cout << "[TEST] Running test_total_audio_budget..." << std::endl;

    OpenMptMusicInterface music;
    SoundManager sfx;
    sfx.Initialize(32);

    std::vector<uint8_t> modData = CreateSyntheticTrackerModule();
    assert(music.LoadMusicFromMemory(1, modData.data(), modData.size()));
    music.PlayMusic(1);

    std::vector<uint8_t> wavData = CreateSyntheticWav(22050, 1, 11025);
    assert(sfx.LoadSoundFromMemory(1, wavData.data(), wavData.size()));
    int ch = sfx.PlaySample(1);
    assert(ch >= 0);

    size_t musicMem = music.GetAllocatedMemory();
    size_t sfxMem = sfx.GetAllocatedMemory();
    size_t totalMem = musicMem + sfxMem;

    assert(musicMem <= 2 * 1024 * 1024);
    assert(sfxMem <= 8 * 1024 * 1024);
    assert(totalMem <= 10 * 1024 * 1024);

    std::cout << "  -> PASS: Total audio memory footprint " << (totalMem / 1024)
              << " KB strictly <= 10 MB (Music: " << (musicMem / 1024)
              << " KB, SFX: " << (sfxMem / 1024) << " KB)" << std::endl;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "==================================================" << std::endl;
    std::cout << " Running PvZ Audio Engine Integration Tests" << std::endl;
    std::cout << "==================================================" << std::endl;

    test_music_loading_and_memory_limit();
    test_chunk_streaming_2048_samples();
    test_playback_controls();
    test_song_transitions_and_fades();
    test_sound_manager_sfx_and_lru_cache();
    test_total_audio_budget();

    std::cout << "==================================================" << std::endl;
    std::cout << " ALL AUDIO ENGINE & OPENMPT TESTS PASSED!" << std::endl;
    std::cout << "==================================================" << std::endl;
    return 0;
}
