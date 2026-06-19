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

    /* Screen-space SDL_FPoint array consumed by SDL_RenderLines each frame.
     * Mapped to the current freq-domain plot rect (may be upper half in BOTH mode). */
    SDL_FPoint pts[SPEC_W];

    /* Screen-space points for the time-domain waveform (green line).
     * Mapped to the current time-domain plot rect by spectrum_update_time(). */
    SDL_FPoint time_pts[SPEC_W];

    /* Previous frame's amplitude snapshot (SPEC_W values in [-1, 1]).
     * Used by spectrum_update_time() for cross-correlation alignment.
     * Zeroed by spectrum_init_lut(); updated every spectrum_update_time() call. */
    f32  prev_window[SPEC_W];

    /* Per-pixel exponentially smoothed amplitude for the rendered waveform.
     * Smoothed with WAVE_SMOOTH_COEFF each frame to suppress jitter.
     * Zeroed by spectrum_init_lut(); updated every spectrum_update_time() call. */
    f32  smooth_time[SPEC_W];

    /* Precomputed log-frequency → bin index LUT.  Rebuilt by
     * spectrum_init_lut() whenever the sample rate changes. */
    i32  bin_lo[SPEC_W];    /* floor bin index  */
    i32  bin_hi[SPEC_W];    /* ceil  bin index  */
    f32  bin_frac[SPEC_W];  /* fractional part for linear interpolation */

    /* ── Band-decomposed waveform (wave_decompose mode) ─────────────────── */
    /* Screen-space points for each band sub-panel (BAND_W points each). */
    SDL_FPoint bass_pts[BAND_W];
    SDL_FPoint mid_pts[BAND_W];
    SDL_FPoint high_pts[BAND_W];
    /* Per-pixel exponential smoothing (parallel to smooth_time). */
    f32  smooth_bass[BAND_W];
    f32  smooth_mid[BAND_W];
    f32  smooth_high[BAND_W];
    /* Previous-frame filtered amplitudes for per-band xcorr trigger alignment. */
    f32  prev_bass[BAND_W];
    f32  prev_mid[BAND_W];
    f32  prev_high[BAND_W];
    /* IIR filter output scratch: reused for each of the 3 bands in sequence. */
    f32  band_scratch[FFT_SIZE];
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
 * pts[] are always mapped to the full [SPEC_Y, SPEC_Y+SPEC_H] rect.
 * Call spectrum_map_freq_pts() afterward if a different rect is needed.
 */
void spectrum_update(spectrum_t *s, const fft_t *f);

/*
 * Re-map s->smooth_db[] → s->pts[] for an arbitrary vertical sub-rect.
 * Use for GRAPH_MODE_BOTH to confine the freq subplot to the upper half.
 * plot_y and plot_h define the target rect in logical pixels.
 */
void spectrum_map_freq_pts(spectrum_t *s, i32 plot_y, i32 plot_h);

/*
 * Fill s->time_pts[] from the most recent window of PCM samples.
 * Uses cross-correlation with s->prev_window for stable phase-locked display.
 *
 * samples     — chronological snapshot (oldest first), length n.
 * sample_rate — must be > 0.
 * plot_y / plot_h — target rect in logical pixels.
 *
 * Algorithm:
 *   1. win = clamp((i32)(WAVE_WINDOW_S * sample_rate), 1, n).
 *   2. Zero-crossing fallback: scan forward (oldest-first) across
 *      WAVE_TRIGGER_RANGE samples before n-1-win for the oldest neg→pos
 *      crossing.  Used as the start when prev_window has near-zero energy.
 *   3. Cross-correlation: search the last XCORR_SEARCH samples of the ring
 *      buffer for the start position whose downsampled window (at XCORR_STRIDE
 *      pixel spacing) maximises the dot-product with s->prev_window.  Falls
 *      back to step 2 on first call or during silence.
 *   4. For each output pixel x in [0, SPEC_W):
 *        src = start + x * win / SPEC_W
 *        amp = clamp(samples[src], -1, 1)
 *        y   = plot_y + (plot_h/2) * (1 - amp)   (zero maps to centre)
 *        time_pts[x] = { SPEC_X + x, clamp(y, plot_y, plot_y+plot_h) }
 *   5. Save raw amplitudes into s->prev_window (for next-frame xcorr).
 *   6. Apply exponential smoothing: smooth_time[x] = WAVE_SMOOTH_COEFF *
 *      smooth_time[x] + (1-WAVE_SMOOTH_COEFF) * amp  — use smooth_time[x]
 *      for the final y coordinate to suppress per-frame phase jitter.
 */
void spectrum_update_time(spectrum_t *s, const f32 *samples, i32 n,
                           f32 sample_rate, i32 plot_y, i32 plot_h);

/*
 * Compute band-decomposed waveform points for bass, mids, and highs.
 *
 * Applies 2nd-order Butterworth IIR biquad filters (Direct Form II Transposed)
 * directly to the raw PCM snapshot to isolate each frequency band:
 *   - Bass:  lowpass  at DECOMP_BASS_HZ
 *   - Mids:  highpass at DECOMP_BASS_HZ then lowpass at DECOMP_MIDS_HZ
 *   - Highs: highpass at DECOMP_MIDS_HZ
 * Each band is written to s->band_scratch[], then the same zero-crossing +
 * xcorr trigger alignment used by spectrum_update_time is applied per-band
 * (using prev_bass/mid/high).  BAND_W pixels are mapped into the band's
 * horizontal sub-panel within [SPEC_X + offset, plot_y .. plot_y+plot_h].
 *
 * samples    — chronological PCM snapshot (oldest first), length n.
 * sample_rate — must be > 0.
 * plot_y / plot_h define the time-domain rect.
 */
void spectrum_update_bands(spectrum_t *s, const f32 *samples, i32 n,
                           f32 sample_rate, i32 plot_y, i32 plot_h);

#endif /* CSPLAYER_SPECTRUM_H */
