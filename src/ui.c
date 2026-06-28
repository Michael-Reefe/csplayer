/*
 * ui.c — SDL3 rendering and input handling.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "ui.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>

#ifdef CSPLAYER_TESTING
#include "test_mocks.h"
#endif

/* ── Layout helpers ──────────────────────────────────────────────────────── */

/* Volume slider geometry (between the 3 transport buttons and the seek bar). */
#define VOL_X    (4 + 3 * BTN_SZ + 2 * BTN_GAP + 4)   /* = 72 */
#define VOL_W    36
#define VOL_Y    (CTRL_Y + 3)
#define VOL_H    (CTRL_H - 6)

/* Playlist scrollbar (rightmost strip of the playlist panel). */
#define PL_SB_W     5
#define PL_SB_X     (LOG_W - PL_SB_W)              /* = 635 */

/* Delete button — sits immediately left of the scrollbar. */
#define PL_DEL_W    12
#define PL_DEL_X    (PL_SB_X - PL_DEL_W - 2)       /* = 621 */

/* Seek bar geometry (after the volume slider). */
#define SEEK_X   (VOL_X + VOL_W + 4)   /* = 112 */
#define SEEK_Y   (CTRL_Y + 2)
#define SEEK_W   (LOG_W - SEEK_X - 4)
#define SEEK_H   (CTRL_H - 4)

/* Font dimensions (SDL_RenderDebugText built-in 8 × 8 font). */
#define FONT_W  8
#define FONT_H  8

/* Mode toggle buttons — "Play" and "Synth" side by side in the header bar. */
#define MODE_BTN_X    78   /* 8 px gap after "csplayer" (ends at x=68) */
#define MODE_BTN_Y     2
#define MODE_BTN1_W   40   /* "Play"  = 4 chars × 8 px + 4 px padding each side */
#define MODE_BTN2_W   48   /* "Synth" = 5 chars × 8 px + 4 px padding each side */
#define MODE_BTN_GAP   2
#define MODE_BTN_H    16

/* ── Internal: colour helpers ────────────────────────────────────────────── */

static void set_col(SDL_Renderer *r, i32 rv, i32 gv, i32 bv, i32 av)
{
    assert(r != NULL);
    assert(rv >= 0 && gv >= 0 && bv >= 0 && av >= 0);
    (void)SDL_SetRenderDrawColor(r, (Uint8)rv, (Uint8)gv, (Uint8)bv, (Uint8)av);
}

/* Draw a filled rectangle. */
static void fill_rect(SDL_Renderer *r, f32 x, f32 y, f32 w, f32 h)
{
    assert(r != NULL);
    assert(w >= 0.0f && h >= 0.0f);
    SDL_FRect rc = { x, y, w, h };
    (void)SDL_RenderFillRect(r, &rc);
}

/* Draw a rectangle outline (1 px border). */
static void outline_rect(SDL_Renderer *r, f32 x, f32 y, f32 w, f32 h)
{
    assert(r != NULL);
    assert(w >= 0.0f && h >= 0.0f);
    SDL_FRect rc = { x, y, w, h };
    (void)SDL_RenderRect(r, &rc);
}

/* ── Internal: Unicode-aware text renderer ───────────────────────────────── */

/*
 * Render a UTF-8 string using the Unifont TTF font if available, otherwise
 * fall back to SDL_RenderDebugText (ASCII only, 8×8 built-in font).
 * The clip rect must be set by the caller if overflow protection is needed.
 */
static void render_utf8(SDL_Renderer *r, TTF_Font *font,
                         const char *text, f32 x, f32 y,
                         i32 rv, i32 gv, i32 bv, i32 av)
{
    assert(r    != NULL);
    assert(text != NULL);
    if (text[0] == '\0') { return; }
    if (font == NULL) {
        set_col(r, rv, gv, bv, av);
        (void)SDL_RenderDebugText(r, x, y, text);
        return;
    }
    SDL_Color col = { (Uint8)rv, (Uint8)gv, (Uint8)bv, (Uint8)av };
    SDL_Surface *surf = TTF_RenderText_Solid(font, text, (size_t)0, col);
    if (surf == NULL) { return; }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (tex == NULL) { return; }
    (void)SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    /* Nearest-neighbour: the texture was rasterised at DISPLAY_SCALE × logical
     * size; downscaling 1/DISPLAY_SCALE here keeps the pixels sharp after SDL's
     * integer upscale brings them back to physical-pixel resolution. */
    (void)SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    f32 tw = 0.0f, th = 0.0f;
    (void)SDL_GetTextureSize(tex, &tw, &th);
    SDL_FRect dst = { x, y,
                      tw / (f32)DISPLAY_SCALE,
                      th / (f32)DISPLAY_SCALE };
    (void)SDL_RenderTexture(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* ── Internal: axis tick tables (static, compile-time constants) ─────────── */

/* Frequency tick positions for the X axis. */
static const f32  freq_ticks[]  = { 20.f, 50.f, 100.f, 200.f, 500.f,
                                    1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
static const char *freq_labels[] = { "20", "50", "100", "200", "500",
                                     "1k", "2k", "5k", "10k", "20k" };
#define NUM_FREQ_TICKS  10

/* dB tick positions for the Y axis. */
static const f32  db_ticks[]   = { 0.0f, -30.0f, -60.0f, -90.0f };
static const char *db_labels[] = { "0", "-30", "-60", "-90" };
#define NUM_DB_TICKS  4

/* Amplitude axis labels for the time-domain Y axis. */
static const char *amp_labels[] = { "+1", "0", "-1" };
static const f32   amp_frac[]   = { 0.0f, 0.5f, 1.0f };
#define NUM_AMP_LABELS  3

/* ── Internal: pixel-position helpers ───────────────────────────────────── */

/*
 * Convert a frequency (Hz) to a pixel column within the spectrum plot.
 * Returns -1 if the frequency is outside [FREQ_MIN, FREQ_MAX].
 */
static i32 freq_to_px(f32 freq)
{
    assert(freq > 0.0f);
    assert(FREQ_MAX > FREQ_MIN);
    if (freq < FREQ_MIN || freq > FREQ_MAX) { return -1; }

    f32 lmin = log10f(FREQ_MIN);
    f32 lmax = log10f(FREQ_MAX);
    f32 t    = (log10f(freq) - lmin) / (lmax - lmin);
    return SPEC_X + (i32)(t * (f32)(SPEC_W - 1));
}

/*
 * Map a dB value to a pixel row within an arbitrary vertical rect.
 * Replaces the old fixed-rect db_to_py; call with (SPEC_Y, SPEC_H) for the
 * full-height case or (SPEC_Y, SPEC_H/2) for the upper-half split.
 */
static i32 db_to_py_r(f32 db, i32 plot_y, i32 plot_h)
{
    assert(DB_TOP > DB_BOT);
    assert(plot_h > 0);
    f32 t = (DB_TOP - db) / (DB_TOP - DB_BOT);
    if (t < 0.0f) { t = 0.0f; }
    if (t > 1.0f) { t = 1.0f; }
    return plot_y + (i32)(t * (f32)plot_h);
}

/* ── Internal draw sub-functions ─────────────────────────────────────────── */

/* Return the logical-pixel width of text rendered with font (ASCII fallback if NULL). */
static f32 text_pixel_w(TTF_Font *font, const char *text)
{
    assert(text != NULL);
    assert(FONT_W > 0);
    if (text[0] == '\0') { return 0.0f; }
    if (font == NULL)    { return (f32)((i32)SDL_strlen(text) * FONT_W); }
    SDL_Color dummy = { 0, 0, 0, 0 };
    SDL_Surface *surf = TTF_RenderText_Solid(font, text, (size_t)0, dummy);
    if (surf == NULL)    { return (f32)((i32)SDL_strlen(text) * FONT_W); }
    f32 w = (f32)surf->w / (f32)DISPLAY_SCALE;
    SDL_DestroySurface(surf);
    return w;
}

/* Marquee scroll speed (logical pixels/second) and pause at each end (ms). */
#define MARQUEE_SPEED_PPS  25.0f
#define MARQUEE_PAUSE_MS   2500u

/*
 * Update header marquee state for the current frame.
 * track    — pl->current (used to detect track changes).
 * overflow — how many pixels the full text exceeds the clip width; <= 0 = no scroll.
 */
static void update_marquee(ui_t *ui, i32 track, f32 overflow)
{
    assert(ui != NULL);
    assert(MARQUEE_SPEED_PPS > 0.0f);

    /* Reset on track change or when text fits. */
    if (track != ui->marquee_track || overflow <= 0.0f) {
        ui->marquee_x           = 0.0f;
        ui->marquee_dir         = 0.0f;
        ui->marquee_pause_until = 0;
        ui->marquee_last_ms     = 0;
        ui->marquee_track       = track;
    }
    if (overflow <= 0.0f) { return; }

    Uint64 now = SDL_GetTicks();

    /* First frame with overflow — seed state and start initial pause. */
    if (ui->marquee_last_ms == 0) {
        ui->marquee_last_ms     = now;
        ui->marquee_dir         = 1.0f;
        ui->marquee_pause_until = now + MARQUEE_PAUSE_MS;
        return;
    }

    /* Still in a pause at one end — just update the clock, don't move. */
    if (now < ui->marquee_pause_until) {
        ui->marquee_last_ms = now;
        return;
    }

    f32 dt = (f32)(now - ui->marquee_last_ms) / 1000.0f;
    ui->marquee_last_ms = now;
    if (dt > 0.1f) { dt = 0.1f; }   /* cap to avoid jump after pause/sleep */

    ui->marquee_x += ui->marquee_dir * MARQUEE_SPEED_PPS * dt;

    if (ui->marquee_dir > 0.0f && ui->marquee_x >= overflow) {
        ui->marquee_x           = overflow;
        ui->marquee_dir         = -1.0f;
        ui->marquee_pause_until = now + MARQUEE_PAUSE_MS;
    } else if (ui->marquee_dir < 0.0f && ui->marquee_x <= 0.0f) {
        ui->marquee_x           = 0.0f;
        ui->marquee_dir         = 1.0f;
        ui->marquee_pause_until = now + MARQUEE_PAUSE_MS;
    }
}

/* ── Internal: header title texture cache (Rule 3: no per-frame TTF alloc) ── */

/* Build a TTF texture from a UTF-8 string; NULL on empty/font=NULL/error. */
static SDL_Texture *build_text_tex(SDL_Renderer *r, TTF_Font *font,
                                    const char *text,
                                    i32 rv, i32 gv, i32 bv, i32 av)
{
    assert(r    != NULL);
    assert(text != NULL);
    if (text[0] == '\0' || font == NULL) { return NULL; }
    SDL_Color col = { (Uint8)rv, (Uint8)gv, (Uint8)bv, (Uint8)av };
    SDL_Surface *surf = TTF_RenderText_Solid(font, text, (size_t)0, col);
    if (surf == NULL) { return NULL; }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (tex == NULL) { return NULL; }
    (void)SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    (void)SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return tex;
}

/* Return the logical-pixel width of a pre-built texture (0.0 if NULL). */
static f32 tex_logical_w(SDL_Texture *tex)
{
    assert(tex != NULL);
    assert(DISPLAY_SCALE > 0);
    f32 w = 0.0f;
    (void)SDL_GetTextureSize(tex, &w, NULL);
    return w / (f32)DISPLAY_SCALE;
}

/* Render a pre-built texture at logical coordinates. */
static void render_tex_at(SDL_Renderer *r, SDL_Texture *tex, f32 x, f32 y)
{
    assert(r   != NULL);
    assert(tex != NULL);
    f32 tw = 0.0f, th = 0.0f;
    (void)SDL_GetTextureSize(tex, &tw, &th);
    SDL_FRect dst = { x, y, tw / (f32)DISPLAY_SCALE, th / (f32)DISPLAY_SCALE };
    (void)SDL_RenderTexture(r, tex, NULL, &dst);
}

/*
 * Rebuild cached title/artist textures when the displayed text changes.
 * Called at most once per track change, never in the steady-state render loop.
 */
static void rebuild_hdr_tex_cache(SDL_Renderer *r, ui_t *ui,
                                   const char *title, const char *artist)
{
    assert(r      != NULL);
    assert(ui     != NULL);
    assert(title  != NULL);
    assert(artist != NULL);
    if (ui->hdr_title_tex  != NULL) { SDL_DestroyTexture(ui->hdr_title_tex);  }
    if (ui->hdr_artist_tex != NULL) { SDL_DestroyTexture(ui->hdr_artist_tex); }

    ui->hdr_title_tex = build_text_tex(r, ui->font, title, COL_TEXT);
    ui->hdr_title_w   = (ui->hdr_title_tex != NULL)
                            ? tex_logical_w(ui->hdr_title_tex)
                            : text_pixel_w(ui->font, title);
    (void)SDL_strlcpy(ui->hdr_cache_title, title, sizeof(ui->hdr_cache_title));

    ui->hdr_artist_tex     = NULL;
    ui->hdr_artist_w       = 0.0f;
    ui->hdr_cache_artist[0] = '\0';
    if (artist[0] != '\0') {
        char sep[TITLE_MAX_LEN + 4];
        (void)SDL_snprintf(sep, sizeof(sep), " - %s", artist);
        ui->hdr_artist_tex = build_text_tex(r, ui->font, sep, COL_DIM);
        ui->hdr_artist_w   = (ui->hdr_artist_tex != NULL)
                                 ? tex_logical_w(ui->hdr_artist_tex)
                                 : text_pixel_w(ui->font, sep);
        (void)SDL_strlcpy(ui->hdr_cache_artist, artist, sizeof(ui->hdr_cache_artist));
    }
}

/*
 * Render the current track's title and artist into the header clip area.
 * Uses cached textures when a TTF font is loaded (Rule 3: no per-frame alloc).
 */
static void draw_header_title(SDL_Renderer *r, const playlist_t *pl,
                               ui_t *ui, i32 title_x, i32 clip_w)
{
    assert(r  != NULL);
    assert(pl != NULL);
    assert(ui != NULL);

    const char *title  = pl->entries[pl->current].title;
    const char *artist = pl->entries[pl->current].artist;

    /* Rebuild only when text changes — not every frame. */
    if (ui->font != NULL &&
        (ui->hdr_title_tex == NULL ||
         SDL_strcmp(title,  ui->hdr_cache_title)  != 0 ||
         SDL_strcmp(artist, ui->hdr_cache_artist) != 0)) {
        rebuild_hdr_tex_cache(r, ui, title, artist);
    }

    f32 title_w, total_w;
    if (ui->font != NULL) {
        title_w = ui->hdr_title_w;
        total_w = title_w + ui->hdr_artist_w;
    } else {
        title_w = text_pixel_w(NULL, title);
        total_w = title_w;
        if (artist[0] != '\0') {
            char sep[TITLE_MAX_LEN + 4];
            SDL_snprintf(sep, sizeof(sep), " - %s", artist);
            total_w += text_pixel_w(NULL, sep);
        }
    }
    update_marquee(ui, pl->current, total_w - (f32)clip_w);

    SDL_Rect clip = { title_x, HDR_Y, clip_w, HDR_H };
    (void)SDL_SetRenderClipRect(r, &clip);
    f32 rx = (f32)title_x - ui->marquee_x;

    if (ui->font != NULL) {
        if (ui->hdr_title_tex  != NULL) {
            render_tex_at(r, ui->hdr_title_tex, rx, (f32)(HDR_Y + 6));
        }
        if (ui->hdr_artist_tex != NULL) {
            render_tex_at(r, ui->hdr_artist_tex, rx + title_w, (f32)(HDR_Y + 6));
        }
    } else {
        set_col(r, COL_TEXT);
        (void)SDL_RenderDebugText(r, rx, (f32)(HDR_Y + 6), title);
        if (artist[0] != '\0') {
            char sep[TITLE_MAX_LEN + 4];
            SDL_snprintf(sep, sizeof(sep), " - %s", artist);
            set_col(r, COL_DIM);
            (void)SDL_RenderDebugText(r, rx + title_w, (f32)(HDR_Y + 6), sep);
        }
    }
    (void)SDL_SetRenderClipRect(r, NULL);
}

/*
 * Draw the two "Play" | "Synth" mode-toggle buttons in the header bar; the
 * button matching the active mode is drawn hot.
 */
static void draw_mode_buttons(SDL_Renderer *r, app_mode_t mode)
{
    assert(r != NULL);

    static const char *const mode_labels[2] = { "Play", "Synth" };
    static const i32 mode_bw[2] = { MODE_BTN1_W, MODE_BTN2_W };
    i32 bx = MODE_BTN_X;
    for (i32 b = 0; b < 2; b++) {
        bool active = (b == 0) ? (mode == APP_MODE_PLAYER) : (mode == APP_MODE_SYNTH);
        if (active) { set_col(r, COL_BTN_HOT); } else { set_col(r, COL_BTN); }
        fill_rect(r, (f32)bx, (f32)MODE_BTN_Y, (f32)mode_bw[b], (f32)MODE_BTN_H);
        set_col(r, COL_BORDER);
        outline_rect(r, (f32)bx, (f32)MODE_BTN_Y, (f32)mode_bw[b], (f32)MODE_BTN_H);
        i32 llen = (i32)SDL_strlen(mode_labels[b]);
        f32 mlx  = (f32)(bx + (mode_bw[b] - llen * FONT_W) / 2);
        f32 mly  = (f32)(MODE_BTN_Y + (MODE_BTN_H - FONT_H) / 2);
        set_col(r, COL_TEXT);
        (void)SDL_RenderDebugText(r, mlx, mly, mode_labels[b]);
        bx += mode_bw[b] + MODE_BTN_GAP;
    }
}

/*
 * Draw the header bar: dark background, app name, current track title
 * and elapsed / total time.
 */
static void draw_header(SDL_Renderer *r,
                         const audio_t *audio, const playlist_t *pl,
                         ui_t *ui, app_mode_t mode)
{
    assert(r     != NULL);
    assert(audio != NULL);
    assert(pl    != NULL);
    assert(ui    != NULL);

    /* Background */
    set_col(r, COL_HEADER);
    fill_rect(r, 0.0f, (f32)HDR_Y, (f32)LOG_W, (f32)HDR_H);

    /* App name (ASCII — no TTF needed). */
    set_col(r, COL_NOW);
    (void)SDL_RenderDebugText(r, 4.0f, (f32)(HDR_Y + 6), CSPLAYER_APP_NAME);

    /* Mode toggle buttons: "Play" | "Synth" */
    draw_mode_buttons(r, mode);

    /* Compute time string first so we know its pixel width. */
    f32 pos_frac = audio_get_pos(audio);
    f64 dur_sec  = (audio->duration_frames > 0 && audio->sample_rate > 0.0f)
                       ? (f64)audio->duration_frames / (f64)audio->sample_rate
                       : 0.0;
    f64 elapsed  = pos_frac * dur_sec;
    char timebuf[24];
    SDL_snprintf(timebuf, sizeof(timebuf), "%d:%02d / %d:%02d",
                 (i32)(elapsed / 60.0), (i32)elapsed % 60,
                 (i32)(dur_sec / 60.0), (i32)dur_sec % 60);
    i32 tlen = (i32)SDL_strlen(timebuf);
    f32 tx   = (f32)(LOG_W - tlen * FONT_W - 4);

    /* Track title — player mode only, clip between mode button and time display. */
    if (mode == APP_MODE_PLAYER && pl->current >= 0 && pl->current < pl->count) {
        i32 title_x = MODE_BTN_X + MODE_BTN1_W + MODE_BTN_GAP + MODE_BTN2_W + 8;
        i32 clip_w  = (i32)tx - title_x - 4;
        if (clip_w > 0) { draw_header_title(r, pl, ui, title_x, clip_w); }
    }

    /* Elapsed / total time (right-aligned, ASCII). */
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, tx, (f32)(HDR_Y + 6), timebuf);
}

/* dB grid labels for the freq subplot; draws full-width lines at top/bottom. */
static void draw_freq_db_labels(SDL_Renderer *r, i32 plot_y, i32 plot_h,
                                 bool at_bottom)
{
    assert(r != NULL);
    assert(plot_h > 0);

    for (i32 t = 0; t < NUM_DB_TICKS; t++) {
        i32 py = db_to_py_r(db_ticks[t], plot_y, plot_h);

        if (t == 0 || t == NUM_DB_TICKS - 1) {
            set_col(r, COL_AXIS);
            (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)py,
                                    (f32)(SPEC_X + SPEC_W), (f32)py);
        }

        i32 llen = (i32)SDL_strlen(db_labels[t]);
        f32 lx   = (f32)(SPEC_X - llen * FONT_W - 2);
        f32 ly   = (f32)(py - FONT_H / 2);
        /* In half-height mode the bottom label sits on the split line — push above. */
        if (!at_bottom && t == NUM_DB_TICKS - 1) { ly = (f32)(py - FONT_H); }
        if (lx < 0.0f) { lx = 0.0f; }
        set_col(r, COL_TICK);
        (void)SDL_RenderDebugText(r, lx, ly, db_labels[t]);
    }
}

/* Frequency tick marks (always) + external labels (only at panel bottom). */
static void draw_freq_ticks(SDL_Renderer *r, i32 plot_y, i32 plot_h,
                             bool at_bottom)
{
    assert(r != NULL);
    assert(plot_h > 0);
    assert(NUM_FREQ_TICKS > 0);

    for (i32 t = 0; t < NUM_FREQ_TICKS; t++) {
        i32 px = freq_to_px(freq_ticks[t]);
        if (px < 0) { continue; }

        set_col(r, COL_AXIS);
        (void)SDL_RenderLine(r, (f32)px, (f32)(plot_y + plot_h - 3),
                                (f32)px, (f32)(plot_y + plot_h));

        if (at_bottom) {
            i32 llen = (i32)SDL_strlen(freq_labels[t]);
            f32 lx   = (f32)(px - llen * FONT_W / 2);
            set_col(r, COL_TICK);
            (void)SDL_RenderDebugText(r, lx, (f32)(plot_y + plot_h + 4), freq_labels[t]);
        }
    }
}

/*
 * Draw the frequency-domain subplot: panel bg, axis lines, dB labels,
 * Y-axis and X-axis titles, and (only when at the full bottom of the panel)
 * external frequency tick labels below the plot.
 */
static void draw_freq_axes(SDL_Renderer *r, i32 plot_y, i32 plot_h)
{
    assert(r != NULL);
    assert(plot_h > 0);

    bool at_bottom = (plot_y + plot_h == SPEC_Y + SPEC_H);

    /* Panel background */
    set_col(r, COL_PANEL);
    fill_rect(r, (f32)SPEC_X, (f32)plot_y, (f32)SPEC_W, (f32)plot_h);

    /* Y-axis title "dB" at top-left interior */
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, (f32)(SPEC_X + 2), (f32)(plot_y + 2), "dB");

    /* X-axis title at bottom-right interior */
    {
        const char *xlabel = "Freq (Hz)";
        i32 xlen = (i32)SDL_strlen(xlabel);
        set_col(r, COL_DIM);
        (void)SDL_RenderDebugText(r, (f32)(SPEC_X + SPEC_W - xlen * FONT_W - 2),
                                     (f32)(plot_y + plot_h - FONT_H - 2), xlabel);
    }

    /* Left Y-axis, bottom X-axis, right border */
    set_col(r, COL_AXIS);
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)plot_y,
                            (f32)SPEC_X, (f32)(plot_y + plot_h));
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)(plot_y + plot_h),
                            (f32)(SPEC_X + SPEC_W), (f32)(plot_y + plot_h));
    (void)SDL_RenderLine(r, (f32)(SPEC_X + SPEC_W), (f32)plot_y,
                            (f32)(SPEC_X + SPEC_W), (f32)(plot_y + plot_h));

    draw_freq_db_labels(r, plot_y, plot_h, at_bottom);
    draw_freq_ticks(r, plot_y, plot_h, at_bottom);
}

/*
 * Draw the frequency-domain spectrum line (blue) from precomputed pts[].
 */
static void draw_spectrum_line(SDL_Renderer *r, const spectrum_t *spectrum)
{
    assert(r        != NULL);
    assert(spectrum != NULL);

    set_col(r, COL_SPEC);
    (void)SDL_RenderLines(r, spectrum->pts, SPEC_W);
}

/*
 * Draw the time-domain waveform line (green) from precomputed time_pts[].
 */
static void draw_time_line(SDL_Renderer *r, const spectrum_t *spectrum)
{
    assert(r        != NULL);
    assert(spectrum != NULL);

    set_col(r, COL_WAVE);
    (void)SDL_RenderLines(r, spectrum->time_pts, SPEC_W);
}

/*
 * Draw the amplitude axis labels +1 / 0 / -1 to the left of a time-domain plot.
 * Shared by draw_time_axes and draw_band_axes.  In half-height (split) mode the
 * top label sits on the split line, so it is pushed down to the plot top.
 */
static void draw_amp_labels(SDL_Renderer *r, i32 plot_y, i32 plot_h)
{
    assert(r != NULL);
    assert(plot_h > 0);

    for (i32 t = 0; t < NUM_AMP_LABELS; t++) {
        i32 llen = (i32)SDL_strlen(amp_labels[t]);
        f32 lx   = (f32)(SPEC_X - llen * FONT_W - 2);
        f32 ly   = (f32)(plot_y + (i32)(amp_frac[t] * (f32)plot_h) - FONT_H / 2);
        if (t == 0 && plot_y != SPEC_Y) { ly = (f32)plot_y; }
        if (lx < 0.0f) { lx = 0.0f; }
        set_col(r, COL_TICK);
        (void)SDL_RenderDebugText(r, lx, ly, amp_labels[t]);
    }
}

/* Time tick marks every 2 ms; labels on every-4-ms ticks only at panel bottom. */
static void draw_time_ticks(SDL_Renderer *r, i32 plot_y, i32 plot_h,
                             bool at_bottom)
{
    assert(r != NULL);
    assert(plot_h > 0);

    for (i32 t = 0; t <= 10; t++) {
        f32 frac = (f32)t / 10.0f;
        i32 px   = SPEC_X + (i32)(frac * (f32)(SPEC_W - 1));

        set_col(r, COL_AXIS);
        (void)SDL_RenderLine(r, (f32)px, (f32)(plot_y + plot_h - 3),
                                (f32)px, (f32)(plot_y + plot_h));

        if (at_bottom && (t % 2 == 0)) {
            char lbuf[4];
            (void)SDL_snprintf(lbuf, sizeof(lbuf), "%d", t * 2);
            i32 llen = (i32)SDL_strlen(lbuf);
            f32 lx   = (f32)(px - llen * FONT_W / 2);
            set_col(r, COL_TICK);
            (void)SDL_RenderDebugText(r, lx, (f32)(plot_y + plot_h + 4), lbuf);
        }
    }
}

/*
 * Draw the time-domain subplot: panel bg, axis lines, amplitude labels,
 * zero midline, and (only when at the full bottom of the panel) external
 * time tick labels below the plot.
 */
static void draw_time_axes(SDL_Renderer *r, i32 plot_y, i32 plot_h)
{
    assert(r != NULL);
    assert(plot_h > 0);

    bool at_bottom = (plot_y + plot_h == SPEC_Y + SPEC_H);

    /* Panel background */
    set_col(r, COL_PANEL);
    fill_rect(r, (f32)SPEC_X, (f32)plot_y, (f32)SPEC_W, (f32)plot_h);

    /* Y-axis title "Amp" at top-left interior */
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, (f32)(SPEC_X + 2), (f32)(plot_y + 2), "Amp");

    /* X-axis title at bottom-right interior */
    {
        const char *xlabel = "Time (ms)";
        i32 xlen = (i32)SDL_strlen(xlabel);
        (void)SDL_RenderDebugText(r, (f32)(SPEC_X + SPEC_W - xlen * FONT_W - 2),
                                     (f32)(plot_y + plot_h - FONT_H - 2), xlabel);
    }

    /* Top border, left Y-axis, bottom X-axis, right border */
    set_col(r, COL_AXIS);
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)plot_y,
                            (f32)(SPEC_X + SPEC_W), (f32)plot_y);
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)plot_y,
                            (f32)SPEC_X, (f32)(plot_y + plot_h));
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)(plot_y + plot_h),
                            (f32)(SPEC_X + SPEC_W), (f32)(plot_y + plot_h));
    (void)SDL_RenderLine(r, (f32)(SPEC_X + SPEC_W), (f32)plot_y,
                            (f32)(SPEC_X + SPEC_W), (f32)(plot_y + plot_h));

    /* Horizontal zero-amplitude midline */
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)(plot_y + plot_h / 2),
                            (f32)(SPEC_X + SPEC_W), (f32)(plot_y + plot_h / 2));

    draw_amp_labels(r, plot_y, plot_h);
    draw_time_ticks(r, plot_y, plot_h, at_bottom);
}

/*
 * Draw the three graph-mode toggle buttons in the top-right interior of the
 * spectrum panel.  The button matching ui->graph_mode is drawn hot.
 */
static void draw_graph_buttons(SDL_Renderer *r, const ui_t *ui)
{
    assert(r  != NULL);
    assert(ui != NULL);
    assert(GRAPH_BTN_W > 0 && GRAPH_BTN_H > 0);

    static const char *const labels[3] = { "Spec", "Wave", "Both" };
    i32 x0 = SPEC_X + SPEC_W - 3 * GRAPH_BTN_W - 2 * GRAPH_BTN_GAP - 2;
    i32 by = SPEC_Y + 2;

    for (i32 b = 0; b < (i32)GRAPH_MODE_COUNT; b++) {
        i32 bx     = x0 + b * (GRAPH_BTN_W + GRAPH_BTN_GAP);
        bool active = ((graph_mode_t)b == ui->graph_mode);

        if (active) { set_col(r, COL_BTN_HOT); } else { set_col(r, COL_BTN); }
        fill_rect(r, (f32)bx, (f32)by, (f32)GRAPH_BTN_W, (f32)GRAPH_BTN_H);
        set_col(r, COL_BORDER);
        outline_rect(r, (f32)bx, (f32)by, (f32)GRAPH_BTN_W, (f32)GRAPH_BTN_H);

        i32 llen = (i32)SDL_strlen(labels[b]);
        f32 lx   = (f32)(bx + (GRAPH_BTN_W - llen * FONT_W) / 2);
        f32 ly   = (f32)(by + (GRAPH_BTN_H - FONT_H) / 2);
        set_col(r, COL_TEXT);
        (void)SDL_RenderDebugText(r, lx, ly, labels[b]);
    }

    /* "Deconstruct" toggle: bottom-left of the panel, only when time-domain is visible. */
    if (ui->graph_mode != GRAPH_MODE_FREQ) {
        if (ui->wave_decompose) { set_col(r, COL_BTN_HOT); } else { set_col(r, COL_BTN); }
        fill_rect(r, (f32)DECOMP_BTN_X, (f32)DECOMP_BTN_Y,
                     (f32)DECOMP_BTN_W,  (f32)GRAPH_BTN_H);
        set_col(r, COL_BORDER);
        outline_rect(r, (f32)DECOMP_BTN_X, (f32)DECOMP_BTN_Y,
                        (f32)DECOMP_BTN_W,  (f32)GRAPH_BTN_H);
        f32 blx = (f32)(DECOMP_BTN_X + (DECOMP_BTN_W - 11 * FONT_W) / 2);
        f32 bly = (f32)(DECOMP_BTN_Y + (GRAPH_BTN_H - FONT_H) / 2);
        set_col(r, COL_TEXT);
        (void)SDL_RenderDebugText(r, blx, bly, "Deconstruct");
    }
}

/*
 * Draw one band sub-panel (index b in 0..2): left divider, zero midline, band
 * label, and time tick marks (with external labels when at_bottom).
 */
static void draw_band_panel(SDL_Renderer *r, i32 b, i32 plot_y, i32 plot_h,
                             bool at_bottom)
{
    assert(r != NULL);
    assert(b >= 0 && b < 3);
    assert(plot_h > 0);

    static const char *const band_labels[3]    = { "Bass", "Mids", "High" };
    static const i32         band_window_ms[3] = { 50, 10, 5 };
    i32 bx = SPEC_X + b * BAND_W;

    /* Left border / divider for this sub-panel. */
    set_col(r, COL_AXIS);
    (void)SDL_RenderLine(r, (f32)bx, (f32)plot_y,
                            (f32)bx, (f32)(plot_y + plot_h));

    /* Horizontal zero-amplitude midline. */
    (void)SDL_RenderLine(r, (f32)bx,            (f32)(plot_y + plot_h / 2),
                            (f32)(bx + BAND_W), (f32)(plot_y + plot_h / 2));

    /* Band label centred at the top of the sub-panel. */
    i32 llen = (i32)SDL_strlen(band_labels[b]);
    f32 lx   = (f32)(bx + (BAND_W - llen * FONT_W) / 2);
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, lx, (f32)(plot_y + 2), band_labels[b]);

    /* Time tick marks at 0, 10, 20 ms; labels below the panel when at_bottom.
     * At internal dividers the "20" of one panel and the "0" of the next share
     * the same x — right-justify "20" and left-justify "0" to keep them clear. */
    for (i32 t = 0; t <= 2; t++) {
        i32 px = bx + t * (BAND_W - 1) / 2;
        set_col(r, COL_AXIS);
        (void)SDL_RenderLine(r, (f32)px, (f32)(plot_y + plot_h - 3),
                                (f32)px, (f32)(plot_y + plot_h));
        if (at_bottom) {
            char lbuf[4];
            (void)SDL_snprintf(lbuf, sizeof(lbuf), "%d", t * band_window_ms[b] / 2);
            i32 llen2 = (i32)SDL_strlen(lbuf);
            f32 lx2;
            if      (t == 2 && b < 2) { lx2 = (f32)(px - llen2 * FONT_W - 2); }
            else if (t == 0 && b > 0) { lx2 = (f32)(px + 2); }
            else                      { lx2 = (f32)(px - llen2 * FONT_W / 2); }
            set_col(r, COL_TICK);
            (void)SDL_RenderDebugText(r, lx2, (f32)(plot_y + plot_h + 4), lbuf);
        }
    }
}

/*
 * Draw axes for three side-by-side band sub-panels within the time-domain rect.
 * Each BAND_W-wide panel has a left border, horizontal midline, and a label.
 */
static void draw_band_axes(SDL_Renderer *r, i32 plot_y, i32 plot_h)
{
    assert(r      != NULL);
    assert(plot_h >  0);
    assert(BAND_W >  0);

    bool at_bottom = (plot_y + plot_h == SPEC_Y + SPEC_H);

    set_col(r, COL_PANEL);
    fill_rect(r, (f32)SPEC_X, (f32)plot_y, (f32)SPEC_W, (f32)plot_h);

    /* Top and bottom borders spanning the full panel width. */
    set_col(r, COL_AXIS);
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)plot_y,
                            (f32)(SPEC_X + SPEC_W), (f32)plot_y);
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)(plot_y + plot_h),
                            (f32)(SPEC_X + SPEC_W), (f32)(plot_y + plot_h));

    /* Amplitude labels: +1 (top), 0 (middle), -1 (bottom) — left of first panel. */
    draw_amp_labels(r, plot_y, plot_h);

    for (i32 b = 0; b < 3; b++) {
        draw_band_panel(r, b, plot_y, plot_h, at_bottom);
    }

    /* Right border. */
    set_col(r, COL_AXIS);
    (void)SDL_RenderLine(r, (f32)(SPEC_X + SPEC_W), (f32)plot_y,
                            (f32)(SPEC_X + SPEC_W), (f32)(plot_y + plot_h));
}

/*
 * Draw the three band waveform lines (orange / green / cyan).
 */
static void draw_band_lines(SDL_Renderer *r, const spectrum_t *spectrum)
{
    assert(r        != NULL);
    assert(spectrum != NULL);
    assert(BAND_W   >  0);

    set_col(r, COL_BASS);
    (void)SDL_RenderLines(r, spectrum->bass_pts, BAND_W);
    set_col(r, COL_WAVE);
    (void)SDL_RenderLines(r, spectrum->mid_pts,  BAND_W);
    set_col(r, COL_HIGHS);
    (void)SDL_RenderLines(r, spectrum->high_pts, BAND_W);
}

/*
 * Top-level graph panel dispatcher.  Draws the correct subplot layout based
 * on ui->graph_mode, then overlays the mode toggle buttons.
 */
static void draw_graph(SDL_Renderer *r, const spectrum_t *spectrum,
                        const ui_t *ui)
{
    assert(r        != NULL);
    assert(spectrum != NULL);
    assert(ui       != NULL);
    assert(SPEC_H   >  0);

    switch (ui->graph_mode) {
    case GRAPH_MODE_FREQ:
        draw_freq_axes(r, SPEC_Y, SPEC_H);
        draw_spectrum_line(r, spectrum);
        break;
    case GRAPH_MODE_TIME:
        if (ui->wave_decompose) {
            draw_band_axes(r, SPEC_Y, SPEC_H);
            draw_band_lines(r, spectrum);
        } else {
            draw_time_axes(r, SPEC_Y, SPEC_H);
            draw_time_line(r, spectrum);
        }
        break;
    case GRAPH_MODE_BOTH: {
        i32 half = SPEC_H / 2;
        draw_freq_axes(r, SPEC_Y, half);
        draw_spectrum_line(r, spectrum);
        if (ui->wave_decompose) {
            draw_band_axes(r, SPEC_Y + half, half);
            draw_band_lines(r, spectrum);
        } else {
            draw_time_axes(r, SPEC_Y + half, half);
            draw_time_line(r, spectrum);
        }
        break;
    }
    case GRAPH_MODE_COUNT:
        break;
    }

    draw_graph_buttons(r, ui);
}

/*
 * Format mm:ss from a total-seconds float.  buf must be at least 8 bytes.
 */
static void fmt_time(char *buf, size_t bufsz, f64 sec)
{
    assert(buf   != NULL);
    assert(bufsz >= 8);
    i32 m = (i32)(sec / 60.0);
    i32 s = (i32)sec % 60;
    (void)SDL_snprintf(buf, bufsz, "%d:%02d", m, s);
}

/*
 * Draw a single pixel-art button with a label centred in its box.
 */
static void draw_button(SDL_Renderer *r, i32 bx, i32 by,
                         bool hot, const char *label)
{
    assert(r     != NULL);
    assert(label != NULL);

    /* Fill — cannot use ternary here because COL_* macros expand to
     * comma-separated arguments, not a single value. */
    if (hot) { set_col(r, COL_BTN_HOT); } else { set_col(r, COL_BTN); }
    fill_rect(r, (f32)bx, (f32)by, (f32)BTN_SZ, (f32)BTN_SZ);

    /* Border */
    set_col(r, COL_BORDER);
    outline_rect(r, (f32)bx, (f32)by, (f32)BTN_SZ, (f32)BTN_SZ);

    /* Label */
    i32 llen = (i32)SDL_strlen(label);
    f32 lx   = (f32)(bx + (BTN_SZ - llen * FONT_W) / 2);
    f32 ly   = (f32)(by + (BTN_SZ - FONT_H) / 2);
    set_col(r, COL_TEXT);
    (void)SDL_RenderDebugText(r, lx, ly, label);
}

/*
 * Draw the control bar: rewind, play/pause, next buttons + seek bar.
 */
static void draw_controls(SDL_Renderer *r, const audio_t *audio, const ui_t *ui)
{
    assert(r     != NULL);
    assert(audio != NULL);
    assert(ui    != NULL);

    /* Bar background */
    set_col(r, COL_PANEL);
    fill_rect(r, 0.0f, (f32)CTRL_Y, (f32)LOG_W, (f32)CTRL_H);

    /* Top border */
    set_col(r, COL_BORDER);
    (void)SDL_RenderLine(r, 0.0f, (f32)CTRL_Y, (f32)LOG_W, (f32)CTRL_Y);

    /* Buttons: [<<] [▶/‖] [>>] */
    i32 bx = 4;
    i32 by = CTRL_Y + 2;
    draw_button(r, bx, by, false, "<<");
    bx += BTN_SZ + BTN_GAP;

    const char *play_label = (audio->status == AUDIO_STATUS_PLAYING)
                                 ? "||" : "> ";
    draw_button(r, bx, by, audio->status != AUDIO_STATUS_STOPPED, play_label);
    bx += BTN_SZ + BTN_GAP;

    draw_button(r, bx, by, false, ">>");

    /* Volume slider (logarithmic: slider pos maps linearly to dB). */
    set_col(r, COL_SCRUB_BG);
    fill_rect(r, (f32)VOL_X, (f32)VOL_Y, (f32)VOL_W, (f32)VOL_H);

    f32 vfill = ui->vol_pos * (f32)VOL_W;
    if (vfill < 0.0f)       { vfill = 0.0f; }
    if (vfill > (f32)VOL_W) { vfill = (f32)VOL_W; }
    set_col(r, COL_VOL_FG);
    fill_rect(r, (f32)VOL_X, (f32)VOL_Y, vfill, (f32)VOL_H);

    set_col(r, COL_BORDER);
    outline_rect(r, (f32)VOL_X, (f32)VOL_Y, (f32)VOL_W, (f32)VOL_H);

    /* Seek bar */
    set_col(r, COL_SCRUB_BG);
    fill_rect(r, (f32)SEEK_X, (f32)SEEK_Y, (f32)SEEK_W, (f32)SEEK_H);

    f32 pos  = ui->scrubbing ? ui->scrub_pos : audio_get_pos(audio);
    f32 fill = pos * (f32)SEEK_W;
    if (fill < 0.0f)          { fill = 0.0f; }
    if (fill > (f32)SEEK_W)   { fill = (f32)SEEK_W; }

    set_col(r, COL_SCRUB_FG);
    fill_rect(r, (f32)SEEK_X, (f32)SEEK_Y, fill, (f32)SEEK_H);

    /* Seek bar border */
    set_col(r, COL_BORDER);
    outline_rect(r, (f32)SEEK_X, (f32)SEEK_Y, (f32)SEEK_W, (f32)SEEK_H);
}

static void draw_pl_row_text(SDL_Renderer *r, const playlist_t *pl,
                              const ui_t *ui, i32 idx, i32 x_title, i32 ry, i32 ty)
{
    assert(r  != NULL);
    assert(pl != NULL);
    assert(ui != NULL);

    char tbuf[12];
    i32  tlen = 0;
    if (pl->entries[idx].duration_sec > 0.0) {
        fmt_time(tbuf, sizeof(tbuf), pl->entries[idx].duration_sec);
        tlen = (i32)SDL_strlen(tbuf);
    }

    i32 clip_right = (tlen > 0) ? PL_DEL_X - tlen * FONT_W - 8 : PL_DEL_X - 4;
    i32 clip_w     = clip_right - x_title;
    if (clip_w > 0) {
        SDL_Rect title_clip = { x_title, ry, clip_w, PL_ROW };
        (void)SDL_SetRenderClipRect(r, &title_clip);
        render_utf8(r, ui->font, pl->entries[idx].title,
                    (f32)x_title, (f32)ty, COL_TEXT);
        (void)SDL_SetRenderClipRect(r, NULL);
    }

    if (tlen > 0) {
        f32 tx = (f32)(PL_DEL_X - tlen * FONT_W - 4);
        set_col(r, COL_DIM);
        (void)SDL_RenderDebugText(r, tx, (f32)ty, tbuf);
    }

    f32 dx = (f32)(PL_DEL_X + (PL_DEL_W - FONT_W) / 2);
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, dx, (f32)ty, "x");
}

/*
 * Draw a single playlist row (track number, title, duration, delete button).
 */
static void draw_playlist_row(SDL_Renderer *r, const playlist_t *pl,
                               const ui_t *ui, i32 idx, i32 row)
{
    assert(r   != NULL);
    assert(pl  != NULL);
    assert(ui  != NULL);
    assert(idx >= 0 && idx < pl->count);

    i32 track_x = PL_ADD_X + PL_ADD_SZ + 2;
    i32 x_ind   = track_x;
    i32 x_num   = track_x + FONT_W + 2;
    /* PLAYLIST_MAX is 256 (3 digits), so reserve 3 character widths.
     * Using 2 caused numbers ≥ 100 to overlap the title. */
    i32 x_title = x_num   + 3 * FONT_W + 2;

    i32 ry = PL_Y + row * PL_ROW;
    i32 ty = ry + (PL_ROW - FONT_H) / 2;

    if (idx == pl->current) {
        set_col(r, COL_BTN);
        fill_rect(r, (f32)track_x, (f32)ry, (f32)(LOG_W - track_x), (f32)PL_ROW);
    }
    if (idx == ui->pl_hover && idx != pl->current) {
        set_col(r, COL_BTN_HOT);
        fill_rect(r, (f32)track_x, (f32)ry, (f32)(LOG_W - track_x), (f32)PL_ROW);
    }
    if (idx == pl->current) {
        set_col(r, COL_NOW);
        (void)SDL_RenderDebugText(r, (f32)x_ind, (f32)ty, ">");
    }

    char numbuf[8];
    SDL_snprintf(numbuf, sizeof(numbuf), "%02d", idx + 1);
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, (f32)x_num, (f32)ty, numbuf);

    draw_pl_row_text(r, pl, ui, idx, x_title, ry, ty);
}

/*
 * Draw the playlist scrollbar thumb (only when list overflows visible area).
 */
static void draw_playlist_scrollbar(SDL_Renderer *r, const playlist_t *pl)
{
    assert(r  != NULL);
    assert(pl != NULL);

    i32 vis      = PL_H / PL_ROW;
    i32 max_scrl = pl->count - vis;
    if (max_scrl <= 0) { return; }

    set_col(r, COL_SCRUB_BG);
    fill_rect(r, (f32)PL_SB_X, (f32)PL_Y, (f32)PL_SB_W, (f32)PL_H);

    f32 thumb_h = (f32)vis / (f32)pl->count * (f32)PL_H;
    if (thumb_h < 4.0f) { thumb_h = 4.0f; }
    f32 travel  = (f32)PL_H - thumb_h;
    f32 thumb_y = (f32)PL_Y + ((f32)pl->scroll / (f32)max_scrl) * travel;

    set_col(r, COL_DIM);
    fill_rect(r, (f32)PL_SB_X, thumb_y, (f32)PL_SB_W, thumb_h);
}

/*
 * Draw the playlist panel.
 */
static void draw_playlist(SDL_Renderer *r, const playlist_t *pl, const ui_t *ui)
{
    assert(r  != NULL);
    assert(pl != NULL);
    assert(ui != NULL);

    set_col(r, COL_PANEL);
    fill_rect(r, 0.0f, (f32)PL_Y, (f32)LOG_W, (f32)PL_H);

    set_col(r, COL_BORDER);
    (void)SDL_RenderLine(r, 0.0f, (f32)PL_Y, (f32)LOG_W, (f32)PL_Y);

    i32 visible = PL_H / PL_ROW;
    assert(visible > 0);

    for (i32 row = 0; row < visible; row++) {
        i32 idx = pl->scroll + row;
        if (idx >= pl->count) { break; }
        draw_playlist_row(r, pl, ui, idx, row);
    }

    set_col(r, COL_BORDER);
    (void)SDL_RenderLine(r, 0.0f, (f32)(PL_Y + PL_H),
                            (f32)LOG_W, (f32)(PL_Y + PL_H));

    draw_playlist_scrollbar(r, pl);

    /* [+] button drawn last so it renders on top of any row highlights. */
    bool hot = ui->dialog_open;
    if (hot) { set_col(r, COL_BTN_HOT); } else { set_col(r, COL_BTN); }
    fill_rect(r, (f32)PL_ADD_X, (f32)(PL_Y + 1), (f32)PL_ADD_SZ, (f32)PL_ADD_SZ);
    set_col(r, COL_BORDER);
    outline_rect(r, (f32)PL_ADD_X, (f32)(PL_Y + 1), (f32)PL_ADD_SZ, (f32)PL_ADD_SZ);
    f32 lx = (f32)(PL_ADD_X + (PL_ADD_SZ - FONT_W) / 2);
    f32 ly = (f32)(PL_Y + 1 + (PL_ADD_SZ - FONT_H) / 2);
    set_col(r, COL_TEXT);
    (void)SDL_RenderDebugText(r, lx, ly, "+");
}

/* ── Synth dropdown geometry ─────────────────────────────────────────────── */

#define DD_ROWS_VISIBLE  4
#define DD_ROW_H         PL_ROW                          /* 16 px */
#define DD_H             (DD_ROWS_VISIBLE * DD_ROW_H)   /* 64 px */
#define DD_SB_W          5                               /* scrollbar strip width */

/* Instrument dropdown starts immediately below row 0. */
#define DD_INST_Y  (PL_Y + 16)
/* Note dropdown pinned to the bottom of the visible area. */
#define DD_NOTE_Y  (LOG_H - DD_H)

/*
 * Draw the synthesiser panel (instrument name, note name, PLAY/STOP button).
 * Occupies the same pixel region as the playlist panel.
 */
static void draw_synth_panel(SDL_Renderer *r, const ui_t *ui, const synth_t *synth)
{
    assert(r     != NULL);
    assert(ui    != NULL);
    assert(synth != NULL);

    /* Background + top/bottom borders */
    set_col(r, COL_PANEL);
    fill_rect(r, 0.0f, (f32)PL_Y, (f32)LOG_W, (f32)PL_H);
    set_col(r, COL_BORDER);
    (void)SDL_RenderLine(r, 0.0f, (f32)PL_Y, (f32)LOG_W, (f32)PL_Y);
    (void)SDL_RenderLine(r, 0.0f, (f32)(PL_Y + PL_H), (f32)LOG_W, (f32)(PL_Y + PL_H));

    /* Row 0: instrument name + open/close indicator */
    {
        char inst_name[64];
        char label[80];
        synth_instrument_name(ui->synth_inst, inst_name, (i32)sizeof(inst_name));
        SDL_snprintf(label, sizeof(label), "INST: %s", inst_name);
        i32 ty = PL_Y + (16 - FONT_H) / 2;
        /* Leave 2 chars + 4 px on the right for the indicator. */
        SDL_Rect clip = { 4, PL_Y, LOG_W - 8 - 2 * FONT_W - 4, 16 };
        (void)SDL_SetRenderClipRect(r, &clip);
        set_col(r, COL_TEXT);
        (void)SDL_RenderDebugText(r, 4.0f, (f32)ty, label);
        (void)SDL_SetRenderClipRect(r, NULL);
        set_col(r, COL_DIM);
        (void)SDL_RenderDebugText(r, (f32)(LOG_W - 4 - 2 * FONT_W), (f32)ty,
                                   ui->synth_inst_open ? " ^" : " v");
    }

    set_col(r, COL_BORDER);
    (void)SDL_RenderLine(r, 0.0f, (f32)(PL_Y + 16), (f32)LOG_W, (f32)(PL_Y + 16));

    /* Row 1: note name + open/close indicator */
    {
        char note_name[8];
        char label[24];
        synth_note_name(ui->synth_note, note_name, (i32)sizeof(note_name));
        SDL_snprintf(label, sizeof(label), "NOTE: %s", note_name);
        i32 ty = PL_Y + 16 + (16 - FONT_H) / 2;
        SDL_Rect clip = { 4, PL_Y + 16, LOG_W - 8 - 2 * FONT_W - 4, 16 };
        (void)SDL_SetRenderClipRect(r, &clip);
        set_col(r, COL_TEXT);
        (void)SDL_RenderDebugText(r, 4.0f, (f32)ty, label);
        (void)SDL_SetRenderClipRect(r, NULL);
        set_col(r, COL_DIM);
        (void)SDL_RenderDebugText(r, (f32)(LOG_W - 4 - 2 * FONT_W), (f32)ty,
                                   ui->synth_note_open ? " ^" : " v");
    }

}

static void draw_dd_scrollbar(SDL_Renderer *r, i32 sb_x, i32 dd_y,
                               i32 item_count, i32 dd_scroll)
{
    assert(r != NULL);
    assert(item_count > DD_ROWS_VISIBLE);
    i32 max_scroll = item_count - DD_ROWS_VISIBLE;
    set_col(r, COL_SCRUB_BG);
    fill_rect(r, (f32)sb_x, (f32)dd_y, (f32)DD_SB_W, (f32)DD_H);
    if (max_scroll > 0) {
        f32 thumb_h = (f32)DD_H * (f32)DD_ROWS_VISIBLE / (f32)item_count;
        if (thumb_h < 4.0f) { thumb_h = 4.0f; }
        f32 travel  = (f32)DD_H - thumb_h;
        f32 thumb_y = (f32)dd_y + ((f32)dd_scroll / (f32)max_scroll) * travel;
        set_col(r, COL_DIM);
        fill_rect(r, (f32)sb_x, thumb_y, (f32)DD_SB_W, thumb_h);
    }
}

/*
 * Draw whichever dropdown (instrument or note) is currently open.
 * Called after draw_controls so the dropdown overlays everything.
 */
static void draw_synth_dropdowns(SDL_Renderer *r, ui_t *ui)
{
    assert(r  != NULL);
    assert(ui != NULL);

    if (!ui->synth_inst_open && !ui->synth_note_open) { return; }

    bool is_inst    = ui->synth_inst_open;
    i32  selected   = is_inst ? ui->synth_inst  : ui->synth_note;
    i32  dd_scroll  = ui->synth_dd_scroll;
    i32  dd_x       = 4;
    i32  dd_w       = LOG_W - 8;
    i32  dd_y       = is_inst ? DD_INST_Y : DD_NOTE_Y;
    i32  item_count = is_inst ? SYNTH_INSTRUMENTS : SYNTH_NOTES;
    i32  text_x     = dd_x + 2;
    i32  item_area  = dd_w - DD_SB_W;

    set_col(r, COL_PANEL);
    fill_rect(r, (f32)dd_x, (f32)dd_y, (f32)dd_w, (f32)DD_H);
    set_col(r, COL_BORDER);
    outline_rect(r, (f32)dd_x, (f32)dd_y, (f32)dd_w, (f32)DD_H);

    for (i32 row = 0; row < DD_ROWS_VISIBLE; row++) {
        i32 item  = dd_scroll + row;
        if (item >= item_count) { break; }
        i32 row_y = dd_y + row * DD_ROW_H;
        i32 ty    = row_y + (DD_ROW_H - FONT_H) / 2;

        if (item == selected) {
            set_col(r, COL_BTN);
            fill_rect(r, (f32)dd_x, (f32)row_y, (f32)item_area, (f32)DD_ROW_H);
        }
        if (item == ui->synth_dd_hover) {
            set_col(r, COL_BTN_HOT);
            fill_rect(r, (f32)dd_x, (f32)row_y, (f32)item_area, (f32)DD_ROW_H);
        }

        char name_buf[64];
        if (is_inst) {
            synth_instrument_name(item, name_buf, (i32)sizeof(name_buf));
        } else {
            synth_note_name(item, name_buf, (i32)sizeof(name_buf));
        }
        SDL_Rect item_clip = { dd_x, row_y, item_area, DD_ROW_H };
        (void)SDL_SetRenderClipRect(r, &item_clip);
        set_col(r, COL_TEXT);
        (void)SDL_RenderDebugText(r, (f32)text_x, (f32)ty, name_buf);
        (void)SDL_SetRenderClipRect(r, NULL);

        if (row < DD_ROWS_VISIBLE - 1) {
            set_col(r, COL_BORDER);
            (void)SDL_RenderLine(r, (f32)dd_x, (f32)(row_y + DD_ROW_H),
                                    (f32)(dd_x + item_area), (f32)(row_y + DD_ROW_H));
        }
    }

    draw_dd_scrollbar(r, dd_x + dd_w - DD_SB_W, dd_y, item_count, dd_scroll);
}

/* ── Public interface ────────────────────────────────────────────────────── */

void ui_draw(SDL_Renderer       *r,
             const audio_t      *audio,
             const spectrum_t   *spectrum,
             const playlist_t   *playlist,
             ui_t               *ui,
             app_mode_t          mode,
             const synth_t      *synth)
{
    assert(r        != NULL);
    assert(audio    != NULL);
    assert(spectrum != NULL);
    assert(playlist != NULL);
    assert(ui       != NULL);
    assert(synth    != NULL);

    (void)SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Clear to background colour */
    set_col(r, COL_BG);
    (void)SDL_RenderClear(r);

    draw_header(r, audio, playlist, ui, mode);
    draw_graph(r, spectrum, ui);

    if (mode == APP_MODE_SYNTH) {
        draw_synth_panel(r, ui, synth);
    } else {
        draw_playlist(r, playlist, ui);
    }

    draw_controls(r, audio, ui);

    /* Dropdowns overlay everything — drawn last. */
    if (mode == APP_MODE_SYNTH) {
        draw_synth_dropdowns(r, ui);
    }

    (void)SDL_RenderPresent(r);
}

/* ── Input handlers ──────────────────────────────────────────────────────── */

/*
 * Check whether a logical-space mouse position is inside a button.
 * btn_idx: 0=rewind, 1=play-pause, 2=next  (left to right in control bar).
 */
static bool in_button(i32 btn_idx, f32 lx, f32 ly)
{
    assert(btn_idx >= 0 && btn_idx <= 2);
    assert(BTN_SZ > 0);
    i32 bx = 4 + btn_idx * (BTN_SZ + BTN_GAP);
    i32 by = CTRL_Y + 2;
    return (lx >= (f32)bx && lx < (f32)(bx + BTN_SZ) &&
            ly >= (f32)by && ly < (f32)(by + BTN_SZ));
}

static bool in_seek_bar(f32 lx, f32 ly)
{
    assert(SEEK_W > 0);
    assert(SEEK_H > 0);
    return (lx >= (f32)SEEK_X && lx < (f32)(SEEK_X + SEEK_W) &&
            ly >= (f32)SEEK_Y && ly < (f32)(SEEK_Y + SEEK_H));
}

static bool in_vol_slider(f32 lx, f32 ly)
{
    assert(VOL_W > 0);
    assert(VOL_H > 0);
    return (lx >= (f32)VOL_X && lx < (f32)(VOL_X + VOL_W) &&
            ly >= (f32)VOL_Y && ly < (f32)(VOL_Y + VOL_H));
}

static i32  pl_row_at(f32 lx, f32 ly, const playlist_t *pl);  /* forward */

static bool in_pl_sb(f32 lx, f32 ly)
{
    assert(PL_SB_W > 0);
    assert(PL_H    > 0);
    return (lx >= (f32)PL_SB_X && lx < (f32)(PL_SB_X + PL_SB_W) &&
            ly >= (f32)PL_Y    && ly < (f32)(PL_Y + PL_H));
}

/* Map a y coordinate inside the playlist panel to a scroll index. */
static i32 sb_pos_to_scroll(f32 ly, i32 count)
{
    assert(count > 0);
    assert(count <= PLAYLIST_MAX);
    i32 vis        = PL_H / PL_ROW;
    i32 max_scroll = count - vis;
    if (max_scroll <= 0) { return 0; }

    f32 t = (ly - (f32)PL_Y) / (f32)PL_H;
    if (t < 0.0f) { t = 0.0f; }
    if (t > 1.0f) { t = 1.0f; }

    i32 s = (i32)(t * (f32)max_scroll + 0.5f);
    if (s < 0)          { s = 0; }
    if (s > max_scroll) { s = max_scroll; }
    return s;
}

static bool in_pl_del(f32 lx, f32 ly, const playlist_t *pl)
{
    assert(pl != NULL);
    assert(PL_DEL_W > 0);
    if (lx < (f32)PL_DEL_X || lx >= (f32)(PL_DEL_X + PL_DEL_W)) { return false; }
    return pl_row_at(lx, ly, pl) >= 0;
}

static bool in_add_button(f32 lx, f32 ly)
{
    assert(PL_ADD_SZ > 0);
    assert(PL_ADD_X  >= 0);
    return (lx >= (f32)PL_ADD_X && lx < (f32)(PL_ADD_X + PL_ADD_SZ) &&
            ly >= (f32)(PL_Y + 1) && ly < (f32)(PL_Y + 1 + PL_ADD_SZ));
}

/* ── Synth panel hit tests ───────────────────────────────────────────────── */

/* Return 0 if "Play" clicked, 1 if "Synth" clicked, -1 if neither. */
static i32 which_mode_btn(f32 lx, f32 ly)
{
    assert(MODE_BTN1_W > 0 && MODE_BTN2_W > 0 && MODE_BTN_H > 0);
    if (ly < (f32)MODE_BTN_Y || ly >= (f32)(MODE_BTN_Y + MODE_BTN_H)) { return -1; }
    if (lx >= (f32)MODE_BTN_X && lx < (f32)(MODE_BTN_X + MODE_BTN1_W)) { return 0; }
    i32 x2 = MODE_BTN_X + MODE_BTN1_W + MODE_BTN_GAP;
    if (lx >= (f32)x2 && lx < (f32)(x2 + MODE_BTN2_W)) { return 1; }
    return -1;
}

/*
 * Hit test: is (lx, ly) inside graph-mode toggle button btn_idx?
 * btn 0 = Freq, btn 1 = Wave, btn 2 = Both.
 * Buttons sit in the top-right interior of the spectrum panel.
 */
static bool in_graph_btn(f32 lx, f32 ly, i32 btn_idx)
{
    assert(btn_idx >= 0 && btn_idx < (i32)GRAPH_MODE_COUNT);
    assert(GRAPH_BTN_W > 0 && GRAPH_BTN_H > 0);
    i32 x0 = SPEC_X + SPEC_W - 3 * GRAPH_BTN_W - 2 * GRAPH_BTN_GAP - 2;
    i32 bx = x0 + btn_idx * (GRAPH_BTN_W + GRAPH_BTN_GAP);
    i32 by = SPEC_Y + 2;
    return (lx >= (f32)bx && lx < (f32)(bx + GRAPH_BTN_W) &&
            ly >= (f32)by && ly < (f32)(by + GRAPH_BTN_H));
}

/* Hit-test the "Deconstruct" decompose-toggle button. */
static bool in_decomp_btn(f32 lx, f32 ly)
{
    assert(DECOMP_BTN_W > 0);
    assert(GRAPH_BTN_H  > 0);
    return (lx >= (f32)DECOMP_BTN_X && lx < (f32)(DECOMP_BTN_X + DECOMP_BTN_W) &&
            ly >= (f32)DECOMP_BTN_Y && ly < (f32)(DECOMP_BTN_Y + GRAPH_BTN_H));
}

static bool in_synth_inst_row(f32 lx, f32 ly)
{
    assert(PL_Y >= 0);
    assert(PL_ROW > 0);
    (void)lx;
    return (ly >= (f32)PL_Y && ly < (f32)(PL_Y + PL_ROW));
}

static bool in_synth_note_row(f32 lx, f32 ly)
{
    assert(PL_Y   >= 0);
    assert(PL_ROW >  0);
    (void)lx;
    return (ly >= (f32)(PL_Y + PL_ROW) && ly < (f32)(PL_Y + 2 * PL_ROW));
}

/*
 * Return the playlist row index under (lx, ly), or -1 if none.
 */
static i32 pl_row_at(f32 lx, f32 ly, const playlist_t *pl)
{
    assert(pl != NULL);
    assert(PL_ROW > 0);
    (void)lx;

    if (ly < (f32)PL_Y || ly >= (f32)(PL_Y + PL_H)) { return -1; }

    i32 row = (i32)(ly - (f32)PL_Y) / PL_ROW;
    i32 idx = pl->scroll + row;
    if (idx < 0 || idx >= pl->count) { return -1; }
    return idx;
}

static ui_action_t mouse_down_dd(ui_t *ui, f32 lx, f32 ly, bool is_inst)
{
    assert(ui != NULL);
    assert(DD_ROWS_VISIBLE > 0);
    i32 dd_y       = is_inst ? DD_INST_Y : DD_NOTE_Y;
    i32 item_count = is_inst ? SYNTH_INSTRUMENTS : SYNTH_NOTES;
    if (lx >= 4.0f && lx < (f32)(LOG_W - 4) &&
        ly >= (f32)dd_y && ly < (f32)(dd_y + DD_H)) {
        if (lx < (f32)(LOG_W - 4 - DD_SB_W)) {
            i32 row  = (i32)(ly - (f32)dd_y) / DD_ROW_H;
            i32 item = ui->synth_dd_scroll + row;
            if (item >= 0 && item < item_count) {
                ui->synth_dd_select = item;
                if (is_inst) { ui->synth_inst_open = false; }
                else         { ui->synth_note_open = false; }
                ui->synth_dd_hover = -1;
                return is_inst ? UI_ACTION_SYNTH_INST_SET : UI_ACTION_SYNTH_NOTE_SET;
            }
        } else {
            i32 max_scroll = item_count - DD_ROWS_VISIBLE;
            f32 t = (ly - (f32)dd_y) / (f32)DD_H;
            if (t < 0.0f) { t = 0.0f; }
            if (t > 1.0f) { t = 1.0f; }
            ui->synth_dd_scroll = (i32)(t * (f32)max_scroll + 0.5f);
            if (ui->synth_dd_scroll < 0)          { ui->synth_dd_scroll = 0; }
            if (ui->synth_dd_scroll > max_scroll) { ui->synth_dd_scroll = max_scroll; }
            ui->synth_dd_sb_dragging = true;
        }
    } else {
        if (is_inst) { ui->synth_inst_open = false; }
        else         { ui->synth_note_open = false; }
        ui->synth_dd_hover = -1;
    }
    return UI_ACTION_NONE;
}

static ui_action_t mouse_down_synth(ui_t *ui, f32 lx, f32 ly)
{
    assert(ui != NULL);
    assert(SYNTH_INSTRUMENTS > 0);
    if (ui->synth_inst_open) { return mouse_down_dd(ui, lx, ly, true);  }
    if (ui->synth_note_open) { return mouse_down_dd(ui, lx, ly, false); }
    if (in_synth_inst_row(lx, ly)) {
        ui->synth_inst_open = true;
        i32 s = ui->synth_inst > 0 ? ui->synth_inst - 1 : 0;
        if (s > SYNTH_INSTRUMENTS - DD_ROWS_VISIBLE) { s = SYNTH_INSTRUMENTS - DD_ROWS_VISIBLE; }
        ui->synth_dd_scroll = s;
        ui->synth_dd_hover  = -1;
        return UI_ACTION_NONE;
    }
    if (in_synth_note_row(lx, ly)) {
        ui->synth_note_open = true;
        i32 s = ui->synth_note > 0 ? ui->synth_note - 1 : 0;
        if (s > SYNTH_NOTES - DD_ROWS_VISIBLE) { s = SYNTH_NOTES - DD_ROWS_VISIBLE; }
        ui->synth_dd_scroll = s;
        ui->synth_dd_hover  = -1;
        return UI_ACTION_NONE;
    }
    return UI_ACTION_NONE;
}

static ui_action_t mouse_down_player(ui_t *ui, f32 lx, f32 ly, playlist_t *pl)
{
    assert(ui != NULL);
    assert(pl != NULL);
    if (in_add_button(lx, ly) && !ui->dialog_open) { return UI_ACTION_OPEN_DIALOG; }
    if (in_pl_sb(lx, ly)) {
        ui->sb_dragging = true;
        if (pl->count > 0) { pl->scroll = sb_pos_to_scroll(ly, pl->count); }
        return UI_ACTION_NONE;
    }
    if (in_pl_del(lx, ly, pl)) {
        ui->pl_hover = pl_row_at(lx, ly, pl);
        return UI_ACTION_PL_REMOVE;
    }
    i32 row = pl_row_at(lx, ly, pl);
    if (row >= 0) { ui->pl_hover = row; return UI_ACTION_PL_SELECT; }
    return UI_ACTION_NONE;
}

/*
 * Hit-test the top toolbar clusters shared by both app modes: the "Deconstruct"
 * toggle, graph-mode buttons, the Play/Synth mode toggle, and the transport
 * buttons.  Returns the triggered action, or UI_ACTION_NONE if no toolbar
 * control was hit (the caller then checks the volume/seek/panel regions).
 */
static ui_action_t mouse_down_toolbar(ui_t *ui, f32 lx, f32 ly, app_mode_t mode)
{
    assert(ui != NULL);

    /* "Deconstruct" toggle — only visible when time-domain is shown. */
    if (ui->graph_mode != GRAPH_MODE_FREQ && in_decomp_btn(lx, ly)) {
        ui->wave_decompose = !ui->wave_decompose;
        return UI_ACTION_WAVE_DECOMPOSE;
    }

    /* Graph mode toggle buttons — active in both app modes, checked first. */
    for (i32 b = 0; b < (i32)GRAPH_MODE_COUNT; b++) {
        if (in_graph_btn(lx, ly, b)) {
            ui->graph_mode = (graph_mode_t)b;
            return UI_ACTION_GRAPH_MODE;
        }
    }

    /* Mode toggle — no-op when clicking the already-active mode. */
    i32 mb = which_mode_btn(lx, ly);
    if (mb == 0 && mode != APP_MODE_PLAYER) { return UI_ACTION_MODE_TOGGLE; }
    if (mb == 1 && mode != APP_MODE_SYNTH)  { return UI_ACTION_MODE_TOGGLE; }

    /* Transport buttons — active in both modes. */
    if (in_button(0, lx, ly)) { return UI_ACTION_REWIND;     }
    if (in_button(1, lx, ly)) { return UI_ACTION_PLAY_PAUSE; }
    if (in_button(2, lx, ly)) { return UI_ACTION_NEXT;       }

    return UI_ACTION_NONE;
}

ui_action_t ui_on_mouse_down(ui_t *ui, const SDL_MouseButtonEvent *e,
                              const audio_t *audio, playlist_t *pl,
                              app_mode_t mode)
{
    assert(ui    != NULL);
    assert(e     != NULL);
    assert(audio != NULL);
    assert(pl    != NULL);

    if (e->button != SDL_BUTTON_LEFT) { return UI_ACTION_NONE; }

    f32 lx = e->x;
    f32 ly = e->y;

    /* Open synth dropdowns have exclusive priority — consume the click before
     * transport / control hit tests can intercept overlapping rows. */
    if (mode == APP_MODE_SYNTH && (ui->synth_inst_open || ui->synth_note_open)) {
        return mouse_down_synth(ui, lx, ly);
    }

    ui_action_t toolbar = mouse_down_toolbar(ui, lx, ly, mode);
    if (toolbar != UI_ACTION_NONE) { return toolbar; }

    /* Volume and seek bar — active in both modes. */
    if (in_vol_slider(lx, ly)) {
        ui->vol_dragging = true;
        ui->vol_pos      = (lx - (f32)VOL_X) / (f32)VOL_W;
        if (ui->vol_pos < 0.0f) { ui->vol_pos = 0.0f; }
        if (ui->vol_pos > 1.0f) { ui->vol_pos = 1.0f; }
        return UI_ACTION_SET_VOLUME;
    }

    if (in_seek_bar(lx, ly)) {
        ui->scrubbing  = true;
        ui->scrub_pos  = (lx - (f32)SEEK_X) / (f32)SEEK_W;
        if (ui->scrub_pos < 0.0f) { ui->scrub_pos = 0.0f; }
        if (ui->scrub_pos > 1.0f) { ui->scrub_pos = 1.0f; }
        return UI_ACTION_SEEK;
    }

    if (mode == APP_MODE_SYNTH) { return mouse_down_synth(ui, lx, ly); }
    return mouse_down_player(ui, lx, ly, pl);
}

static void motion_update_dd_hover(ui_t *ui, f32 lx, f32 ly)
{
    assert(ui != NULL);
    assert(DD_ROW_H > 0);
    if (ui->synth_inst_open) {
        if (lx >= 4.0f && lx < (f32)(LOG_W - 4 - DD_SB_W) &&
            ly >= (f32)DD_INST_Y && ly < (f32)(DD_INST_Y + DD_H)) {
            ui->synth_dd_hover = ui->synth_dd_scroll + (i32)(ly - (f32)DD_INST_Y) / DD_ROW_H;
        } else {
            ui->synth_dd_hover = -1;
        }
    } else if (ui->synth_note_open) {
        if (lx >= 4.0f && lx < (f32)(LOG_W - 4 - DD_SB_W) &&
            ly >= (f32)DD_NOTE_Y && ly < (f32)(DD_NOTE_Y + DD_H)) {
            ui->synth_dd_hover = ui->synth_dd_scroll + (i32)(ly - (f32)DD_NOTE_Y) / DD_ROW_H;
        } else {
            ui->synth_dd_hover = -1;
        }
    }
}

ui_action_t ui_on_mouse_motion(ui_t *ui, const SDL_MouseMotionEvent *e,
                                bool held, playlist_t *pl)
{
    assert(ui != NULL);
    assert(e  != NULL);
    assert(pl != NULL);

    f32 lx = e->x;
    f32 ly = e->y;

    /* Update playlist hover — use pl_row_at so scroll offset is accounted for. */
    ui->pl_hover = pl_row_at(lx, ly, pl);

    /* Update dropdown hover when a dropdown is open. */
    motion_update_dd_hover(ui, lx, ly);

    /* Drag dropdown scrollbar. */
    if (ui->synth_dd_sb_dragging && held) {
        bool is_inst  = ui->synth_inst_open;
        i32  item_cnt = is_inst ? SYNTH_INSTRUMENTS : SYNTH_NOTES;
        i32  max_scrl = item_cnt - DD_ROWS_VISIBLE;
        i32  dd_y     = is_inst ? DD_INST_Y : DD_NOTE_Y;
        f32  t        = (ly - (f32)dd_y) / (f32)DD_H;
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }
        ui->synth_dd_scroll = (i32)(t * (f32)max_scrl + 0.5f);
        if (ui->synth_dd_scroll < 0)        { ui->synth_dd_scroll = 0; }
        if (ui->synth_dd_scroll > max_scrl) { ui->synth_dd_scroll = max_scrl; }
        return UI_ACTION_NONE;
    }

    /* Drag scrollbar. */
    if (ui->sb_dragging && held && pl->count > 0) {
        pl->scroll = sb_pos_to_scroll(ly, pl->count);
        return UI_ACTION_NONE;
    }

    /* Drag volume slider. */
    if (ui->vol_dragging && held) {
        ui->vol_pos = (lx - (f32)VOL_X) / (f32)VOL_W;
        if (ui->vol_pos < 0.0f) { ui->vol_pos = 0.0f; }
        if (ui->vol_pos > 1.0f) { ui->vol_pos = 1.0f; }
        return UI_ACTION_SET_VOLUME;
    }

    /* Drag seek bar if scrubbing. */
    if (ui->scrubbing && held) {
        ui->scrub_pos = (lx - (f32)SEEK_X) / (f32)SEEK_W;
        if (ui->scrub_pos < 0.0f) { ui->scrub_pos = 0.0f; }
        if (ui->scrub_pos > 1.0f) { ui->scrub_pos = 1.0f; }
        return UI_ACTION_SEEK;
    }
    return UI_ACTION_NONE;
}

ui_action_t ui_on_mouse_up(ui_t *ui, const SDL_MouseButtonEvent *e)
{
    assert(ui != NULL);
    assert(e  != NULL);

    if (e->button != SDL_BUTTON_LEFT) { return UI_ACTION_NONE; }

    if (ui->synth_dd_sb_dragging) {
        ui->synth_dd_sb_dragging = false;
        return UI_ACTION_NONE;
    }

    if (ui->sb_dragging) {
        ui->sb_dragging = false;
        return UI_ACTION_NONE;
    }

    if (ui->vol_dragging) {
        f32 lx      = e->x;
        ui->vol_pos = (lx - (f32)VOL_X) / (f32)VOL_W;
        if (ui->vol_pos < 0.0f) { ui->vol_pos = 0.0f; }
        if (ui->vol_pos > 1.0f) { ui->vol_pos = 1.0f; }
        ui->vol_dragging = false;
        return UI_ACTION_SET_VOLUME;
    }

    if (ui->scrubbing) {
        f32 lx        = e->x;
        ui->scrub_pos = (lx - (f32)SEEK_X) / (f32)SEEK_W;
        if (ui->scrub_pos < 0.0f) { ui->scrub_pos = 0.0f; }
        if (ui->scrub_pos > 1.0f) { ui->scrub_pos = 1.0f; }
        ui->scrubbing = false;
        return UI_ACTION_SEEK;
    }
    return UI_ACTION_NONE;
}

ui_action_t ui_on_key(ui_t *ui, const SDL_KeyboardEvent *e, app_mode_t mode)
{
    assert(ui != NULL);
    assert(e  != NULL);
    assert(e->type == SDL_EVENT_KEY_DOWN);

    bool shift = (e->mod & SDL_KMOD_SHIFT) != 0;

    switch (e->scancode) {
    case SDL_SCANCODE_SPACE: return UI_ACTION_PLAY_PAUSE;
    case SDL_SCANCODE_LEFT:  return UI_ACTION_REWIND;
    case SDL_SCANCODE_RIGHT: return UI_ACTION_NEXT;
    case SDL_SCANCODE_F11:   return UI_ACTION_FULLSCREEN;
    case SDL_SCANCODE_S:
        if (shift) {
            if (mode == APP_MODE_SYNTH) { return UI_ACTION_NONE; }
            return UI_ACTION_MODE_TOGGLE;
        }
        ui->graph_mode = GRAPH_MODE_FREQ;
        return UI_ACTION_GRAPH_MODE;
    case SDL_SCANCODE_W:
        ui->graph_mode = GRAPH_MODE_TIME;
        return UI_ACTION_GRAPH_MODE;
    case SDL_SCANCODE_B:
        ui->graph_mode = GRAPH_MODE_BOTH;
        return UI_ACTION_GRAPH_MODE;
    case SDL_SCANCODE_D:
        ui->wave_decompose = !ui->wave_decompose;
        return UI_ACTION_WAVE_DECOMPOSE;
    case SDL_SCANCODE_P:
        if (shift) {
            if (mode == APP_MODE_PLAYER) { return UI_ACTION_NONE; }
            return UI_ACTION_MODE_TOGGLE;
        }
        break;
    default: break;
    }
    return UI_ACTION_NONE;
}

/*
 * Convert a mouse-wheel delta into whole discrete scroll steps, carrying the
 * sub-step remainder in ui->scroll_accum.  A wheel notch (dy = ±1) yields
 * exactly ±1 step; a trackpad's small fractional deltas accumulate across
 * events instead of truncating to zero, and a fast flick steps proportionally.
 */
static i32 scroll_steps(ui_t *ui, f32 dy)
{
    assert(ui != NULL);
    ui->scroll_accum += dy;
    i32 steps = (i32)ui->scroll_accum;   /* truncates toward zero */
    ui->scroll_accum -= (f32)steps;
    return steps;
}

ui_action_t ui_on_scroll(ui_t *ui, f32 lx, f32 ly, f32 dy,
                          f32 current_seek_pos, playlist_t *pl,
                          app_mode_t mode)
{
    assert(ui != NULL);
    assert(pl != NULL);

    if (in_vol_slider(lx, ly)) {
        ui->vol_pos += dy * 0.05f;
        if (ui->vol_pos < 0.0f) { ui->vol_pos = 0.0f; }
        if (ui->vol_pos > 1.0f) { ui->vol_pos = 1.0f; }
        return UI_ACTION_SET_VOLUME;
    }

    if (in_seek_bar(lx, ly)) {
        ui->scrub_pos = current_seek_pos + dy * 0.02f;
        if (ui->scrub_pos < 0.0f) { ui->scrub_pos = 0.0f; }
        if (ui->scrub_pos > 1.0f) { ui->scrub_pos = 1.0f; }
        return UI_ACTION_SEEK;
    }

    /* Synth panel: scroll within an open dropdown, or over rows to change selection. */
    if (mode == APP_MODE_SYNTH) {
        if (ui->synth_inst_open || ui->synth_note_open) {
            i32 item_count = ui->synth_inst_open ? SYNTH_INSTRUMENTS : SYNTH_NOTES;
            i32 max_scroll = item_count - DD_ROWS_VISIBLE;
            ui->synth_dd_scroll -= scroll_steps(ui, dy);
            if (ui->synth_dd_scroll < 0)           { ui->synth_dd_scroll = 0; }
            if (ui->synth_dd_scroll > max_scroll)  { ui->synth_dd_scroll = max_scroll; }
            return UI_ACTION_NONE;
        }
        if (in_synth_inst_row(lx, ly)) {
            i32 steps = scroll_steps(ui, dy);
            if (steps == 0) { return UI_ACTION_NONE; }
            return (steps > 0) ? UI_ACTION_SYNTH_INST_UP : UI_ACTION_SYNTH_INST_DOWN;
        }
        if (in_synth_note_row(lx, ly)) {
            i32 steps = scroll_steps(ui, dy);
            if (steps == 0) { return UI_ACTION_NONE; }
            return (steps > 0) ? UI_ACTION_SYNTH_NOTE_UP : UI_ACTION_SYNTH_NOTE_DOWN;
        }
        return UI_ACTION_NONE;
    }

    /* Player mode: scroll the playlist (positive dy = scroll up = earlier tracks). */
    if (ly >= (f32)PL_Y && ly < (f32)(PL_Y + PL_H)) {
        i32 vis        = PL_H / PL_ROW;
        i32 max_scroll = pl->count - vis;
        if (max_scroll > 0) {
            pl->scroll -= scroll_steps(ui, dy);
            if (pl->scroll < 0)          { pl->scroll = 0; }
            if (pl->scroll > max_scroll) { pl->scroll = max_scroll; }
        }
        return UI_ACTION_NONE;
    }

    return UI_ACTION_NONE;
}
