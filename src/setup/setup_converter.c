#include "setup_converter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#ifndef _WIN32
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Image Resizing Implementation
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} DynBuffer;

static void dyn_buf_write_cb(void *context, void *data, int size) {
    DynBuffer *buf = (DynBuffer *)context;
    if (size <= 0) return;
    if (buf->size + (size_t)size > buf->capacity) {
        size_t new_cap = (buf->capacity == 0) ? 8192 : buf->capacity * 2;
        while (new_cap < buf->size + (size_t)size) {
            new_cap *= 2;
        }
        uint8_t *new_data = (uint8_t *)realloc(buf->data, new_cap);
        if (!new_data) return;
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->size, data, (size_t)size);
    buf->size += (size_t)size;
}

static int str_ends_with_case_insensitive(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (str_len < suf_len) return 0;
    const char *p = str + str_len - suf_len;
    for (size_t i = 0; i < suf_len; ++i) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_font_asset(const char *path) {
    if (!path) return 0;
    // Check if path starts with data/ or data\ (standard PopCap font directory)
    if (strlen(path) >= 5) {
        if ((path[0] == 'd' || path[0] == 'D') &&
            (path[1] == 'a' || path[1] == 'A') &&
            (path[2] == 't' || path[2] == 'T') &&
            (path[3] == 'a' || path[3] == 'A') &&
            (path[4] == '/' || path[4] == '\\')) {
            return 1;
        }
    }
    // Check for "font" anywhere in path (case-insensitive)
    const char *p = path;
    while (*p) {
        if ((p[0] == 'f' || p[0] == 'F') &&
            (p[1] == 'o' || p[1] == 'O') &&
            (p[2] == 'n' || p[2] == 'N') &&
            (p[3] == 't' || p[3] == 'T')) {
            return 1;
        }
        p++;
    }
    return 0;
}

static int is_preserved_image(const char *path) {
    if (!path) return 0;
    if (is_font_asset(path)) return 1;
    // Check for "caustic" anywhere in path (case-insensitive)
    const char *p = path;
    while (*p) {
        if ((p[0] == 'c' || p[0] == 'C') &&
            (p[1] == 'a' || p[1] == 'A') &&
            (p[2] == 'u' || p[2] == 'U') &&
            (p[3] == 's' || p[3] == 'S') &&
            (p[4] == 't' || p[4] == 'T') &&
            (p[5] == 'i' || p[5] == 'I') &&
            (p[6] == 'c' || p[6] == 'C')) {
            return 1;
        }
        p++;
    }
    return 0;
}

int convert_image_buffer(const uint8_t *in_data, size_t in_size, const char *filename,
                         uint8_t **out_data, size_t *out_size) {
    if (!in_data || in_size == 0 || !out_data || !out_size) {
        return -1;
    }
    *out_data = NULL;
    *out_size = 0;

    int is_jpeg = 0;
    if (filename && (str_ends_with_case_insensitive(filename, ".jpg") ||
                     str_ends_with_case_insensitive(filename, ".jpeg"))) {
        is_jpeg = 1;
    } else if (in_size >= 2 && in_data[0] == 0xFF && in_data[1] == 0xD8) {
        is_jpeg = 1;
    }

    int w = 0, h = 0, channels = 0;
    uint8_t *pixels = stbi_load_from_memory(in_data, (int)in_size, &w, &h, &channels, 0);
    if (!pixels || w <= 0 || h <= 0 || channels <= 0) {
        if (pixels) stbi_image_free(pixels);
        return -1;
    }

    // Target 80% resolution (0.8x downscale)
    int target_w = (int)round((double)w * 0.8);
    int target_h = (int)round((double)h * 0.8);
    if (target_w < 1) target_w = 1;
    if (target_h < 1) target_h = 1;

    // If dimensions are unchanged, skip resizing
    if (target_w == w && target_h == h) {
        stbi_image_free(pixels);
        return -1;
    }

    size_t resized_bytes = (size_t)target_w * (size_t)target_h * (size_t)channels;
    uint8_t *resized_pixels = (uint8_t *)malloc(resized_bytes);
    if (!resized_pixels) {
        stbi_image_free(pixels);
        return -1;
    }

    stbir_pixel_layout layout = (stbir_pixel_layout)channels;
    uint8_t *res_ret = stbir_resize_uint8_linear(
        pixels, w, h, 0,
        resized_pixels, target_w, target_h, 0,
        layout
    );

    stbi_image_free(pixels);
    pixels = NULL;

    if (!res_ret) {
        free(resized_pixels);
        return -1;
    }

    DynBuffer dyn_buf = {NULL, 0, 0};
    int write_ok = 0;

    if (is_jpeg) {
        write_ok = stbi_write_jpg_to_func(dyn_buf_write_cb, &dyn_buf, target_w, target_h, channels, resized_pixels, 85);
    } else {
        write_ok = stbi_write_png_to_func(dyn_buf_write_cb, &dyn_buf, target_w, target_h, channels, resized_pixels, target_w * channels);
    }

    free(resized_pixels);

    if (!write_ok || dyn_buf.size == 0) {
        if (dyn_buf.data) free(dyn_buf.data);
        return -1;
    }

    *out_data = dyn_buf.data;
    *out_size = dyn_buf.size;
    return 0;
}

// ---------------------------------------------------------------------------
// Audio Resampling Implementation (22.05 kHz Mono 16-bit PCM)
// ---------------------------------------------------------------------------

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

int convert_wav_buffer(const uint8_t *in_data, size_t in_size,
                       uint8_t **out_data, size_t *out_size) {
    if (!in_data || in_size < 44 || !out_data || !out_size) {
        return -1;
    }
    *out_data = NULL;
    *out_size = 0;

    // Verify RIFF WAVE header
    if (memcmp(in_data, "RIFF", 4) != 0 || memcmp(in_data + 8, "WAVE", 4) != 0) {
        return -1;
    }

    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    const uint8_t *pcm_data_ptr = NULL;
    size_t pcm_data_len = 0;

    size_t offset = 12;
    while (offset + 8 <= in_size) {
        char chunk_id[5] = {0};
        memcpy(chunk_id, in_data + offset, 4);
        uint32_t chunk_len = read_u32_le(in_data + offset + 4);
        offset += 8;

        if (strcmp(chunk_id, "fmt ") == 0 && chunk_len >= 16 && offset + 16 <= in_size) {
            audio_format = read_u16_le(in_data + offset);
            num_channels = read_u16_le(in_data + offset + 2);
            sample_rate = read_u32_le(in_data + offset + 4);
            bits_per_sample = read_u16_le(in_data + offset + 14);
        } else if (strcmp(chunk_id, "data") == 0) {
            pcm_data_ptr = in_data + offset;
            pcm_data_len = chunk_len;
            if (offset + chunk_len > in_size) {
                pcm_data_len = in_size - offset;
            }
        }

        // Align chunk to 2-byte boundary
        offset += chunk_len + (chunk_len & 1);
    }

    if (!pcm_data_ptr || pcm_data_len == 0 || num_channels == 0 || sample_rate == 0) {
        return -1;
    }

    // Supported formats: PCM (1) or IEEE float (3)
    if (audio_format != 1 && audio_format != 3) {
        return -1;
    }

    size_t bytes_per_sample = bits_per_sample / 8;
    if (bytes_per_sample == 0) return -1;
    size_t block_align = num_channels * bytes_per_sample;
    size_t total_frames = pcm_data_len / block_align;
    if (total_frames == 0) return -1;

    // Decode and downmix all frames to float mono [-1.0, 1.0]
    float *mono_in = (float *)malloc(total_frames * sizeof(float));
    if (!mono_in) return -1;

    const uint8_t *sample_cursor = pcm_data_ptr;
    for (size_t f = 0; f < total_frames; ++f) {
        double frame_sum = 0.0;
        for (int ch = 0; ch < num_channels; ++ch) {
            double val = 0.0;
            if (audio_format == 1) { // PCM integer
                if (bits_per_sample == 8) {
                    uint8_t u = *sample_cursor++;
                    val = ((double)u - 128.0) / 128.0;
                } else if (bits_per_sample == 16) {
                    int16_t s = (int16_t)read_u16_le(sample_cursor);
                    sample_cursor += 2;
                    val = (double)s / 32768.0;
                } else if (bits_per_sample == 24) {
                    int32_t s = (int32_t)(sample_cursor[0] | (sample_cursor[1] << 8) | (sample_cursor[2] << 16));
                    if (s & 0x800000) s |= ~0xFFFFFF; // sign-extend
                    sample_cursor += 3;
                    val = (double)s / 8388608.0;
                } else if (bits_per_sample == 32) {
                    int32_t s = (int32_t)read_u32_le(sample_cursor);
                    sample_cursor += 4;
                    val = (double)s / 2147483648.0;
                }
            } else if (audio_format == 3) { // IEEE Float 32
                if (bits_per_sample == 32) {
                    float flt = 0.0f;
                    memcpy(&flt, sample_cursor, 4);
                    sample_cursor += 4;
                    val = (double)flt;
                }
            }
            frame_sum += val;
        }
        mono_in[f] = (float)(frame_sum / (double)num_channels);
    }

    // Resample to target 22,050 Hz via linear interpolation
    uint32_t target_rate = 22050;
    size_t out_frames = 0;
    float *mono_out = NULL;

    if (sample_rate == target_rate) {
        out_frames = total_frames;
        mono_out = mono_in;
        mono_in = NULL;
    } else {
        double ratio = (double)target_rate / (double)sample_rate;
        out_frames = (size_t)round((double)total_frames * ratio);
        if (out_frames < 1) out_frames = 1;
        mono_out = (float *)malloc(out_frames * sizeof(float));
        if (!mono_out) {
            free(mono_in);
            return -1;
        }

        size_t max_in_idx = (total_frames > 0) ? (total_frames - 1) : 0;
        for (size_t j = 0; j < out_frames; ++j) {
            double pos = (out_frames > 1) ? ((double)j * (double)max_in_idx / (double)(out_frames - 1)) : 0.0;
            size_t idx0 = (size_t)pos;
            size_t idx1 = (idx0 + 1 <= max_in_idx) ? (idx0 + 1) : max_in_idx;
            float frac = (float)(pos - (double)idx0);
            mono_out[j] = (1.0f - frac) * mono_in[idx0] + frac * mono_in[idx1];
        }
        free(mono_in);
        mono_in = NULL;
    }

    // Construct 16-bit mono PCM output WAV
    size_t out_pcm_bytes = out_frames * 2;
    size_t total_out_size = 44 + out_pcm_bytes;
    uint8_t *out_wav = (uint8_t *)malloc(total_out_size);
    if (!out_wav) {
        free(mono_out);
        return -1;
    }

    // RIFF Chunk
    memcpy(out_wav + 0, "RIFF", 4);
    write_u32_le(out_wav + 4, (uint32_t)(36 + out_pcm_bytes));
    memcpy(out_wav + 8, "WAVE", 4);

    // fmt subchunk
    memcpy(out_wav + 12, "fmt ", 4);
    write_u32_le(out_wav + 16, 16);               // Subchunk1Size (16 for PCM)
    write_u16_le(out_wav + 20, 1);                // AudioFormat (1 = PCM)
    write_u16_le(out_wav + 22, 1);                // NumChannels (1 = Mono)
    write_u32_le(out_wav + 24, target_rate);      // SampleRate (22050)
    write_u32_le(out_wav + 28, target_rate * 2);  // ByteRate (22050 * 1 * 2)
    write_u16_le(out_wav + 32, 2);                // BlockAlign (1 * 2)
    write_u16_le(out_wav + 34, 16);               // BitsPerSample (16)

    // data subchunk
    memcpy(out_wav + 36, "data", 4);
    write_u32_le(out_wav + 40, (uint32_t)out_pcm_bytes);

    // Quantize float samples to int16
    int16_t *dst_samples = (int16_t *)(out_wav + 44);
    for (size_t i = 0; i < out_frames; ++i) {
        float s = mono_out[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int val = (int)round(s * 32767.0f);
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        dst_samples[i] = (int16_t)val;
    }

    free(mono_out);

    *out_data = out_wav;
    *out_size = total_out_size;
    return 0;
}

// ---------------------------------------------------------------------------
// Streaming PAK Archive Converter
// ---------------------------------------------------------------------------

int convert_pak_streaming(const char *in_pak_path, const char *out_pak_path,
                          ProgressCallback progress_cb, void *user_data,
                          ConverterStats *stats) {
    if (!in_pak_path || !out_pak_path) {
        return -1;
    }

    FILE *in_fp = fopen(in_pak_path, "rb");
    if (!in_fp) {
        fprintf(stderr, "[SETUP] Error: Cannot open input PAK archive '%s'\n", in_pak_path);
        return -1;
    }

    // 1. Read header
    uint32_t magic = 0, version = 0;
    if (fread(&magic, 4, 1, in_fp) != 1 || fread(&version, 4, 1, in_fp) != 1) {
        fclose(in_fp);
        return -1;
    }

    int encrypted_header = 0;
    if (magic == POP_PAK_MAGIC_ENCRYPTED) {
        magic ^= 0xF7F7F7F7;
        version ^= 0xF7F7F7F7;
        encrypted_header = 1;
    }

    if (magic != POP_PAK_MAGIC || version != POP_PAK_VERSION) {
        fprintf(stderr, "[SETUP] Error: Invalid PAK magic (0x%08X) or version (%u)\n", magic, version);
        fclose(in_fp);
        return -1;
    }

    // 2. Parse record table metadata
    size_t cap_entries = 128;
    size_t count_entries = 0;
    PakEntryInfo *entries = (PakEntryInfo *)malloc(cap_entries * sizeof(PakEntryInfo));
    if (!entries) {
        fclose(in_fp);
        return -1;
    }

    while (1) {
        uint8_t flags = 0;
        if (fread(&flags, 1, 1, in_fp) != 1) break;
        if (encrypted_header) flags ^= POP_PAK_XOR_KEY;
        if (flags & POP_PAK_FLAG_END) break;

        uint8_t name_len = 0;
        if (fread(&name_len, 1, 1, in_fp) != 1) {
            free(entries);
            fclose(in_fp);
            return -1;
        }
        if (encrypted_header) name_len ^= POP_PAK_XOR_KEY;

        if (count_entries >= cap_entries) {
            cap_entries *= 2;
            PakEntryInfo *new_entries = (PakEntryInfo *)realloc(entries, cap_entries * sizeof(PakEntryInfo));
            if (!new_entries) {
                free(entries);
                fclose(in_fp);
                return -1;
            }
            entries = new_entries;
        }

        PakEntryInfo *entry = &entries[count_entries];
        memset(entry, 0, sizeof(*entry));
        entry->flags = flags;

        if (fread(entry->name, 1, name_len, in_fp) != name_len) {
            free(entries);
            fclose(in_fp);
            return -1;
        }
        if (encrypted_header) {
            for (uint8_t k = 0; k < name_len; ++k) {
                entry->name[k] ^= POP_PAK_XOR_KEY;
            }
        }
        entry->name[name_len] = '\0';

        uint8_t meta_bytes[12];
        if (fread(meta_bytes, 1, 12, in_fp) != 12) {
            free(entries);
            fclose(in_fp);
            return -1;
        }
        if (encrypted_header) {
            for (int k = 0; k < 12; ++k) {
                meta_bytes[k] ^= POP_PAK_XOR_KEY;
            }
        }
        memcpy(&entry->orig_size, meta_bytes, 4);
        memcpy(&entry->low_time, meta_bytes + 4, 4);
        memcpy(&entry->high_time, meta_bytes + 8, 4);

        entry->new_size = entry->orig_size;
        count_entries++;
    }

    // Calculate payload absolute offsets in input archive
    long current_in_offset = ftell(in_fp);
    for (size_t i = 0; i < count_entries; ++i) {
        entries[i].data_offset = (uint32_t)current_in_offset;
        current_in_offset += entries[i].orig_size;
    }

    // 3. Open output archive for streaming write
    FILE *out_fp = fopen(out_pak_path, "wb+");
    if (!out_fp) {
        fprintf(stderr, "[SETUP] Error: Cannot create output PAK '%s'\n", out_pak_path);
        free(entries);
        fclose(in_fp);
        return -1;
    }

    // Write output header (always standard unencrypted header in output)
    uint32_t out_magic = POP_PAK_MAGIC;
    uint32_t out_version = POP_PAK_VERSION;
    fwrite(&out_magic, 4, 1, out_fp);
    fwrite(&out_version, 4, 1, out_fp);


    // Write placeholder record table (records exact name lengths, dummy sizes)
    for (size_t i = 0; i < count_entries; ++i) {
        entries[i].record_offset = ftell(out_fp);

        uint8_t flag = entries[i].flags;
        uint8_t name_len = (uint8_t)strlen(entries[i].name);
        uint32_t dummy_size = 0;

        fwrite(&flag, 1, 1, out_fp);
        fwrite(&name_len, 1, 1, out_fp);
        fwrite(entries[i].name, 1, name_len, out_fp);
        fwrite(&dummy_size, 4, 1, out_fp);
        fwrite(&entries[i].low_time, 4, 1, out_fp);
        fwrite(&entries[i].high_time, 4, 1, out_fp);
    }

    uint8_t end_flag = POP_PAK_FLAG_END;
    fwrite(&end_flag, 1, 1, out_fp);

    // 4. Stream and convert payload file by file
    size_t peak_memory = sizeof(PakEntryInfo) * cap_entries;
    size_t images_scaled = 0;
    size_t audio_resampled = 0;
    size_t passthrough_files = 0;
    size_t total_in_bytes = 0;
    size_t total_out_bytes = 0;

    for (size_t i = 0; i < count_entries; ++i) {
        fseek(in_fp, entries[i].data_offset, SEEK_SET);

        size_t orig_sz = entries[i].orig_size;
        total_in_bytes += orig_sz;

        uint8_t *in_buf = (uint8_t *)malloc(orig_sz ? orig_sz : 1);
        if (!in_buf) {
            fclose(in_fp);
            fclose(out_fp);
            free(entries);
            return -1;
        }

        if (orig_sz > 0 && fread(in_buf, 1, orig_sz, in_fp) != orig_sz) {
            free(in_buf);
            fclose(in_fp);
            fclose(out_fp);
            free(entries);
            return -1;
        }

        // Fast XOR 0xF7 decryption
        for (size_t b = 0; b < orig_sz; ++b) {
            in_buf[b] ^= POP_PAK_XOR_KEY;
        }

        uint8_t *final_buf = in_buf;
        size_t final_sz = orig_sz;
        int converted = 0;

        // Track memory usage for this file
        size_t file_mem = peak_memory + orig_sz;

        const char *fname = entries[i].name;
        if (!is_preserved_image(fname) &&
            (str_ends_with_case_insensitive(fname, ".png") ||
             str_ends_with_case_insensitive(fname, ".jpg") ||
             str_ends_with_case_insensitive(fname, ".jpeg"))) {
            uint8_t *scaled_data = NULL;
            size_t scaled_sz = 0;
            if (convert_image_buffer(in_buf, orig_sz, fname, &scaled_data, &scaled_sz) == 0 && scaled_data) {
                final_buf = scaled_data;
                final_sz = scaled_sz;
                free(in_buf);
                converted = 1;
                images_scaled++;
                file_mem += scaled_sz;
            } else {
                passthrough_files++;
            }
        } else if (str_ends_with_case_insensitive(fname, ".wav")) {
            uint8_t *resampled_data = NULL;
            size_t resampled_sz = 0;
            if (convert_wav_buffer(in_buf, orig_sz, &resampled_data, &resampled_sz) == 0 && resampled_data) {
                final_buf = resampled_data;
                final_sz = resampled_sz;
                free(in_buf);
                converted = 1;
                audio_resampled++;
                file_mem += resampled_sz;
            } else {
                passthrough_files++;
            }
        } else {
            passthrough_files++;
        }

        if (file_mem > peak_memory) {
            peak_memory = file_mem;
        }

        // Fast XOR 0xF7 encryption for container output
        for (size_t b = 0; b < final_sz; ++b) {
            final_buf[b] ^= POP_PAK_XOR_KEY;
        }

        if (final_sz > 0) {
            fwrite(final_buf, 1, final_sz, out_fp);
        }

        entries[i].new_size = (uint32_t)final_sz;
        total_out_bytes += final_sz;

        free(final_buf);

        if (progress_cb) {
            float prog = (float)(i + 1) / (float)count_entries;
            progress_cb(prog, entries[i].name, user_data);
        }
    }

    // 5. Seek back and update record table with final sizes
    for (size_t i = 0; i < count_entries; ++i) {
        uint8_t name_len = (uint8_t)strlen(entries[i].name);
        long size_offset = entries[i].record_offset + 1 + 1 + name_len;
        fseek(out_fp, size_offset, SEEK_SET);
        uint32_t final_size = entries[i].new_size;
        fwrite(&final_size, 4, 1, out_fp);
    }

    fclose(in_fp);
    fclose(out_fp);

    if (stats) {
        stats->total_files = count_entries;
        stats->processed_files = count_entries;
        stats->images_scaled = images_scaled;
        stats->audio_resampled = audio_resampled;
        stats->passthrough_files = passthrough_files;
        stats->total_in_bytes = total_in_bytes;
        stats->total_out_bytes = total_out_bytes;
        stats->peak_memory_bytes = peak_memory;
    }

    free(entries);
    return 0;
}

// ---------------------------------------------------------------------------
// Transactional Finalization & Self-Destruction
// ---------------------------------------------------------------------------

int finalize_installation(const char *tmp_pak, const char *final_pak,
                          const char *raw_pak, const char *bak_pak,
                          const char *marker_path, const char *self_exe) {
    if (!tmp_pak || !final_pak) {
        return -1;
    }

    // 1. Atomic rename main_mmp.pak.tmp -> main_mmp.pak
    // Remove destination if it already exists (ensures compatibility across FAT32/Windows)
    remove(final_pak);
    if (rename(tmp_pak, final_pak) != 0) {
        fprintf(stderr, "[SETUP] Error: Failed to rename '%s' -> '%s'\n", tmp_pak, final_pak);
        return -1;
    }

    // 2. Rename raw main.pak -> main.pak.bak
    if (raw_pak && bak_pak) {
        remove(bak_pak);
        if (rename(raw_pak, bak_pak) != 0) {
            fprintf(stderr, "[SETUP] Warning: Could not rename '%s' -> '%s'\n", raw_pak, bak_pak);
        }
    }

    // 3. Create .installed marker
    if (marker_path) {
        FILE *mf = fopen(marker_path, "w");
        if (mf) {
            fprintf(mf, "installed\n");
            fclose(mf);
        }
    }

    // 4. Flush filesystem cache to SD card
#ifndef _WIN32
    sync();
#endif

    // 5. Self-destruction: unlink binary
    if (self_exe && *self_exe) {
#ifndef _WIN32
        unlink(self_exe);
#else
        remove(self_exe);
#endif
    }

    return 0;
}
