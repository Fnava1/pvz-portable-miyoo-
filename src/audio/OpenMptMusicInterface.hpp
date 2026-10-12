#ifndef PVZ_AUDIO_OPENMPTMUSICINTERFACE_HPP
#define PVZ_AUDIO_OPENMPTMUSICINTERFACE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Sexy {

/**
 * Base PopCap MusicInterface.
 */
class MusicInterface {
public:
    virtual ~MusicInterface() = default;

    virtual bool LoadMusic(int theSongId, const std::string& theFileName) = 0;
    virtual bool LoadMusicFromMemory(int theSongId, const void* theData, size_t theSize) = 0;
    virtual void PlayMusic(int theSongId, int theOffset = 0, bool theLoop = true) = 0;
    virtual void StopMusic(int theSongId) = 0;
    virtual void PauseMusic(int theSongId) = 0;
    virtual void ResumeMusic(int theSongId) = 0;
    virtual void StopAllMusic() = 0;
    virtual void UnloadMusic(int theSongId) = 0;
    virtual void UnloadAllMusic() = 0;
    virtual void SetVolume(double theVolume) = 0;
    virtual void SetMusicVolume(int theSongId, double theVolume) = 0;
    virtual double GetMusicVolume(int theSongId) = 0;
    virtual bool IsPlaying(int theSongId) = 0;
    virtual bool IsPaused(int theSongId) = 0;
    virtual void FadeIn(int theSongId, int theOffset = -1, double theSpeed = 0.002, bool theLoop = true) = 0;
    virtual void FadeOut(int theSongId, bool theStop = true, double theSpeed = 0.004) = 0;
    virtual void SeekMusic(int theSongId, double thePositionSeconds) = 0;
    virtual double GetMusicPosition(int theSongId) = 0;
    virtual double GetMusicLength(int theSongId) = 0;
    virtual void Update() = 0;
};

} // namespace Sexy

namespace PvZ {
namespace Audio {

using MusicInterface = Sexy::MusicInterface;

/**
 * OpenMPT module C API function pointer signatures for dynamic linkage.
 */
typedef struct openmpt_module openmpt_module;
typedef openmpt_module* (*openmpt_module_create_from_memory2_fn)(const void* filedata, size_t filesize, void* logfunc, void* loguser, void* errfunc, void* erruser, int* error, const char** error_message, const void* ctls);
typedef void (*openmpt_module_destroy_fn)(openmpt_module* mod);
typedef size_t (*openmpt_module_read_interleaved_stereo_fn)(openmpt_module* mod, int32_t samplerate, size_t count, int16_t* interleaved_stereo);
typedef double (*openmpt_module_get_duration_seconds_fn)(openmpt_module* mod);
typedef double (*openmpt_module_get_position_seconds_fn)(openmpt_module* mod);
typedef double (*openmpt_module_set_position_seconds_fn)(openmpt_module* mod, double seconds);
typedef int (*openmpt_module_set_repeat_count_fn)(openmpt_module* mod, int32_t repeat_count);

/**
 * Procedural/synthetic fallback tracker voice generator.
 */
struct FallbackVoice {
    float phase = 0.0f;
    float frequency = 440.0f;
    float amplitude = 0.0f;
    int waveform = 0; // 0=Sine, 1=Triangle, 2=Square, 3=Noise
};

struct FallbackTrackerState {
    FallbackVoice voices[4];
    double positionSeconds = 0.0;
    double durationSeconds = 120.0; // Default 2-minute loop length
    int currentPatternRow = 0;
    float tempoBpm = 125.0f;
    float sampleAccumulator = 0.0f;
    bool active = false;
};

/**
 * Represents an in-memory tracker song.
 */
struct SongEntry {
    int songId = -1;
    std::string fileName;
    std::vector<uint8_t> buffer;
    double volume = 1.0;
    double fadeVolume = 1.0;
    double fadeSpeed = 0.0;
    bool fading = false;
    bool fadeStopOnComplete = false;
    bool loop = true;
    bool isPlaying = false;
    bool isPaused = false;
    double duration = 120.0;
    double currentPosition = 0.0;

    openmpt_module* openmptMod = nullptr;
    FallbackTrackerState fallbackState;
};

/**
 * OpenMptMusicInterface: PopCap MusicInterface streaming tracker music
 * in chunks of 2,048 samples, strictly respecting <= 2 MB memory budget.
 */
class OpenMptMusicInterface : public MusicInterface {
public:
    static constexpr size_t kChunkFrames = 2048;
    static constexpr size_t kSampleRate = 44100;
    static constexpr size_t kNumChannels = 2;
    static constexpr size_t kMaxMusicMemory = 2 * 1024 * 1024; // 2 MB limit

    OpenMptMusicInterface();
    ~OpenMptMusicInterface() override;

    // PopCap MusicInterface overrides
    bool LoadMusic(int theSongId, const std::string& theFileName) override;
    bool LoadMusicFromMemory(int theSongId, const void* theData, size_t theSize) override;
    void PlayMusic(int theSongId, int theOffset = 0, bool theLoop = true) override;
    void StopMusic(int theSongId) override;
    void PauseMusic(int theSongId) override;
    void ResumeMusic(int theSongId) override;
    void StopAllMusic() override;
    void UnloadMusic(int theSongId) override;
    void UnloadAllMusic() override;
    void SetVolume(double theVolume) override;
    void SetMusicVolume(int theSongId, double theVolume) override;
    double GetMusicVolume(int theSongId) override;
    bool IsPlaying(int theSongId) override;
    bool IsPaused(int theSongId) override;
    void FadeIn(int theSongId, int theOffset = -1, double theSpeed = 0.002, bool theLoop = true) override;
    void FadeOut(int theSongId, bool theStop = true, double theSpeed = 0.004) override;
    void SeekMusic(int theSongId, double thePositionSeconds) override;
    double GetMusicPosition(int theSongId) override;
    double GetMusicLength(int theSongId) override;
    void Update() override;

    // Memory footprint & streaming
    size_t GetAllocatedMemory() const;
    size_t GetMaxMemory() const { return kMaxMusicMemory; }
    bool IsOpenMptLoaded() const { return mOpenMptAvailable; }

    /**
     * Renders exactly `frameCount` stereo sample frames (2 * frameCount int16_t samples).
     * Guaranteed zero dynamic heap allocations.
     */
    size_t RenderAudioChunk(int16_t* interleavedStereoOutput, size_t frameCount);

private:
    void TryLoadOpenMptSymbols();
    void InitFallbackState(FallbackTrackerState& state, const uint8_t* data, size_t size);
    void RenderFallbackAudio(FallbackTrackerState& state, int16_t* output, size_t frameCount, double vol);

    void* mLibHandle = nullptr;
    bool mOpenMptAvailable = false;

    // Dynamic OpenMPT symbols
    openmpt_module_create_from_memory2_fn mFnCreateFromMemory = nullptr;
    openmpt_module_destroy_fn mFnDestroy = nullptr;
    openmpt_module_read_interleaved_stereo_fn mFnReadStereo = nullptr;
    openmpt_module_get_duration_seconds_fn mFnGetDuration = nullptr;
    openmpt_module_get_position_seconds_fn mFnGetPosition = nullptr;
    openmpt_module_set_position_seconds_fn mFnSetPosition = nullptr;
    openmpt_module_set_repeat_count_fn mFnSetRepeatCount = nullptr;

    double mMasterVolume = 1.0;
    int mActiveSongId = -1;
    std::unordered_map<int, SongEntry> mSongs;

    // Pre-allocated streaming buffer (2048 stereo frames = 4096 int16_t samples)
    alignas(8) int16_t mInternalChunkBuffer[kChunkFrames * kNumChannels];
};

/**
 * SoundManager: Low-overhead sound effect manager with 32-channel pool
 * and on-demand lazy loading of 22.05 kHz WAV files, enforcing <= 8 MB limit.
 */
struct SoundChannel {
    unsigned int soundId = 0;
    int channelIndex = -1;
    bool active = false;
    bool paused = false;
    double volume = 1.0;
    int pan = 0;
    size_t playbackFrame = 0;
};

struct SoundAsset {
    unsigned int soundId = 0;
    std::string fileName;
    std::vector<uint8_t> encodedWavData; // Kept lazily or until loaded
    std::vector<int16_t> pcmData;        // Decoded 16-bit PCM samples
    uint32_t sampleRate = 22050;
    uint16_t channels = 1;
    size_t dataSizeBytes = 0;
    uint64_t lastUsedTick = 0;
    double volume = 1.0;
    bool isLoaded = false;
    void* mixChunk = nullptr; // Mix_Chunk* when SDL_mixer is available
};

class SoundManager {
public:
    static constexpr size_t kMaxSfxMemory = 8 * 1024 * 1024; // 8 MB limit
    static constexpr int kDefaultChannels = 32;

    SoundManager();
    virtual ~SoundManager();

    bool Initialize(int mixChannels = kDefaultChannels);
    void Shutdown();

    bool LoadSound(unsigned int theSoundId, const std::string& theFilename);
    bool LoadSoundFromMemory(unsigned int theSoundId, const void* theData, size_t theSize);
    int PlaySample(unsigned int theSoundId, int thePan = 0);
    void StopSound(unsigned int theSoundId);
    void StopAllSounds();
    void SetVolume(double theVolume);
    void SetSoundVolume(unsigned int theSoundId, double theVolume);
    double GetSoundVolume(unsigned int theSoundId);
    void ReleaseSound(unsigned int theSoundId);
    void ReleaseAllSounds();
    void Update();

    size_t GetAllocatedMemory() const { return mAllocatedSfxMemory; }
    size_t GetMaxMemory() const { return kMaxSfxMemory; }
    bool Initialized() const { return mInitialized; }

private:
    bool EnsureLoaded(unsigned int theSoundId);
    void EvictLruUntilFits(size_t requiredBytes);
    bool DecodeWav(const uint8_t* wavBytes, size_t size, SoundAsset& asset);

    bool mInitialized = false;
    int mNumChannels = kDefaultChannels;
    double mMasterVolume = 1.0;
    size_t mAllocatedSfxMemory = 0;
    uint64_t mCurrentTick = 0;

    std::vector<SoundChannel> mChannels;
    std::unordered_map<unsigned int, SoundAsset> mSounds;
    bool mSdlMixerOpen = false;
};

} // namespace Audio
} // namespace PvZ

#endif // PVZ_AUDIO_OPENMPTMUSICINTERFACE_HPP
