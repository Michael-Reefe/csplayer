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
 * Copyright (C) 2026 csplayer contributors.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_UI_H
#define CSPLAYER_UI_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include "types.h"
#include "audio.h"
#include "spectrum.h"
#include "playlist.h"

/* ── Action codes returned by input handlers ─────────────────────────────── */

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_PLAY_PAUSE,     /* toggle play / pause                    */
    UI_ACTION_REWIND,         /* seek to position 0                     */
    UI_ACTION_NEXT,           /* advance to next track                  */
    UI_ACTION_SEEK,           /* scrub — ui_t.scrub_pos is valid        */
    UI_ACTION_SET_VOLUME,     /* volume drag — ui_t.vol_pos is valid    */
    UI_ACTION_OPEN_DIALOG,    /* open the file picker dialog            */
    UI_ACTION_PL_SELECT,      /* click on a playlist row — ui_t.pl_hover */
    UI_ACTION_PL_REMOVE,      /* delete button on a row  — ui_t.pl_hover */
} ui_action_t;

/* ── Module state ────────────────────────────────────────────────────────── */

typedef struct {
    bool  scrubbing;     /* true while the mouse button is held on the seek bar  */
    f32   scrub_pos;     /* [0, 1] fraction; valid when scrubbing == true         */
    bool  vol_dragging;  /* true while the mouse button is held on the vol slider */
    f32   vol_pos;       /* [0, 1] slider position; 1.0 = 0 dB (full volume)     */
    i32   pl_hover;      /* playlist row under the mouse cursor, -1 = none        */
    bool  dialog_open;   /* true while a file-picker dialog is pending            */
} ui_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Render one full frame.  Called every SDL_AppIterate tick.
 *
 * The function draws (in order):
 *   1. Background
 *   2. Header bar (title + time)
 *   3. Spectrum axes (dB grid lines, tick labels, frequency labels)
 *   4. Spectrum line
 *   5. Playlist panel
 *   6. Control bar (buttons + seek bar)
 */
void ui_draw(SDL_Renderer       *r,
             const audio_t      *audio,
             const spectrum_t   *spectrum,
             const playlist_t   *playlist,
             ui_t               *ui);

/*
 * Handle a mouse-button-down event.
 * Returns the action triggered (UI_ACTION_NONE if none).
 */
ui_action_t ui_on_mouse_down(ui_t *ui, const SDL_MouseButtonEvent *e,
                              const audio_t *audio, const playlist_t *pl);

/*
 * Handle a mouse-motion event (hover + seek-bar drag).
 * held: true when the left button is held down.
 * Returns UI_ACTION_SEEK while dragging (ui->scrub_pos is updated).
 */
ui_action_t ui_on_mouse_motion(ui_t *ui, const SDL_MouseMotionEvent *e,
                                bool held);

/*
 * Handle a mouse-button-up event (end of seek-bar drag).
 * Returns UI_ACTION_SEEK once if a drag was in progress.
 */
ui_action_t ui_on_mouse_up(ui_t *ui, const SDL_MouseButtonEvent *e);

/*
 * Handle a key-down event.
 * Space → UI_ACTION_PLAY_PAUSE
 * Left  → UI_ACTION_REWIND
 * Right → UI_ACTION_NEXT
 */
ui_action_t ui_on_key(const SDL_KeyboardEvent *e);

/*
 * Handle a mouse-wheel event.  lx, ly are logical renderer coordinates.
 * dy > 0 means scrolled up (away from user), dy < 0 means down.
 * current_seek_pos is the current playback position [0, 1], used as the
 * baseline when scrolling over the seek bar.
 * Returns UI_ACTION_SET_VOLUME (ui->vol_pos updated) or UI_ACTION_SEEK
 * (ui->scrub_pos updated), or UI_ACTION_NONE.
 */
ui_action_t ui_on_scroll(ui_t *ui, float lx, float ly, float dy,
                          f32 current_seek_pos);

#endif /* CSPLAYER_UI_H */
