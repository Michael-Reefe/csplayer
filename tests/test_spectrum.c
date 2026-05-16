/*
 * test_spectrum.c — Unit tests for src/spectrum.c.
 *
 * Tests run without SDL_Init.  The spectrum module uses SDL_FPoint (a struct)
 * but no SDL subsystem calls.  We exercise every branch in spectrum_init_lut
 * and spectrum_update.
 */

#include <math.h>
#include <SDL3/SDL.h>

#include "harness.h"
#include "spectrum.h"
#include "fft.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static spectrum_t g_spec;
static fft_t      g_fft;

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_spectrum_lut_20hz(void)
{
    TEST_BEGIN("spectrum_lut_20hz");
    /* At 44100 Hz, 20 Hz maps to bin ≈ 20*8192/44100 ≈ 3.7 → bin_lo ≈ 3 */
    CHECK(g_spec.bin_lo[0] >= 0 && g_spec.bin_lo[0] <= 5);
    TEST_END();
}

static void test_spectrum_lut_20khz(void)
{
    TEST_BEGIN("spectrum_lut_20khz");
    /* At 44100 Hz, 20 kHz → bin ≈ 20000*8192/44100 ≈ 3714.
     * The last column's bin should be close to that value. */
    i32 last = g_spec.bin_lo[SPEC_W - 1];
    CHECK(last >= 3700 && last < FFT_BINS);
    TEST_END();
}

static void test_spectrum_lut_bounds(void)
{
    TEST_BEGIN("spectrum_lut_bounds");
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.bin_lo[x] >= 0 && g_spec.bin_lo[x] < FFT_BINS);
        CHECK(g_spec.bin_hi[x] >= 0 && g_spec.bin_hi[x] < FFT_BINS);
        CHECK(g_spec.bin_hi[x] >= g_spec.bin_lo[x]);
    }
    TEST_END();
}

static void test_spectrum_lut_low_sr(void)
{
    TEST_BEGIN("spectrum_lut_low_sr");
    /* At a very low sample rate (2000 Hz), many columns would map to bins
     * beyond FFT_BINS — exercises the clamp branches in spectrum_init_lut. */
    spectrum_t s;
    SDL_memset(&s, 0, sizeof(s));
    spectrum_init_lut(&s, 2000.0f);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(s.bin_lo[x] >= 0 && s.bin_lo[x] < FFT_BINS);
        CHECK(s.bin_hi[x] >= 0 && s.bin_hi[x] < FFT_BINS);
    }
    TEST_END();
}

static void test_spectrum_update_silence(void)
{
    TEST_BEGIN("spectrum_update_silence");
    /* Zero magnitudes → db < DB_BOT → clamped to DB_BOT. */
    SDL_memset(g_fft.mags, 0, sizeof(g_fft.mags));
    /* Reset smoothing so the previous state does not bleed through. */
    for (i32 x = 0; x < SPEC_W; x++) g_spec.smooth_db[x] = DB_BOT;
    spectrum_update(&g_spec, &g_fft);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.smooth_db[x] >= DB_BOT);
        CHECK(g_spec.smooth_db[x] <= DB_TOP);
    }
    TEST_END();
}

static void test_spectrum_update_overload(void)
{
    TEST_BEGIN("spectrum_update_overload");
    /* Magnitudes > 1 → db > DB_TOP (0 dB) → clamped. */
    for (i32 k = 0; k < FFT_BINS; k++) g_fft.mags[k] = 2.0f;
    for (i32 x = 0; x < SPEC_W; x++) g_spec.smooth_db[x] = DB_TOP;
    spectrum_update(&g_spec, &g_fft);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.smooth_db[x] <= DB_TOP + 1e-4f);
    }
    TEST_END();
}

static void test_spectrum_update_smoothing(void)
{
    TEST_BEGIN("spectrum_update_smoothing");
    /* Two consecutive frames with the same input → smooth_db converges
     * monotonically toward the target.  The second call must not increase
     * the distance from the target relative to the first call. */
    for (i32 k = 0; k < FFT_BINS; k++) g_fft.mags[k] = 0.1f;
    for (i32 x = 0; x < SPEC_W; x++) g_spec.smooth_db[x] = DB_BOT;
    spectrum_update(&g_spec, &g_fft);
    f32 after_first = g_spec.smooth_db[0];
    spectrum_update(&g_spec, &g_fft);
    f32 after_second = g_spec.smooth_db[0];
    /* Both values must be above DB_BOT (signal is non-trivial). */
    CHECK(after_first  > DB_BOT);
    CHECK(after_second > DB_BOT);
    /* Second frame must be closer to the steady-state target (monotone). */
    CHECK(after_second >= after_first);
    TEST_END();
}

static void test_spectrum_pts_in_bounds(void)
{
    TEST_BEGIN("spectrum_pts_in_bounds");
    /* After any spectrum_update, every screen y must be within the plot. */
    for (i32 x = 0; x < SPEC_W; x++) {
        f32 y = g_spec.pts[x].y;
        CHECK(y >= (f32)SPEC_Y);
        CHECK(y <= (f32)(SPEC_Y + SPEC_H));
    }
    TEST_END();
}

static void test_spectrum_t_clamp_lo(void)
{
    TEST_BEGIN("spectrum_t_clamp_lo");
    /* Force smooth_db very high (> DB_TOP) to exercise t < 0 clamp in
     * spectrum_update.  Inject overload then check pts stay in bounds. */
    for (i32 k = 0; k < FFT_BINS; k++) g_fft.mags[k] = 100.0f;
    for (i32 x = 0; x < SPEC_W; x++) g_spec.smooth_db[x] = DB_TOP + 10.0f;
    spectrum_update(&g_spec, &g_fft);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.pts[x].y >= (f32)SPEC_Y);
    }
    TEST_END();
}

static void test_spectrum_t_clamp_hi(void)
{
    TEST_BEGIN("spectrum_t_clamp_hi");
    /* Force smooth_db very low (< DB_BOT) to exercise t > 1 clamp. */
    for (i32 k = 0; k < FFT_BINS; k++) g_fft.mags[k] = 0.0f;
    for (i32 x = 0; x < SPEC_W; x++) g_spec.smooth_db[x] = DB_BOT - 10.0f;
    spectrum_update(&g_spec, &g_fft);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.pts[x].y <= (f32)(SPEC_Y + SPEC_H));
    }
    TEST_END();
}

/* ── Suite ───────────────────────────────────────────────────────────────── */

void run_spectrum_tests(void)
{
    SDL_memset(&g_spec, 0, sizeof(g_spec));
    SDL_memset(&g_fft,  0, sizeof(g_fft));
    fft_init(&g_fft);
    spectrum_init_lut(&g_spec, 44100.0f);

    test_spectrum_lut_20hz();
    test_spectrum_lut_20khz();
    test_spectrum_lut_bounds();
    test_spectrum_lut_low_sr();
    test_spectrum_update_silence();
    test_spectrum_update_overload();
    test_spectrum_update_smoothing();
    test_spectrum_pts_in_bounds();
    test_spectrum_t_clamp_lo();
    test_spectrum_t_clamp_hi();

    fft_quit(&g_fft);
}
