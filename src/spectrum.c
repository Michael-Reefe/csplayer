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

    f32 lmin = log10f(FREQ_MIN);
    f32 lmax = log10f(FREQ_MAX);
    f32 span = lmax - lmin;
    assert(span > 0.0f);

    for (i32 x = 0; x < SPEC_W; x++) {
        assert(x < SPEC_W);

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

    /* Reset smoothed values to silence so old data does not bleed through
     * after a sample-rate change. */
    for (i32 x = 0; x < SPEC_W; x++) {
        s->smooth_db[x] = DB_BOT;
    }
}

void spectrum_update(spectrum_t *s, const fft_t *f)
{
    assert(s != NULL);
    assert(f != NULL);

    f32 db_range = DB_TOP - DB_BOT;
    assert(db_range > 0.0f);

    for (i32 x = 0; x < SPEC_W; x++) {
        assert(x < SPEC_W);
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
