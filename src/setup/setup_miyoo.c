#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setup_converter.h"
#include "fb_progress.h"

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

static void on_progress_update(float progress, const char *current_file, void *user_data) {
    (void)user_data;
    fb_progress_update(progress, current_file);
}

int main(int argc, char *argv[]) {
    const char *raw_pak_path = "main.pak";
    const char *tmp_pak_path = "main_mmp.pak.tmp";
    const char *final_pak_path = "main_mmp.pak";
    const char *bak_pak_path = "main.pak.bak";
    const char *marker_path = ".installed";

    // Allow overriding input/output via CLI arguments
    if (argc >= 2) {
        raw_pak_path = argv[1];
    }

    printf("=====================================================\n");
    printf(" Plants vs. Zombies — On-Device Miyoo Asset Setup\n");
    printf("=====================================================\n");
    printf("Input archive:  %s\n", raw_pak_path);
    printf("Output archive: %s\n", final_pak_path);

    // Verify raw input archive exists
    if (access(raw_pak_path, F_OK) != 0) {
        fprintf(stderr, "[SETUP] Error: Target archive '%s' not found!\n", raw_pak_path);
        fprintf(stderr, "Please place your PC 'main.pak' file in this folder.\n");
        return 1;
    }

    // 1. Initialize framebuffer UI
    if (fb_progress_init() != 0) {
        fprintf(stderr, "[SETUP] Warning: Framebuffer init returned non-zero (continuing)\n");
    }

    fb_progress_update(0.0f, "Iniciando analisis de archivos...");

    // 2. Perform sequential streaming conversion
    ConverterStats stats = {0};
    int rc = convert_pak_streaming(raw_pak_path, tmp_pak_path, on_progress_update, NULL, &stats);

    if (rc != 0) {
        fprintf(stderr, "\n[SETUP] Fatal Error: Asset conversion failed! (code %d)\n", rc);
        remove(tmp_pak_path);
        fb_progress_close();
        return 1;
    }

    printf("\n[SETUP] Conversion Summary:\n");
    printf("  Total files:     %zu\n", stats.total_files);
    printf("  Images scaled:   %zu (0.8x downscale)\n", stats.images_scaled);
    printf("  Audio converted: %zu (22.05 kHz mono PCM)\n", stats.audio_resampled);
    printf("  Passthrough:     %zu\n", stats.passthrough_files);
    printf("  Input size:      %.2f MB\n", (double)stats.total_in_bytes / (1024.0 * 1024.0));
    printf("  Output size:     %.2f MB\n", (double)stats.total_out_bytes / (1024.0 * 1024.0));
    printf("  Peak RAM:        %.2f MB (Ceiling: 10.00 MB)\n", (double)stats.peak_memory_bytes / (1024.0 * 1024.0));

    // 3. Finalize installation transaction & self-destruction
    fb_progress_update(1.0f, "Finalizando instalacion y limpiando...");

    // We pass argv[0] so setup_miyoo can unlink itself upon success
    const char *self_binary = (argc > 0) ? argv[0] : NULL;
    int finalize_rc = finalize_installation(
        tmp_pak_path,
        final_pak_path,
        raw_pak_path,
        bak_pak_path,
        marker_path,
        self_binary
    );

    if (finalize_rc != 0) {
        fprintf(stderr, "[SETUP] Error finalizing installation transaction!\n");
        fb_progress_close();
        return 1;
    }

    fb_progress_close();
    printf("[SETUP] Installation successfully completed! Ready to launch PvZ.\n");
    return 0;
}
