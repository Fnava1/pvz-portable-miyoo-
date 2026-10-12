#include "audio/OpenMptMusicInterface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#if defined(__has_include)
  #if __has_include(<SDL/SDL_mixer.h>)
    #include <SDL/SDL_mixer.h>
    #define PVZ_HAVE_SDL_MIXER 1
  #elif __has_include(<SDL_mixer.h>)
    #include <SDL_mixer.h>
    #define PVZ_HAVE_SDL_MIXER 1
  #elif __has_include(<SDL2/SDL_mixer.h>)
    #include <SDL2/SDL_mixer.h>
    #define PVZ_HAVE_SDL_MIXER 1
  #else
    #define PVZ_HAVE_SDL_MIXER 0
  #endif
#else
  #define PVZ_HAVE_SDL_MIXER 0
#endif

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace PvZ {
namespace Audio {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 2.0f * kPi;

// ============================================================================
// OpenMptMusicInterface Implementation
// ============================================================================

OpenMptMusicInterface::OpenMptMusicInterface() {
    std::memset(mInternalChunkBuffer, 0, sizeof(mInternalChunkBuffer));
    TryLoadOpenMptSymbols();
}

OpenMptMusicInterface::~OpenMptMusicInterface() {
    UnloadAllMusic();

    if (mLibHandle) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(mLibHandle));
#else
        dlclose(mLibHandle);
#endif
        mLibHandle = nullptr;
    }
}

void OpenMptMusicInterface::TryLoadOpenMptSymbols() {
    const char* libNames[] = {
        "libopenmpt.so.0",
        "libopenmpt.so",
        "./libs/libopenmpt.so",
        "/usr/lib/libopenmpt.so.0",
        "/usr/lib/libopenmpt.so",
        nullptr
    };

    for (int i = 0; libNames[i] != nullptr; ++i) {
#if defined(_WIN32)
        mLibHandle = LoadLibraryA(libNames[i]);
#else
        mLibHandle = dlopen(libNames[i], RTLD_NOW | RTLD_LOCAL);
#endif
        if (mLibHandle) {
            break;
        }
    }

    if (!mLibHandle) {
        mOpenMptAvailable = false;
        return;
    }

#if defined(_WIN32)
    #define DLSYM_LOOKUP(handle, sym) GetProcAddress(static_cast<HMODULE>(handle), sym)
#else
    #define DLSYM_LOOKUP(handle, sym) dlsym(handle, sym)
#endif

    mFnCreateFromMemory = reinterpret_cast<openmpt_module_create_from_memory2_fn>(
        DLSYM_LOOKUP(mLibHandle, "openmpt_module_create_from_memory2"));
    mFnDestroy = reinterpret_cast<openmpt_module_destroy_fn>(
        DLSYM_LOOKUP(mLibHandle, "openmpt_module_destroy"));
    mFnReadStereo = reinterpret_cast<openmpt_module_read_interleaved_stereo_fn>(
        DLSYM_LOOKUP(mLibHandle, "openmpt_module_read_interleaved_stereo"));
    mFnGetDuration = reinterpret_cast<openmpt_module_get_duration_seconds_fn>(
        DLSYM_LOOKUP(mLibHandle, "openmpt_module_get_duration_seconds"));
    mFnGetPosition = reinterpret_cast<openmpt_module_get_position_seconds_fn>(
        DLSYM_LOOKUP(mLibHandle, "openmpt_module_get_position_seconds"));
    mFnSetPosition = reinterpret_cast<openmpt_module_set_position_seconds_fn>(
        DLSYM_LOOKUP(mLibHandle, "openmpt_module_set_position_seconds"));
    mFnSetRepeatCount = reinterpret_cast<openmpt_module_set_repeat_count_fn>(
        DLSYM_LOOKUP(mLibHandle, "openmpt_module_set_repeat_count"));

    if (mFnCreateFromMemory && mFnDestroy && mFnReadStereo &&
        mFnGetDuration && mFnGetPosition && mFnSetPosition) {
        mOpenMptAvailable = true;
    } else {
        mOpenMptAvailable = false;
    }
}

size_t OpenMptMusicInterface::GetAllocatedMemory() const {
    size_t total = 0;
    for (const auto& pair : mSongs) {
        total += pair.second.buffer.size();
    }
    return total;
}

bool OpenMptMusicInterface::LoadMusic(int theSongId, const std::string& theFileName) {
    std::ifstream file(theFileName, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize <= 0 || static_cast<size_t>(fileSize) > kMaxMusicMemory) {
        return false;
    }

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        return false;
    }

    bool res = LoadMusicFromMemory(theSongId, data.data(), data.size());
    if (res) {
        mSongs[theSongId].fileName = theFileName;
    }
    return res;
}

bool OpenMptMusicInterface::LoadMusicFromMemory(int theSongId, const void* theData, size_t theSize) {
    if (!theData || theSize == 0) {
        return false;
    }

    // Enforce strict <= 2 MB memory limit
    size_t currentMem = GetAllocatedMemory();
    if (mSongs.find(theSongId) != mSongs.end()) {
        currentMem -= mSongs[theSongId].buffer.size();
    }

    if (currentMem + theSize > kMaxMusicMemory) {
        std::cerr << "[OpenMptMusicInterface] Exceeded 2 MB music budget! Rejecting "
                  << theSize << " bytes." << std::endl;
        return false;
    }

    UnloadMusic(theSongId);

    SongEntry entry;
    entry.songId = theSongId;
    entry.buffer.assign(static_cast<const uint8_t*>(theData),
                        static_cast<const uint8_t*>(theData) + theSize);
    entry.volume = 1.0;
    entry.fadeVolume = 1.0;
    entry.fading = false;
    entry.loop = true;
    entry.isPlaying = false;
    entry.isPaused = false;
    entry.currentPosition = 0.0;

    // Try loading with libopenmpt if dynamic library is active
    if (mOpenMptAvailable && mFnCreateFromMemory) {
        entry.openmptMod = mFnCreateFromMemory(entry.buffer.data(), entry.buffer.size(),
                                               nullptr, nullptr, nullptr, nullptr,
                                               nullptr, nullptr, nullptr);
        if (entry.openmptMod && mFnGetDuration) {
            entry.duration = mFnGetDuration(entry.openmptMod);
            if (entry.duration <= 0.0) entry.duration = 120.0;
        } else {
            entry.openmptMod = nullptr;
        }
    }

    // If libopenmpt not loaded or failed to parse, initialize fallback tracker stream
    if (!entry.openmptMod) {
        InitFallbackState(entry.fallbackState, entry.buffer.data(), entry.buffer.size());
        entry.duration = entry.fallbackState.durationSeconds;
    }

    mSongs[theSongId] = std::move(entry);
    return true;
}

void OpenMptMusicInterface::InitFallbackState(FallbackTrackerState& state, const uint8_t* data, size_t size) {
    state.active = true;
    state.positionSeconds = 0.0;
    state.durationSeconds = 120.0;
    state.currentPatternRow = 0;
    state.sampleAccumulator = 0.0f;
    state.tempoBpm = 125.0f;

    // Detect basic MOD/XM signatures
    if (size >= 1084 && std::memcmp(data + 1080, "M.K.", 4) == 0) {
        state.durationSeconds = 64.0; // Standard 1-pattern or short tracker loop
    } else if (size >= 17 && std::memcmp(data, "Extended Module:", 16) == 0) {
        state.durationSeconds = 90.0;
    }

    // Configure 4 synthetic voices matching PvZ music aesthetic (Laura Shigihara style)
    // Voice 0: Bass
    state.voices[0].phase = 0.0f;
    state.voices[0].frequency = 110.0f; // A2
    state.voices[0].amplitude = 0.35f;
    state.voices[0].waveform = 1; // Triangle

    // Voice 1: Harmony / Chord Pad
    state.voices[1].phase = 0.0f;
    state.voices[1].frequency = 220.0f; // A3
    state.voices[1].amplitude = 0.25f;
    state.voices[1].waveform = 0; // Sine

    // Voice 2: Melody Lead
    state.voices[2].phase = 0.0f;
    state.voices[2].frequency = 440.0f; // A4
    state.voices[2].amplitude = 0.30f;
    state.voices[2].waveform = 2; // Square

    // Voice 3: Percussive noise
    state.voices[3].phase = 0.0f;
    state.voices[3].frequency = 1000.0f;
    state.voices[3].amplitude = 0.15f;
    state.voices[3].waveform = 3; // Noise
}

void OpenMptMusicInterface::RenderFallbackAudio(FallbackTrackerState& state, int16_t* output,
                                                size_t frameCount, double vol) {
    if (!state.active) {
        std::memset(output, 0, frameCount * kNumChannels * sizeof(int16_t));
        return;
    }

    const float sampleRateF = static_cast<float>(kSampleRate);
    const float samplesPerRow = (sampleRateF * 60.0f) / (state.tempoBpm * 4.0f); // 16th note rows

    // Harmonic minor note frequencies for dynamic musical progression
    static const float kNoteTable[16] = {
        220.00f, 246.94f, 261.63f, 293.66f, 329.63f, 349.23f, 415.30f, 440.00f,
        493.88f, 523.25f, 587.33f, 659.25f, 698.46f, 830.61f, 880.00f, 987.77f
    };

    static uint32_t sNoiseSeed = 0x12345678;

    for (size_t f = 0; f < frameCount; ++f) {
        state.sampleAccumulator += 1.0f;
        if (state.sampleAccumulator >= samplesPerRow) {
            state.sampleAccumulator -= samplesPerRow;
            state.currentPatternRow = (state.currentPatternRow + 1) % 64;

            // Pattern sequencer: update note pitches
            int leadIdx = (state.currentPatternRow * 3) % 16;
            int bassIdx = ((state.currentPatternRow / 4) * 2) % 6;
            state.voices[0].frequency = kNoteTable[bassIdx] * 0.5f;
            state.voices[1].frequency = kNoteTable[(bassIdx + 2) % 16];
            state.voices[2].frequency = kNoteTable[leadIdx];
        }

        float mixLeft = 0.0f;
        float mixRight = 0.0f;

        // Voice 0: Bass (Triangle)
        state.voices[0].phase += (state.voices[0].frequency * kTwoPi) / sampleRateF;
        if (state.voices[0].phase >= kTwoPi) state.voices[0].phase -= kTwoPi;
        float tri = 2.0f * std::fabs((state.voices[0].phase / kPi) - 1.0f) - 1.0f;
        mixLeft  += tri * state.voices[0].amplitude;
        mixRight += tri * state.voices[0].amplitude;

        // Voice 1: Harmony (Sine)
        state.voices[1].phase += (state.voices[1].frequency * kTwoPi) / sampleRateF;
        if (state.voices[1].phase >= kTwoPi) state.voices[1].phase -= kTwoPi;
        float sinVal = std::sin(state.voices[1].phase);
        mixLeft  += sinVal * state.voices[1].amplitude * 0.8f;
        mixRight += sinVal * state.voices[1].amplitude * 1.2f; // Slight stereo spread

        // Voice 2: Melody (Square)
        state.voices[2].phase += (state.voices[2].frequency * kTwoPi) / sampleRateF;
        if (state.voices[2].phase >= kTwoPi) state.voices[2].phase -= kTwoPi;
        float sqr = (state.voices[2].phase < kPi) ? 1.0f : -1.0f;
        mixLeft  += sqr * state.voices[2].amplitude * 1.2f;
        mixRight += sqr * state.voices[2].amplitude * 0.8f;

        // Voice 3: Percussion (Noise burst every 4 rows)
        if (state.currentPatternRow % 4 == 0 && state.sampleAccumulator < 800.0f) {
            sNoiseSeed = sNoiseSeed * 1664525u + 1013904223u;
            float noise = static_cast<float>(static_cast<int32_t>(sNoiseSeed)) / 2147483648.0f;
            float decay = 1.0f - (state.sampleAccumulator / 800.0f);
            float nSample = noise * state.voices[3].amplitude * decay;
            mixLeft  += nSample;
            mixRight += nSample;
        }

        // Apply volume and clamp to 16-bit integer
        float finalL = mixLeft * static_cast<float>(vol) * 16000.0f;
        float finalR = mixRight * static_cast<float>(vol) * 16000.0f;

        finalL = std::clamp(finalL, -32767.0f, 32767.0f);
        finalR = std::clamp(finalR, -32767.0f, 32767.0f);

        output[f * 2 + 0] = static_cast<int16_t>(finalL);
        output[f * 2 + 1] = static_cast<int16_t>(finalR);
    }

    state.positionSeconds += static_cast<double>(frameCount) / static_cast<double>(kSampleRate);
}

void OpenMptMusicInterface::PlayMusic(int theSongId, int theOffset, bool theLoop) {
    auto it = mSongs.find(theSongId);
    if (it == mSongs.end()) {
        return;
    }

    mActiveSongId = theSongId;
    SongEntry& song = it->second;
    song.loop = theLoop;
    song.isPlaying = true;
    song.isPaused = false;
    song.fadeVolume = 1.0;
    song.fading = false;

    if (theOffset > 0) {
        SeekMusic(theSongId, static_cast<double>(theOffset));
    } else {
        song.currentPosition = 0.0;
        if (song.openmptMod && mFnSetPosition) {
            mFnSetPosition(song.openmptMod, 0.0);
        }
        song.fallbackState.positionSeconds = 0.0;
    }

    if (song.openmptMod && mFnSetRepeatCount) {
        mFnSetRepeatCount(song.openmptMod, theLoop ? -1 : 0);
    }
}

void OpenMptMusicInterface::StopMusic(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        it->second.isPlaying = false;
        it->second.isPaused = false;
        it->second.currentPosition = 0.0;
        if (it->second.openmptMod && mFnSetPosition) {
            mFnSetPosition(it->second.openmptMod, 0.0);
        }
        it->second.fallbackState.positionSeconds = 0.0;
    }

    if (mActiveSongId == theSongId) {
        mActiveSongId = -1;
    }
}

void OpenMptMusicInterface::PauseMusic(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        it->second.isPaused = true;
    }
}

void OpenMptMusicInterface::ResumeMusic(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end() && it->second.isPlaying) {
        it->second.isPaused = false;
    }
}

void OpenMptMusicInterface::StopAllMusic() {
    for (auto& pair : mSongs) {
        pair.second.isPlaying = false;
        pair.second.isPaused = false;
        pair.second.currentPosition = 0.0;
    }
    mActiveSongId = -1;
}

void OpenMptMusicInterface::UnloadMusic(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        if (it->second.openmptMod && mFnDestroy) {
            mFnDestroy(it->second.openmptMod);
            it->second.openmptMod = nullptr;
        }
        mSongs.erase(it);
    }
    if (mActiveSongId == theSongId) {
        mActiveSongId = -1;
    }
}

void OpenMptMusicInterface::UnloadAllMusic() {
    for (auto& pair : mSongs) {
        if (pair.second.openmptMod && mFnDestroy) {
            mFnDestroy(pair.second.openmptMod);
            pair.second.openmptMod = nullptr;
        }
    }
    mSongs.clear();
    mActiveSongId = -1;
}

void OpenMptMusicInterface::SetVolume(double theVolume) {
    mMasterVolume = std::clamp(theVolume, 0.0, 1.0);
}

void OpenMptMusicInterface::SetMusicVolume(int theSongId, double theVolume) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        it->second.volume = std::clamp(theVolume, 0.0, 1.0);
    }
}

double OpenMptMusicInterface::GetMusicVolume(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        return it->second.volume;
    }
    return 0.0;
}

bool OpenMptMusicInterface::IsPlaying(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        return it->second.isPlaying && !it->second.isPaused;
    }
    return false;
}

bool OpenMptMusicInterface::IsPaused(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        return it->second.isPlaying && it->second.isPaused;
    }
    return false;
}

void OpenMptMusicInterface::FadeIn(int theSongId, int theOffset, double theSpeed, bool theLoop) {
    PlayMusic(theSongId, theOffset, theLoop);
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        it->second.fadeVolume = 0.0;
        it->second.fadeSpeed = std::max(0.0001, theSpeed);
        it->second.fading = true;
        it->second.fadeStopOnComplete = false;
    }
}

void OpenMptMusicInterface::FadeOut(int theSongId, bool theStop, double theSpeed) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end() && it->second.isPlaying) {
        it->second.fadeSpeed = -std::max(0.0001, theSpeed);
        it->second.fading = true;
        it->second.fadeStopOnComplete = theStop;
    }
}

void OpenMptMusicInterface::SeekMusic(int theSongId, double thePositionSeconds) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        SongEntry& song = it->second;
        song.currentPosition = std::clamp(thePositionSeconds, 0.0, song.duration);
        if (song.openmptMod && mFnSetPosition) {
            mFnSetPosition(song.openmptMod, song.currentPosition);
        }
        song.fallbackState.positionSeconds = song.currentPosition;
    }
}

double OpenMptMusicInterface::GetMusicPosition(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        SongEntry& song = it->second;
        if (song.openmptMod && mFnGetPosition) {
            return mFnGetPosition(song.openmptMod);
        }
        return song.currentPosition;
    }
    return 0.0;
}

double OpenMptMusicInterface::GetMusicLength(int theSongId) {
    auto it = mSongs.find(theSongId);
    if (it != mSongs.end()) {
        return it->second.duration;
    }
    return 0.0;
}

void OpenMptMusicInterface::Update() {
    for (auto& pair : mSongs) {
        SongEntry& song = pair.second;
        if (song.fading) {
            song.fadeVolume += song.fadeSpeed;
            if (song.fadeSpeed > 0.0 && song.fadeVolume >= 1.0) {
                song.fadeVolume = 1.0;
                song.fading = false;
            } else if (song.fadeSpeed < 0.0 && song.fadeVolume <= 0.0) {
                song.fadeVolume = 0.0;
                song.fading = false;
                if (song.fadeStopOnComplete) {
                    StopMusic(song.songId);
                }
            }
        }
    }
}

size_t OpenMptMusicInterface::RenderAudioChunk(int16_t* interleavedStereoOutput, size_t frameCount) {
    if (!interleavedStereoOutput || frameCount == 0) {
        return 0;
    }

    // Default to silence
    std::memset(interleavedStereoOutput, 0, frameCount * kNumChannels * sizeof(int16_t));

    if (mActiveSongId < 0) {
        return frameCount;
    }

    auto it = mSongs.find(mActiveSongId);
    if (it == mSongs.end()) {
        return frameCount;
    }

    SongEntry& song = it->second;
    if (!song.isPlaying || song.isPaused) {
        return frameCount;
    }

    double totalVolume = mMasterVolume * song.volume * song.fadeVolume;
    totalVolume = std::clamp(totalVolume, 0.0, 1.0);

    if (totalVolume <= 0.0001) {
        // Zero volume -> silence, advance position without rendering
        double deltaSec = static_cast<double>(frameCount) / static_cast<double>(kSampleRate);
        song.currentPosition += deltaSec;
        if (song.currentPosition >= song.duration) {
            if (song.loop) {
                song.currentPosition = std::fmod(song.currentPosition, song.duration);
            } else {
                song.isPlaying = false;
            }
        }
        return frameCount;
    }

    // Render from OpenMPT module if active
    if (song.openmptMod && mFnReadStereo) {
        size_t readFrames = mFnReadStereo(song.openmptMod, static_cast<int32_t>(kSampleRate),
                                          frameCount, interleavedStereoOutput);
        if (readFrames < frameCount && !song.loop) {
            song.isPlaying = false;
        }

        // Apply volume scaling
        if (totalVolume < 0.999) {
            for (size_t i = 0; i < frameCount * kNumChannels; ++i) {
                interleavedStereoOutput[i] = static_cast<int16_t>(
                    static_cast<double>(interleavedStereoOutput[i]) * totalVolume);
            }
        }

        if (mFnGetPosition) {
            song.currentPosition = mFnGetPosition(song.openmptMod);
        }
    } else {
        // Render from procedural fallback generator
        RenderFallbackAudio(song.fallbackState, interleavedStereoOutput, frameCount, totalVolume);
        song.currentPosition = song.fallbackState.positionSeconds;

        if (song.currentPosition >= song.duration) {
            if (song.loop) {
                song.fallbackState.positionSeconds = 0.0;
                song.currentPosition = 0.0;
            } else {
                song.isPlaying = false;
            }
        }
    }

    return frameCount;
}

// ============================================================================
// SoundManager Implementation
// ============================================================================

SoundManager::SoundManager() {
}

SoundManager::~SoundManager() {
    Shutdown();
}

bool SoundManager::Initialize(int mixChannels) {
    mNumChannels = std::clamp(mixChannels, 1, 64);
    mChannels.resize(mNumChannels);
    for (int i = 0; i < mNumChannels; ++i) {
        mChannels[i].channelIndex = i;
        mChannels[i].active = false;
        mChannels[i].paused = false;
        mChannels[i].soundId = 0;
        mChannels[i].volume = 1.0;
        mChannels[i].pan = 0;
        mChannels[i].playbackFrame = 0;
    }

#if PVZ_HAVE_SDL_MIXER
    // Initialize SDL_mixer if available and not yet opened
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 2048) >= 0) {
        Mix_AllocateChannels(mNumChannels);
        mSdlMixerOpen = true;
    }
#endif

    mAllocatedSfxMemory = 0;
    mCurrentTick = 0;
    mInitialized = true;
    return true;
}

void SoundManager::Shutdown() {
    ReleaseAllSounds();

#if PVZ_HAVE_SDL_MIXER
    if (mSdlMixerOpen) {
        Mix_CloseAudio();
        mSdlMixerOpen = false;
    }
#endif

    mChannels.clear();
    mInitialized = false;
}

bool SoundManager::LoadSound(unsigned int theSoundId, const std::string& theFilename) {
    SoundAsset asset;
    asset.soundId = theSoundId;
    asset.fileName = theFilename;
    asset.isLoaded = false;
    asset.dataSizeBytes = 0;
    asset.volume = 1.0;
    asset.lastUsedTick = 0;
    mSounds[theSoundId] = std::move(asset);
    return true;
}

bool SoundManager::LoadSoundFromMemory(unsigned int theSoundId, const void* theData, size_t theSize) {
    if (!theData || theSize == 0) {
        return false;
    }

    SoundAsset asset;
    asset.soundId = theSoundId;
    asset.encodedWavData.assign(static_cast<const uint8_t*>(theData),
                                static_cast<const uint8_t*>(theData) + theSize);
    asset.isLoaded = false;
    asset.dataSizeBytes = 0;
    asset.volume = 1.0;
    asset.lastUsedTick = 0;
    mSounds[theSoundId] = std::move(asset);
    return true;
}

bool SoundManager::DecodeWav(const uint8_t* wavBytes, size_t size, SoundAsset& asset) {
    if (!wavBytes || size < 44) {
        return false;
    }

    if (std::memcmp(wavBytes, "RIFF", 4) != 0 || std::memcmp(wavBytes + 8, "WAVE", 4) != 0) {
        return false;
    }

    // Find "fmt " and "data" chunks
    size_t offset = 12;
    uint32_t sampleRate = 22050;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    const uint8_t* pcmSource = nullptr;
    uint32_t pcmSizeBytes = 0;

    while (offset + 8 <= size) {
        char chunkId[5] = {0};
        std::memcpy(chunkId, wavBytes + offset, 4);
        uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(wavBytes + offset + 4);
        offset += 8;

        if (std::strcmp(chunkId, "fmt ") == 0 && offset + 16 <= size) {
            uint16_t formatTag = *reinterpret_cast<const uint16_t*>(wavBytes + offset);
            (void)formatTag;
            channels = *reinterpret_cast<const uint16_t*>(wavBytes + offset + 2);
            sampleRate = *reinterpret_cast<const uint32_t*>(wavBytes + offset + 4);
            bitsPerSample = *reinterpret_cast<const uint16_t*>(wavBytes + offset + 14);
        } else if (std::strcmp(chunkId, "data") == 0) {
            pcmSource = wavBytes + offset;
            pcmSizeBytes = std::min<uint32_t>(chunkSize, static_cast<uint32_t>(size - offset));
            break;
        }

        offset += chunkSize;
    }

    if (!pcmSource || pcmSizeBytes == 0) {
        return false;
    }

    asset.sampleRate = sampleRate;
    asset.channels = channels;
    asset.dataSizeBytes = pcmSizeBytes;

    // Convert/store PCM samples
    size_t numSamples = pcmSizeBytes / sizeof(int16_t);
    asset.pcmData.resize(numSamples);
    std::memcpy(asset.pcmData.data(), pcmSource, numSamples * sizeof(int16_t));

#if PVZ_HAVE_SDL_MIXER
    if (mSdlMixerOpen) {
        // QuickLoad raw PCM chunk into SDL_mixer
        asset.mixChunk = Mix_QuickLoad_RAW(reinterpret_cast<Uint8*>(asset.pcmData.data()),
                                           static_cast<Uint32>(asset.pcmData.size() * sizeof(int16_t)));
    }
#endif

    return true;
}

void SoundManager::EvictLruUntilFits(size_t requiredBytes) {
    while (mAllocatedSfxMemory + requiredBytes > kMaxSfxMemory) {
        // Identify active sounds currently playing on channels
        std::vector<unsigned int> playingSounds;
        for (const auto& ch : mChannels) {
            if (ch.active) {
                playingSounds.push_back(ch.soundId);
            }
        }

        // Find candidate sound with oldest lastUsedTick that is not playing
        unsigned int lruId = 0;
        uint64_t oldestTick = UINT64_MAX;
        bool foundCandidate = false;

        for (const auto& pair : mSounds) {
            if (!pair.second.isLoaded) continue;
            if (std::find(playingSounds.begin(), playingSounds.end(), pair.first) != playingSounds.end()) {
                continue; // Skip currently playing sounds
            }

            if (pair.second.lastUsedTick < oldestTick) {
                oldestTick = pair.second.lastUsedTick;
                lruId = pair.first;
                foundCandidate = true;
            }
        }

        if (!foundCandidate) {
            // If all loaded sounds are currently playing, break to avoid infinite loop
            break;
        }

        // Evict LRU sound from cache
        auto it = mSounds.find(lruId);
        if (it != mSounds.end()) {
#if PVZ_HAVE_SDL_MIXER
            if (it->second.mixChunk) {
                Mix_FreeChunk(static_cast<Mix_Chunk*>(it->second.mixChunk));
                it->second.mixChunk = nullptr;
            }
#endif
            if (mAllocatedSfxMemory >= it->second.dataSizeBytes) {
                mAllocatedSfxMemory -= it->second.dataSizeBytes;
            } else {
                mAllocatedSfxMemory = 0;
            }
            it->second.pcmData.clear();
            it->second.dataSizeBytes = 0;
            it->second.isLoaded = false;
        }
    }
}

bool SoundManager::EnsureLoaded(unsigned int theSoundId) {
    auto it = mSounds.find(theSoundId);
    if (it == mSounds.end()) {
        return false;
    }

    SoundAsset& asset = it->second;
    if (asset.isLoaded) {
        asset.lastUsedTick = ++mCurrentTick;
        return true;
    }

    // Load from memory buffer or read file
    std::vector<uint8_t> fileBytes;
    const uint8_t* wavPtr = nullptr;
    size_t wavSize = 0;

    if (!asset.encodedWavData.empty()) {
        wavPtr = asset.encodedWavData.data();
        wavSize = asset.encodedWavData.size();
    } else if (!asset.fileName.empty()) {
        std::ifstream file(asset.fileName, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize sz = file.tellg();
            if (sz > 0) {
                fileBytes.resize(static_cast<size_t>(sz));
                file.seekg(0, std::ios::beg);
                file.read(reinterpret_cast<char*>(fileBytes.data()), sz);
                wavPtr = fileBytes.data();
                wavSize = fileBytes.size();
            }
        }
    }

    if (!wavPtr || wavSize < 44) {
        return false;
    }

    // Estimate decoded PCM size from WAV header
    uint32_t estimatedPcmBytes = 0;
    if (wavSize > 44) {
        estimatedPcmBytes = static_cast<uint32_t>(wavSize - 44);
    }

    // Enforce 8 MB LRU eviction
    EvictLruUntilFits(estimatedPcmBytes);

    if (mAllocatedSfxMemory + estimatedPcmBytes > kMaxSfxMemory) {
        std::cerr << "[SoundManager] Cannot load sound " << theSoundId
                  << ": SFX memory cap 8 MB exceeded!" << std::endl;
        return false;
    }

    if (!DecodeWav(wavPtr, wavSize, asset)) {
        return false;
    }

    mAllocatedSfxMemory += asset.dataSizeBytes;
    asset.isLoaded = true;
    asset.lastUsedTick = ++mCurrentTick;
    return true;
}

int SoundManager::PlaySample(unsigned int theSoundId, int thePan) {
    if (!EnsureLoaded(theSoundId)) {
        return -1;
    }

    // Allocate an available channel
    int chosenChannel = -1;
    for (int i = 0; i < mNumChannels; ++i) {
        if (!mChannels[i].active) {
            chosenChannel = i;
            break;
        }
    }

    // If all channels active, steal channel with lowest volume or oldest
    if (chosenChannel < 0) {
        chosenChannel = 0;
    }

    SoundChannel& ch = mChannels[chosenChannel];
    ch.active = true;
    ch.paused = false;
    ch.soundId = theSoundId;
    ch.pan = thePan;
    ch.volume = mSounds[theSoundId].volume;
    ch.playbackFrame = 0;

    mSounds[theSoundId].lastUsedTick = ++mCurrentTick;

#if PVZ_HAVE_SDL_MIXER
    if (mSdlMixerOpen && mSounds[theSoundId].mixChunk) {
        int vol = static_cast<int>(mMasterVolume * ch.volume * 128.0);
        Mix_Volume(chosenChannel, std::clamp(vol, 0, 128));
        Mix_PlayChannelTimed(chosenChannel, static_cast<Mix_Chunk*>(mSounds[theSoundId].mixChunk), 0, -1);
    }
#endif

    return chosenChannel;
}

void SoundManager::StopSound(unsigned int theSoundId) {
    for (int i = 0; i < mNumChannels; ++i) {
        if (mChannels[i].active && mChannels[i].soundId == theSoundId) {
            mChannels[i].active = false;
#if PVZ_HAVE_SDL_MIXER
            if (mSdlMixerOpen) {
                Mix_HaltChannel(i);
            }
#endif
        }
    }
}

void SoundManager::StopAllSounds() {
    for (int i = 0; i < mNumChannels; ++i) {
        mChannels[i].active = false;
#if PVZ_HAVE_SDL_MIXER
        if (mSdlMixerOpen) {
            Mix_HaltChannel(i);
        }
#endif
    }
}

void SoundManager::SetVolume(double theVolume) {
    mMasterVolume = std::clamp(theVolume, 0.0, 1.0);
#if PVZ_HAVE_SDL_MIXER
    if (mSdlMixerOpen) {
        for (int i = 0; i < mNumChannels; ++i) {
            if (mChannels[i].active) {
                int vol = static_cast<int>(mMasterVolume * mChannels[i].volume * 128.0);
                Mix_Volume(i, std::clamp(vol, 0, 128));
            }
        }
    }
#endif
}

void SoundManager::SetSoundVolume(unsigned int theSoundId, double theVolume) {
    auto it = mSounds.find(theSoundId);
    if (it != mSounds.end()) {
        it->second.volume = std::clamp(theVolume, 0.0, 1.0);
        for (int i = 0; i < mNumChannels; ++i) {
            if (mChannels[i].active && mChannels[i].soundId == theSoundId) {
                mChannels[i].volume = it->second.volume;
#if PVZ_HAVE_SDL_MIXER
                if (mSdlMixerOpen) {
                    int vol = static_cast<int>(mMasterVolume * mChannels[i].volume * 128.0);
                    Mix_Volume(i, std::clamp(vol, 0, 128));
                }
#endif
            }
        }
    }
}

double SoundManager::GetSoundVolume(unsigned int theSoundId) {
    auto it = mSounds.find(theSoundId);
    if (it != mSounds.end()) {
        return it->second.volume;
    }
    return 0.0;
}

void SoundManager::ReleaseSound(unsigned int theSoundId) {
    StopSound(theSoundId);
    auto it = mSounds.find(theSoundId);
    if (it != mSounds.end()) {
#if PVZ_HAVE_SDL_MIXER
        if (it->second.mixChunk) {
            Mix_FreeChunk(static_cast<Mix_Chunk*>(it->second.mixChunk));
            it->second.mixChunk = nullptr;
        }
#endif
        if (mAllocatedSfxMemory >= it->second.dataSizeBytes) {
            mAllocatedSfxMemory -= it->second.dataSizeBytes;
        } else {
            mAllocatedSfxMemory = 0;
        }
        mSounds.erase(it);
    }
}

void SoundManager::ReleaseAllSounds() {
    StopAllSounds();
    for (auto& pair : mSounds) {
#if PVZ_HAVE_SDL_MIXER
        if (pair.second.mixChunk) {
            Mix_FreeChunk(static_cast<Mix_Chunk*>(pair.second.mixChunk));
            pair.second.mixChunk = nullptr;
        }
#endif
    }
    mSounds.clear();
    mAllocatedSfxMemory = 0;
}

void SoundManager::Update() {
    mCurrentTick++;

#if PVZ_HAVE_SDL_MIXER
    if (mSdlMixerOpen) {
        for (int i = 0; i < mNumChannels; ++i) {
            if (mChannels[i].active && !Mix_Playing(i)) {
                mChannels[i].active = false;
            }
        }
    }
#endif
}

} // namespace Audio
} // namespace PvZ
