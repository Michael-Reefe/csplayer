/*
 * test_fft.c — Unit tests for src/fft.c.
 *
 * All tests run without SDL_Init (SDL_memset and assert work standalone).
 * Tests cover every branch in fft_init, fft_process, fft_quit, and all
 * static helpers (uint_log2, rfft_plan_init, c2c_fft, rfft_execute) which
 * are exercised transitively through the public API.
 */

#include <math.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "harness.h"
#include "fft.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static fft_t g_fft;

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_fft_init_quit(void)
{
    TEST_BEGIN("fft_init_quit");
    fft_t f;
    SDL_memset(&f, 0, sizeof(f));
    CHECK(fft_init(&f));
    CHECK(f.plan.N == (u32)FFT_SIZE);
    fft_quit(&f);
    CHECK(f.plan.N == 0);
    TEST_END();
}

static void test_fft_hann_window(void)
{
    TEST_BEGIN("fft_hann_window");
    /* Endpoints should be near 0, midpoint near 1. */
    CHECK_NEAR(g_fft.window[0],            0.0f, 1e-5f);
    CHECK_NEAR(g_fft.window[FFT_SIZE - 1], 0.0f, 1e-3f);
    CHECK_NEAR(g_fft.window[FFT_SIZE / 2], 1.0f, 1e-4f);
    TEST_END();
}

static void test_fft_process_silence(void)
{
    TEST_BEGIN("fft_process_silence");
    static f32 pcm[FFT_SIZE]; /* zero-initialised (static) */
    fft_process(&g_fft, pcm);
    for (i32 k = 0; k < FFT_BINS; k++) {
        CHECK(g_fft.mags[k] < 1e-6f);
    }
    TEST_END();
}

static void test_fft_process_dc(void)
{
    TEST_BEGIN("fft_process_dc");
    static f32 pcm[FFT_SIZE];
    for (i32 i = 0; i < FFT_SIZE; i++) pcm[i] = 1.0f;
    fft_process(&g_fft, pcm);
    /* DC bin (k=0) must be the largest. */
    f32 dc = g_fft.mags[0];
    for (i32 k = 1; k < FFT_BINS; k++) {
        CHECK(dc >= g_fft.mags[k]);
    }
    TEST_END();
}

static void test_fft_process_sine_peak(void)
{
    TEST_BEGIN("fft_process_sine_peak");
    /* Sine at bin k0 = 16 → peak within ±2 bins of k0. */
    static const i32 k0 = 16;
    static f32 pcm[FFT_SIZE];
    for (i32 i = 0; i < FFT_SIZE; i++) {
        f32 phase = 2.0f * 3.14159265f * (f32)k0 * (f32)i / (f32)FFT_SIZE;
        pcm[i] = sinf(phase);
    }
    fft_process(&g_fft, pcm);

    /* Find peak bin. */
    i32 peak = 0;
    for (i32 k = 1; k < FFT_BINS; k++) {
        if (g_fft.mags[k] > g_fft.mags[peak]) peak = k;
    }
    CHECK(peak >= k0 - 2 && peak <= k0 + 2);
    TEST_END();
}

static void test_fft_process_nyquist(void)
{
    TEST_BEGIN("fft_process_nyquist");
    /* Alternating ±1 → energy concentrated in the top bin. */
    static f32 pcm[FFT_SIZE];
    for (i32 i = 0; i < FFT_SIZE; i++) pcm[i] = (i & 1) ? -1.0f : 1.0f;
    fft_process(&g_fft, pcm);
    f32 nyquist = g_fft.mags[FFT_BINS - 1];
    for (i32 k = 0; k < FFT_BINS - 1; k++) {
        CHECK(nyquist >= g_fft.mags[k]);
    }
    TEST_END();
}

static void test_fft_plan_bit_reversal(void)
{
    TEST_BEGIN("fft_plan_bit_reversal");
    /* bit_rev[0] == 0 → no-swap path in c2c_fft (k not > i). */
    CHECK(g_fft.plan.bit_rev[0] == 0);
    /* For M=FFT_SIZE/2=4096, log2M=12. bit_rev[1]=0b100000000000=2048 > 1,
     * so the swap path fires for i=1. */
    CHECK(g_fft.plan.bit_rev[1] > 1);
    TEST_END();
}

/* ── Suite ───────────────────────────────────────────────────────────────── */

void run_fft_tests(void)
{
    SDL_memset(&g_fft, 0, sizeof(g_fft));
    fft_init(&g_fft);

    test_fft_init_quit();

    /* Re-init after quit test. */
    SDL_memset(&g_fft, 0, sizeof(g_fft));
    fft_init(&g_fft);

    test_fft_hann_window();
    test_fft_process_silence();
    test_fft_process_dc();
    test_fft_process_sine_peak();
    test_fft_process_nyquist();
    test_fft_plan_bit_reversal();

    fft_quit(&g_fft);
}
