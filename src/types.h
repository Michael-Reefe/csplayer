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

/* ── Application metadata ────────────────────────────────────────────────── */
#define CSPLAYER_APP_NAME    "csplayer"
#define CSPLAYER_APP_VERSION "0.1.0"
#define CSPLAYER_APP_ID      "com.csplayer.csplayer"
#define CSPLAYER_ORG         "csplayer"

/* ── Window / presentation ───────────────────────────────────────────────── */
/* The renderer uses a logical resolution of LOG_W × LOG_H (16:9).
 * SDL3 scales it to the physical window with integer scaling. */
#define LOG_W          512
#define LOG_H          288
#define PHYS_W         1024
#define PHYS_H         576
/* Integer scale factor from logical to physical pixels (= PHYS_W / LOG_W = 2).
 * Fonts are opened at ptsize × DISPLAY_SCALE and rendered at 1/DISPLAY_SCALE
 * logical size so FreeType rasterises at the physical pixel resolution. */
#define DISPLAY_SCALE  (PHYS_W / LOG_W)

/* ── FFT / audio capture ─────────────────────────────────────────────────── */
/* FFT_SIZE must be a power of two.  8192 gives ≈5.4 Hz/bin at 44100 Hz. */
#define FFT_SIZE      8192
#define FFT_BINS      (FFT_SIZE / 2 + 1)   /* r2c output bins = 4097 */
#define RING_SIZE     FFT_SIZE              /* PCM ring buffer (mono samples) */
#define SMOOTH_COEFF  0.80f                 /* exponential smoothing per frame */

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
 * SPEC_W leaves 16 px on the right so the "20k" frequency label (24 px
 * wide, centred on the rightmost tick) does not overflow the window. */
#define SPEC_X  40
#define SPEC_Y  (HDR_H + 8)   /* = 28 */
#define SPEC_W  456
#define SPEC_H  152

/* Playlist panel — 14 px below the spectrum (room for frequency tick labels) */
#define PL_Y    (SPEC_Y + SPEC_H + 14)   /* = 194 */
#define PL_H    64
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

/* ── Playlist ────────────────────────────────────────────────────────────── */
#define PLAYLIST_MAX    256   /* maximum number of tracks      */
#define PATH_MAX_LEN    512   /* maximum file path (bytes)     */
#define TITLE_MAX_LEN   128   /* maximum display title (bytes) */

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

#endif /* CSPLAYER_TYPES_H */
