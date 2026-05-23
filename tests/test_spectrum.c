/*
 * test_spectrum.c — Unit tests for src/spectrum.c.
 *
 * Tests run without SDL_Init.  The spectrum module uses SDL_FPoint (a struct)
 * but no SDL subsystem calls.  We exercise every branch in spectrum_init_lut
 * and spectrum_update.
 */

#include <assert.h>
#include <math.h>
#include <SDL3/SDL.h>

#include "harness.h"
#include "spectrum.h"
#include "fft.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static spectrum_t g_spec;
static fft_t      g_fft;
static f32        g_samples[RING_SIZE];

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_spectrum_lut_20hz(void)
{
    TEST_BEGIN("spectrum_lut_20hz");
    /* At 44100 Hz, 20 Hz maps to bin ≈ 20*8192/44100 ≈ 3.7 → bin_lo ≈ 3 */
    CHECK(g_spec.bin_lo[0] >= 0 && g_spec.bin_lo[0] <= 5);
    CHECK(g_spec.bin_frac[0] >= 0.0f && g_spec.bin_frac[0] < 1.0f);
    TEST_END();
}

static void test_spectrum_lut_20khz(void)
{
    TEST_BEGIN("spectrum_lut_20khz");
    /* At 44100 Hz, 20 kHz → bin ≈ 20000*8192/44100 ≈ 3714.
     * The last column's bin should be close to that value. */
    i32 last = g_spec.bin_lo[SPEC_W - 1];
    CHECK(last >= 3700 && last < FFT_BINS);
    CHECK(g_spec.bin_frac[SPEC_W - 1] >= 0.0f && g_spec.bin_frac[SPEC_W - 1] < 1.0f);
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
        CHECK(g_spec.smooth_db[x] >= DB_BOT - 1e-4f);
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
        CHECK(g_spec.pts[x].y <= (f32)(SPEC_Y + SPEC_H));
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
        CHECK(g_spec.pts[x].y >= (f32)SPEC_Y);
    }
    TEST_END();
}

/* ── spectrum_update_time tests ──────────────────────────────────────────── */

static void test_time_silence(void)
{
    TEST_BEGIN("time_silence");
    SDL_memset(g_samples, 0, sizeof(g_samples));
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    f32 expected_y = (f32)SPEC_Y + (f32)SPEC_H * 0.5f;
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(fabsf(g_spec.time_pts[x].y - expected_y) < 1.0f);
        CHECK(g_spec.time_pts[x].x == (f32)(SPEC_X + x));
    }
    TEST_END();
}

static void test_time_full_pos(void)
{
    TEST_BEGIN("time_full_pos");
    /* Pre-seed smooth_time to the steady-state value (+1) so one call suffices. */
    for (i32 x = 0; x < SPEC_W; x++) { g_spec.smooth_time[x] = 1.0f; }
    for (i32 i = 0; i < RING_SIZE; i++) { g_samples[i] = 1.0f; }
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(fabsf(g_spec.time_pts[x].y - (f32)SPEC_Y) < 1.0f);
        CHECK(g_spec.time_pts[x].x == (f32)(SPEC_X + x));
    }
    TEST_END();
}

static void test_time_full_neg(void)
{
    TEST_BEGIN("time_full_neg");
    /* Pre-seed smooth_time to the steady-state value (-1) so one call suffices. */
    for (i32 x = 0; x < SPEC_W; x++) { g_spec.smooth_time[x] = -1.0f; }
    for (i32 i = 0; i < RING_SIZE; i++) { g_samples[i] = -1.0f; }
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    f32 expected_y = (f32)(SPEC_Y + SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(fabsf(g_spec.time_pts[x].y - expected_y) < 1.0f);
        CHECK(g_spec.time_pts[x].x == (f32)(SPEC_X + x));
    }
    TEST_END();
}

static void test_time_trigger_found(void)
{
    TEST_BEGIN("time_trigger_found");
    /* Fill with -0.5 then inject a single neg→pos crossing at index 7200,
     * which lies within the search window [6797, 7309] for win=882 at 44100 Hz.
     * Reset prev_window so xcorr falls back to the zero-crossing trigger.
     * Reset smooth_time so smoothing starts from 0 (neutral). */
    SDL_memset(g_spec.prev_window,  0, sizeof(g_spec.prev_window));
    SDL_memset(g_spec.smooth_time,  0, sizeof(g_spec.smooth_time));
    for (i32 i = 0; i < RING_SIZE; i++) { g_samples[i] = -0.5f; }
    g_samples[7200] = 0.5f;   /* samples[7199]<0 and samples[7200]>=0 → trigger */
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    /* Window starts at the +0.5 trigger → first point is in the upper half. */
    f32 center = (f32)SPEC_Y + (f32)SPEC_H * 0.5f;
    CHECK(g_spec.time_pts[0].y < center);
    CHECK(g_spec.time_pts[0].x == (f32)SPEC_X);
    TEST_END();
}

static void test_time_no_trigger(void)
{
    TEST_BEGIN("time_no_trigger");
    /* All-positive samples → no neg→pos crossing → falls back to last win samples. */
    for (i32 i = 0; i < RING_SIZE; i++) { g_samples[i] = 0.5f; }
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)SPEC_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(SPEC_Y + SPEC_H));
    }
    TEST_END();
}

static void test_time_pts_bounds(void)
{
    TEST_BEGIN("time_pts_bounds");
    /* Alternating ±1 — exercises amplitude clamping on every sample. */
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    }
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)SPEC_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(SPEC_Y + SPEC_H));
        CHECK(g_spec.time_pts[x].x >= (f32)SPEC_X);
        CHECK(g_spec.time_pts[x].x <  (f32)(SPEC_X + SPEC_W));
    }
    TEST_END();
}

static void test_time_both_rects(void)
{
    TEST_BEGIN("time_both_rects");
    /* Same zero input mapped to upper-half rect then lower-half rect:
     * centre y must differ by exactly SPEC_H/2. */
    SDL_memset(g_samples, 0, sizeof(g_samples));
    i32 half = SPEC_H / 2;
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, half);
    f32 upper_y = g_spec.time_pts[0].y;
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y + half, half);
    f32 lower_y = g_spec.time_pts[0].y;
    CHECK(lower_y > upper_y);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)(SPEC_Y + half));
        CHECK(g_spec.time_pts[x].y <= (f32)(SPEC_Y + SPEC_H));
    }
    TEST_END();
}

static void test_time_low_sr(void)
{
    TEST_BEGIN("time_low_sr");
    /* sample_rate=100 Hz → win≈1 sample; must not crash and pts must stay in bounds. */
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = (i % 2 == 0) ? 0.5f : -0.5f;
    }
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 100.0f, SPEC_Y, SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)SPEC_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(SPEC_Y + SPEC_H));
    }
    TEST_END();
}

static void test_time_xcorr_aligns(void)
{
    TEST_BEGIN("time_xcorr_aligns");
    /* Unit spike at P1=7100, zeros elsewhere.  Set prev_window[0]=1 so xcorr
     * at c=P1 gives corr=1.0 (unique maximum over all candidates = 0).
     * xcorr must select P1 → pts[0] maps to amplitude +1 → y == SPEC_Y. */
    i32 win = (i32)(WAVE_WINDOW_S * 44100.0f);  /* 882 */
    i32 P1  = 7100;
    assert(P1 >= (RING_SIZE - XCORR_SEARCH) && P1 + win <= RING_SIZE);
    SDL_memset(g_samples,          0, sizeof(g_samples));
    SDL_memset(g_spec.prev_window, 0, sizeof(g_spec.prev_window));
    SDL_memset(g_spec.smooth_time, 0, sizeof(g_spec.smooth_time));
    g_samples[P1]          = 1.0f;   /* single +1 spike at P1 */
    g_spec.prev_window[0]  = 1.0f;   /* only pixel-0 participates in xcorr */
    g_spec.smooth_time[0]  = 1.0f;   /* steady-state so pts[0].y == SPEC_Y */
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    /* xcorr found P1 → pts[0] maps to +1.0 → y is at the top of the plot. */
    CHECK(fabsf(g_spec.time_pts[0].y - (f32)SPEC_Y) < 1.0f);
    CHECK(g_spec.time_pts[0].x == (f32)SPEC_X);
    TEST_END();
}

/* ── spectrum_map_freq_pts tests ─────────────────────────────────────────── */

static void test_map_pts_upper_half(void)
{
    TEST_BEGIN("map_pts_upper_half");
    for (i32 k = 0; k < FFT_BINS; k++) { g_fft.mags[k] = 0.1f; }
    for (i32 x = 0; x < SPEC_W; x++) { g_spec.smooth_db[x] = DB_BOT; }
    spectrum_update(&g_spec, &g_fft);
    i32 half = SPEC_H / 2;
    spectrum_map_freq_pts(&g_spec, SPEC_Y, half);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.pts[x].y >= (f32)SPEC_Y);
        CHECK(g_spec.pts[x].y <= (f32)(SPEC_Y + half));
    }
    TEST_END();
}

static void test_map_pts_full(void)
{
    TEST_BEGIN("map_pts_full");
    /* map_freq_pts(SPEC_Y, SPEC_H) must reproduce the same pts as spectrum_update. */
    for (i32 k = 0; k < FFT_BINS; k++) { g_fft.mags[k] = 0.05f; }
    for (i32 x = 0; x < SPEC_W; x++) { g_spec.smooth_db[x] = -45.0f; }
    spectrum_update(&g_spec, &g_fft);
    SDL_FPoint saved[SPEC_W];
    SDL_memcpy(saved, g_spec.pts, sizeof(saved));
    spectrum_map_freq_pts(&g_spec, SPEC_Y, SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(fabsf(g_spec.pts[x].y - saved[x].y) < 1e-3f);
        CHECK(g_spec.pts[x].x == (f32)(SPEC_X + x));
    }
    TEST_END();
}

static void test_map_pts_silence(void)
{
    TEST_BEGIN("map_pts_silence");
    /* DB_BOT → t = 1.0 → y == plot_y + plot_h (bottom of plot). */
    for (i32 x = 0; x < SPEC_W; x++) { g_spec.smooth_db[x] = DB_BOT; }
    spectrum_map_freq_pts(&g_spec, SPEC_Y, SPEC_H);
    f32 expected_y = (f32)(SPEC_Y + SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(fabsf(g_spec.pts[x].y - expected_y) < 1e-3f);
        CHECK(g_spec.pts[x].x == (f32)(SPEC_X + x));
    }
    TEST_END();
}

static void test_time_xcorr_shi_lt_slo(void)
{
    TEST_BEGIN("time_xcorr_shi_lt_slo");
    /* sample_rate=150000 Hz → win=3000 > XCORR_SEARCH=2048 →
     * xcorr_best_start: shi=RING_SIZE-3000=5192 < slo=RING_SIZE-2048=6144
     * → returns fallback immediately; output must still be in-bounds. */
    SDL_memset(g_samples, 0, sizeof(g_samples));
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 150000.0f, SPEC_Y, SPEC_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)SPEC_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(SPEC_Y + SPEC_H));
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

    test_time_silence();
    test_time_full_pos();
    test_time_full_neg();
    test_time_trigger_found();
    test_time_no_trigger();
    test_time_pts_bounds();
    test_time_both_rects();
    test_time_low_sr();
    test_time_xcorr_aligns();
    test_map_pts_upper_half();
    test_map_pts_full();
    test_map_pts_silence();
    test_time_xcorr_shi_lt_slo();

    fft_quit(&g_fft);
}
