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
        CHECK(y >= (f32)PLOT_Y);
        CHECK(y <= (f32)(PLOT_Y + PLOT_H));
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
        CHECK(g_spec.pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.pts[x].y <= (f32)(PLOT_Y + PLOT_H));
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
        CHECK(g_spec.pts[x].y <= (f32)(PLOT_Y + PLOT_H));
        CHECK(g_spec.pts[x].y >= (f32)PLOT_Y);
    }
    TEST_END();
}

/* ── spectrum_update_time tests ──────────────────────────────────────────── */

static void test_time_silence(void)
{
    TEST_BEGIN("time_silence");
    SDL_memset(g_samples, 0, sizeof(g_samples));
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    f32 expected_y = (f32)PLOT_Y + (f32)PLOT_H * 0.5f;
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
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(fabsf(g_spec.time_pts[x].y - (f32)PLOT_Y) < 1.0f);
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
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    f32 expected_y = (f32)(PLOT_Y + PLOT_H);
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
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    /* Window starts at the +0.5 trigger → first point is in the upper half. */
    f32 center = (f32)PLOT_Y + (f32)PLOT_H * 0.5f;
    CHECK(g_spec.time_pts[0].y < center);
    CHECK(g_spec.time_pts[0].x == (f32)SPEC_X);
    TEST_END();
}

static void test_time_no_trigger(void)
{
    TEST_BEGIN("time_no_trigger");
    /* All-positive samples → no neg→pos crossing → falls back to last win samples. */
    for (i32 i = 0; i < RING_SIZE; i++) { g_samples[i] = 0.5f; }
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(PLOT_Y + PLOT_H));
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
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(PLOT_Y + PLOT_H));
        CHECK(g_spec.time_pts[x].x >= (f32)SPEC_X);
        CHECK(g_spec.time_pts[x].x <  (f32)(SPEC_X + SPEC_W));
    }
    TEST_END();
}

static void test_time_both_rects(void)
{
    TEST_BEGIN("time_both_rects");
    /* Same zero input mapped to upper-half rect then lower-half rect:
     * centre y must differ by exactly PLOT_H/2. */
    SDL_memset(g_samples, 0, sizeof(g_samples));
    i32 half = PLOT_H / 2;
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, half);
    f32 upper_y = g_spec.time_pts[0].y;
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y + half, half);
    f32 lower_y = g_spec.time_pts[0].y;
    CHECK(lower_y > upper_y);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)(PLOT_Y + half));
        CHECK(g_spec.time_pts[x].y <= (f32)(PLOT_Y + PLOT_H));
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
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 100.0f, PLOT_Y, PLOT_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(PLOT_Y + PLOT_H));
    }
    TEST_END();
}

static void test_time_xcorr_aligns(void)
{
    TEST_BEGIN("time_xcorr_aligns");
    /* Unit spike at P1=7100, zeros elsewhere.  Set prev_window[0]=1 so xcorr
     * at c=P1 gives corr=1.0 (unique maximum over all candidates = 0).
     * xcorr must select P1 → pts[0] maps to amplitude +1 → y == PLOT_Y. */
    i32 win = (i32)(WAVE_WINDOW_S * 44100.0f);  /* 882 */
    i32 P1  = 7100;
    assert(P1 >= (RING_SIZE - XCORR_SEARCH) && P1 + win <= RING_SIZE);
    SDL_memset(g_samples,          0, sizeof(g_samples));
    SDL_memset(g_spec.prev_window, 0, sizeof(g_spec.prev_window));
    SDL_memset(g_spec.smooth_time, 0, sizeof(g_spec.smooth_time));
    g_samples[P1]          = 1.0f;   /* single +1 spike at P1 */
    g_spec.prev_window[0]  = 1.0f;   /* only pixel-0 participates in xcorr */
    g_spec.smooth_time[0]  = 1.0f;   /* steady-state so pts[0].y == PLOT_Y */
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    /* xcorr found P1 → pts[0] maps to +1.0 → y is at the top of the plot. */
    CHECK(fabsf(g_spec.time_pts[0].y - (f32)PLOT_Y) < 1.0f);
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
    i32 half = PLOT_H / 2;
    spectrum_map_freq_pts(&g_spec, PLOT_Y, half);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.pts[x].y <= (f32)(PLOT_Y + half));
    }
    TEST_END();
}

static void test_map_pts_full(void)
{
    TEST_BEGIN("map_pts_full");
    /* map_freq_pts(PLOT_Y, PLOT_H) must reproduce the same pts as spectrum_update. */
    for (i32 k = 0; k < FFT_BINS; k++) { g_fft.mags[k] = 0.05f; }
    for (i32 x = 0; x < SPEC_W; x++) { g_spec.smooth_db[x] = -45.0f; }
    spectrum_update(&g_spec, &g_fft);
    SDL_FPoint saved[SPEC_W];
    SDL_memcpy(saved, g_spec.pts, sizeof(saved));
    spectrum_map_freq_pts(&g_spec, PLOT_Y, PLOT_H);
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
    spectrum_map_freq_pts(&g_spec, PLOT_Y, PLOT_H);
    f32 expected_y = (f32)(PLOT_Y + PLOT_H);
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
    spectrum_update_time(&g_spec, g_samples, RING_SIZE, 150000.0f, PLOT_Y, PLOT_H);
    for (i32 x = 0; x < SPEC_W; x++) {
        CHECK(g_spec.time_pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.time_pts[x].y <= (f32)(PLOT_Y + PLOT_H));
    }
    TEST_END();
}

/* ── IIR biquad band filter tests (via spectrum_update_bands) ────────────── */

static void test_biquad_lp_attenuates_highs(void)
{
    TEST_BEGIN("biquad_lp_attenuates_highs");
    /* 8 kHz sine through LP bass band (cutoff 200 Hz) — output should be ~0. */
    f32 sr   = 44100.0f;
    f32 freq = 8000.0f;
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = sinf(2.0f * 3.14159265f * freq * (f32)i / sr);
    }
    SDL_memset(g_spec.smooth_bass, 0, sizeof(g_spec.smooth_bass));
    SDL_memset(g_spec.prev_bass,   0, sizeof(g_spec.prev_bass));
    for (i32 c = 0; c < 30; c++) {
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, PLOT_Y, PLOT_H);
    }
    f32 center = (f32)PLOT_Y + (f32)PLOT_H * 0.5f;
    for (i32 x = 0; x < BAND_W; x++) {
        CHECK(fabsf(g_spec.bass_pts[x].y - center) < 2.0f);
    }
    CHECK(g_spec.bass_pts[0].x >= (f32)SPEC_X);
    TEST_END();
}

static void test_biquad_lp_passes_bass(void)
{
    TEST_BEGIN("biquad_lp_passes_bass");
    /* 40 Hz sine through LP bass band (cutoff 200 Hz) — output should be large. */
    f32 sr   = 44100.0f;
    f32 freq = 40.0f;
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = sinf(2.0f * 3.14159265f * freq * (f32)i / sr);
    }
    SDL_memset(g_spec.smooth_bass, 0, sizeof(g_spec.smooth_bass));
    SDL_memset(g_spec.prev_bass,   0, sizeof(g_spec.prev_bass));
    for (i32 c = 0; c < 30; c++) {
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, PLOT_Y, PLOT_H);
    }
    f32 center = (f32)PLOT_Y + (f32)PLOT_H * 0.5f;
    i32 non_zero = 0;
    for (i32 x = 0; x < BAND_W; x++) {
        if (fabsf(g_spec.bass_pts[x].y - center) > 2.0f) { non_zero++; }
    }
    CHECK(non_zero > BAND_W / 2);
    CHECK(g_spec.bass_pts[0].x >= (f32)SPEC_X);
    TEST_END();
}

static void test_biquad_hp_attenuates_bass(void)
{
    TEST_BEGIN("biquad_hp_attenuates_bass");
    /* 40 Hz sine through HP highs band (cutoff 4 kHz) — output should be ~0. */
    f32 sr   = 44100.0f;
    f32 freq = 40.0f;
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = sinf(2.0f * 3.14159265f * freq * (f32)i / sr);
    }
    SDL_memset(g_spec.smooth_high, 0, sizeof(g_spec.smooth_high));
    SDL_memset(g_spec.prev_high,   0, sizeof(g_spec.prev_high));
    for (i32 c = 0; c < 30; c++) {
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, PLOT_Y, PLOT_H);
    }
    f32 center = (f32)PLOT_Y + (f32)PLOT_H * 0.5f;
    for (i32 x = 0; x < BAND_W; x++) {
        CHECK(fabsf(g_spec.high_pts[x].y - center) < 2.0f);
    }
    CHECK(g_spec.high_pts[0].x >= (f32)(SPEC_X + 2 * BAND_W));
    TEST_END();
}

static void test_bands_pts_bounds_iir(void)
{
    TEST_BEGIN("bands_pts_bounds_iir");
    /* Multi-band signal → all band pts must stay within the plot rect. */
    f32 sr = 44100.0f;
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = 0.4f * sinf(2.0f * 3.14159265f *   73.0f * (f32)i / sr)
                     + 0.4f * sinf(2.0f * 3.14159265f * 1200.0f * (f32)i / sr)
                     + 0.4f * sinf(2.0f * 3.14159265f * 6000.0f * (f32)i / sr);
    }
    for (i32 c = 0; c < 5; c++) {
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, PLOT_Y, PLOT_H);
    }
    for (i32 x = 0; x < BAND_W; x++) {
        CHECK(g_spec.bass_pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.bass_pts[x].y <= (f32)(PLOT_Y + PLOT_H));
        CHECK(g_spec.mid_pts[x].y  >= (f32)PLOT_Y);
        CHECK(g_spec.mid_pts[x].y  <= (f32)(PLOT_Y + PLOT_H));
        CHECK(g_spec.high_pts[x].y >= (f32)PLOT_Y);
        CHECK(g_spec.high_pts[x].y <= (f32)(PLOT_Y + PLOT_H));
    }
    TEST_END();
}

static void test_bands_silence_iir(void)
{
    TEST_BEGIN("bands_silence_iir");
    /* Zero samples, zeroed smooth arrays → all band pts at centre line. */
    SDL_memset(g_samples,          0, sizeof(g_samples));
    SDL_memset(g_spec.smooth_bass, 0, sizeof(g_spec.smooth_bass));
    SDL_memset(g_spec.smooth_mid,  0, sizeof(g_spec.smooth_mid));
    SDL_memset(g_spec.smooth_high, 0, sizeof(g_spec.smooth_high));
    SDL_memset(g_spec.prev_bass,   0, sizeof(g_spec.prev_bass));
    SDL_memset(g_spec.prev_mid,    0, sizeof(g_spec.prev_mid));
    SDL_memset(g_spec.prev_high,   0, sizeof(g_spec.prev_high));
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    f32 center = (f32)PLOT_Y + (f32)PLOT_H * 0.5f;
    for (i32 x = 0; x < BAND_W; x++) {
        CHECK(fabsf(g_spec.bass_pts[x].y - center) < 1.0f);
        CHECK(fabsf(g_spec.mid_pts[x].y  - center) < 1.0f);
        CHECK(fabsf(g_spec.high_pts[x].y - center) < 1.0f);
    }
    TEST_END();
}

static void test_bands_coeff_cache(void)
{
    TEST_BEGIN("bands_coeff_cache");
    /* Coefficients are cached and only rebuilt when the sample rate changes. */
    SDL_memset(g_samples, 0, sizeof(g_samples));

    /* First call at 44.1 kHz builds the coefficients. */
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    CHECK(g_spec.band_coeff_rate == 44100.0f);
    f32 b0_44k = g_spec.band_lp_bass.b0;

    /* Second call at the same rate must NOT rebuild (cache-hit branch): the
     * cached coefficients are unchanged. */
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 44100.0f, PLOT_Y, PLOT_H);
    CHECK(g_spec.band_coeff_rate == 44100.0f);
    CHECK(g_spec.band_lp_bass.b0 == b0_44k);

    /* Changing the rate must rebuild (different cutoff/rate → different b0). */
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 48000.0f, PLOT_Y, PLOT_H);
    CHECK(g_spec.band_coeff_rate == 48000.0f);
    CHECK(g_spec.band_lp_bass.b0 != b0_44k);

    /* spectrum_init_lut() resets the cache so the next call rebuilds. */
    spectrum_init_lut(&g_spec, 48000.0f);
    CHECK(g_spec.band_coeff_rate == 0.0f);
    TEST_END();
}

/* ── Scrolling spectrogram tests ─────────────────────────────────────────── */

/* Set every FFT magnitude to `mag` so col_db is LUT-independent. */
static void set_flat_mags(f32 mag)
{
    for (i32 k = 0; k < FFT_BINS; k++) { g_fft.mags[k] = mag; }
}

static void test_specgram_accumulate_and_advance(void)
{
    TEST_BEGIN("specgram_accumulate_and_advance");
    spectrum_specgram_clear(&g_spec);
    spectrum_specgram_clear_dirty(&g_spec);
    set_flat_mags(1.0f);   /* 0 dBFS everywhere → quantised amplitude 255 */

    /* Sub-row dt: head stays, current frame merges into it (one dirty row). */
    spectrum_update_specgram(&g_spec, &g_fft, SPECGRAM_ROW_S * 0.25f);
    CHECK(g_spec.sg_head == 0);
    CHECK(g_spec.sg_dirty == 1);
    CHECK(g_spec.sg_amp[0][0] == 255);
    CHECK(g_spec.sg_amp[0][SPECGRAM_COLS - 1] == 255);

    /* Crossing one row period advances the head once, wrapping downward.
     * The dirty run is the accumulate-or-zone rule: max(prior + advances,
     * advances + SPECGRAM_HOT_ROWS) — written out so it holds for any
     * configured zone depth, including 0 (hot highlighting disabled). */
    spectrum_update_specgram(&g_spec, &g_fft, SPECGRAM_ROW_S);
    CHECK(g_spec.sg_head == SPECGRAM_ROWS - 1);
    i32 want = 1 + 1;   /* prior merge row + one advance */
    if (want < 1 + SPECGRAM_HOT_ROWS) { want = 1 + SPECGRAM_HOT_ROWS; }
    CHECK(g_spec.sg_dirty == want);
    CHECK(g_spec.sg_amp[SPECGRAM_ROWS - 1][0] == 255);
    TEST_END();
}

static void test_specgram_max_merge(void)
{
    TEST_BEGIN("specgram_max_merge");
    spectrum_specgram_clear(&g_spec);
    spectrum_specgram_clear_dirty(&g_spec);

    /* Loud frame then silent frame within one row period: the peak survives. */
    set_flat_mags(1.0f);
    spectrum_update_specgram(&g_spec, &g_fft, 0.0f);
    CHECK(g_spec.sg_amp[g_spec.sg_head][10] == 255);
    set_flat_mags(0.0f);
    spectrum_update_specgram(&g_spec, &g_fft, 0.0f);
    CHECK(g_spec.sg_amp[g_spec.sg_head][10] == 255);
    CHECK(g_spec.sg_dirty == 1);
    TEST_END();
}

static void test_specgram_col_alignment(void)
{
    TEST_BEGIN("specgram_col_alignment");
    spectrum_init_lut(&g_spec, 44100.0f);   /* fresh LUT + cleared grid */
    spectrum_specgram_clear_dirty(&g_spec);

    /* Energy ONLY in the FFT bins of the highest LUT column: it must land in
     * the LAST texture column and not in the first — i.e. the column-bucket
     * mapping spans the full frequency axis for any SPECGRAM_COLS.
     * Regression: a hardcoded 2:1 decimation sampled only the lower half of
     * the axis when SPECGRAM_COLS was changed. */
    set_flat_mags(0.0f);
    for (i32 k = g_spec.bin_lo[SPEC_W - 1]; k < FFT_BINS; k++) {
        g_fft.mags[k] = 1.0f;
    }
    spectrum_update_specgram(&g_spec, &g_fft, 0.0f);
    CHECK(g_spec.sg_amp[0][SPECGRAM_COLS - 1] == 255);
    CHECK(g_spec.sg_amp[0][0] == 0);
    TEST_END();
}

static void test_specgram_huge_dt_clamps(void)
{
    TEST_BEGIN("specgram_huge_dt_clamps");
    spectrum_specgram_clear(&g_spec);
    spectrum_specgram_clear_dirty(&g_spec);
    set_flat_mags(0.0f);

    /* Two full windows of elapsed time: advances are bounded to one ring
     * (head returns to where it started) and the excess time is discarded. */
    spectrum_update_specgram(&g_spec, &g_fft, 2.0f * SPECGRAM_WINDOW_S);
    CHECK(g_spec.sg_head == 0);
    CHECK(g_spec.sg_dirty == SPECGRAM_ROWS);
    CHECK(g_spec.sg_acc < SPECGRAM_ROW_S);
    TEST_END();
}

static void test_specgram_dirty_accumulates(void)
{
    TEST_BEGIN("specgram_dirty_accumulates");
    spectrum_specgram_clear(&g_spec);
    spectrum_specgram_clear_dirty(&g_spec);
    set_flat_mags(0.0f);

    /* Successive advancing calls with no draw between them (display toggled
     * off): the dirty count must ACCUMULATE one row per advance on top of
     * the hot-zone term, not reset to a per-call value.  Regression: a
     * non-accumulating rule once forgot rows written while hidden, leaving
     * stale pixels after re-enabling. */
    spectrum_update_specgram(&g_spec, &g_fft, SPECGRAM_ROW_S);
    CHECK(g_spec.sg_dirty == 1 + SPECGRAM_HOT_ROWS);
    spectrum_update_specgram(&g_spec, &g_fft, SPECGRAM_ROW_S);
    CHECK(g_spec.sg_dirty == 2 + SPECGRAM_HOT_ROWS);
    spectrum_update_specgram(&g_spec, &g_fft, 3.5f * SPECGRAM_ROW_S);
    CHECK(g_spec.sg_dirty == 5 + SPECGRAM_HOT_ROWS);
    TEST_END();
}

static void test_specgram_break(void)
{
    TEST_BEGIN("specgram_break");
    spectrum_specgram_clear(&g_spec);
    spectrum_specgram_clear_dirty(&g_spec);
    set_flat_mags(1.0f);
    spectrum_update_specgram(&g_spec, &g_fft, 0.0f);   /* data in head row */
    CHECK(g_spec.sg_amp[g_spec.sg_head][0] == 255);

    /* Break: SPECGRAM_BREAK_ROWS blank rows scroll in ahead of the content. */
    i32 head0  = g_spec.sg_head;
    i32 dirty0 = g_spec.sg_dirty;
    spectrum_specgram_break(&g_spec);
    i32 head1 = (head0 + SPECGRAM_ROWS - SPECGRAM_BREAK_ROWS) % SPECGRAM_ROWS;
    CHECK(g_spec.sg_head == head1);
    /* Same accumulate-or-zone dirty rule as the update path. */
    i32 want = dirty0 + SPECGRAM_BREAK_ROWS;
    i32 zone = SPECGRAM_BREAK_ROWS + SPECGRAM_HOT_ROWS;
    if (want < zone)          { want = zone; }
    if (want > SPECGRAM_ROWS) { want = SPECGRAM_ROWS; }
    CHECK(g_spec.sg_dirty == want);
    CHECK(g_spec.sg_acc == SPECGRAM_ROW_S);
    for (i32 i = 0; i < SPECGRAM_BREAK_ROWS; i++) {
        i32 r = (head1 + i) % SPECGRAM_ROWS;
        CHECK(g_spec.sg_amp[r][0] == 0);
        CHECK(g_spec.sg_amp[r][SPECGRAM_COLS - 1] == 0);
    }

    /* The pre-charged accumulator makes the next update start a FRESH row,
     * so the break rows stay fully blank.  (Modulo hoisted out of CHECK —
     * '%' inside a CHECK breaks its stringified printf format.) */
    spectrum_update_specgram(&g_spec, &g_fft, 0.0f);
    i32 head2 = (head1 + SPECGRAM_ROWS - 1) % SPECGRAM_ROWS;
    CHECK(g_spec.sg_head == head2);
    CHECK(g_spec.sg_amp[g_spec.sg_head][0] == 255);
    for (i32 i = 0; i < SPECGRAM_BREAK_ROWS; i++) {
        i32 r = (head1 + i) % SPECGRAM_ROWS;
        CHECK(g_spec.sg_amp[r][0] == 0);
    }
    TEST_END();
}

static void test_specgram_clear_and_init(void)
{
    TEST_BEGIN("specgram_clear_and_init");
    set_flat_mags(1.0f);
    spectrum_update_specgram(&g_spec, &g_fft, 0.0f);
    CHECK(g_spec.sg_amp[g_spec.sg_head][0] == 255);

    /* Explicit clear: grid zeroed, head/acc reset, everything dirty. */
    spectrum_specgram_clear(&g_spec);
    CHECK(g_spec.sg_head  == 0);
    CHECK(g_spec.sg_acc   == 0.0f);
    CHECK(g_spec.sg_dirty == SPECGRAM_ROWS);
    CHECK(g_spec.sg_amp[0][0] == 0);
    CHECK(g_spec.sg_amp[SPECGRAM_ROWS - 1][SPECGRAM_COLS - 1] == 0);

    /* spectrum_init_lut clears too (track / sample-rate change). */
    spectrum_update_specgram(&g_spec, &g_fft, 0.0f);
    CHECK(g_spec.sg_amp[0][0] == 255);
    spectrum_init_lut(&g_spec, 44100.0f);
    CHECK(g_spec.sg_amp[0][0] == 0);
    CHECK(g_spec.sg_dirty == SPECGRAM_ROWS);

    spectrum_specgram_clear_dirty(&g_spec);
    CHECK(g_spec.sg_dirty == 0);
    TEST_END();
}

static void test_specgram_cmap(void)
{
    TEST_BEGIN("specgram_cmap");
    spectrum_init_lut(&g_spec, 44100.0f);   /* builds sg_cmap */

    /* Exact cubehelix endpoints: black at amplitude 0, white at full scale. */
    CHECK(g_spec.sg_cmap[0][0] == 0 && g_spec.sg_cmap[0][1] == 0 &&
          g_spec.sg_cmap[0][2] == 0);
    CHECK(g_spec.sg_cmap[SPECGRAM_CMAP_N - 1][0] == 255 &&
          g_spec.sg_cmap[SPECGRAM_CMAP_N - 1][1] == 255 &&
          g_spec.sg_cmap[SPECGRAM_CMAP_N - 1][2] == 255);

    /* The defining cubehelix property: perceived brightness (the luminance
     * weights from Green 2011) rises monotonically along the map.  Allow a
     * ±1 tolerance for 8-bit rounding jitter. */
    f32 prev = -1.0f;
    for (i32 q = 0; q < SPECGRAM_CMAP_N; q++) {
        f32 y = 0.30f * (f32)g_spec.sg_cmap[q][0]
              + 0.59f * (f32)g_spec.sg_cmap[q][1]
              + 0.11f * (f32)g_spec.sg_cmap[q][2];
        CHECK(y >= prev - 1.0f);
        prev = y;
    }

    /* Mid-ramp is an actual colour, not grey (the helix has saturation). */
    i32 mid = SPECGRAM_CMAP_N / 2;
    CHECK(g_spec.sg_cmap[mid][0] != g_spec.sg_cmap[mid][1] ||
          g_spec.sg_cmap[mid][1] != g_spec.sg_cmap[mid][2]);
    TEST_END();
}

static void test_specgram_pixels_ramp(void)
{
    TEST_BEGIN("specgram_pixels_ramp");
    static u8 px[SPECGRAM_COLS * 4];
    /* Same knee quantisation as spectrum_specgram_pixels; amplitudes are
     * derived from it so the checks hold for any configured knee. */
    i32 knee_q = (i32)(SPECGRAM_ALPHA_KNEE * 255.0f);
    i32 faint  = knee_q / 4;
    i32 mid    = knee_q / 2;
    i32 above  = knee_q + 2;
    if (above > 255) { above = 255; }

    /* Non-hot cells sample the cubehelix LUT; alpha is a smoothstep that
     * saturates to fully opaque at the knee. */
    spectrum_init_lut(&g_spec, 44100.0f);   /* builds sg_cmap, clears grid */
    g_spec.sg_amp[20][0] = 0;           /* silence          */
    g_spec.sg_amp[20][1] = (u8)faint;   /* faint            */
    g_spec.sg_amp[20][2] = (u8)mid;     /* mid, below knee  */
    g_spec.sg_amp[20][3] = (u8)above;   /* just above knee  */
    spectrum_specgram_pixels(&g_spec, 20, px);

    /* Silence: fully transparent (colour is the map's black end, unseen). */
    CHECK(px[3] == 0);
    /* Cells carry the LUT colour for their amplitude. */
    CHECK(px[4] == g_spec.sg_cmap[faint][0] &&
          px[5] == g_spec.sg_cmap[faint][1] &&
          px[6] == g_spec.sg_cmap[faint][2]);
    CHECK(px[8]  == g_spec.sg_cmap[mid][0] &&
          px[9]  == g_spec.sg_cmap[mid][1] &&
          px[10] == g_spec.sg_cmap[mid][2]);
    /* Below the knee alpha is partial and rises with amplitude; at/above
     * the knee it saturates at the SPECGRAM_ALPHA_MAX cap (same rounding as
     * the source: smoothstep = 1 there). */
    u8 amax = (u8)(1.0f * (SPECGRAM_ALPHA_MAX * 255.0f) + 0.5f);
    CHECK(px[7] > 0 && px[7] < amax);
    CHECK(px[11] > px[7]);
    CHECK(px[11] < amax);
    CHECK(px[15] == amax);
    TEST_END();
}

static void test_specgram_pixels_hot(void)
{
    TEST_BEGIN("specgram_pixels_hot");
    static const u8 col_hot[3] = { SPECGRAM_COL_HOT };
    static u8 px[SPECGRAM_COLS * 4];
    static u8 px_out[SPECGRAM_COLS * 4];
    /* Same quantisation as spectrum_specgram_pixels. */
    const u8 hot_q = (u8)((SPECGRAM_HOT_DB - DB_BOT) / (DB_TOP - DB_BOT)
                          * 255.0f + 0.5f);

    /* After clear the head is row 0; rows 0..SPECGRAM_HOT_ROWS-1 form the
     * hot-eligible zone (a depth of 0 disables hot highlighting entirely —
     * the checks below adapt to the configured depth). */
    spectrum_specgram_clear(&g_spec);
    CHECK(g_spec.sg_head == 0);
    g_spec.sg_amp[0][0] = (u8)(hot_q - 1);   /* just below threshold */
    g_spec.sg_amp[0][1] = hot_q;             /* exactly at threshold */
    g_spec.sg_amp[0][2] = 255;               /* full scale           */
    spectrum_specgram_pixels(&g_spec, 0, px);

    /* Just below threshold: always the colormap, never the hot colour. */
    CHECK(px[0] == g_spec.sg_cmap[hot_q - 1][0] &&
          px[1] == g_spec.sg_cmap[hot_q - 1][1] &&
          px[2] == g_spec.sg_cmap[hot_q - 1][2]);

    /* Alpha values mirror the source: both branches scale by the
     * SPECGRAM_ALPHA_MAX cap. */
    u8 amax  = (u8)(1.0f * (SPECGRAM_ALPHA_MAX * 255.0f) + 0.5f);
    u8 hot_a = (u8)((f32)hot_q / 255.0f * (SPECGRAM_ALPHA_MAX * 255.0f) + 0.5f);

    if (SPECGRAM_HOT_ROWS > 0) {
        /* At threshold, in zone: flat hot colour, linear capped alpha. */
        CHECK(px[4] == col_hot[0] && px[5] == col_hot[1] && px[6] == col_hot[2]);
        CHECK(px[7] == hot_a);
        /* Full scale, in zone: hot colour at the opacity cap. */
        CHECK(px[8] == col_hot[0] && px[9] == col_hot[1] && px[10] == col_hot[2]);
        CHECK(px[11] == amax);
    } else {
        /* Hot disabled: even at/above threshold the colormap applies. */
        CHECK(px[4] == g_spec.sg_cmap[hot_q][0] &&
              px[5] == g_spec.sg_cmap[hot_q][1] &&
              px[6] == g_spec.sg_cmap[hot_q][2]);
        CHECK(px[8] == g_spec.sg_cmap[255][0] &&
              px[9] == g_spec.sg_cmap[255][1] &&
              px[10] == g_spec.sg_cmap[255][2]);
        CHECK(px[11] == amax);
    }

    /* The same full-scale amplitude OUTSIDE the zone uses the colormap
     * (cubehelix full scale = white) at the opacity cap. */
    g_spec.sg_amp[SPECGRAM_HOT_ROWS][0] = 255;
    spectrum_specgram_pixels(&g_spec, SPECGRAM_HOT_ROWS, px_out);
    CHECK(px_out[0] == g_spec.sg_cmap[255][0] &&
          px_out[1] == g_spec.sg_cmap[255][1] &&
          px_out[2] == g_spec.sg_cmap[255][2]);
    CHECK(px_out[3] == amax);
    TEST_END();
}

/* ── Instrument-stem waveforms ───────────────────────────────────────────── */

static void test_stem_pts_in_own_cell(void)
{
    TEST_BEGIN("stem_pts_in_own_cell");
    /* Each stem's points must land inside its own 3×2 grid cell and nowhere
     * else — a wrong row or column would silently draw over a neighbour. */
    f32 sr = 44100.0f;
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = 0.8f * sinf(2.0f * 3.14159265f * 300.0f * (f32)i / sr);
    }
    for (i32 idx = 0; idx < STEM_COUNT; idx++) {
        for (i32 c = 0; c < 3; c++) {
            spectrum_update_stem(&g_spec, idx, g_samples, RING_SIZE, sr);
        }
        i32 x0 = SPEC_X + (idx % STEM_COLS) * STEM_W;
        i32 y0 = PLOT_Y + (idx / STEM_COLS) * STEM_H;
        for (i32 x = 0; x < STEM_W; x++) {
            CHECK(g_spec.stem_pts[idx][x].x == (f32)(x0 + x));
            CHECK(g_spec.stem_pts[idx][x].y >= (f32)y0);
            CHECK(g_spec.stem_pts[idx][x].y <= (f32)(y0 + STEM_H));
        }
    }
    TEST_END();
}

static void test_stem_silence_is_centred(void)
{
    TEST_BEGIN("stem_silence_is_centred");
    SDL_memset(g_samples, 0, sizeof(g_samples));
    SDL_memset(g_spec.smooth_stem, 0, sizeof(g_spec.smooth_stem));
    SDL_memset(g_spec.prev_stem,   0, sizeof(g_spec.prev_stem));
    for (i32 idx = 0; idx < STEM_COUNT; idx++) {
        spectrum_update_stem(&g_spec, idx, g_samples, RING_SIZE, 44100.0f);
        f32 centre = (f32)(PLOT_Y + (idx / STEM_COLS) * STEM_H) + (f32)STEM_H * 0.5f;
        for (i32 x = 0; x < STEM_W; x++) {
            CHECK(fabsf(g_spec.stem_pts[idx][x].y - centre) < 1.0f);
        }
    }
    TEST_END();
}

static void test_stem_shared_time_axis(void)
{
    TEST_BEGIN("stem_shared_time_axis");
    /* The point of this view is comparability: every stem must sample the same
     * window length, unlike the per-band windows of the decompose view. */
    f32 sr = 44100.0f;
    i32 expect = (i32)(STEM_WINDOW_S * sr);
    CHECK(expect > 0);
    CHECK(expect <= RING_SIZE);
    /* A ramp makes the sampled span visible: with one shared window, the first
     * and last pixel of every cell span the same number of source samples. */
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = -1.0f + 2.0f * (f32)i / (f32)(RING_SIZE - 1);
    }
    SDL_memset(g_spec.smooth_stem, 0, sizeof(g_spec.smooth_stem));
    SDL_memset(g_spec.prev_stem,   0, sizeof(g_spec.prev_stem));
    for (i32 idx = 0; idx < STEM_COUNT; idx++) {
        spectrum_update_stem(&g_spec, idx, g_samples, RING_SIZE, sr);
    }
    /* Same input to every cell → identical vertical shape, cell offset aside. */
    for (i32 x = 0; x < STEM_W; x++) {
        f32 base = g_spec.stem_pts[0][x].y - (f32)PLOT_Y;
        for (i32 idx = 1; idx < STEM_COUNT; idx++) {
            f32 rel = g_spec.stem_pts[idx][x].y
                    - (f32)(PLOT_Y + (idx / STEM_COLS) * STEM_H);
            CHECK(fabsf(rel - base) < 1.0f);
        }
    }
    TEST_END();
}

static void test_stem_extremes_clamp(void)
{
    TEST_BEGIN("stem_extremes_clamp");
    /* Out-of-range input must clamp to the cell, not overdraw its neighbour. */
    for (i32 i = 0; i < RING_SIZE; i++) {
        g_samples[i] = (i % 2 == 0) ? 4.0f : -4.0f;
    }
    i32 idx = STEM_COUNT - 1;                  /* bottom-right cell */
    for (i32 c = 0; c < 40; c++) {
        spectrum_update_stem(&g_spec, idx, g_samples, RING_SIZE, 44100.0f);
    }
    i32 y0 = PLOT_Y + (idx / STEM_COLS) * STEM_H;
    for (i32 x = 0; x < STEM_W; x++) {
        CHECK(g_spec.stem_pts[idx][x].y >= (f32)y0);
        CHECK(g_spec.stem_pts[idx][x].y <= (f32)(y0 + STEM_H));
    }
    TEST_END();
}

static void test_stem_grid_tiles_plot_exactly(void)
{
    TEST_BEGIN("stem_grid_tiles_plot_exactly");
    /* The grid must divide the plot with no leftover pixels, or cells would
     * drift out of alignment with the panel edges. */
    CHECK(SPEC_W == STEM_COLS * STEM_W);
    CHECK(PLOT_H == STEM_ROWS * STEM_H);
    CHECK(STEM_COLS * STEM_ROWS == STEM_COUNT);
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

    test_biquad_lp_attenuates_highs();
    test_biquad_lp_passes_bass();
    test_biquad_hp_attenuates_bass();
    test_bands_pts_bounds_iir();
    test_bands_silence_iir();
    test_bands_coeff_cache();

    test_stem_grid_tiles_plot_exactly();
    test_stem_pts_in_own_cell();
    test_stem_silence_is_centred();
    test_stem_shared_time_axis();
    test_stem_extremes_clamp();

    test_specgram_accumulate_and_advance();
    test_specgram_max_merge();
    test_specgram_col_alignment();
    test_specgram_huge_dt_clamps();
    test_specgram_dirty_accumulates();
    test_specgram_break();
    test_specgram_clear_and_init();
    test_specgram_cmap();
    test_specgram_pixels_ramp();
    test_specgram_pixels_hot();

    fft_quit(&g_fft);
}
