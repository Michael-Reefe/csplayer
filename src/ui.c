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

/* ── Layout helpers ──────────────────────────────────────────────────────── */

/* Volume slider geometry (between the 3 transport buttons and the seek bar). */
#define VOL_X    (4 + 3 * BTN_SZ + 2 * BTN_GAP + 4)   /* = 72 */
#define VOL_W    36
#define VOL_Y    (CTRL_Y + 3)
#define VOL_H    (CTRL_H - 6)

/* Playlist scrollbar (rightmost strip of the playlist panel). */
#define PL_SB_W     5
#define PL_SB_X     (LOG_W - PL_SB_W)              /* = 507 */

/* Delete button — sits immediately left of the scrollbar. */
#define PL_DEL_W    12
#define PL_DEL_X    (PL_SB_X - PL_DEL_W - 2)       /* = 493 */

/* Seek bar geometry (after the volume slider). */
#define SEEK_X   (VOL_X + VOL_W + 4)   /* = 112 */
#define SEEK_Y   (CTRL_Y + 2)
#define SEEK_W   (LOG_W - SEEK_X - 4)
#define SEEK_H   (CTRL_H - 4)

/* Font dimensions (SDL_RenderDebugText built-in 8 × 8 font). */
#define FONT_W  8
#define FONT_H  8

/* ── Internal: colour helpers ────────────────────────────────────────────── */

static void set_col(SDL_Renderer *r, i32 rv, i32 gv, i32 bv, i32 av)
{
    (void)SDL_SetRenderDrawColor(r, (Uint8)rv, (Uint8)gv, (Uint8)bv, (Uint8)av);
}

/* Draw a filled rectangle. */
static void fill_rect(SDL_Renderer *r, f32 x, f32 y, f32 w, f32 h)
{
    SDL_FRect rc = { x, y, w, h };
    (void)SDL_RenderFillRect(r, &rc);
}

/* Draw a rectangle outline (1 px border). */
static void outline_rect(SDL_Renderer *r, f32 x, f32 y, f32 w, f32 h)
{
    SDL_FRect rc = { x, y, w, h };
    (void)SDL_RenderRect(r, &rc);
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
 * Convert a dB value to a pixel row within the spectrum plot.
 */
static i32 db_to_py(f32 db)
{
    assert(DB_TOP > DB_BOT);
    assert(SPEC_H > 0);
    f32 t = (DB_TOP - db) / (DB_TOP - DB_BOT);
    if (t < 0.0f) { t = 0.0f; }
    if (t > 1.0f) { t = 1.0f; }
    return SPEC_Y + (i32)(t * (f32)SPEC_H);
}

/* ── Internal draw sub-functions ─────────────────────────────────────────── */

/*
 * Draw the header bar: dark background, app name, current track title
 * and elapsed / total time.
 */
static void draw_header(SDL_Renderer *r,
                         const audio_t *audio, const playlist_t *pl)
{
    assert(r     != NULL);
    assert(audio != NULL);
    assert(pl    != NULL);

    /* Background */
    set_col(r, COL_HEADER);
    fill_rect(r, 0.0f, (f32)HDR_Y, (f32)LOG_W, (f32)HDR_H);

    /* App name */
    set_col(r, COL_NOW);
    (void)SDL_RenderDebugText(r, 4.0f, (f32)(HDR_Y + 6), CSPLAYER_APP_NAME);

    /* Track title (display the entry title, not the full path). */
    set_col(r, COL_TEXT);
    if (pl->current >= 0 && pl->current < pl->count) {
        (void)SDL_RenderDebugText(r, 72.0f, (f32)(HDR_Y + 6),
                                  pl->entries[pl->current].title);
    }

    /* Elapsed / total time (right-aligned). */
    f32 pos_frac = audio_get_pos(audio);
    f64 dur_sec  = (audio->duration_frames > 0 && audio->sample_rate > 0.0f)
                       ? (f64)audio->duration_frames / (f64)audio->sample_rate
                       : 0.0;
    f64 elapsed  = pos_frac * dur_sec;

    char timebuf[24];
    SDL_snprintf(timebuf, sizeof(timebuf), "%d:%02d / %d:%02d",
                 (i32)(elapsed / 60.0), (i32)elapsed % 60,
                 (i32)(dur_sec / 60.0), (i32)dur_sec % 60);

    /* Right-align: each char is FONT_W pixels wide. */
    i32 tlen = (i32)SDL_strlen(timebuf);
    f32 tx   = (f32)(LOG_W - tlen * FONT_W - 4);
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, tx, (f32)(HDR_Y + 6), timebuf);
}

/*
 * Draw the spectrum background, axis lines, dB labels and frequency labels.
 */
static void draw_spectrum_axes(SDL_Renderer *r)
{
    assert(r != NULL);

    /* Plot background */
    set_col(r, COL_PANEL);
    fill_rect(r, (f32)SPEC_X, (f32)SPEC_Y, (f32)SPEC_W, (f32)SPEC_H);

    /* Left Y-axis line */
    set_col(r, COL_AXIS);
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)SPEC_Y,
                            (f32)SPEC_X, (f32)(SPEC_Y + SPEC_H));
    /* Bottom X-axis line */
    (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)(SPEC_Y + SPEC_H),
                            (f32)(SPEC_X + SPEC_W), (f32)(SPEC_Y + SPEC_H));

    /* dB horizontal grid lines + labels */
    for (i32 t = 0; t < NUM_DB_TICKS; t++) {
        assert(t < NUM_DB_TICKS);
        i32 py = db_to_py(db_ticks[t]);

        set_col(r, COL_AXIS);
        (void)SDL_RenderLine(r, (f32)SPEC_X, (f32)py,
                                (f32)(SPEC_X + SPEC_W), (f32)py);

        /* Label to the left of the axis, vertically centred on the line. */
        i32 llen = (i32)SDL_strlen(db_labels[t]);
        f32 lx   = (f32)(SPEC_X - llen * FONT_W - 2);
        f32 ly   = (f32)(py - FONT_H / 2);
        if (lx < 0.0f) { lx = 0.0f; }

        set_col(r, COL_TICK);
        (void)SDL_RenderDebugText(r, lx, ly, db_labels[t]);
    }

    /* Frequency tick marks + labels below the X axis */
    for (i32 t = 0; t < NUM_FREQ_TICKS; t++) {
        assert(t < NUM_FREQ_TICKS);
        i32 px = freq_to_px(freq_ticks[t]);
        if (px < 0) { continue; }

        /* Short vertical tick at the bottom of the plot */
        set_col(r, COL_AXIS);
        (void)SDL_RenderLine(r, (f32)px, (f32)(SPEC_Y + SPEC_H),
                                (f32)px, (f32)(SPEC_Y + SPEC_H + 3));

        /* Label centred below the tick */
        i32 llen = (i32)SDL_strlen(freq_labels[t]);
        f32 lx   = (f32)(px - llen * FONT_W / 2);
        f32 ly   = (f32)(SPEC_Y + SPEC_H + 4);

        set_col(r, COL_TICK);
        (void)SDL_RenderDebugText(r, lx, ly, freq_labels[t]);
    }
}

/*
 * Draw the spectrum line using the precomputed SDL_FPoint array.
 */
static void draw_spectrum_line(SDL_Renderer *r, const spectrum_t *spectrum)
{
    assert(r        != NULL);
    assert(spectrum != NULL);

    set_col(r, COL_SPEC);
    (void)SDL_RenderLines(r, spectrum->pts, SPEC_W);
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
    SDL_snprintf(buf, bufsz, "%d:%02d", m, s);
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
    i32 x_title = x_num   + 2 * FONT_W + 2;

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

    set_col(r, COL_TEXT);
    char title_trunc[TITLE_MAX_LEN];
    SDL_strlcpy(title_trunc, pl->entries[idx].title, sizeof(title_trunc));
    i32 max_chars = (PL_DEL_X - x_title - 44) / FONT_W;
    if (max_chars < 1) { max_chars = 1; }
    if ((i32)SDL_strlen(title_trunc) > max_chars) { title_trunc[max_chars] = '\0'; }
    (void)SDL_RenderDebugText(r, (f32)x_title, (f32)ty, title_trunc);

    if (pl->entries[idx].duration_sec > 0.0) {
        char tbuf[12];
        fmt_time(tbuf, sizeof(tbuf), pl->entries[idx].duration_sec);
        i32 tlen = (i32)SDL_strlen(tbuf);
        f32 tx   = (f32)(PL_DEL_X - tlen * FONT_W - 4);
        set_col(r, COL_DIM);
        (void)SDL_RenderDebugText(r, tx, (f32)ty, tbuf);
    }

    f32 dx = (f32)(PL_DEL_X + (PL_DEL_W - FONT_W) / 2);
    set_col(r, COL_DIM);
    (void)SDL_RenderDebugText(r, dx, (f32)ty, "x");
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
        assert(row < visible);
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

/* ── Public interface ────────────────────────────────────────────────────── */

void ui_draw(SDL_Renderer *r,
             const audio_t    *audio,
             const spectrum_t *spectrum,
             const playlist_t *playlist,
             ui_t             *ui)
{
    assert(r        != NULL);
    assert(audio    != NULL);
    assert(spectrum != NULL);
    assert(playlist != NULL);
    assert(ui       != NULL);

    (void)SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Clear to background colour */
    set_col(r, COL_BG);
    (void)SDL_RenderClear(r);

    draw_header(r, audio, playlist);
    draw_spectrum_axes(r);
    draw_spectrum_line(r, spectrum);
    draw_playlist(r, playlist, ui);
    draw_controls(r, audio, ui);

    (void)SDL_RenderPresent(r);
}

/* ── Input handlers ──────────────────────────────────────────────────────── */

/*
 * Check whether a logical-space mouse position is inside a button.
 * btn_idx: 0=rewind, 1=play-pause, 2=next  (left to right in control bar).
 */
static bool in_button(i32 btn_idx, f32 lx, f32 ly)
{
    i32 bx = 4 + btn_idx * (BTN_SZ + BTN_GAP);
    i32 by = CTRL_Y + 2;
    return (lx >= (f32)bx && lx < (f32)(bx + BTN_SZ) &&
            ly >= (f32)by && ly < (f32)(by + BTN_SZ));
}

static bool in_seek_bar(f32 lx, f32 ly)
{
    return (lx >= (f32)SEEK_X && lx < (f32)(SEEK_X + SEEK_W) &&
            ly >= (f32)SEEK_Y && ly < (f32)(SEEK_Y + SEEK_H));
}

static bool in_vol_slider(f32 lx, f32 ly)
{
    return (lx >= (f32)VOL_X && lx < (f32)(VOL_X + VOL_W) &&
            ly >= (f32)VOL_Y && ly < (f32)(VOL_Y + VOL_H));
}

static i32  pl_row_at(f32 lx, f32 ly, const playlist_t *pl);  /* forward */

static bool in_pl_sb(f32 lx, f32 ly)
{
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
    if (lx < (f32)PL_DEL_X || lx >= (f32)(PL_DEL_X + PL_DEL_W)) { return false; }
    return pl_row_at(lx, ly, pl) >= 0;
}

static bool in_add_button(f32 lx, f32 ly)
{
    return (lx >= (f32)PL_ADD_X && lx < (f32)(PL_ADD_X + PL_ADD_SZ) &&
            ly >= (f32)(PL_Y + 1) && ly < (f32)(PL_Y + 1 + PL_ADD_SZ));
}

/*
 * Return the playlist row index under (lx, ly), or -1 if none.
 */
static i32 pl_row_at(f32 lx, f32 ly, const playlist_t *pl)
{
    assert(pl != NULL);
    (void)lx;

    if (ly < (f32)PL_Y || ly >= (f32)(PL_Y + PL_H)) { return -1; }

    i32 row = (i32)(ly - (f32)PL_Y) / PL_ROW;
    i32 idx = pl->scroll + row;
    if (idx < 0 || idx >= pl->count) { return -1; }
    return idx;
}

ui_action_t ui_on_mouse_down(ui_t *ui, const SDL_MouseButtonEvent *e,
                              const audio_t *audio, playlist_t *pl)
{
    assert(ui    != NULL);
    assert(e     != NULL);
    assert(audio != NULL);
    assert(pl    != NULL);

    if (e->button != SDL_BUTTON_LEFT) { return UI_ACTION_NONE; }

    f32 lx = e->x;
    f32 ly = e->y;

    if (in_button(0, lx, ly)) { return UI_ACTION_REWIND;     }
    if (in_button(1, lx, ly)) { return UI_ACTION_PLAY_PAUSE; }
    if (in_button(2, lx, ly)) { return UI_ACTION_NEXT;       }

    if (in_add_button(lx, ly) && !ui->dialog_open) {
        return UI_ACTION_OPEN_DIALOG;
    }

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

    if (in_pl_sb(lx, ly)) {
        ui->sb_dragging = true;
        if (pl->count > 0) {
            pl->scroll = sb_pos_to_scroll(ly, pl->count);
        }
        return UI_ACTION_NONE;
    }

    if (in_pl_del(lx, ly, pl)) {
        ui->pl_hover = pl_row_at(lx, ly, pl);
        return UI_ACTION_PL_REMOVE;
    }

    i32 row = pl_row_at(lx, ly, pl);
    if (row >= 0) {
        ui->pl_hover = row;
        return UI_ACTION_PL_SELECT;
    }

    return UI_ACTION_NONE;
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

ui_action_t ui_on_key(const SDL_KeyboardEvent *e)
{
    assert(e != NULL);

    switch (e->scancode) {
    case SDL_SCANCODE_SPACE: return UI_ACTION_PLAY_PAUSE;
    case SDL_SCANCODE_LEFT:  return UI_ACTION_REWIND;
    case SDL_SCANCODE_RIGHT: return UI_ACTION_NEXT;
    default:                 break;
    }
    return UI_ACTION_NONE;
}

ui_action_t ui_on_scroll(ui_t *ui, f32 lx, f32 ly, f32 dy,
                          f32 current_seek_pos, playlist_t *pl)
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

    /* Scroll the playlist (positive dy = scroll up = earlier tracks). */
    if (ly >= (f32)PL_Y && ly < (f32)(PL_Y + PL_H)) {
        i32 vis        = PL_H / PL_ROW;
        i32 max_scroll = pl->count - vis;
        if (max_scroll > 0) {
            pl->scroll -= (i32)dy;
            if (pl->scroll < 0)          { pl->scroll = 0; }
            if (pl->scroll > max_scroll) { pl->scroll = max_scroll; }
        }
        return UI_ACTION_NONE;
    }

    return UI_ACTION_NONE;
}
