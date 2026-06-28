/*
 * ui.h — SDL3 rendering and input handling.
 *
 * The UI is laid out at a logical resolution of LOG_W × LOG_H (512 × 384).
 * SDL3 scales this to the physical window with integer scaling so the pixel-
 * art aesthetic is preserved at any window size.
 *
 * All text is rendered with SDL_RenderDebugText (built-in 8 × 8 pixel font).
 * No additional font library is required.
 *
 * Input events are translated into ui_action_t values that main.c dispatches
 * to the audio and playlist modules, keeping the UI layer state-free with
 * respect to playback logic.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_UI_H
#define CSPLAYER_UI_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "types.h"
#include "audio.h"
#include "spectrum.h"
#include "playlist.h"
#include "synth.h"

/* ── Action codes returned by input handlers ─────────────────────────────── */

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_PLAY_PAUSE,        /* toggle play / pause                       */
    UI_ACTION_REWIND,            /* seek to position 0                        */
    UI_ACTION_NEXT,              /* advance to next track                     */
    UI_ACTION_SEEK,              /* scrub — ui_t.scrub_pos is valid           */
    UI_ACTION_SET_VOLUME,        /* volume drag — ui_t.vol_pos is valid       */
    UI_ACTION_OPEN_DIALOG,       /* open the file picker dialog               */
    UI_ACTION_PL_SELECT,         /* click on a playlist row — ui_t.pl_hover  */
    UI_ACTION_PL_REMOVE,         /* delete button on a row  — ui_t.pl_hover  */
    /* Synth mode actions */
    UI_ACTION_MODE_TOGGLE,       /* header mode button — toggle PLAYER ↔ SYNTH */
    UI_ACTION_SYNTH_INST_UP,     /* instrument +1 (wrap)                      */
    UI_ACTION_SYNTH_INST_DOWN,   /* instrument -1 (wrap)                      */
    UI_ACTION_SYNTH_NOTE_UP,     /* MIDI note +1 (clamp at 127)               */
    UI_ACTION_SYNTH_NOTE_DOWN,   /* MIDI note -1 (clamp at 0)                 */
    UI_ACTION_SYNTH_INST_SET,    /* dropdown item selected — synth_dd_select   */
    UI_ACTION_SYNTH_NOTE_SET,    /* dropdown item selected — synth_dd_select   */
    /* Graph display mode */
    UI_ACTION_GRAPH_MODE,        /* graph_mode already updated by ui.c         */
    /* Band-decompose toggle */
    UI_ACTION_WAVE_DECOMPOSE,    /* wave_decompose toggled; already updated by ui.c */
    /* Window */
    UI_ACTION_FULLSCREEN,        /* toggle SDL fullscreen (F11)                */
} ui_action_t;

/* ── Module state ────────────────────────────────────────────────────────── */

typedef struct {
    /* Player mode */
    bool      scrubbing;    /* true while the mouse button is held on the seek bar  */
    f32       scrub_pos;    /* [0, 1] fraction; valid when scrubbing == true         */
    bool      vol_dragging; /* true while the mouse button is held on the vol slider */
    f32       vol_pos;      /* [0, 1] slider position; 1.0 = 0 dB (full volume)     */
    i32       pl_hover;     /* playlist row under the mouse cursor, -1 = none        */
    bool      sb_dragging;  /* true while dragging the playlist scrollbar            */
    bool      dialog_open;  /* true while a file-picker dialog is pending            */
    TTF_Font *font;         /* Unifont for Unicode titles; NULL = ASCII fallback     */
    /* Synth mode */
    i32       synth_inst;       /* displayed instrument [0, SYNTH_INSTRUMENTS) */
    i32       synth_note;       /* displayed note       [0, SYNTH_NOTES)       */
    bool      synth_playing;    /* true while HOLD button is held              */
    bool      synth_inst_open;  /* instrument dropdown visible                 */
    bool      synth_note_open;  /* note dropdown visible                       */
    i32       synth_dd_scroll;  /* first row visible in open dropdown          */
    i32       synth_dd_hover;   /* hovered dropdown row (-1 = none)            */
    i32       synth_dd_select;  /* item chosen by last INST_SET / NOTE_SET     */
    bool      synth_dd_sb_dragging; /* true while dragging the dropdown scrollbar  */
    /* Marquee scrolling for the header title (all zero = idle / not yet started). */
    f32    marquee_x;           /* pixels shifted left; 0 = start of text  */
    f32    marquee_dir;         /* +1.0 or -1.0 current scroll direction   */
    Uint64 marquee_pause_until; /* SDL_GetTicks() value when pause ends    */
    Uint64 marquee_last_ms;     /* SDL_GetTicks() of last animation update */
    i32    marquee_track;       /* pl->current when marquee state was set  */
    /* Header title texture cache — rebuilt only when the displayed text changes
     * so the render loop makes no dynamic allocations in the steady state (Rule 3).
     * Playlist row TTF is a bounded exception: max PL_H/PL_ROW alloc-free pairs
     * per frame, proportional to the visible row count. */
    SDL_Texture *hdr_title_tex;               /* pre-rendered title texture       */
    SDL_Texture *hdr_artist_tex;              /* pre-rendered " - Artist" texture */
    f32          hdr_title_w;                 /* logical px width of title        */
    f32          hdr_artist_w;                /* logical px width of artist part  */
    char         hdr_cache_title[TITLE_MAX_LEN];       /* title at last build     */
    char         hdr_cache_artist[TITLE_MAX_LEN + 4];  /* artist at last build    */
    /* Playlist row title texture cache — one slot per visible row.  Each slot's
     * texture is rebuilt only when the title shown in that slot changes (e.g. on
     * scroll), so the steady-state render loop does no per-row TTF rasterisation
     * or GPU texture allocation (mirrors the header cache; NASA Rule 3). */
    SDL_Texture *pl_row_tex[PL_H / PL_ROW];
    char         pl_row_text[PL_H / PL_ROW][TITLE_MAX_LEN];
    /* Graph display mode — orthogonal to APP_MODE_PLAYER/SYNTH, persists across
     * mode switches.  Initialised to GRAPH_MODE_FREQ (= 0) by SDL_memset. */
    graph_mode_t graph_mode;
    /* Frequency-band decompose toggle — splits the time-domain area into three
     * side-by-side sub-panels (bass / mids / highs).  Initialised false by SDL_memset.
     * No effect when graph_mode == GRAPH_MODE_FREQ. */
    bool wave_decompose;
    /* Sub-row remainder of mouse-wheel deltas for the discrete list/dropdown
     * scrollers.  A traditional wheel sends dy = ±1 per notch (→ one step), but a
     * trackpad sends many small fractional deltas; carrying the remainder here
     * lets slow trackpad gestures accumulate instead of truncating to zero.
     * Initialised to 0 by SDL_memset. */
    f32 scroll_accum;
} ui_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Render one full frame.  Called every SDL_AppIterate tick.
 *
 * When mode == APP_MODE_SYNTH the playlist panel is replaced by the synth
 * panel; otherwise the playlist is shown as normal.
 */
void ui_draw(SDL_Renderer       *r,
             const audio_t      *audio,
             const spectrum_t   *spectrum,
             const playlist_t   *playlist,
             ui_t               *ui,
             app_mode_t          mode,
             const synth_t      *synth);

/*
 * Handle a mouse-button-down event.
 * Returns the action triggered (UI_ACTION_NONE if none).
 */
ui_action_t ui_on_mouse_down(ui_t *ui, const SDL_MouseButtonEvent *e,
                              const audio_t *audio, playlist_t *pl,
                              app_mode_t mode);

/*
 * Handle a mouse-motion event (hover + seek-bar drag + scrollbar drag).
 * held: true when the left button is held down.
 * Returns UI_ACTION_SEEK while scrubbing (ui->scrub_pos is updated).
 */
ui_action_t ui_on_mouse_motion(ui_t *ui, const SDL_MouseMotionEvent *e,
                                bool held, playlist_t *pl);

/*
 * Handle a mouse-button-up event (end of seek-bar drag).
 * Returns UI_ACTION_SEEK once if a drag was in progress.
 */
ui_action_t ui_on_mouse_up(ui_t *ui, const SDL_MouseButtonEvent *e);

/*
 * Handle a key-down event.
 * Space   → UI_ACTION_PLAY_PAUSE
 * Left    → UI_ACTION_REWIND
 * Right   → UI_ACTION_NEXT
 * F11     → UI_ACTION_FULLSCREEN
 * S       → GRAPH_MODE_FREQ  (spec)
 * W       → GRAPH_MODE_TIME  (wave)
 * B       → GRAPH_MODE_BOTH  (both)
 * D       → toggle wave_decompose
 * Shift+P → APP_MODE_PLAYER
 * Shift+S → APP_MODE_SYNTH
 */
ui_action_t ui_on_key(ui_t *ui, const SDL_KeyboardEvent *e, app_mode_t mode);

/*
 * Handle a mouse-wheel event.  lx, ly are logical renderer coordinates.
 * dy > 0 means scrolled up (away from user), dy < 0 means down.
 * current_seek_pos is the current playback position [0, 1], used as the
 * baseline when scrolling over the seek bar.
 * Returns UI_ACTION_SET_VOLUME (ui->vol_pos updated) or UI_ACTION_SEEK
 * (ui->scrub_pos updated), or UI_ACTION_NONE.
 */
ui_action_t ui_on_scroll(ui_t *ui, f32 lx, f32 ly, f32 dy,
                          f32 current_seek_pos, playlist_t *pl,
                          app_mode_t mode);

#endif /* CSPLAYER_UI_H */
