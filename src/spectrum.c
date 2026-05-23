/*
 * spectrum.c — Log-frequency mapping and per-frame spectrum smoothing.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "spectrum.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <assert.h>

void spectrum_init_lut(spectrum_t *s, f32 sample_rate)
{
    assert(s           != NULL);
    assert(sample_rate > 0.0f);
    assert(FREQ_MAX    > FREQ_MIN);

    f32 lmin = log10f(FREQ_MIN);
    f32 lmax = log10f(FREQ_MAX);
    f32 span = lmax - lmin;

    for (i32 x = 0; x < SPEC_W; x++) {

        /* Map pixel column to log-spaced frequency in [FREQ_MIN, FREQ_MAX]. */
        f32 t    = (f32)x / (f32)(SPEC_W - 1);
        f32 freq = powf(10.0f, lmin + t * span);

        /* Convert frequency to fractional FFT bin index. */
        f32 fbin = freq * (f32)FFT_SIZE / sample_rate;

        s->bin_lo[x]   = (i32)fbin;
        s->bin_frac[x] = fbin - (f32)s->bin_lo[x];
        s->bin_hi[x]   = s->bin_lo[x] + 1;

        /* Clamp to valid bin range. */
        if (s->bin_lo[x] < 0)           { s->bin_lo[x] = 0; }
        if (s->bin_hi[x] >= FFT_BINS)   { s->bin_hi[x] = FFT_BINS - 1; }
        if (s->bin_lo[x] >= FFT_BINS)   { s->bin_lo[x] = FFT_BINS - 1; }
        if (s->bin_hi[x] <= s->bin_lo[x]) {
            s->bin_hi[x]   = s->bin_lo[x];
            s->bin_frac[x] = 0.0f;
        }
    }

    /* Reset smoothed values and xcorr state so old data does not bleed through
     * after a sample-rate change. */
    for (i32 x = 0; x < SPEC_W; x++) {
        s->smooth_db[x]   = DB_BOT;
        s->prev_window[x] = 0.0f;
        s->smooth_time[x] = 0.0f;
    }
}

void spectrum_update(spectrum_t *s, const fft_t *f)
{
    assert(s != NULL);
    assert(f != NULL);

    f32 db_range = DB_TOP - DB_BOT;
    assert(db_range > 0.0f);

    for (i32 x = 0; x < SPEC_W; x++) {
        assert(s->bin_lo[x] >= 0 && s->bin_lo[x] < FFT_BINS);
        assert(s->bin_hi[x] >= 0 && s->bin_hi[x] < FFT_BINS);

        /* Linearly interpolate between adjacent FFT bins. */
        f32 lo  = f->mags[s->bin_lo[x]];
        f32 hi  = f->mags[s->bin_hi[x]];
        f32 mag = lo + s->bin_frac[x] * (hi - lo);

        /* Convert to dB; add a small epsilon to avoid log10(0). */
        f32 db = 20.0f * log10f(mag + 1e-10f);

        /* Clamp to the display range. */
        if (db < DB_BOT) { db = DB_BOT; }
        if (db > DB_TOP) { db = DB_TOP; }

        /* Exponential smoothing in dB space. */
        s->smooth_db[x] = SMOOTH_COEFF * s->smooth_db[x]
                          + (1.0f - SMOOTH_COEFF) * db;

        /* Map dB to screen y:  DB_TOP → SPEC_Y (top),  DB_BOT → SPEC_Y + SPEC_H. */
        f32 t = (DB_TOP - s->smooth_db[x]) / db_range;
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }

        s->pts[x].x = (f32)(SPEC_X + x);
        s->pts[x].y = (f32)SPEC_Y + (f32)SPEC_H * t;
    }
}

void spectrum_map_freq_pts(spectrum_t *s, i32 plot_y, i32 plot_h)
{
    assert(s       != NULL);
    assert(plot_h  >  0);

    f32 db_range = DB_TOP - DB_BOT;
    assert(db_range > 0.0f);

    for (i32 x = 0; x < SPEC_W; x++) {
        f32 t = (DB_TOP - s->smooth_db[x]) / db_range;
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }
        s->pts[x].x = (f32)(SPEC_X + x);
        s->pts[x].y = (f32)plot_y + (f32)plot_h * t;
    }
}

/* Search samples[slo..shi] for the start whose downsampled window best
 * correlates with prev_win[SPEC_W].  Returns fallback when prev_win has
 * near-zero energy (silence / first frame). */
static i32 xcorr_best_start(const f32 *samples, i32 n, i32 win,
                             const f32 *prev_win, i32 fallback)
{
    assert(samples  != NULL);
    assert(prev_win != NULL);
    assert(win > 0 && win <= n);

    i32 slo = n - XCORR_SEARCH;
    if (slo < 0)    { slo = 0; }
    i32 shi = n - win;
    if (shi < slo)  { return fallback; }

    f32 energy = 0.0f;
    for (i32 x = 0; x < SPEC_W; x += XCORR_STRIDE) {
        energy += prev_win[x] * prev_win[x];
    }
    if (energy < 1e-6f) { return fallback; }

    f32 best   = -1e30f;
    i32 best_s = fallback;
    for (i32 c = slo; c <= shi; c++) {
        f32 corr = 0.0f;
        for (i32 x = 0; x < SPEC_W; x += XCORR_STRIDE) {
            i32 src = c + x * win / SPEC_W;
            if (src >= n) { src = n - 1; }
            f32 a = samples[src];
            if (a >  1.0f) { a =  1.0f; }
            if (a < -1.0f) { a = -1.0f; }
            corr += a * prev_win[x];
        }
        if (corr > best) { best = corr; best_s = c; }
    }
    return best_s;
}

void spectrum_update_time(spectrum_t *s, const f32 *samples, i32 n,
                           f32 sample_rate, i32 plot_y, i32 plot_h)
{
    assert(s             != NULL);
    assert(samples       != NULL);
    assert(n > 0 && n <= RING_SIZE);
    assert(sample_rate   > 0.0f);
    assert(plot_h        > 0);
    assert(WAVE_WINDOW_S > 0.0f);
    assert(SPEC_W        > 0);

    i32 win = (i32)(WAVE_WINDOW_S * sample_rate);
    if (win < 1) { win = 1; }
    if (win > n) { win = n; }

    /* Zero-crossing fallback — used on first call and during silence. */
    i32 ss = n - 1 - win;
    if (ss < 1) { ss = 1; }
    i32 se = ss - WAVE_TRIGGER_RANGE;
    if (se < 1) { se = 1; }
    i32 trigger = -1;
    for (i32 i = se; i <= ss; i++) {
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) { trigger = i; break; }
    }
    i32 zc = (trigger >= 0) ? trigger : (n - win);
    if (zc < 0)       { zc = 0; }
    if (zc + win > n) { zc = n - win; }

    /* Cross-correlation alignment — overrides zero-crossing when prior data exists. */
    i32 start = xcorr_best_start(samples, n, win, s->prev_window, zc);
    if (start < 0)       { start = 0; }
    if (start + win > n) { start = n - win; }

    /* Map window to pixels; persist amplitudes for next-frame correlation. */
    f32 half_h = (f32)plot_h * 0.5f;
    for (i32 x = 0; x < SPEC_W; x++) {
        i32 src = start + x * win / SPEC_W;
        if (src < 0)  { src = 0; }
        if (src >= n) { src = n - 1; }
        f32 amp = samples[src];
        if (amp >  1.0f) { amp =  1.0f; }
        if (amp < -1.0f) { amp = -1.0f; }
        s->prev_window[x]  = amp;   /* raw — xcorr tracks the true signal */
        s->smooth_time[x]  = WAVE_SMOOTH_COEFF * s->smooth_time[x]
                           + (1.0f - WAVE_SMOOTH_COEFF) * amp;
        f32 y = (f32)plot_y + half_h * (1.0f - s->smooth_time[x]);
        if (y < (f32)plot_y)            { y = (f32)plot_y; }
        if (y > (f32)(plot_y + plot_h)) { y = (f32)(plot_y + plot_h); }
        s->time_pts[x].x = (f32)(SPEC_X + x);
        s->time_pts[x].y = y;
    }
}
