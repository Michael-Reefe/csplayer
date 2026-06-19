/*
 * fft.h — Real-to-complex FFT computation.
 *
 * Implements a radix-2 decimation-in-time (DIT) Cooley-Tukey real FFT using
 * the Sorensen/Brigham half-length complex split:
 *   1. Pack N real samples into N/2 complex values: G[n] = x[2n] + j·x[2n+1]
 *   2. Run an N/2-point complex DIT butterfly to get G_hat[k].
 *   3. Reconstruct the N/2+1 unique output bins from G_hat[k].
 *
 * All buffers are statically allocated inside fft_t (NASA Rule 3: no runtime
 * dynamic allocation).
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_FFT_H
#define CSPLAYER_FFT_H

#include <stdbool.h>
#include "types.h"

/* ── FFT plan ────────────────────────────────────────────────────────────── */

/* Precomputed tables for one fixed-length real FFT.  All fields are statically
 * sized — no heap allocation.  Initialised once by rfft_plan_init(). */
typedef struct {
    u32 N;                       /* transform length (= FFT_SIZE)              */
    u32 bit_rev[FFT_SIZE / 2];   /* bit-reversal permutation indices           */
    c32 tw[FFT_SIZE / 2];        /* twiddle factors: exp(-2πjk/N), k=0..N/2-1 */
} rfft_plan_t;

/* ── Module state ────────────────────────────────────────────────────────── */

typedef struct {
    rfft_plan_t plan;                /* embedded plan — no pointer, no malloc     */
    f32         in[FFT_SIZE];        /* Hann-windowed PCM input                   */
    c32         out[FFT_BINS];       /* r2c complex output, bins 0..N/2           */
    f32         window[FFT_SIZE];    /* precomputed Hann coefficients             */
    f32         mags[FFT_BINS];      /* linear magnitudes (0 dBFS → 1.0)         */
} fft_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Initialise the FFT plan and precompute the Hann window.
 * Must be called once at startup.  Returns true on success.
 */
bool fft_init(fft_t *f);

/*
 * Run one FFT frame:
 *   1. Copy pcm_snapshot (FFT_SIZE samples) into f->in, applying the Hann window.
 *   2. Execute the real FFT.
 *   3. Compute f->mags[k] = |out[k]| / (FFT_SIZE / 2) for k in [0, FFT_BINS).
 *      After this call, f->mags[k] ≈ 1.0 for a full-scale sine wave.
 */
void fft_process(fft_t *f, const f32 *pcm_snapshot);

/*
 * Tear down the FFT state.  Safe to call even if fft_init did not succeed.
 */
void fft_quit(fft_t *f);

#endif /* CSPLAYER_FFT_H */
