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
#define CSPLAYER_APP_VERSION "0.5.0"
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

/* The spectrum panel is split into a button toolbar across the top and the
 * drawable plot region below it.  Every graph button lives in the toolbar, so
 * no button overlaps plot content and no button position depends on the active
 * graph mode.  SPEC_Y / SPEC_H remain the panel's outer footprint (nothing
 * below it moves); PLOT_Y / PLOT_H are the plot bounds and are what every
 * drawing and point-mapping routine must use.
 *
 * GRAPH_BAR_H = 16 is chosen so PLOT_H = 176 factors cleanly: PLOT_H / 2 = 88
 * serves the Both-mode halves, the spectrogram ring, and the stem grid rows. */
#define GRAPH_BAR_H  16                      /* GRAPH_BTN_H (10) + 3 px above/below */
#define PLOT_Y       (SPEC_Y + GRAPH_BAR_H)  /* =  44 */
#define PLOT_H       (SPEC_H - GRAPH_BAR_H)  /* = 176 */

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
    GRAPH_MODE_STEMS,      /* 3×2 grid of per-instrument waveforms     */
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

/* Graph toolbar buttons.  All of them sit in the GRAPH_BAR_H strip at the top
 * of the spectrum panel: the graph-mode selectors are right-aligned, the
 * overlay toggles (Spectrogram, Deconstruct) left-aligned.
 *
 * Every label parenthesises its keyboard shortcut — "(S)pec", "S(t)em",
 * "Spectro(g)ram" — so the widths below allow for those two extra characters. */
#define GRAPH_BTN_W    52   /* 6-char label ("(S)pec") × 8 px + 4 px padding */
#define GRAPH_BTN_H    10   /* logical px per button height                  */
#define GRAPH_BTN_GAP   2   /* gap between adjacent buttons                  */

/* Common vertical placement: centred within the toolbar strip. */
#define GBAR_BTN_Y   (SPEC_Y + (GRAPH_BAR_H - GRAPH_BTN_H) / 2)   /* = 31 */

/* Left group — overlay toggles, in draw order. */
#define GBAR_L_X        (SPEC_X + 2)                        /* = 42  */
#define SPECGRAM_BTN_W  112  /* "Spectro(g)ram" = 13 chars × 8 px + padding */
#define SPECGRAM_BTN_X  GBAR_L_X                            /* = 42  */
#define DECOMP_BTN_W    112  /* "(D)econstruct" = 13 chars × 8 px + padding */
#define DECOMP_BTN_X    (SPECGRAM_BTN_X + SPECGRAM_BTN_W + GRAPH_BTN_GAP)  /* = 156 */

/* Right group origin — derived from GRAPH_MODE_COUNT so that adding a graph
 * mode needs no arithmetic edit here or in the hit test.  Not usable in #if
 * (it expands to an enum constant), which we never need. */
#define GBAR_R_X  (SPEC_X + SPEC_W - (i32)GRAPH_MODE_COUNT * GRAPH_BTN_W \
                   - ((i32)GRAPH_MODE_COUNT - 1) * GRAPH_BTN_GAP - 2)

/* ── Scrolling spectrogram (waterfall) ──────────────────────────────────────
 * Ring buffer of amplitude rows behind the frequency plot: X = log frequency
 * (same LUT as the spectrum line), Y = recent time — the top row is "now" and
 * the image slides downward, oldest rows leaving through the plot bottom.
 *
 * SPECGRAM_ROWS and SPECGRAM_COLS are free knobs: everything else (cell size,
 * frequency-bucket mapping, texture size, upload buffers, row period) derives
 * from them, so they can be changed independently.  Each cell renders as a
 * block of (PLOT_H/ROWS × SPEC_W/COLS) logical px with NEAREST scaling.
 * Crispest results when SPECGRAM_ROWS divides PLOT_H — and PLOT_H/2 for Both
 * mode — evenly (divisors of 176: 88, 44, 22, 16, 11, …); otherwise blocks are
 * merely uneven by a pixel, never misaligned. */
#define SPECGRAM_ROWS      (PLOT_H / 2)   /* = 88 ring rows (2 logical px per row) */
#define SPECGRAM_COLS      (SPEC_W / 2)   /* columns (~2 logical px per col)  */
/* Seconds of history spanning the plot: one ring row per two rendered
 * frames, so the scroll rate is tied to TARGET_FPS. */
#define SPECGRAM_WINDOW_S  (2.0f * ((f32)SPECGRAM_ROWS / (f32)TARGET_FPS))
#define SPECGRAM_ROW_S     (SPECGRAM_WINDOW_S / (f32)SPECGRAM_ROWS)
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
/* The "Deconstruct" toggle's geometry lives with the other toolbar buttons
 * (DECOMP_BTN_X / DECOMP_BTN_W, above). */
/* Band waveform colours (mids reuse COL_WAVE green). */
#define COL_BASS    255, 120,   0, 255   /* warm orange — bass  */
#define COL_HIGHS     0, 180, 255, 255   /* cyan        — highs */

/* ── Instrument-stem decomposition (GRAPH_MODE_STEMS) ────────────────────────
 * Demucs v4 6-source separation, displayed as a 3×2 grid of time-domain plots
 * on one shared time axis so the panels are directly comparable.  Stem order is
 * fixed by the separation engine: drums, bass, other, vocals, guitar, piano. */
#define STEM_COUNT      6
#define STEM_COLS       3
#define STEM_ROWS       2
#define STEM_W          (SPEC_W / STEM_COLS)   /* 194 px per cell */
#define STEM_H          (PLOT_H / STEM_ROWS)   /*  88 px per cell */
/* One window for every stem (unlike the per-band windows above) — the point of
 * this view is cross-instrument comparison, which a shared axis makes possible. */
#define STEM_WINDOW_S   0.020f              /* 20 ms */
#define STEM_SMOOTH     WAVE_SMOOTH_COEFF
#define STEM_CB_SZ      8    /* mute checkbox, top-left interior of each cell */

/* Centred action button (Separate / Cancel / Retry) shown when stems are not
 * ready, with a one-line explanatory message beneath it. */
#define STEM_ACT_W      120
#define STEM_ACT_H      16
#define STEM_ACT_X      (SPEC_X + (SPEC_W - STEM_ACT_W) / 2)   /* = 271 */
#define STEM_ACT_Y      (PLOT_Y + PLOT_H / 2 - STEM_ACT_H)     /* = 116 */

/* Header progress widget, shown while a separation job runs. */
#define STEM_PROG_W     120
#define STEM_PROG_H      6

/* Chunks the engine separates in parallel.  Each one holds its own copy of the
 * model, so this trades memory for speed and cannot simply track core count —
 * too many and the engine dies part-way through. */
#define STEM_MAX_THREADS  4
/* Bytes of the engine's most recent output kept for failure messages. */
#define STEM_TAIL_BYTES   96
/* Upper bound on decode iterations, so streaming a file to WAV cannot spin if a
 * decoder never reports EOF (NASA Rule 2).  At 32 KB per chunk this allows just
 * over 3 hours of 44.1 kHz stereo 16-bit audio, well past any real track and
 * comfortably inside the 4 GB limit of the WAV size fields. */
#define STEM_DECODE_MAX_CHUNKS  60000
/* Cap on pipe-drain iterations per poll, so a misbehaving child cannot hold
 * the render loop (NASA Rule 2: every loop needs a fixed bound). */
#define STEM_DRAIN_MAX    64
#define STEMS_TAG       "stems"        /* MIX tag grouping the six tracks     */
#define STEMS_DIR       "stems"        /* subdirectory of the pref directory  */
#define STEMS_DONE_FILE "done.txt"
#define STEMS_INPUT_WAV "input.wav"
#define STEM_MODEL_FILE "ggml-model-htdemucs-6s-f16.bin"
/* Separation engine executable, shipped next to csplayer.  The multi-threaded
 * variant: it splits the track into STEM_MAX_THREADS chunks and separates them
 * in parallel, which is measurably faster than the single-threaded build. */
#ifdef _WIN32
#define STEM_ENGINE_EXE "demucs_mt.cpp.main.exe"
#else
#define STEM_ENGINE_EXE "demucs_mt.cpp.main"
#endif
/* Stems are cached as 44.1 kHz stereo 16-bit WAV: transparent for playback and
 * display, and half the size of the engine's float32 output. */
#define STEM_RATE       44100
#define STEM_CHANNELS   2
#define STEM_BITS       16

/* Per-stem waveform colours, indexed by stem.  Muted stems ignore these and
 * draw in flat dimmed grey so the off state is unmistakable. */
#define COL_STEM_DRUMS  255, 140,  40, 255   /* orange    */
#define COL_STEM_BASS   130, 110, 255, 255   /* indigo    */
#define COL_STEM_OTHER  170, 170, 195, 255   /* grey-blue */
#define COL_STEM_VOCALS 100, 200, 120, 255   /* green     */
#define COL_STEM_GUITAR 255, 205,  70, 255   /* amber     */
#define COL_STEM_PIANO    0, 180, 255, 255   /* cyan      */
#define COL_STEM_MUTED  105, 105, 118, 255   /* monotone grey, dimmed */

#endif /* CSPLAYER_TYPES_H */
