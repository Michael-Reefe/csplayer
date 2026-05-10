/*
 * fft.h — FFTW-based real-to-complex spectrum computation.
 *
 * Uses the double-precision r2c plan (fftw_plan_dft_r2c_1d) to transform
 * a block of FFT_SIZE mono PCM samples into FFT_BINS complex output bins.
 * A Hann window is applied before each transform to reduce spectral leakage.
 *
 * All buffers are statically allocated inside fft_t (NASA Rule 3: no runtime
 * dynamic allocation).  FFTW_ESTIMATE is used so that FFTW never writes to
 * the input/output arrays during plan creation, making static buffers safe.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_FFT_H
#define CSPLAYER_FFT_H

#include <stdbool.h>
/* Include complex.h before fftw3.h so that FFTW uses the C99 _Complex type
 * for fftw_complex instead of double[2].  This lets us use creal/cimag. */
#include <complex.h>
#include <fftw3.h>
#include "types.h"

/* ── Module state ────────────────────────────────────────────────────────── */

typedef struct {
    fftw_plan    plan;                   /* FFTW r2c plan                    */
    f64          in[FFT_SIZE];           /* Hann-windowed PCM input          */
    fftw_complex out[FFT_BINS];          /* r2c complex output               */
    f64          window[FFT_SIZE];       /* precomputed Hann coefficients    */
    f32          mags[FFT_BINS];         /* linear magnitudes (0 dBFS → 1.0) */
} fft_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Allocate the FFTW plan and precompute the Hann window.
 * Must be called once at startup.  Returns true on success.
 */
bool fft_init(fft_t *f);

/*
 * Run one FFT frame:
 *   1. Copy pcm_snapshot (RING_SIZE = FFT_SIZE samples, oldest-first) into f->in,
 *      applying the Hann window.
 *   2. Execute the FFTW plan.
 *   3. Compute f->mags[k] = |out[k]| / (FFT_SIZE / 2)  for k in [0, FFT_BINS).
 *      After this call, f->mags[k] ≈ 1.0 for a full-scale sine wave.
 */
void fft_process(fft_t *f, const f32 *pcm_snapshot);

/*
 * Destroy the FFTW plan.  Safe to call even if fft_init did not succeed.
 */
void fft_quit(fft_t *f);

#endif /* CSPLAYER_FFT_H */
