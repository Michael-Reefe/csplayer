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
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, SPEC_Y, SPEC_H);
    }
    f32 center = (f32)SPEC_Y + (f32)SPEC_H * 0.5f;
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
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, SPEC_Y, SPEC_H);
    }
    f32 center = (f32)SPEC_Y + (f32)SPEC_H * 0.5f;
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
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, SPEC_Y, SPEC_H);
    }
    f32 center = (f32)SPEC_Y + (f32)SPEC_H * 0.5f;
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
        spectrum_update_bands(&g_spec, g_samples, RING_SIZE, sr, SPEC_Y, SPEC_H);
    }
    for (i32 x = 0; x < BAND_W; x++) {
        CHECK(g_spec.bass_pts[x].y >= (f32)SPEC_Y);
        CHECK(g_spec.bass_pts[x].y <= (f32)(SPEC_Y + SPEC_H));
        CHECK(g_spec.mid_pts[x].y  >= (f32)SPEC_Y);
        CHECK(g_spec.mid_pts[x].y  <= (f32)(SPEC_Y + SPEC_H));
        CHECK(g_spec.high_pts[x].y >= (f32)SPEC_Y);
        CHECK(g_spec.high_pts[x].y <= (f32)(SPEC_Y + SPEC_H));
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
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    f32 center = (f32)SPEC_Y + (f32)SPEC_H * 0.5f;
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
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    CHECK(g_spec.band_coeff_rate == 44100.0f);
    f32 b0_44k = g_spec.band_lp_bass.b0;

    /* Second call at the same rate must NOT rebuild (cache-hit branch): the
     * cached coefficients are unchanged. */
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 44100.0f, SPEC_Y, SPEC_H);
    CHECK(g_spec.band_coeff_rate == 44100.0f);
    CHECK(g_spec.band_lp_bass.b0 == b0_44k);

    /* Changing the rate must rebuild (different cutoff/rate → different b0). */
    spectrum_update_bands(&g_spec, g_samples, RING_SIZE, 48000.0f, SPEC_Y, SPEC_H);
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

    /* Crossing one row period advances the head once, wrapping downward; the
     * dirty run covers the advance plus the demoted hot zone. */
    spectrum_update_specgram(&g_spec, &g_fft, SPECGRAM_ROW_S);
    CHECK(g_spec.sg_head == SPECGRAM_ROWS - 1);
    CHECK(g_spec.sg_dirty == 1 + SPECGRAM_HOT_ROWS);
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

static void test_specgram_pixels_ramp(void)
{
    TEST_BEGIN("specgram_pixels_ramp");
    static const u8 ramp_lo[3] = { SPECGRAM_COL_LO };
    static const u8 ramp_hi[3] = { SPECGRAM_COL_HI };
    static u8 px[SPECGRAM_COLS * 4];

    /* Below-threshold cells use the pink history ramp with quadratic alpha. */
    spectrum_specgram_clear(&g_spec);
    g_spec.sg_amp[20][0] = 0;     /* silence           */
    g_spec.sg_amp[20][1] = 128;   /* mid               */
    g_spec.sg_amp[20][2] = 200;   /* loud, not yet hot */
    spectrum_specgram_pixels(&g_spec, 20, px);

    /* Silence: low-ramp colour, fully transparent. */
    CHECK(px[0] == ramp_lo[0] && px[1] == ramp_lo[1] && px[2] == ramp_lo[2]);
    CHECK(px[3] == 0);
    /* Mid amplitude: every colour channel strictly between the ramp ends;
     * alpha below the linear midpoint (quadratic fade). */
    CHECK(px[4] > ramp_lo[0] && px[4] < ramp_hi[0]);
    CHECK(px[5] > ramp_lo[1] && px[5] < ramp_hi[1]);
    CHECK(px[6] > ramp_lo[2] && px[6] < ramp_hi[2]);
    CHECK(px[7] > 0 && px[7] < 128);
    /* Louder (still below threshold): higher alpha than mid. */
    CHECK(px[11] > px[7]);
    TEST_END();
}

static void test_specgram_pixels_hot(void)
{
    TEST_BEGIN("specgram_pixels_hot");
    static const u8 ramp_hi[3] = { SPECGRAM_COL_HI };
    static const u8 col_hot[3] = { SPECGRAM_COL_HOT };
    static u8 px[SPECGRAM_COLS * 4];
    static u8 px_out[SPECGRAM_COLS * 4];
    /* Same quantisation as spectrum_specgram_pixels. */
    const u8 hot_q = (u8)((SPECGRAM_HOT_DB - DB_BOT) / (DB_TOP - DB_BOT)
                          * 255.0f + 0.5f);

    /* After clear the head is row 0; rows 0..SPECGRAM_HOT_ROWS-1 form the
     * hot-eligible zone. */
    spectrum_specgram_clear(&g_spec);
    CHECK(g_spec.sg_head == 0);
    g_spec.sg_amp[0][0] = (u8)(hot_q - 1);   /* just below threshold */
    g_spec.sg_amp[0][1] = hot_q;             /* exactly at threshold */
    g_spec.sg_amp[0][2] = 255;               /* full scale           */
    spectrum_specgram_pixels(&g_spec, 0, px);

    /* Just below threshold: still the pink ramp, not the hot colour. */
    CHECK(px[1] != col_hot[1]);
    /* At threshold, in zone: flat hot colour, LINEAR alpha (== amplitude). */
    CHECK(px[4] == col_hot[0] && px[5] == col_hot[1] && px[6] == col_hot[2]);
    CHECK(px[7] == hot_q);
    /* Full scale, in zone: hot colour, fully opaque. */
    CHECK(px[8] == col_hot[0] && px[9] == col_hot[1] && px[10] == col_hot[2]);
    CHECK(px[11] == 255);

    /* The same full-scale amplitude OUTSIDE the zone stays on the ramp. */
    g_spec.sg_amp[SPECGRAM_HOT_ROWS][0] = 255;
    spectrum_specgram_pixels(&g_spec, SPECGRAM_HOT_ROWS, px_out);
    CHECK(px_out[0] == ramp_hi[0] && px_out[1] == ramp_hi[1] &&
          px_out[2] == ramp_hi[2]);
    CHECK(px_out[3] == 255);
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

    test_specgram_accumulate_and_advance();
    test_specgram_max_merge();
    test_specgram_col_alignment();
    test_specgram_huge_dt_clamps();
    test_specgram_dirty_accumulates();
    test_specgram_break();
    test_specgram_clear_and_init();
    test_specgram_pixels_ramp();
    test_specgram_pixels_hot();

    fft_quit(&g_fft);
}
