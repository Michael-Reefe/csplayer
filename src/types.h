/*
 * types.h — Standard-width type aliases and compile-time constants.
 *
 * Every numeric constant and layout parameter lives here so that
 * nothing magic appears in .c files.  Changing a constant in one place
 * changes it everywhere (NASA Power of 10, Rule 8: preprocessor for
 * includes and constants only).
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_TYPES_H
#define CSPLAYER_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <complex.h>

/* ── Integer / float aliases ─────────────────────────────────────────────── */
typedef  int8_t   i8;
typedef  int16_t  i16;
typedef  int32_t  i32;
typedef  int64_t  i64;
typedef  uint8_t  u8;
typedef  uint16_t u16;
typedef  uint32_t u32;
typedef  uint64_t u64;
typedef  float    f32;
typedef  double   f64;

/* ── Complex aliases ─────────────────────────────────────────────────────── */
typedef  float  complex c32;
typedef  double complex c64;

/* ── Application metadata ────────────────────────────────────────────────── */
#define CSPLAYER_APP_NAME    "csplayer"
#define CSPLAYER_APP_VERSION "0.4.0"
#define CSPLAYER_APP_ID      "com.csplayer.csplayer"
#define CSPLAYER_ORG         "csplayer"

/* ── Window / presentation ───────────────────────────────────────────────── */
/* The renderer uses a logical resolution of LOG_W × LOG_H (16:9).
 * SDL3 scales it to the physical window with integer scaling. */
#define LOG_W          640
#define LOG_H          360
#define PHYS_W         1280
#define PHYS_H         720   /* = LOG_H * DISPLAY_SCALE */
/* Integer scale factor from logical to physical pixels (= PHYS_W / LOG_W = 2).
 * Fonts are opened at ptsize × DISPLAY_SCALE and rendered at 1/DISPLAY_SCALE
 * logical size so FreeType rasterises at the physical pixel resolution. */
#define DISPLAY_SCALE  (PHYS_W / LOG_W)

/* Render-loop frame cap.  SDL's default SDL_AppIterate rate is "as fast as
 * possible"; vsync only throttles the CPU on backends where present blocks the
 * calling thread (e.g. Direct3D on Windows), not on macOS/Metal, where the loop
 * would otherwise busy-spin at thousands of fps.  The loop is paced explicitly
 * to this rate so CPU use is bounded on every platform. */
#define TARGET_FPS     60
#define FRAME_TIME_NS  (1000000000ULL / TARGET_FPS)

/* ── FFT / audio capture ─────────────────────────────────────────────────── */
/* FFT_SIZE must be a power of two.  8192 gives ≈5.4 Hz/bin at 44100 Hz. */
#define FFT_SIZE      8192
#define FFT_BINS      (FFT_SIZE / 2 + 1)   /* r2c output bins = 4097 */
#define RING_SIZE     FFT_SIZE              /* PCM ring buffer (mono samples) */
#define SMOOTH_COEFF  0.20f                 /* exponential smoothing per frame */

/* ── Spectrum display ────────────────────────────────────────────────────── */
#define FREQ_MIN   20.0f      /* Hz — lowest displayed frequency  */
#define FREQ_MAX   20000.0f   /* Hz — highest displayed frequency */
#define DB_TOP     0.0f       /* dB — top of plot    (0 dB = loud)  */
#define DB_BOT     (-90.0f)   /* dB — bottom of plot (-90 dB = quiet) */

/* ── Layout (logical pixels) ─────────────────────────────────────────────── */
/* Header bar */
#define HDR_Y   0
#define HDR_H   20

/* Spectrum plot.
 * SPEC_Y is offset 8 px below the header so the 0 dB label (rendered at
 * SPEC_Y − FONT_H/2 = SPEC_Y − 4) clears the header bottom edge.
 * SPEC_W leaves 18 px on the right so the "20k" frequency label (24 px
 * wide, centred on the rightmost tick) does not overflow the window.
 * SPEC_W must be divisible by 3 (BAND_W = SPEC_W / 3). */
#define SPEC_X  40
#define SPEC_Y  (HDR_H + 8)   /* = 28 */
#define SPEC_W  582            /* 582 / 3 = 194 = BAND_W */
#define SPEC_H  192

/* Playlist panel — 22 px below spectrum:
 * 4 px gap + 8 px tick labels + 2 px gap + 8 px X-axis title = 22 px */
#define PL_Y    (SPEC_Y + SPEC_H + 22)   /* = 242 */
#define PL_H    96             /* 6 rows × PL_ROW (16 px) */
#define PL_ROW  16    /* height of each playlist row in logical pixels */

/* [+] add-track button inside the playlist panel.
 * PL_ADD_SZ is sized to fit within one playlist row (PL_ROW − 2). */
#define PL_ADD_X    2
#define PL_ADD_SZ   (PL_ROW - 2)   /* = 14 */

/* Control bar — pinned to the bottom of the window */
#define CTRL_Y  (LOG_H - 24)   /* = 264 */
#define CTRL_H  24
#define BTN_SZ  20   /* button width and height */
#define BTN_GAP 2    /* gap between buttons */

/* ── Volume ──────────────────────────────────────────────────────────────── */
/* Minimum volume in dB; the volume slider maps linearly from
 * VOL_DB_MIN (slider pos 0) to 0 dB (slider pos 1). */
#define VOL_DB_MIN  (-50.0f)

/* ── Application mode ────────────────────────────────────────────────────── */
typedef enum {
    APP_MODE_PLAYER = 0,
    APP_MODE_SYNTH  = 1,
} app_mode_t;

/* ── Graph display mode ──────────────────────────────────────────────────── */
/* Orthogonal to APP_MODE_PLAYER/SYNTH — persists across mode switches. */
typedef enum {
    GRAPH_MODE_FREQ  = 0,  /* log-freq FFT plot (default)              */
    GRAPH_MODE_TIME,       /* time-domain waveform (green)             */
    GRAPH_MODE_BOTH,       /* horizontal split: freq top, time bottom  */
    GRAPH_MODE_COUNT,      /* sentinel — do not use as a runtime value */
} graph_mode_t;

/* Time-domain waveform window */
#define WAVE_WINDOW_S       0.020f  /* 20 ms — ~2 cycles at 100 Hz, ~8.8 at 440 Hz */
/* Per-band display windows: wider for bass (slow oscillations), narrower for highs. */
#define BAND_BASS_WINDOW_S  0.050f  /* 50 ms — captures full low-frequency cycles   */
#define BAND_MIDS_WINDOW_S  0.010f  /* 10 ms — vocal / instrument detail            */
#define BAND_HIGH_WINDOW_S  0.005f  /*  5 ms — high-frequency transient detail      */
#define WAVE_TRIGGER_RANGE  512     /* samples scanned for zero-crossing fallback    */
#define XCORR_SEARCH        2048    /* sample range searched for cross-correlation   */
#define XCORR_STRIDE        4       /* pixel stride used in cross-correlation        */
#define WAVE_SMOOTH_COEFF   0.65f   /* per-frame exponential smoothing for time pts */
#define BAND_BASS_SMOOTH    0.80f   /* stronger smoothing for bass (wide, slow window) */
#define BAND_MIDS_SMOOTH    WAVE_SMOOTH_COEFF
#define BAND_HIGH_SMOOTH    WAVE_SMOOTH_COEFF

/* Graph mode toggle buttons (top-right interior of spectrum panel) */
#define GRAPH_BTN_W    38   /* logical px per button (fits 4-char label + padding) */
#define GRAPH_BTN_H    10   /* logical px per button height                        */
#define GRAPH_BTN_GAP   2   /* gap between adjacent buttons                        */

/* ── Scrolling spectrogram (waterfall) ──────────────────────────────────────
 * Ring buffer of amplitude rows behind the frequency plot: X = log frequency
 * (same LUT as the spectrum line), Y = recent time — the top row is "now" and
 * the image slides downward, oldest rows leaving through the plot bottom.
 *
 * SPECGRAM_ROWS and SPECGRAM_COLS are free knobs: everything else (cell size,
 * frequency-bucket mapping, texture size, upload buffers, row period) derives
 * from them, so they can be changed independently.  Each cell renders as a
 * block of (SPEC_H/ROWS × SPEC_W/COLS) logical px with NEAREST scaling.
 * Crispest results when SPECGRAM_ROWS divides SPEC_H — and SPEC_H/2 for Both
 * mode — evenly (divisors of 96: 96, 48, 32, 24, …); otherwise blocks are
 * merely uneven by a pixel, never misaligned. */
#define SPECGRAM_ROWS      (SPEC_H / 2)   /* ring rows (2 logical px per row) */
#define SPECGRAM_COLS      (SPEC_W / 2)   /* columns (~2 logical px per col)  */
/* Seconds of history spanning the plot: one ring row per two rendered
 * frames, so the scroll rate is tied to TARGET_FPS. */
#define SPECGRAM_WINDOW_S  (2.0f * ((f32)SPECGRAM_ROWS / (f32)TARGET_FPS))
#define SPECGRAM_ROW_S     (SPECGRAM_WINDOW_S / (f32)SPECGRAM_ROWS)
/* "Spectrogram" toggle: bottom-left interior of the freq subplot.  The y
 * coordinate is computed at runtime (the freq plot rect differs between
 * Spec and Both modes); x matches the Deconstruct button column. */
#define SPECGRAM_BTN_X     (SPEC_X + 2)
#define SPECGRAM_BTN_W     96   /* "Spectrogram" = 11 chars × 8 px + padding */
/* Amplitude colormap: cubehelix (D.A. Green 2011, Bull. Astron. Soc. India
 * 39, 289) — a helix around the grey diagonal whose perceived brightness
 * rises monotonically from black (amplitude 0) to white (full scale).  The
 * faint end still fades to transparent so silence blends into the panel
 * background rather than rendering as solid black. */
#define SPECGRAM_CMAP_N    256     /* LUT entries (one per quantised level) */
#define SPECGRAM_CH_START  0.5f    /* start direction (R = 1, G = 2, B = 3) */
#define SPECGRAM_CH_ROT   (-1.5f)  /* helix rotations over the full ramp    */
#define SPECGRAM_CH_HUE    1.0f    /* saturation of the helix               */
#define SPECGRAM_CH_GAMMA  1.0f    /* lightness gamma                       */
/* Alpha ramp: smoothstep from transparent at amplitude 0 up to the cap at
 * SPECGRAM_ALPHA_KNEE, clamped there above it — soft at both ends, with
 * everything past the knee shown at full strength.  The cap stays below
 * 100% so the blue spectrum line keeps contrast over bright regions. */
#define SPECGRAM_ALPHA_KNEE  0.5f   /* amplitude fraction where alpha caps */
#define SPECGRAM_ALPHA_MAX   0.5f   /* opacity ceiling of the waterfall    */
/* Cells at or above SPECGRAM_HOT_DB *within the newest SPECGRAM_HOT_ROWS
 * rows* render in the flat bright "hot" colour with LINEAR alpha, so the
 * most prominent frequencies flash yellow at the top of the waterfall and
 * settle into the normal pink ramp as they scroll down. */
#define SPECGRAM_HOT_DB    (-45.0f)       /* dBFS threshold for hot cells */
#define SPECGRAM_HOT_ROWS  0              /* newest rows eligible for hot */
#define SPECGRAM_COL_HOT  255, 255, 180   /* R, G, B of hot cells         */
/* Blank rows inserted into the waterfall when the user jumps the timeline,
 * marking the discontinuity.  Seeks arriving within the cooldown of the last
 * break (e.g. a scrub drag) coalesce into a single break. */
#define SPECGRAM_BREAK_ROWS         3
#define SPECGRAM_BREAK_COOLDOWN_NS  400000000ULL   /* 0.4 s */

/* ── Synthesizer ─────────────────────────────────────────────────────────── */
#define SYNTH_SAMPLE_RATE  44100
/* 10-second sustain buffer — attack repeats only every 10 s when looping (~1.7 MB in BSS). */
#define SYNTH_MAX_FRAMES   (10 * SYNTH_SAMPLE_RATE)
#define SYNTH_INSTRUMENTS  128   /* all General MIDI programs 0–127 */
#define SYNTH_NOTES        128   /* MIDI note range 0–127           */

/* ── Playlist ────────────────────────────────────────────────────────────── */
#define PLAYLIST_MAX    256   /* maximum number of tracks      */
#define PATH_MAX_LEN    512   /* maximum file path (bytes)     */
#define TITLE_MAX_LEN   256   /* maximum display title (bytes) */

/* ── Colors (R, G, B, A) — used with SDL_SetRenderDrawColor ─────────────── */
#define COL_BG          8,   8,  16, 255   /* near-black background     */
#define COL_PANEL      17,  17,  40, 255   /* dark panel interior       */
#define COL_BORDER     37,  37,  80, 255   /* panel borders             */
#define COL_AXIS       60,  60, 100, 255   /* spectrum axis lines       */
#define COL_TICK      120, 120, 180, 255   /* axis tick marks/labels    */
#define COL_SPEC       80, 140, 255, 255   /* spectrum line             */
#define COL_TEXT      220, 220, 255, 255   /* primary text              */
#define COL_DIM        80,  80, 120, 255   /* dimmed / secondary text   */
#define COL_BTN        30,  30,  70, 255   /* button background         */
#define COL_BTN_HOT    70,  50, 140, 255   /* button hover / active     */
#define COL_SCRUB_BG   20,  20,  50, 255   /* seek-bar / vol-bar track  */
#define COL_SCRUB_FG   90, 150, 255, 255   /* seek-bar filled portion   */
#define COL_VOL_FG    100, 200, 120, 255   /* volume-bar filled portion */
#define COL_NOW       100, 200, 120, 255   /* "now playing" accent      */
#define COL_HEADER     20,  20,  50, 255   /* header bar background     */
#define COL_WAVE      100, 200, 120, 255   /* time-domain waveform line */

/* ── Frequency-band waveform decomposition ───────────────────────────────── */
/* Width of each band sub-panel in decompose mode (SPEC_W must be divisible by 3). */
#define BAND_W          (SPEC_W / 3)   /* 152 px — bass | mids | highs */
/* Frequency cutoffs separating bass, mids, and highs. */
#define DECOMP_BASS_HZ   200.0f        /* bass / mids boundary (Hz)    */
#define DECOMP_MIDS_HZ  4000.0f        /* mids / highs boundary (Hz)   */
/* "Deconstruct" toggle button: bottom-left interior of the spectrum panel. */
#define DECOMP_BTN_X    (SPEC_X + 2)
#define DECOMP_BTN_Y    (SPEC_Y + SPEC_H - GRAPH_BTN_H - 2)
#define DECOMP_BTN_W    96   /* "Deconstruct" = 11 chars × 8 px + 8 px padding */
/* Band waveform colours (mids reuse COL_WAVE green). */
#define COL_BASS    255, 120,   0, 255   /* warm orange — bass  */
#define COL_HIGHS     0, 180, 255, 255   /* cyan        — highs */

#endif /* CSPLAYER_TYPES_H */
