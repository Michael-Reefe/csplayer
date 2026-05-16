/*
 * fft.c — Real-to-complex FFT computation.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "fft.h"

#include <SDL3/SDL.h>
#include <complex.h>
#include <math.h>
#include <assert.h>

#define CSP_PI 3.14159265358979323846f

/* Compute integer log2 of a power-of-two value using bit-counting. */
static u32 uint_log2(u32 v)
{
    assert(v >= 1);
    assert((v & (v - 1)) == 0);
    u32 r = 0;
    while (v >>= 1) { r++; }
    return r;
}

/* Precompute bit-reversal table and twiddle factors for an N-point real FFT. */
static void rfft_plan_init(rfft_plan_t *plan, u32 N)
{
    assert(plan != NULL);
    assert(N >= 2 && (N & (N - 1)) == 0);

    plan->N   = N;
    u32 M     = N / 2;
    u32 log2M = uint_log2(M);

    for (u32 i = 0; i < M; i++) {
        u32 k = 0;
        for (u32 j = 0; j < log2M; j++)
            k = (k << 1) | (i >> j & 1);
        plan->bit_rev[i] = k;
    }

    for (u32 k = 0; k < M; k++)
        plan->tw[k] = cexpf(-2.0f * CSP_PI / (f32)N * (f32)k * I);
}

/* In-place N/2-point complex DIT butterfly on out[0..N/2-1]. */
static void c2c_fft(c32 *out, rfft_plan_t *plan)
{
    assert(plan    != NULL);
    assert(out     != NULL);
    assert(plan->N  > 0);

    u32 M     = plan->N / 2;
    u32 log2M = uint_log2(M);

    /* Bit-reversal permutation. */
    for (u32 i = 0; i < M; i++) {
        u32 k = plan->bit_rev[i];
        if (k > i) {
            c32 tmp = out[i];
            out[i]  = out[k];
            out[k]  = tmp;
        }
    }

    /* Cooley-Tukey butterfly stages. */
    for (u32 stage = 1; stage <= log2M; stage++) {
        u32 m      = (u32)1 << stage;
        u32 halfm  = m >> 1;
        u32 stride = plan->N >> stage;
        for (u32 k = 0; k < M; k += m) {
            for (u32 j = 0; j < halfm; j++) {
                c32 w    = plan->tw[j * stride];
                c32 temp = out[k + j + halfm] * w;
                out[k + j + halfm] = out[k + j] - temp;
                out[k + j]         = out[k + j] + temp;
            }
        }
    }
}

/*
 * Execute the real FFT on in[0..N-1], writing N/2+1 unique bins to out[0..N/2].
 * Uses the Sorensen/Brigham half-length complex split:
 *   pack real input → N/2-point complex DFT → reconstruct unique bins.
 * Only the N/2+1 in-bounds bins are written; the redundant Hermitian-conjugate
 * mirror values (which fft_process never reads) are intentionally omitted.
 */
static void rfft_execute(f32 *in, c32 *out, rfft_plan_t *plan)
{
    assert(plan != NULL);
    assert(in   != NULL);
    assert(out  != NULL);

    u32 N = plan->N;
    u32 M = N / 2;

    /* Pack: G[n] = x[2n] + j·x[2n+1] */
    for (u32 n = 0; n < M; n++)
        out[n] = in[2*n] + in[2*n+1] * I;

    c2c_fft(out, plan);

    /* DC bin (k=0) and Nyquist bin (k=N/2) are both purely real. */
    c32 G0 = out[0];
    out[0] = crealf(G0) + cimagf(G0);
    out[M] = crealf(G0) - cimagf(G0);

    /* Reconstruct bins k = 1 .. M/2-1 from the half-length complex DFT output.
     * out[k] = X[k] and out[M-k] = X[M-k] = conj(X[M+k]) by Hermitian symmetry.
     * The mirror writes out[N-k] and out[M+k] lie outside FFT_BINS and are
     * omitted — fft_process reads only out[0..FFT_BINS-1]. */
    for (u32 k = 1; k < M / 2; k++) {
        c32 w  = plan->tw[k];
        c32 xe = +0.5f * (out[k] + conjf(out[M - k]));
        c32 xo = -0.5f * (out[k] - conjf(out[M - k])) * I;
        c32 p  = xo * w;
        out[k]     = xe + p;
        out[M - k] = conjf(xe - p);
    }

    /* k = M/2 (= N/4) is self-paired; tw[M/2] = exp(-jπ/2) = -j. */
    if (N >= 4) {
        c32 Gq   = out[M / 2];
        out[M/2] = crealf(Gq) + cimagf(Gq) * plan->tw[M / 2];
    }
}


bool fft_init(fft_t *f)
{
    assert(f        != NULL);
    assert(FFT_SIZE  > 0);

    SDL_memset(f, 0, sizeof(*f));
    rfft_plan_init(&f->plan, (u32)FFT_SIZE);

    /* Precompute the Hann window:  w[i] = 0.5 * (1 − cos(2π·i / (N−1))) */
    for (i32 i = 0; i < FFT_SIZE; i++) {
        assert(i < FFT_SIZE);
        f->window[i] = 0.5f * (1.0f - cosf(2.0f * CSP_PI
                                            * (f32)i / (f32)(FFT_SIZE - 1)));
    }

    return true;
}

void fft_process(fft_t *f, const f32 *pcm_snapshot)
{
    assert(f            != NULL);
    assert(pcm_snapshot != NULL);
    assert(f->plan.N   == (u32)FFT_SIZE);

    /* Apply Hann window to the time-domain samples. */
    for (i32 i = 0; i < FFT_SIZE; i++) {
        assert(i < FFT_SIZE);
        f->in[i] = pcm_snapshot[i] * f->window[i];
    }

    rfft_execute(f->in, f->out, &f->plan);

    /* Compute linear magnitude for each output bin.
     * Normalise by (FFT_SIZE / 2) so that a 0-dBFS sine gives mag ≈ 1.0. */
    f32 norm = 1.0f / (f32)(FFT_SIZE / 2);
    for (i32 k = 0; k < FFT_BINS; k++) {
        assert(k < FFT_BINS);
        f32 re     = crealf(f->out[k]);
        f32 im     = cimagf(f->out[k]);
        f->mags[k] = sqrtf(re * re + im * im) * norm;
    }
}

void fft_quit(fft_t *f)
{
    assert(f       != NULL);
    assert(FFT_BINS > 0);
    f->plan.N = 0;   /* mark as uninitialised */
}
