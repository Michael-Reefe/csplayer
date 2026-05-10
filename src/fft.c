/*
 * fft.c — FFTW-based real-to-complex spectrum computation.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "fft.h"

#include <SDL3/SDL.h>
#include <complex.h>
#include <math.h>
#include <assert.h>

/* Value of π used for Hann window calculation. */
#define CSP_PI 3.14159265358979323846

bool fft_init(fft_t *f)
{
    assert(f        != NULL);
    assert(FFT_SIZE  > 0);

    SDL_memset(f, 0, sizeof(*f));

    /*
     * FFTW_ESTIMATE: chooses a heuristic plan without timing benchmarks and
     * without writing to the in/out arrays.  This is required when using
     * statically allocated buffers (NASA Rule 3: no runtime malloc).
     */
    f->plan = fftw_plan_dft_r2c_1d(FFT_SIZE, f->in, f->out, FFTW_ESTIMATE);
    if (f->plan == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "fft_init: fftw_plan_dft_r2c_1d failed");
        return false;
    }

    /* Precompute the Hann window:  w[i] = 0.5 * (1 − cos(2π·i / (N−1))) */
    for (i32 i = 0; i < FFT_SIZE; i++) {
        assert(i < FFT_SIZE);
        f->window[i] = 0.5 * (1.0 - cos(2.0 * CSP_PI
                                         * (f64)i / (f64)(FFT_SIZE - 1)));
    }

    return true;
}

void fft_process(fft_t *f, const f32 *pcm_snapshot)
{
    assert(f            != NULL);
    assert(pcm_snapshot != NULL);
    assert(f->plan      != NULL);

    /* Apply Hann window to the time-domain samples. */
    for (i32 i = 0; i < FFT_SIZE; i++) {
        assert(i < FFT_SIZE);
        f->in[i] = (f64)pcm_snapshot[i] * f->window[i];
    }

    fftw_execute(f->plan);

    /*
     * Compute linear magnitude for each output bin.
     * Normalise by (FFT_SIZE / 2) so that a 0 dBFS sine gives mag ≈ 1.0.
     * The DC bin (k=0) and Nyquist bin (k=FFT_BINS-1) are at half amplitude
     * in an r2c transform, but for display purposes this is acceptable.
     */
    f64 norm = 1.0 / (f64)(FFT_SIZE / 2);
    for (i32 k = 0; k < FFT_BINS; k++) {
        assert(k < FFT_BINS);
        f64 re    = creal(f->out[k]);
        f64 im    = cimag(f->out[k]);
        f->mags[k] = (f32)(sqrt(re * re + im * im) * norm);
    }
}

void fft_quit(fft_t *f)
{
    assert(f       != NULL);
    assert(FFT_BINS  > 0);

    if (f->plan != NULL) {
        fftw_destroy_plan(f->plan);
        f->plan = NULL;
    }
    fftw_cleanup();
}
