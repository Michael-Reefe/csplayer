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
#define CSPLAYER_APP_VERSION "0.3.1"
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
#define VOL_DB_MIN  (-60.0f)

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
