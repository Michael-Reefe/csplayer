/*
 * main.c — SDL3 application entry point for csplayer.
 *
 * Uses the SDL3 callback model (SDL_MAIN_USE_CALLBACKS).  The single global
 * app_t instance owns all module state; no heap allocation is used (NASA
 * Rule 3) except for FFTW plan creation in fft_init().
 *
 * Event flow:
 *   SDL_AppInit     → initialise all modules, load saved playlist.
 *   SDL_AppIterate  → capture PCM snapshot, run FFT, update spectrum, render.
 *   SDL_AppEvent    → translate UI events into module calls.
 *   SDL_AppQuit     → save playlist, tear down all modules.
 *
 * Custom user events:
 *   event_track_done   — pushed by audio_t's stopped callback when a track
 *                        reaches its natural end; handled in SDL_AppEvent.
 *   event_files_added  — pushed by the file-dialog callback after the user
 *                        selects files; the paths are in s_dialog_buf.
 *
 * Copyright (C) 2026 csplayer contributors.  Licensed under GPL-3.0.
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <assert.h>

#include "types.h"
#include "audio.h"
#include "fft.h"
#include "spectrum.h"
#include "playlist.h"
#include "ui.h"

/* ── Application state ───────────────────────────────────────────────────── */

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;

    /* Custom SDL event types (registered in SDL_AppInit). */
    u32 event_track_done;
    u32 event_files_added;

    audio_t    audio;
    fft_t      fft;
    spectrum_t spectrum;
    playlist_t playlist;
    ui_t       ui;

    /* Scratch buffer for the PCM snapshot passed to fft_process. */
    f32 pcm_snapshot[RING_SIZE];
} app_t;

/* Single global instance — zero-initialised in .bss (NASA Rule 3). */
static app_t g_app;

/* ── File-dialog staging buffer ──────────────────────────────────────────── */
/*
 * The SDL file-dialog callback fires on an arbitrary thread.  It copies the
 * selected paths into this static buffer, then pushes event_files_added so
 * the main thread picks them up.  Access is safe because:
 *   • Only one dialog can be open at a time (enforced by ui.dialog_open).
 *   • SDL_PushEvent provides a happens-before barrier between the write and
 *     the subsequent read in SDL_AppEvent.
 */
static char  s_dialog_buf[PLAYLIST_MAX][PATH_MAX_LEN];
static i32   s_dialog_count;

/* ── File-dialog callback ─────────────────────────────────────────────────── */

/*
 * file_dialog_cb — invoked on an unspecified thread when the user dismisses
 * the open-file dialog.  Copies selected paths, then signals the main thread.
 *
 * NOTE: Documented exception to NASA Rule 9 (function pointer required by
 * the SDL3 file-dialog API).
 */
static void file_dialog_cb(void *userdata, const char *const *filelist,
                            i32 filter)
{
    (void)filter;

    app_t *app = (app_t *)userdata;
    assert(app != NULL);

    s_dialog_count = 0;

    if (filelist != NULL) {
        for (i32 i = 0; filelist[i] != NULL && s_dialog_count < PLAYLIST_MAX; i++) {
            assert(i < PLAYLIST_MAX * 2);
            SDL_strlcpy(s_dialog_buf[s_dialog_count], filelist[i],
                        (size_t)PATH_MAX_LEN);
            s_dialog_count++;
        }
    }

    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = app->event_files_added;
    (void)SDL_PushEvent(&ev);

    /* Clear the dialog-open flag (now safe from this thread via SDL event). */
    app->ui.dialog_open = false;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Elapsed playback time in seconds for the current track. */
static f64 audio_elapsed_sec(const audio_t *audio)
{
    assert(audio != NULL);
    if (audio->duration_frames <= 0 || audio->sample_rate <= 0.0f) { return 0.0; }
    return (f64)audio_get_pos(audio)
           * (f64)audio->duration_frames / (f64)audio->sample_rate;
}

/* Convert a volume slider position [0,1] (linear in dB) to a linear gain. */
static f32 vol_pos_to_gain(f32 pos)
{
    assert(pos >= 0.0f && pos <= 1.0f);
    f32 db = VOL_DB_MIN * (1.0f - pos);   /* 0 → VOL_DB_MIN dB, 1 → 0 dB */
    return powf(10.0f, db / 20.0f);
}

/*
 * Load the current playlist track into the audio engine (without playing).
 * Also rebuilds the spectrum LUT in case the new file has a different rate.
 */
static void load_current_track(app_t *app)
{
    assert(app != NULL);

    const char *path = playlist_current_path(&app->playlist);
    if (path == NULL) { return; }

    if (!audio_load(&app->audio, path)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "load_current_track: audio_load failed for '%s'", path);
        return;
    }

    /* Update spectrum LUT for the new file's sample rate. */
    spectrum_init_lut(&app->spectrum, app->audio.sample_rate);

    /* Store duration back into the playlist entry for the UI. */
    if (app->playlist.current >= 0) {
        f32 sr = app->audio.sample_rate;
        i64 df = app->audio.duration_frames;
        if (sr > 0.0f && df > 0) {
            app->playlist.entries[app->playlist.current].duration_sec =
                (f64)df / (f64)sr;
        }
    }
}

/* ── SDL3 callback: SDL_AppInit ──────────────────────────────────────────── */

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Point SDL3 at our static instance. */
    *appstate = &g_app;

    SDL_SetAppMetadata(CSPLAYER_APP_NAME, CSPLAYER_APP_VERSION,
                       CSPLAYER_APP_ID);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_AppInit: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* Register two consecutive custom event types. */
    u32 first_ev = SDL_RegisterEvents(2);
    if (first_ev == (u32)-1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_AppInit: SDL_RegisterEvents failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    g_app.event_track_done  = first_ev;
    g_app.event_files_added = first_ev + 1;

    /* Create window and renderer. */
    if (!SDL_CreateWindowAndRenderer(CSPLAYER_APP_NAME, PHYS_W, PHYS_H,
                                     SDL_WINDOW_RESIZABLE,
                                     &g_app.window, &g_app.renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_AppInit: SDL_CreateWindowAndRenderer failed: %s",
                     SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* Set logical (pixel-art) presentation. */
    SDL_SetRenderLogicalPresentation(g_app.renderer, LOG_W, LOG_H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    /* Lock the window to a 16:9 aspect ratio and set minimum size to 1× logical. */
    SDL_SetWindowAspectRatio(g_app.window, 16.0f / 9.0f, 16.0f / 9.0f);
    SDL_SetWindowMinimumSize(g_app.window, LOG_W, LOG_H);

    /* Initialise modules in dependency order. */
    if (!audio_init(&g_app.audio))     { return SDL_APP_FAILURE; }
    /* Must be set after audio_init (which zeroes the struct). */
    g_app.audio.event_track_done = g_app.event_track_done;

    if (!fft_init  (&g_app.fft))       { return SDL_APP_FAILURE; }

    spectrum_init_lut(&g_app.spectrum, 44100.0f);

    if (!playlist_init(&g_app.playlist)) { return SDL_APP_FAILURE; }
    if (!playlist_load(&g_app.playlist)) { return SDL_APP_FAILURE; }

    /* UI state starts zeroed (from .bss). */
    g_app.ui.pl_hover = -1;
    g_app.ui.vol_pos  = 1.0f;   /* start at full volume (0 dB) */

    /* If the saved playlist has a current track, load it (no auto-play). */
    if (g_app.playlist.current >= 0) {
        load_current_track(&g_app);
    }

    return SDL_APP_CONTINUE;
}

/* ── SDL3 callback: SDL_AppIterate ───────────────────────────────────────── */

SDL_AppResult SDL_AppIterate(void *appstate)
{
    app_t *app = (app_t *)appstate;
    assert(app != NULL);

    /* Capture latest PCM snapshot, run FFT, update display data. */
    audio_snapshot_ring(&app->audio, app->pcm_snapshot);
    fft_process  (&app->fft,      app->pcm_snapshot);
    spectrum_update(&app->spectrum, &app->fft);

    /* Render the frame. */
    ui_draw(app->renderer, &app->audio, &app->spectrum,
            &app->playlist, &app->ui);

    return SDL_APP_CONTINUE;
}

/* ── SDL3 callback: SDL_AppEvent ─────────────────────────────────────────── */

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    app_t *app = (app_t *)appstate;
    assert(app   != NULL);
    assert(event != NULL);

    /* ── Quit ── */
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }

    /* ── Track finished naturally ── */
    if (event->type == app->event_track_done) {
        i32 next = playlist_next(&app->playlist);
        if (next >= 0) {
            load_current_track(app);
            (void)audio_play(&app->audio);
        }
        return SDL_APP_CONTINUE;
    }

    /* ── Files selected via dialog ── */
    if (event->type == app->event_files_added) {
        if (s_dialog_count > 0) {
            /* Build a NULL-terminated pointer array from the staging buffer. */
            const char *ptrs[PLAYLIST_MAX + 1];
            i32 limit = s_dialog_count;
            assert(limit <= PLAYLIST_MAX);
            for (i32 i = 0; i < limit; i++) {
                ptrs[i] = s_dialog_buf[i];
            }
            ptrs[limit] = NULL;

            bool had_tracks = (app->playlist.count > 0);
            (void)playlist_add_paths(&app->playlist, ptrs);

            /* If the playlist was empty before, load the first new track. */
            if (!had_tracks && app->playlist.count > 0) {
                load_current_track(app);
            }
            (void)playlist_save(&app->playlist);
        }
        return SDL_APP_CONTINUE;
    }

    /* ── Mouse events ── */
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        /* SDL3 does not translate mouse coordinates for logical presentation;
         * convert physical window coords → logical renderer coords here. */
        SDL_MouseButtonEvent btn = event->button;
        SDL_RenderCoordinatesFromWindow(g_app.renderer, btn.x, btn.y,
                                        &btn.x, &btn.y);
        ui_action_t action = ui_on_mouse_down(&app->ui, &btn,
                                               &app->audio, &app->playlist);
        switch (action) {
        case UI_ACTION_PLAY_PAUSE:
            if (app->audio.status == AUDIO_STATUS_STOPPED) {
                (void)audio_play(&app->audio);
            } else {
                (void)audio_toggle_pause(&app->audio);
            }
            break;
        case UI_ACTION_REWIND:
            if (audio_elapsed_sec(&app->audio) < 1.0 &&
                    playlist_prev(&app->playlist) >= 0) {
                load_current_track(app);
                (void)audio_play(&app->audio);
            } else {
                (void)audio_seek(&app->audio, 0.0f);
            }
            break;
        case UI_ACTION_NEXT:
            (void)playlist_next(&app->playlist);
            load_current_track(app);
            (void)audio_play(&app->audio);
            break;
        case UI_ACTION_SEEK:
            (void)audio_seek(&app->audio, app->ui.scrub_pos);
            break;
        case UI_ACTION_SET_VOLUME:
            audio_set_volume(&app->audio, vol_pos_to_gain(app->ui.vol_pos));
            break;
        case UI_ACTION_OPEN_DIALOG: {
            app->ui.dialog_open = true;
            static const SDL_DialogFileFilter filters[] = {
                { "Audio files", "wav;mp3;flac;ogg;opus" },
                { "WAV",  "wav"  },
                { "MP3",  "mp3"  },
                { "FLAC", "flac" },
            };
            SDL_ShowOpenFileDialog(file_dialog_cb, app, app->window,
                                   filters, 4, NULL, true);
            break;
        }
        case UI_ACTION_PL_SELECT:
            (void)playlist_set_current(&app->playlist, app->ui.pl_hover);
            load_current_track(app);
            break;
        case UI_ACTION_PL_REMOVE: {
            bool was_current = (app->ui.pl_hover == app->playlist.current);
            playlist_remove(&app->playlist, app->ui.pl_hover);
            if (was_current) {
                if (app->playlist.current >= 0) {
                    load_current_track(app);
                } else {
                    audio_unload(&app->audio);
                }
            }
            (void)playlist_save(&app->playlist);
            break;
        }
        default:
            break;
        }
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        bool held = (event->motion.state & SDL_BUTTON_LMASK) != 0;
        SDL_MouseMotionEvent mot = event->motion;
        SDL_RenderCoordinatesFromWindow(g_app.renderer, mot.x, mot.y,
                                        &mot.x, &mot.y);
        ui_action_t action = ui_on_mouse_motion(&app->ui, &mot, held);
        if (action == UI_ACTION_SEEK) {
            (void)audio_seek(&app->audio, app->ui.scrub_pos);
        } else if (action == UI_ACTION_SET_VOLUME) {
            audio_set_volume(&app->audio, vol_pos_to_gain(app->ui.vol_pos));
        }
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        SDL_MouseButtonEvent btn = event->button;
        SDL_RenderCoordinatesFromWindow(g_app.renderer, btn.x, btn.y,
                                        &btn.x, &btn.y);
        ui_action_t action = ui_on_mouse_up(&app->ui, &btn);
        if (action == UI_ACTION_SEEK) {
            (void)audio_seek(&app->audio, app->ui.scrub_pos);
        } else if (action == UI_ACTION_SET_VOLUME) {
            audio_set_volume(&app->audio, vol_pos_to_gain(app->ui.vol_pos));
        }
        return SDL_APP_CONTINUE;
    }

    /* ── Keyboard events ── */
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        ui_action_t action = ui_on_key(&event->key);
        switch (action) {
        case UI_ACTION_PLAY_PAUSE:
            if (app->audio.status == AUDIO_STATUS_STOPPED) {
                (void)audio_play(&app->audio);
            } else {
                (void)audio_toggle_pause(&app->audio);
            }
            break;
        case UI_ACTION_REWIND:
            if (audio_elapsed_sec(&app->audio) < 1.0 &&
                    playlist_prev(&app->playlist) >= 0) {
                load_current_track(app);
                (void)audio_play(&app->audio);
            } else {
                (void)audio_seek(&app->audio, 0.0f);
            }
            break;
        case UI_ACTION_NEXT:
            (void)playlist_next(&app->playlist);
            load_current_track(app);
            (void)audio_play(&app->audio);
            break;
        default:
            break;
        }
    }

    /* ── Mouse wheel ── */
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        float lx, ly;
        SDL_RenderCoordinatesFromWindow(g_app.renderer,
                                        event->wheel.mouse_x, event->wheel.mouse_y,
                                        &lx, &ly);
        ui_action_t action = ui_on_scroll(&app->ui, lx, ly,
                                          event->wheel.y,
                                          audio_get_pos(&app->audio));
        if (action == UI_ACTION_SET_VOLUME) {
            audio_set_volume(&app->audio, vol_pos_to_gain(app->ui.vol_pos));
        } else if (action == UI_ACTION_SEEK) {
            (void)audio_seek(&app->audio, app->ui.scrub_pos);
        }
        return SDL_APP_CONTINUE;
    }

    return SDL_APP_CONTINUE;
}

/* ── SDL3 callback: SDL_AppQuit ──────────────────────────────────────────── */

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;

    app_t *app = (app_t *)appstate;
    if (app == NULL) { return; }

    (void)playlist_save(&app->playlist);
    audio_quit(&app->audio);
    fft_quit  (&app->fft);
    /* spectrum and playlist have no resources to free beyond what we own. */

    /* SDL destroys window and renderer automatically on SDL_Quit. */
}
