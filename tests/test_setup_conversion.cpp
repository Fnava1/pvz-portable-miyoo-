#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>

#include "setup/setup_converter.h"
#include "setup/fb_progress.h"
#include "setup/stb_image.h"
#include "setup/stb_image_write.h"

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

static bool file_exists(const char* path) {
    return access(path, F_OK) == 0;
}

// Helper to construct a minimal valid RIFF WAV in memory
static std::vector<uint8_t> create_dummy_wav(uint32_t sample_rate, uint16_t num_channels, uint16_t bits_per_sample, size_t num_frames) {
    std::vector<uint8_t> wav;
    uint16_t block_align = num_channels * (bits_per_sample / 8);
    uint32_t byte_rate = sample_rate * block_align;
    uint32_t data_size = static_cast<uint32_t>(num_frames * block_align);
    uint32_t riff_chunk_size = 36 + data_size;

    wav.resize(44 + data_size, 0);
    uint8_t* p = wav.data();

    // RIFF header
    std::memcpy(p, "RIFF", 4);
    std::memcpy(p + 4, &riff_chunk_size, 4);
    std::memcpy(p + 8, "WAVE", 4);

    // fmt subchunk
    std::memcpy(p + 12, "fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1; // PCM
    std::memcpy(p + 16, &fmt_size, 4);
    std::memcpy(p + 20, &audio_format, 2);
    std::memcpy(p + 22, &num_channels, 2);
    std::memcpy(p + 24, &sample_rate, 4);
    std::memcpy(p + 28, &byte_rate, 4);
    std::memcpy(p + 32, &block_align, 2);
    std::memcpy(p + 34, &bits_per_sample, 2);

    // data subchunk
    std::memcpy(p + 36, "data", 4);
    std::memcpy(p + 40, &data_size, 4);

    // Fill with a simple sine-wave pattern or sawtooth
    int16_t* samples = reinterpret_cast<int16_t*>(p + 44);
    size_t total_samples = num_frames * num_channels;
    for (size_t i = 0; i < total_samples; ++i) {
        samples[i] = static_cast<int16_t>((i % 100) * 300);
    }

    return wav;
}

// Helper to write PNG to memory using stb_image_write
static void mem_write_func(void* context, void* data, int size) {
    auto* buf = static_cast<std::vector<uint8_t>*>(context);
    const uint8_t* byte_data = static_cast<const uint8_t*>(data);
    buf->insert(buf->end(), byte_data, byte_data + size);
}

static std::vector<uint8_t> create_dummy_png(int width, int height) {
    std::vector<uint8_t> pixels(width * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>((x * 255) / width);
            pixels[idx + 1] = static_cast<uint8_t>((y * 255) / height);
            pixels[idx + 2] = 128;
            pixels[idx + 3] = 255;
        }
    }

    std::vector<uint8_t> png_buf;
    stbi_write_png_to_func(mem_write_func, &png_buf, width, height, 4, pixels.data(), width * 4);
    return png_buf;
}

// Helper to create a valid PopCap PAK file
struct PakTestFile {
    std::string rel_path;
    std::vector<uint8_t> content;
};

static void write_test_pak(const char* pak_path, const std::vector<PakTestFile>& entries) {
    FILE* fp = std::fopen(pak_path, "wb");
    assert(fp != nullptr);

    // 1. Header
    uint32_t magic = POP_PAK_MAGIC;
    uint32_t version = POP_PAK_VERSION;
    std::fwrite(&magic, 4, 1, fp);
    std::fwrite(&version, 4, 1, fp);

    // 2. Records table
    for (const auto& entry : entries) {
        uint8_t flag = 0x00;
        uint8_t name_len = static_cast<uint8_t>(entry.rel_path.size());
        uint32_t size = static_cast<uint32_t>(entry.content.size());
        uint32_t time_low = 0;
        uint32_t time_high = 0;

        std::fwrite(&flag, 1, 1, fp);
        std::fwrite(&name_len, 1, 1, fp);
        std::fwrite(entry.rel_path.data(), 1, name_len, fp);
        std::fwrite(&size, 4, 1, fp);
        std::fwrite(&time_low, 4, 1, fp);
        std::fwrite(&time_high, 4, 1, fp);
    }

    // End of table
    uint8_t end_flag = POP_PAK_FLAG_END;
    std::fwrite(&end_flag, 1, 1, fp);

    // 3. Payload with XOR 0xF7 encryption
    for (const auto& entry : entries) {
        std::vector<uint8_t> xored(entry.content.size());
        for (size_t i = 0; i < entry.content.size(); ++i) {
            xored[i] = entry.content[i] ^ POP_PAK_XOR_KEY;
        }
        std::fwrite(xored.data(), 1, xored.size(), fp);
    }

    std::fclose(fp);
}

// ---------------------------------------------------------------------------
// Unit Tests
// ---------------------------------------------------------------------------

static void test_image_downscale_png() {
    std::cout << "[TEST] Running test_image_downscale_png..." << std::endl;
    int orig_w = 100;
    int orig_h = 100;
    std::vector<uint8_t> png_in = create_dummy_png(orig_w, orig_h);
    assert(!png_in.empty());

    uint8_t* out_data = nullptr;
    size_t out_size = 0;
    int rc = convert_image_buffer(png_in.data(), png_in.size(), "images/sunflower.png", &out_data, &out_size);
    assert(rc == 0);
    assert(out_data != nullptr);
    assert(out_size > 0);

    // Decode resized image to verify dimensions
    int new_w = 0, new_h = 0, channels = 0;
    uint8_t* decoded = stbi_load_from_memory(out_data, static_cast<int>(out_size), &new_w, &new_h, &channels, 0);
    assert(decoded != nullptr);
    assert(new_w == 80);
    assert(new_h == 80);

    stbi_image_free(decoded);
    std::free(out_data);
    std::cout << "  -> PASS: 100x100 downscaled to 80x80" << std::endl;
}

static void test_audio_downsample_wav() {
    std::cout << "[TEST] Running test_audio_downsample_wav..." << std::endl;
    // 44100 Hz, stereo (2ch), 16-bit PCM, 4410 frames = 0.1s
    std::vector<uint8_t> wav_in = create_dummy_wav(44100, 2, 16, 4410);
    assert(!wav_in.empty());

    uint8_t* out_data = nullptr;
    size_t out_size = 0;
    int rc = convert_wav_buffer(wav_in.data(), wav_in.size(), &out_data, &out_size);
    assert(rc == 0);
    assert(out_data != nullptr);
    assert(out_size > 44);

    // Verify WAV header fields
    uint16_t num_channels = *reinterpret_cast<uint16_t*>(out_data + 22);
    uint32_t sample_rate = *reinterpret_cast<uint32_t*>(out_data + 24);
    uint16_t bits_per_sample = *reinterpret_cast<uint16_t*>(out_data + 34);

    assert(num_channels == 1); // Downmixed to mono
    assert(sample_rate == 22050); // Downsampled to 22.05 kHz
    assert(bits_per_sample == 16);

    std::free(out_data);
    std::cout << "  -> PASS: 44.1kHz stereo converted to 22.05kHz mono" << std::endl;
}

static void test_pak_streaming_conversion_and_peak_memory() {
    std::cout << "[TEST] Running test_pak_streaming_conversion_and_peak_memory..." << std::endl;

    const char* in_pak = "test_input_archive.pak";
    const char* out_pak = "test_output_archive.pak";

    std::vector<PakTestFile> test_entries;
    test_entries.push_back({"images/peashooter.png", create_dummy_png(120, 100)});
    test_entries.push_back({"sounds/groan.wav", create_dummy_wav(44100, 2, 16, 2205)});
    test_entries.push_back({"data/_BrianneTod32Outline.png", create_dummy_png(50, 50)});
    std::string text_data = "ZOMBIES_ARE_COMING_TO_MIYOO_MINI";
    test_entries.push_back({"properties/lawn.txt", std::vector<uint8_t>(text_data.begin(), text_data.end())});

    write_test_pak(in_pak, test_entries);
    assert(file_exists(in_pak));

    ConverterStats stats = {};
    int rc = convert_pak_streaming(in_pak, out_pak, nullptr, nullptr, &stats);
    assert(rc == 0);
    assert(file_exists(out_pak));

    assert(stats.total_files == 4);
    assert(stats.processed_files == 4);
    assert(stats.images_scaled == 1);
    assert(stats.audio_resampled == 1);
    assert(stats.passthrough_files == 2);

    // Peak memory MUST stay <= 10 MB
    std::cout << "  Peak conversion memory: " << (stats.peak_memory_bytes / 1024) << " KB" << std::endl;
    assert(stats.peak_memory_bytes <= 10 * 1024 * 1024);

    // Now inspect output PAK
    FILE* fp = std::fopen(out_pak, "rb");
    assert(fp != nullptr);

    uint32_t magic = 0, version = 0;
    size_t n = std::fread(&magic, 4, 1, fp);
    assert(n == 1 && magic == POP_PAK_MAGIC);
    n = std::fread(&version, 4, 1, fp);
    assert(n == 1 && version == POP_PAK_VERSION);

    // Read record table
    struct OutRecord {
        std::string name;
        uint32_t size;
    };
    std::vector<OutRecord> out_records;

    while (true) {
        uint8_t flag = 0;
        if (std::fread(&flag, 1, 1, fp) != 1) break;
        if (flag & POP_PAK_FLAG_END) break;

        uint8_t name_len = 0;
        assert(std::fread(&name_len, 1, 1, fp) == 1);
        std::vector<char> name_buf(name_len + 1, 0);
        assert(std::fread(name_buf.data(), 1, name_len, fp) == name_len);
        uint32_t size = 0, t_low = 0, t_high = 0;
        assert(std::fread(&size, 4, 1, fp) == 1);
        assert(std::fread(&t_low, 4, 1, fp) == 1);
        assert(std::fread(&t_high, 4, 1, fp) == 1);
        out_records.push_back({std::string(name_buf.data()), size});
    }

    assert(out_records.size() == 4);

    // Read payloads and verify content
    for (const auto& rec : out_records) {
        std::vector<uint8_t> payload(rec.size);
        assert(std::fread(payload.data(), 1, rec.size, fp) == rec.size);
        for (size_t i = 0; i < rec.size; ++i) {
            payload[i] ^= POP_PAK_XOR_KEY;
        }

        if (rec.name == "images/peashooter.png") {
            int w = 0, h = 0, ch = 0;
            uint8_t* dec = stbi_load_from_memory(payload.data(), static_cast<int>(payload.size()), &w, &h, &ch, 0);
            assert(dec != nullptr);
            assert(w == 120); // Preserved 1:1 to avoid double scaling with 640x480 rasterizer
            assert(h == 100);
            stbi_image_free(dec);
        } else if (rec.name == "data/_BrianneTod32Outline.png") {
            int w = 0, h = 0, ch = 0;
            uint8_t* dec = stbi_load_from_memory(payload.data(), static_cast<int>(payload.size()), &w, &h, &ch, 0);
            assert(dec != nullptr);
            assert(w == 50); // Unaltered 1:1 original
            assert(h == 50);
            stbi_image_free(dec);
        } else if (rec.name == "sounds/groan.wav") {
            uint16_t ch = *reinterpret_cast<uint16_t*>(payload.data() + 22);
            uint32_t sr = *reinterpret_cast<uint32_t*>(payload.data() + 24);
            assert(ch == 1);
            assert(sr == 22050);
        } else if (rec.name == "properties/lawn.txt") {
            std::string recovered(payload.begin(), payload.end());
            assert(recovered == text_data);
        }
    }

    std::fclose(fp);

    std::remove(in_pak);
    std::remove(out_pak);
    std::cout << "  -> PASS: Streaming PAK converted successfully within RAM budget" << std::endl;
}

static void test_finalize_transaction() {
    std::cout << "[TEST] Running test_finalize_transaction..." << std::endl;

    const char* tmp_pak = "test_trans.pak.tmp";
    const char* final_pak = "test_trans.pak";
    const char* raw_pak = "test_raw.pak";
    const char* bak_pak = "test_raw.pak.bak";
    const char* marker = "test.installed";

    // Create dummy source files
    FILE* f1 = std::fopen(tmp_pak, "wb");
    std::fputs("NEW_PAK", f1);
    std::fclose(f1);

    FILE* f2 = std::fopen(raw_pak, "wb");
    std::fputs("OLD_PAK", f2);
    std::fclose(f2);

    int rc = finalize_installation(tmp_pak, final_pak, raw_pak, bak_pak, marker, nullptr);
    assert(rc == 0);

    assert(file_exists(final_pak));
    assert(!file_exists(tmp_pak));
    assert(file_exists(bak_pak));
    assert(!file_exists(raw_pak));
    assert(file_exists(marker));

    // Cleanup
    std::remove(final_pak);
    std::remove(bak_pak);
    std::remove(marker);

    std::cout << "  -> PASS: Transactional swap and marker touched successfully" << std::endl;
}

static void test_fb_progress_console_fallback() {
    std::cout << "[TEST] Running test_fb_progress_console_fallback..." << std::endl;
    int rc = fb_progress_init();
    assert(rc == 0);

    // Updating progress should not crash even if fb0 is absent
    fb_progress_update(0.25f, "Testing 25%");
    fb_progress_update(0.75f, "Testing 75%");
    fb_progress_update(1.0f, "Testing Done");

    fb_progress_close();
    std::cout << "  -> PASS: Framebuffer fallback works cleanly" << std::endl;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    std::cout << "==================================================" << std::endl;
    std::cout << " Running PvZ Setup Conversion Unit Tests" << std::endl;
    std::cout << "==================================================" << std::endl;

    test_image_downscale_png();
    test_audio_downsample_wav();
    test_pak_streaming_conversion_and_peak_memory();
    test_finalize_transaction();
    test_fb_progress_console_fallback();

    std::cout << "==================================================" << std::endl;
    std::cout << " ALL UNIT TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "==================================================" << std::endl;
    return 0;
}
