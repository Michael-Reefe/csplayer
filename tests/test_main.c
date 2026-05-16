/*
 * test_main.c — Entry point for the csplayer test suite.
 *
 * Initialises SDL (dummy audio driver + offscreen video driver) so both unit
 * and integration tests run without real hardware.  Writes a 100 ms WAV file
 * of silence for audio_load tests, then calls each test suite in order.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>

#include "harness.h"

/* ── Global state (declared in harness.h) ───────────────────────────────── */

i32         g_passed       = 0;
i32         g_failed       = 0;
const char *g_current_test = "";
i32         g_test_failed  = 0;
clock_t     g_test_clock   = 0;

/* Path to the temporary WAV file used by audio integration tests. */
char g_test_wav_path[512] = {0};

/* ── Suite forward declarations ──────────────────────────────────────────── */

void run_fft_tests(void);
void run_spectrum_tests(void);
void run_playlist_tests(void);
void run_audio_tests(void);
void run_ui_tests(void);

/* ── Test WAV generation ─────────────────────────────────────────────────── */

/*
 * Write a minimal valid WAV to path: 100 ms of silence at 44100 Hz,
 * 16-bit signed little-endian, mono.  Returns true on success.
 */
static bool create_test_wav(const char *path)
{
    static const unsigned int  sample_rate = 44100;
    static const unsigned int  num_samples = 4410;   /* 100 ms */
    static const unsigned int  data_size   = 4410 * 2;
    static const unsigned int  file_size   = 36 + 4410 * 2;
    static const unsigned short channels   = 1;
    static const unsigned short bits       = 16;
    static const unsigned short audio_fmt  = 1;  /* PCM */
    static const unsigned int   byte_rate  = 44100 * 2;
    static const unsigned short blk_align  = 2;
    static const unsigned int   fmt_sz     = 16;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    fwrite("RIFF",      1, 4, f);
    fwrite(&file_size,  4, 1, f);
    fwrite("WAVE",      1, 4, f);
    fwrite("fmt ",      1, 4, f);
    fwrite(&fmt_sz,     4, 1, f);
    fwrite(&audio_fmt,  2, 1, f);
    fwrite(&channels,   2, 1, f);
    fwrite(&sample_rate,4, 1, f);
    fwrite(&byte_rate,  4, 1, f);
    fwrite(&blk_align,  2, 1, f);
    fwrite(&bits,       2, 1, f);
    fwrite("data",      1, 4, f);
    fwrite(&data_size,  4, 1, f);

    /* 100 ms of silence — 8820 bytes of zeros. */
    static const unsigned char silence[8820] = {0};
    fwrite(silence, 1, num_samples * 2, f);

    return fclose(f) == 0;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Use virtual/headless SDL subsystems so tests run without hardware. */
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),
                               "SDL_AUDIODRIVER", "dummy", true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(),
                               "SDL_VIDEODRIVER", "offscreen", true);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Build path for the test WAV next to the current working directory. */
    SDL_snprintf(g_test_wav_path, sizeof(g_test_wav_path), "csplayer_test.wav");

    if (!create_test_wav(g_test_wav_path)) {
        fprintf(stderr, "Failed to create test WAV at '%s'\n", g_test_wav_path);
        SDL_Quit();
        return 1;
    }

    clock_t t_start = clock();

    printf("\n=== csplayer test suite ===\n\n");

    printf("-- fft --\n");
    run_fft_tests();

    printf("\n-- spectrum --\n");
    run_spectrum_tests();

    printf("\n-- playlist --\n");
    run_playlist_tests();

    printf("\n-- audio (integration) --\n");
    run_audio_tests();

    printf("\n-- ui (integration) --\n");
    run_ui_tests();

    f64 total_ms = 1000.0 * (f64)(clock() - t_start) / (f64)CLOCKS_PER_SEC;
    printf("\n");
    if (g_failed == 0) {
        printf("All %d tests passed  (%.1f ms)\n", g_passed, total_ms);
    } else {
        printf("%d passed, %d FAILED  (%.1f ms)\n",
               g_passed, g_failed, total_ms);
    }

    /* Cleanup. */
    remove(g_test_wav_path);
    SDL_Quit();

    return g_failed != 0 ? 1 : 0;
}
