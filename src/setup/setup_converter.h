#ifndef SETUP_CONVERTER_H
#define SETUP_CONVERTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POP_PAK_MAGIC    0xBAC04AC0
#define POP_PAK_VERSION  0x00000000
#define POP_PAK_XOR_KEY  0xF7
#define POP_PAK_FLAG_END 0x80

typedef struct {
    char name[256];
    uint32_t orig_size;
    uint32_t new_size;
    uint32_t data_offset;
    long record_offset;
    uint32_t low_time;
    uint32_t high_time;
    uint8_t flags;
} PakEntryInfo;

typedef struct {
    size_t total_files;
    size_t processed_files;
    size_t images_scaled;
    size_t audio_resampled;
    size_t passthrough_files;
    size_t total_in_bytes;
    size_t total_out_bytes;
    size_t peak_memory_bytes;
} ConverterStats;

typedef void (*ProgressCallback)(float progress, const char *status_text, void *user_data);

/* Downscales PNG or JPEG image buffer to 80% (scale_factor 0.8) using stb_image and stb_image_resize2.
 * Allocates *out_data with malloc; caller must free(*out_data).
 * Returns 0 on success, or non-zero on error. */
int convert_image_buffer(const uint8_t *in_data, size_t in_size, const char *filename,
                         uint8_t **out_data, size_t *out_size);

/* Resamples WAV audio to 22050 Hz, 16-bit mono PCM.
 * Allocates *out_data with malloc; caller must free(*out_data).
 * Returns 0 on success, or non-zero on error. */
int convert_wav_buffer(const uint8_t *in_data, size_t in_size,
                       uint8_t **out_data, size_t *out_size);

/* Streams input PAK file sequentially, converts images & WAVs, and writes output PAK.
 * Ensures peak RAM <= 10 MB by streaming file-by-file. */
int convert_pak_streaming(const char *in_pak_path, const char *out_pak_path,
                          ProgressCallback progress_cb, void *user_data,
                          ConverterStats *stats);

/* Transactional finalization:
 * 1. Renames tmp_pak -> final_pak
 * 2. Renames raw_pak -> bak_pak
 * 3. Creates marker file (e.g. .installed)
 * 4. Calls sync()
 * 5. If self_exe is non-null, unlinks it (unlink(self_exe)).
 * Returns 0 on success, non-zero on failure. */
int finalize_installation(const char *tmp_pak, const char *final_pak,
                          const char *raw_pak, const char *bak_pak,
                          const char *marker_path, const char *self_exe);

#ifdef __cplusplus
}
#endif

#endif /* SETUP_CONVERTER_H */
