/*
 * spectrum.c — Log-frequency mapping and per-frame spectrum smoothing.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "spectrum.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <assert.h>

/*
 * Build the cubehelix colour LUT (D.A. Green 2011, Bull. Astron. Soc. India
 * 39, 289): rgb(t) follows a helix around the grey diagonal so perceived
 * brightness rises monotonically from black (t = 0) to white (t = 1).
 * Parameters (start direction, rotations, hue, gamma) live in types.h.
 */
static void specgram_build_cmap(spectrum_t *s)
{
    assert(s != NULL);
    assert(SPECGRAM_CMAP_N > 1);
    static const f32 TWO_PI = 6.28318530717958647692f;

    for (i32 q = 0; q < SPECGRAM_CMAP_N; q++) {
        f32 t   = (f32)q / (f32)(SPECGRAM_CMAP_N - 1);
        f32 lam = powf(t, SPECGRAM_CH_GAMMA);
        f32 phi = TWO_PI * (SPECGRAM_CH_START / 3.0f
                            + SPECGRAM_CH_ROT * t + 1.0f);
        f32 amp = SPECGRAM_CH_HUE * lam * (1.0f - lam) * 0.5f;
        f32 c   = cosf(phi);
        f32 sn  = sinf(phi);

        f32 rgb[3];
        rgb[0] = lam + amp * (-0.14861f * c + 1.78277f * sn);
        rgb[1] = lam + amp * (-0.29227f * c - 0.90649f * sn);
        rgb[2] = lam + amp * ( 1.97294f * c);
        for (i32 k = 0; k < 3; k++) {
            if (rgb[k] < 0.0f) { rgb[k] = 0.0f; }
            if (rgb[k] > 1.0f) { rgb[k] = 1.0f; }
            s->sg_cmap[q][k] = (u8)(rgb[k] * 255.0f + 0.5f);
        }
    }
}

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

    /* Force spectrum_update_bands() to rebuild its cached filter coefficients
     * (the rate may have changed). */
    s->band_coeff_rate = 0.0f;

    /* Wipe the spectrogram so a new track/rate starts with a blank window,
     * and (re)build its colour LUT. */
    spectrum_specgram_clear(s);
    specgram_build_cmap(s);
}

/*
 * Interpolate the FFT magnitude for pixel column x via the log-frequency LUT
 * and convert to dB, clamped to the display range [DB_BOT, DB_TOP].
 * Shared by the spectrum line and the spectrogram so the mapping is identical.
 */
static f32 col_db(const spectrum_t *s, const fft_t *f, i32 x)
{
    assert(s->bin_lo[x] >= 0 && s->bin_lo[x] < FFT_BINS);
    assert(s->bin_hi[x] >= 0 && s->bin_hi[x] < FFT_BINS);

    /* Linearly interpolate between adjacent FFT bins. */
    f32 lo  = f->mags[s->bin_lo[x]];
    f32 hi  = f->mags[s->bin_hi[x]];
    f32 mag = lo + s->bin_frac[x] * (hi - lo);

    /* Convert to dB; add a small epsilon to avoid log10(0). */
    f32 db = 20.0f * log10f(mag + 1e-10f);

    if (db < DB_BOT) { db = DB_BOT; }
    if (db > DB_TOP) { db = DB_TOP; }
    return db;
}

void spectrum_update(spectrum_t *s, const fft_t *f)
{
    assert(s != NULL);
    assert(f != NULL);

    f32 db_range = DB_TOP - DB_BOT;
    assert(db_range > 0.0f);

    for (i32 x = 0; x < SPEC_W; x++) {
        f32 db = col_db(s, f, x);

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
 * correlates with prev_win[panel_w].  Returns fallback when prev_win has
 * near-zero energy (silence / first frame).
 * panel_w is the number of display pixels (SPEC_W for the full panel,
 * BAND_W for a band sub-panel). */
static i32 xcorr_best_start(const f32 *samples, i32 n, i32 win,
                             const f32 *prev_win, i32 fallback, i32 panel_w)
{
    assert(samples  != NULL);
    assert(prev_win != NULL);
    assert(win > 0 && win <= n);
    assert(panel_w  > 0);

    i32 slo = n - XCORR_SEARCH;
    if (slo < 0)    { slo = 0; }
    i32 shi = n - win;
    if (shi < slo)  { return fallback; }

    f32 energy = 0.0f;
    for (i32 x = 0; x < panel_w; x += XCORR_STRIDE) {
        energy += prev_win[x] * prev_win[x];
    }
    if (energy < 1e-6f) { return fallback; }

    f32 best   = -1e30f;
    i32 best_s = fallback;
    for (i32 c = slo; c <= shi; c++) {
        f32 corr = 0.0f;
        for (i32 x = 0; x < panel_w; x += XCORR_STRIDE) {
            i32 src = c + x * win / panel_w;
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
    i32 start = xcorr_best_start(samples, n, win, s->prev_window, zc, SPEC_W);
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

/* ── 2nd-order Butterworth biquad filters (Direct Form II Transposed) ──────── */
/* biquad_t is declared in spectrum.h so spectrum_t can cache coefficients. */

static biquad_t biquad_lp(f32 fc, f32 fs)
{
    assert(fc > 0.0f && fc < fs * 0.5f);
    assert(fs > 0.0f);
    /* Q = 1/√2 for Butterworth; K/Q = K * √2 */
    static const f32 SQRT2 = 1.41421356237309504880f;
    f32 K    = tanf(3.14159265358979323846f * fc / fs);
    f32 K2   = K * K;
    f32 norm = 1.0f / (1.0f + K * SQRT2 + K2);
    biquad_t c;
    c.b0 = K2 * norm;
    c.b1 = 2.0f * c.b0;
    c.b2 = c.b0;
    c.a1 = 2.0f * (K2 - 1.0f) * norm;
    c.a2 = (1.0f - K * SQRT2 + K2) * norm;
    return c;
}

static biquad_t biquad_hp(f32 fc, f32 fs)
{
    assert(fc > 0.0f && fc < fs * 0.5f);
    assert(fs > 0.0f);
    static const f32 SQRT2 = 1.41421356237309504880f;
    f32 K    = tanf(3.14159265358979323846f * fc / fs);
    f32 K2   = K * K;
    f32 norm = 1.0f / (1.0f + K * SQRT2 + K2);
    biquad_t c;
    c.b0 =  norm;
    c.b1 = -2.0f * norm;
    c.b2 =  norm;
    c.a1 = 2.0f * (K2 - 1.0f) * norm;
    c.a2 = (1.0f - K * SQRT2 + K2) * norm;
    return c;
}

/* Direct Form II Transposed; zero initial state; dst may equal src (in-place). */
static void biquad_process(const biquad_t *c, const f32 *src, f32 *dst, i32 n)
{
    assert(c   != NULL);
    assert(src != NULL);
    assert(dst != NULL);
    assert(n   >  0);
    f32 w1 = 0.0f, w2 = 0.0f;
    for (i32 i = 0; i < n; i++) {
        f32 x  = src[i];
        f32 y  = c->b0 * x + w1;
        w1     = c->b1 * x - c->a1 * y + w2;
        w2     = c->b2 * x - c->a2 * y;
        dst[i] = y;
    }
}

/* Fill BAND_W screen points for one band sub-panel from s->band_scratch[].
 * Runs zero-crossing + xcorr alignment against prev_win for stable display.
 * x_offset is 0, BAND_W, or 2*BAND_W for bass, mids, highs respectively. */
static void fill_band_pts(spectrum_t *s, SDL_FPoint *pts, f32 *smooth,
                          f32 *prev_win, i32 x_offset,
                          i32 plot_y, i32 plot_h, i32 win, i32 n,
                          f32 smooth_coeff)
{
    assert(s            != NULL);
    assert(pts          != NULL);
    assert(smooth       != NULL);
    assert(prev_win     != NULL);
    assert(plot_h       >  0);
    assert(BAND_W       >  0);
    assert(n            >  0);
    assert(smooth_coeff >= 0.0f && smooth_coeff < 1.0f);

    i32 ss = n - 1 - win;
    if (ss < 1) { ss = 1; }
    i32 se = ss - WAVE_TRIGGER_RANGE;
    if (se < 1) { se = 1; }
    i32 trigger = -1;
    for (i32 i = se; i <= ss; i++) {
        if (s->band_scratch[i - 1] < 0.0f && s->band_scratch[i] >= 0.0f) {
            trigger = i;
            break;
        }
    }
    i32 zc = (trigger >= 0) ? trigger : (n - win);
    if (zc < 0)       { zc = 0; }
    if (zc + win > n) { zc = n - win; }

    i32 start = xcorr_best_start(s->band_scratch, n, win, prev_win, zc, BAND_W);
    if (start < 0)       { start = 0; }
    if (start + win > n) { start = n - win; }

    f32 half_h = (f32)plot_h * 0.5f;
    for (i32 x = 0; x < BAND_W; x++) {
        i32 src = start + x * win / BAND_W;
        if (src < 0)  { src = 0; }
        if (src >= n) { src = n - 1; }
        f32 raw = s->band_scratch[src];
        if (raw >  1.0f) { raw =  1.0f; }
        if (raw < -1.0f) { raw = -1.0f; }
        prev_win[x] = raw;
        smooth[x]   = smooth_coeff * smooth[x]
                    + (1.0f - smooth_coeff) * raw;
        f32 y = (f32)plot_y + half_h * (1.0f - smooth[x]);
        if (y < (f32)plot_y)            { y = (f32)plot_y; }
        if (y > (f32)(plot_y + plot_h)) { y = (f32)(plot_y + plot_h); }
        pts[x].x = (f32)(SPEC_X + x_offset + x);
        pts[x].y = y;
    }
}

void spectrum_update_bands(spectrum_t *s, const f32 *samples, i32 n,
                           f32 sample_rate, i32 plot_y, i32 plot_h)
{
    assert(s           != NULL);
    assert(samples     != NULL);
    assert(n > 0 && n <= FFT_SIZE);
    assert(sample_rate >  0.0f);
    assert(plot_h      >  0);
    assert(BAND_W      >  0);
    assert(SPEC_W == 3 * BAND_W);

    i32 bass_win = (i32)(BAND_BASS_WINDOW_S * sample_rate);
    i32 mids_win = (i32)(BAND_MIDS_WINDOW_S * sample_rate);
    i32 high_win = (i32)(BAND_HIGH_WINDOW_S * sample_rate);
    if (bass_win < 1) { bass_win = 1; } if (bass_win > n) { bass_win = n; }
    if (mids_win < 1) { mids_win = 1; } if (mids_win > n) { mids_win = n; }
    if (high_win < 1) { high_win = 1; } if (high_win > n) { high_win = n; }

    /* Coefficients depend only on sample_rate — rebuild only when it changes. */
    if (sample_rate != s->band_coeff_rate) {
        s->band_lp_bass    = biquad_lp(DECOMP_BASS_HZ, sample_rate);
        s->band_hp_bass    = biquad_hp(DECOMP_BASS_HZ, sample_rate);
        s->band_lp_mids    = biquad_lp(DECOMP_MIDS_HZ, sample_rate);
        s->band_hp_mids    = biquad_hp(DECOMP_MIDS_HZ, sample_rate);
        s->band_coeff_rate = sample_rate;
    }

    biquad_process(&s->band_lp_bass, samples, s->band_scratch, n);
    fill_band_pts(s, s->bass_pts, s->smooth_bass, s->prev_bass,
                  0, plot_y, plot_h, bass_win, n, BAND_BASS_SMOOTH);

    biquad_process(&s->band_hp_bass, samples, s->band_scratch, n);
    biquad_process(&s->band_lp_mids, s->band_scratch, s->band_scratch, n);
    fill_band_pts(s, s->mid_pts, s->smooth_mid, s->prev_mid,
                  BAND_W, plot_y, plot_h, mids_win, n, BAND_MIDS_SMOOTH);

    biquad_process(&s->band_hp_mids, samples, s->band_scratch, n);
    fill_band_pts(s, s->high_pts, s->smooth_high, s->prev_high,
                  2 * BAND_W, plot_y, plot_h, high_win, n, BAND_HIGH_SMOOTH);
}

/* ── Scrolling spectrogram (waterfall) ─────────────────────────────────────── */

void spectrum_update_specgram(spectrum_t *s, const fft_t *f, f32 dt)
{
    assert(s  != NULL);
    assert(f  != NULL);
    assert(dt >= 0.0f);
    assert(s->sg_head >= 0 && s->sg_head < SPECGRAM_ROWS);

    /* Advance the ring one row per SPECGRAM_ROW_S of accumulated time.  The
     * loop is bounded to a full ring per call; any excess time beyond that is
     * discarded (an entire window has scrolled past — start fresh). */
    s->sg_acc += dt;
    i32 adv = 0;
    for (i32 i = 0; i < SPECGRAM_ROWS && s->sg_acc >= SPECGRAM_ROW_S; i++) {
        s->sg_acc -= SPECGRAM_ROW_S;
        s->sg_head = (s->sg_head + SPECGRAM_ROWS - 1) % SPECGRAM_ROWS;
        SDL_memset(s->sg_amp[s->sg_head], 0, sizeof(s->sg_amp[s->sg_head]));
        adv++;
    }
    if (s->sg_acc >= SPECGRAM_ROW_S) { s->sg_acc = 0.0f; }

    /* Dirty rule: the run must cover BOTH every row written since the last
     * draw (dirty + adv — the count ACCUMULATES across calls so nothing is
     * missed while the display is toggled off) AND the whole pre-advance hot
     * zone (adv + SPECGRAM_HOT_ROWS — rows shifted out of the zone lose
     * their highlight and need repainting). */
    if (adv > 0) {
        i32 need  = s->sg_dirty + adv;
        i32 zone  = adv + SPECGRAM_HOT_ROWS;
        if (need < zone)          { need = zone; }
        if (need > SPECGRAM_ROWS) { need = SPECGRAM_ROWS; }
        s->sg_dirty = need;
    }

    /* Max-merge the current frame into the newest row so every frame within a
     * row period contributes and short transients survive.  Texture column c
     * covers the logical-px LUT columns [c·SPEC_W/COLS, (c+1)·SPEC_W/COLS);
     * take the bucket max so narrow spectral peaks stay visible at any
     * SPECGRAM_COLS.  Total inner iterations = SPEC_W (fixed bound). */
    f32 range = DB_TOP - DB_BOT;
    assert(range > 0.0f);
    assert(SPECGRAM_COLS > 0 && SPECGRAM_COLS <= SPEC_W);
    for (i32 c = 0; c < SPECGRAM_COLS; c++) {
        i32 x0 = c * SPEC_W / SPECGRAM_COLS;
        i32 x1 = (c + 1) * SPEC_W / SPECGRAM_COLS;
        f32 db = DB_BOT;
        for (i32 x = x0; x < x1; x++) {
            f32 d = col_db(s, f, x);
            if (d > db) { db = d; }
        }
        f32 t = (db - DB_BOT) / range;   /* clamped [0,1] */
        u8  q = (u8)(t * 255.0f + 0.5f);
        if (q > s->sg_amp[s->sg_head][c]) { s->sg_amp[s->sg_head][c] = q; }
    }
    if (s->sg_dirty < 1) { s->sg_dirty = 1; }
}

void spectrum_specgram_pixels(const spectrum_t *s, i32 row, u8 *dst)
{
    assert(s   != NULL);
    assert(dst != NULL);
    assert(row >= 0 && row < SPECGRAM_ROWS);

    static const u8 col_hot[3] = { SPECGRAM_COL_HOT };

    /* Quantised amplitude at/above which a cell renders "hot".  Hot colouring
     * applies only within the newest SPECGRAM_HOT_ROWS rows; rows repaint in
     * the normal colormap as they leave the zone (the dirty rule in
     * spectrum_update_specgram always reaches past the zone's tail). */
    const u8 hot_q = (u8)((SPECGRAM_HOT_DB - DB_BOT) / (DB_TOP - DB_BOT)
                          * 255.0f + 0.5f);
    i32  dist    = (row - s->sg_head + SPECGRAM_ROWS) % SPECGRAM_ROWS;
    bool in_zone = (dist < SPECGRAM_HOT_ROWS);

    assert(SPECGRAM_ALPHA_KNEE > 0.0f);
    assert(SPECGRAM_ALPHA_MAX > 0.0f && SPECGRAM_ALPHA_MAX <= 1.0f);
    const f32 inv_knee = 1.0f / SPECGRAM_ALPHA_KNEE;
    const f32 a_scale  = SPECGRAM_ALPHA_MAX * 255.0f;

    for (i32 c = 0; c < SPECGRAM_COLS; c++) {
        u8  q = s->sg_amp[row][c];
        f32 t = (f32)q / 255.0f;
        if (in_zone && q >= hot_q) {
            /* Prominent frequency: flat bright colour, linear alpha. */
            for (i32 k = 0; k < 3; k++) { dst[c * 4 + k] = col_hot[k]; }
            dst[c * 4 + 3] = (u8)(t * a_scale + 0.5f);
        } else {
            /* Cubehelix colormap; alpha is a smoothstep saturating at the
             * knee, capped at SPECGRAM_ALPHA_MAX so the spectrum line drawn
             * on top keeps contrast, while the faint (black) end still
             * dissolves into the panel. */
            for (i32 k = 0; k < 3; k++) {
                dst[c * 4 + k] = s->sg_cmap[q][k];
            }
            f32 u = t * inv_knee;
            if (u > 1.0f) { u = 1.0f; }
            f32 a = u * u * (3.0f - 2.0f * u);
            dst[c * 4 + 3] = (u8)(a * a_scale + 0.5f);
        }
    }
}

void spectrum_specgram_clear(spectrum_t *s)
{
    assert(s != NULL);
    assert(SPECGRAM_ROWS > 0);
    SDL_memset(s->sg_amp, 0, sizeof(s->sg_amp));
    s->sg_head  = 0;
    s->sg_acc   = 0.0f;
    s->sg_dirty = SPECGRAM_ROWS;   /* force full re-upload of the blank image */
}

void spectrum_specgram_clear_dirty(spectrum_t *s)
{
    assert(s != NULL);
    assert(s->sg_dirty >= 0 && s->sg_dirty <= SPECGRAM_ROWS);
    s->sg_dirty = 0;
}

void spectrum_specgram_break(spectrum_t *s)
{
    assert(s != NULL);
    assert(s->sg_head >= 0 && s->sg_head < SPECGRAM_ROWS);
    assert(SPECGRAM_BREAK_ROWS > 0 && SPECGRAM_BREAK_ROWS < SPECGRAM_ROWS);

    /* Scroll SPECGRAM_BREAK_ROWS blank rows into the waterfall. */
    for (i32 i = 0; i < SPECGRAM_BREAK_ROWS; i++) {
        s->sg_head = (s->sg_head + SPECGRAM_ROWS - 1) % SPECGRAM_ROWS;
        SDL_memset(s->sg_amp[s->sg_head], 0, sizeof(s->sg_amp[s->sg_head]));
    }

    /* Same dirty rule as spectrum_update_specgram: accumulate the written
     * rows and cover the pre-break hot zone, whose rows just shifted out. */
    {
        i32 need = s->sg_dirty + SPECGRAM_BREAK_ROWS;
        i32 zone = SPECGRAM_BREAK_ROWS + SPECGRAM_HOT_ROWS;
        if (need < zone)          { need = zone; }
        if (need > SPECGRAM_ROWS) { need = SPECGRAM_ROWS; }
        s->sg_dirty = need;
    }

    /* Pre-charge the accumulator so the next update advances immediately:
     * post-seek audio lands in a fresh row and the break rows stay blank. */
    s->sg_acc = SPECGRAM_ROW_S;
}
