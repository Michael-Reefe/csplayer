/*
 * spectrum.h — Log-frequency mapping and per-frame spectrum smoothing.
 *
 * Maintains a precomputed look-up table that maps each pixel column of the
 * spectrum plot to a pair of interpolated FFT bin indices (one per column),
 * so that per-frame rendering is a simple array walk with no transcendentals.
 *
 * The smoothed dB values (smooth_db[]) are maintained in actual decibels so
 * that the y-axis labels correspond directly to what is stored.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_SPECTRUM_H
#define CSPLAYER_SPECTRUM_H

#include <SDL3/SDL.h>
#include "types.h"
#include "fft.h"

/* ── Module state ────────────────────────────────────────────────────────── */

typedef struct {
    /* Smoothed dB value per pixel column.  Clamped to [DB_BOT, DB_TOP]. */
    f32        smooth_db[SPEC_W];

    /* Screen-space SDL_FPoint array consumed by SDL_RenderLines each frame. */
    SDL_FPoint pts[SPEC_W];

    /* Precomputed log-frequency → bin index LUT.  Rebuilt by
     * spectrum_init_lut() whenever the sample rate changes. */
    i32  bin_lo[SPEC_W];    /* floor bin index  */
    i32  bin_hi[SPEC_W];    /* ceil  bin index  */
    f32  bin_frac[SPEC_W];  /* fractional part for linear interpolation */
} spectrum_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Precompute the log-frequency → bin LUT for the given sample_rate.
 * Call once at startup with the default rate (44100 Hz), then again
 * after each audio_load() call in case the new file has a different rate.
 */
void spectrum_init_lut(spectrum_t *s, f32 sample_rate);

/*
 * Update smooth_db[] and pts[] from the FFT magnitude array.
 * Call once per rendered frame after fft_process().
 */
void spectrum_update(spectrum_t *s, const fft_t *f);

#endif /* CSPLAYER_SPECTRUM_H */
