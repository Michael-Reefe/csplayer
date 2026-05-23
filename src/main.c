/*
 * main.c — SDL3 application entry point for csplayer.
 *
 * Uses the SDL3 callback model (SDL_MAIN_USE_CALLBACKS).  The single global
 * app_t instance owns all module state; no heap allocation is used (NASA
 * Rule 3).
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
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>
#include <assert.h>

#include "types.h"
#include "audio.h"
#include "fft.h"
#include "spectrum.h"
#include "playlist.h"
#include "ui.h"
#include "synth.h"

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
    app_mode_t mode;
    synth_t    synth;

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
    assert(app->event_files_added != 0);

    s_dialog_count = 0;

    if (filelist != NULL) {
        for (i32 i = 0; filelist[i] != NULL && s_dialog_count < PLAYLIST_MAX; i++) {
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
    assert(audio->sample_rate >= 0.0f);
    if (audio->duration_frames <= 0 || audio->sample_rate <= 0.0f) { return 0.0; }
    return (f64)audio_get_pos(audio)
           * (f64)audio->duration_frames / (f64)audio->sample_rate;
}

/* Convert a volume slider position [0,1] (linear in dB) to a linear gain. */
static f32 vol_pos_to_gain(f32 pos)
{
    assert(pos >= 0.0f);
    assert(pos <= 1.0f);
    f32 db = VOL_DB_MIN * (1.0f - pos);   /* 0 → VOL_DB_MIN dB, 1 → 0 dB */
    return powf(10.0f, db / 20.0f);
}

/*
 * Open the Unifont TTF file and store it in ui->font.  Non-fatal: if TTF
 * cannot be initialised or the font file is missing, font stays NULL and
 * render_utf8 falls back to SDL_RenderDebugText.
 */
static void init_font(ui_t *ui)
{
    assert(ui        != NULL);
    assert(ui->font  == NULL);

    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "init_font: TTF_Init failed: %s — "
                     "Unicode titles will fall back to ASCII.", SDL_GetError());
        return;
    }
    const char *base = SDL_GetBasePath();
    if (base == NULL) { return; }

    char font_path[PATH_MAX_LEN];
    (void)SDL_snprintf(font_path, sizeof(font_path), "%sunifont.otf", base);
    ui->font = TTF_OpenFont(font_path, 8.0f * (f32)DISPLAY_SCALE);
    if (ui->font == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "init_font: TTF_OpenFont('%s') failed: %s — "
                     "Unicode titles will fall back to ASCII.",
                     font_path, SDL_GetError());
    }
}

/*
 * Load the current playlist track into the audio engine (without playing).
 * Also rebuilds the spectrum LUT in case the new file has a different rate.
 */
static void load_current_track(app_t *app)
{
    assert(app != NULL);
    assert(app->playlist.count <= PLAYLIST_MAX);

    const char *path = playlist_current_path(&app->playlist);
    if (path == NULL) { return; }

    if (!audio_load(&app->audio, path)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "load_current_track: audio_load failed for '%s'", path);
        return;
    }

    /* Update spectrum LUT for the new file's sample rate. */
    spectrum_init_lut(&app->spectrum, app->audio.sample_rate);

    /* Store duration back into the playlist entry for the UI.
     * If this is the first time we know the duration, persist it so it appears
     * immediately on the next launch without reloading the file. */
    if (app->playlist.current >= 0) {
        f32 sr = app->audio.sample_rate;
        i64 df = app->audio.duration_frames;
        if (sr > 0.0f && df > 0) {
            bool was_unknown =
                (app->playlist.entries[app->playlist.current].duration_sec <= 0.0);
            app->playlist.entries[app->playlist.current].duration_sec =
                (f64)df / (f64)sr;
            if (was_unknown) {
                (void)playlist_save(&app->playlist);
            }
        }
    }
}

/* Rebuild the synth buffer and restart playback if synth was already playing. */
static void synth_rebuild_if_playing(app_t *app)
{
    assert(app != NULL);
    assert(app->synth.buf_frames <= SYNTH_MAX_FRAMES);
    if (!app->ui.synth_playing) { return; }
    (void)audio_stop(&app->audio);
    synth_build(&app->synth);
    if (audio_load_raw(&app->audio, app->synth.buf,
                       app->synth.buf_frames, (f32)SYNTH_SAMPLE_RATE)) {
        (void)audio_play_looped(&app->audio);
    }
}

/* ── SDL3 callback: SDL_AppInit ──────────────────────────────────────────── */

static SDL_AppResult app_create_window(app_t *app)
{
    assert(app != NULL);
    assert(LOG_W > 0 && LOG_H > 0);

    u32 first_ev = SDL_RegisterEvents(2);
    if (first_ev == (u32)-1) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "app_create_window: SDL_RegisterEvents failed: %s",
                     SDL_GetError());
        return SDL_APP_FAILURE;
    }
    app->event_track_done  = first_ev;
    app->event_files_added = first_ev + 1;

    if (!SDL_CreateWindowAndRenderer(CSPLAYER_APP_NAME, PHYS_W, PHYS_H,
                                     SDL_WINDOW_RESIZABLE,
                                     &app->window, &app->renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "app_create_window: SDL_CreateWindowAndRenderer failed: %s",
                     SDL_GetError());
        return SDL_APP_FAILURE;
    }
    (void)SDL_SetRenderLogicalPresentation(app->renderer, LOG_W, LOG_H,
                                           SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    (void)SDL_SetWindowAspectRatio(app->window, 16.0f / 9.0f, 16.0f / 9.0f);
    (void)SDL_SetWindowMinimumSize(app->window, LOG_W, LOG_H);
    return SDL_APP_CONTINUE;
}

static SDL_AppResult app_init_modules(app_t *app)
{
    assert(app != NULL);
    assert(app->renderer != NULL);

    if (!audio_init(&app->audio))       { return SDL_APP_FAILURE; }
    app->audio.event_track_done = app->event_track_done;
    if (!fft_init(&app->fft))           { return SDL_APP_FAILURE; }
    spectrum_init_lut(&app->spectrum, 44100.0f);
    if (!playlist_init(&app->playlist)) { return SDL_APP_FAILURE; }
    if (!playlist_load(&app->playlist)) { return SDL_APP_FAILURE; }
    init_font(&app->ui);

    {
        const char *base = SDL_GetBasePath();
        char sf2_path[PATH_MAX_LEN];
        if (base != NULL) {
            (void)SDL_snprintf(sf2_path, sizeof(sf2_path), "%sgm.sf2", base);
        } else {
            (void)SDL_strlcpy(sf2_path, "gm.sf2", sizeof(sf2_path));
        }
        (void)synth_init(&app->synth, sf2_path);
    }

    app->ui.pl_hover       = -1;
    app->ui.vol_pos        = 1.0f;
    app->ui.synth_inst     = app->synth.inst;
    app->ui.synth_note     = app->synth.note;
    app->ui.synth_dd_hover = -1;

    if (app->playlist.current >= 0) { load_current_track(app); }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    assert(appstate != NULL);
    assert(PHYS_W > 0 && PHYS_H > 0);
    *appstate = &g_app;
    (void)SDL_SetAppMetadata(CSPLAYER_APP_NAME, CSPLAYER_APP_VERSION,
                             CSPLAYER_APP_ID);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_AppInit: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_AppResult r = app_create_window(&g_app);
    if (r != SDL_APP_CONTINUE) { return r; }
    return app_init_modules(&g_app);
}

/* ── SDL3 callback: SDL_AppIterate ───────────────────────────────────────── */

static void update_spectrum(app_t *app)
{
    assert(app != NULL);
    assert(app->audio.sample_rate >= 0.0f);

    audio_snapshot_ring(&app->audio, app->pcm_snapshot);
    fft_process(&app->fft, app->pcm_snapshot);
    spectrum_update(&app->spectrum, &app->fft);

    f32 sr = (app->audio.sample_rate > 0.0f)
                 ? app->audio.sample_rate : (f32)SYNTH_SAMPLE_RATE;
    i32 th = (app->ui.graph_mode == GRAPH_MODE_BOTH) ? SPEC_H / 2 : SPEC_H;
    i32 ty = (app->ui.graph_mode == GRAPH_MODE_BOTH) ? SPEC_Y + th : SPEC_Y;
    spectrum_update_time(&app->spectrum, app->pcm_snapshot, RING_SIZE, sr, ty, th);

    if (app->ui.graph_mode == GRAPH_MODE_BOTH) {
        spectrum_map_freq_pts(&app->spectrum, SPEC_Y, SPEC_H / 2);
    }
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    app_t *app = (app_t *)appstate;
    assert(app != NULL);
    assert(app->renderer != NULL);

    update_spectrum(app);

    ui_draw(app->renderer, &app->audio, &app->spectrum,
            &app->playlist, &app->ui, app->mode, &app->synth);

    return SDL_APP_CONTINUE;
}

/* ── Event sub-handlers ──────────────────────────────────────────────────── */

static void dispatch_play_pause(app_t *app)
{
    assert(app != NULL);
    assert(app->audio.sample_rate > 0.0f);
    if (app->mode == APP_MODE_SYNTH) {
        if (app->ui.synth_playing) {
            (void)audio_stop(&app->audio);
            app->ui.synth_playing = false;
        } else {
            (void)audio_stop(&app->audio);
            if (app->synth.dirty) { synth_build(&app->synth); }
            if (audio_load_raw(&app->audio, app->synth.buf,
                               app->synth.buf_frames, (f32)SYNTH_SAMPLE_RATE) &&
                audio_play_looped(&app->audio)) {
                app->ui.synth_playing = true;
            }
        }
    } else {
        if (app->audio.status == AUDIO_STATUS_STOPPED) {
            (void)audio_play(&app->audio);
        } else {
            (void)audio_toggle_pause(&app->audio);
        }
    }
}

static void dispatch_rewind(app_t *app)
{
    assert(app != NULL);
    assert(app->audio.sample_rate > 0.0f);
    if (app->mode == APP_MODE_SYNTH) {
        (void)audio_seek(&app->audio, 0.0f);
    } else if (audio_elapsed_sec(&app->audio) < 1.0 &&
               playlist_prev(&app->playlist) >= 0) {
        load_current_track(app);
        (void)audio_play(&app->audio);
    } else {
        (void)audio_seek(&app->audio, 0.0f);
    }
}

static void dispatch_open_dialog(app_t *app)
{
    assert(app != NULL);
    assert(app->window != NULL);
    app->ui.dialog_open = true;
    static const SDL_DialogFileFilter filters[] = {
        { "Audio files", "wav;mp3;flac;ogg;opus" },
        { "WAV",  "wav"  },
        { "MP3",  "mp3"  },
        { "FLAC", "flac" },
        { "OGG",  "ogg"  },
        { "OPUS", "opus" },
    };
    SDL_ShowOpenFileDialog(file_dialog_cb, app, app->window,
                           filters, 4, NULL, true);
}

static void dispatch_pl_remove(app_t *app)
{
    assert(app != NULL);
    assert(app->ui.pl_hover >= 0 && app->ui.pl_hover < app->playlist.count);
    bool was_current = (app->ui.pl_hover == app->playlist.current);
    (void)playlist_remove(&app->playlist, app->ui.pl_hover);
    if (was_current) {
        if (app->playlist.current >= 0) { load_current_track(app); }
        else                            { audio_unload(&app->audio); }
    }
    (void)playlist_save(&app->playlist);
}

static void dispatch_mode_toggle(app_t *app)
{
    assert(app != NULL);
    assert(app->mode == APP_MODE_PLAYER || app->mode == APP_MODE_SYNTH);
    bool was_synth = (app->mode == APP_MODE_SYNTH);
    (void)audio_stop(&app->audio);
    audio_unload(&app->audio);
    app->mode = was_synth ? APP_MODE_PLAYER : APP_MODE_SYNTH;
    app->ui.synth_playing = false;
    if (was_synth && app->playlist.current >= 0) { load_current_track(app); }
}

static void dispatch_synth_inst(app_t *app, i32 new_inst)
{
    assert(app != NULL);
    assert(new_inst >= 0 && new_inst < SYNTH_INSTRUMENTS);
    app->synth.inst    = new_inst;
    app->synth.dirty   = true;
    app->ui.synth_inst = new_inst;
    synth_rebuild_if_playing(app);
}

static void dispatch_synth_note(app_t *app, i32 new_note)
{
    assert(app != NULL);
    assert(new_note >= 0 && new_note < SYNTH_NOTES);
    app->synth.note    = new_note;
    app->synth.dirty   = true;
    app->ui.synth_note = new_note;
    synth_rebuild_if_playing(app);
}

/* Dispatch a ui_action_t returned by any input handler to audio/playlist. */
static void dispatch_action(app_t *app, ui_action_t action)
{
    assert(app != NULL);
    assert(app->playlist.count <= PLAYLIST_MAX);

    switch (action) {
    case UI_ACTION_PLAY_PAUSE:  dispatch_play_pause(app);  break;
    case UI_ACTION_REWIND:      dispatch_rewind(app);      break;
    case UI_ACTION_NEXT:
        if (app->mode != APP_MODE_SYNTH) {
            (void)playlist_next(&app->playlist);
            load_current_track(app);
            (void)audio_play(&app->audio);
        } else {
            (void)audio_seek(&app->audio, 0.0f);
        }
        break;
    case UI_ACTION_SEEK:
        (void)audio_seek(&app->audio, app->ui.scrub_pos);
        break;
    case UI_ACTION_SET_VOLUME:
        audio_set_volume(&app->audio, vol_pos_to_gain(app->ui.vol_pos));
        break;
    case UI_ACTION_OPEN_DIALOG: dispatch_open_dialog(app); break;
    case UI_ACTION_PL_SELECT:
        (void)playlist_set_current(&app->playlist, app->ui.pl_hover);
        load_current_track(app);
        break;
    case UI_ACTION_PL_REMOVE:   dispatch_pl_remove(app);   break;
    case UI_ACTION_MODE_TOGGLE: dispatch_mode_toggle(app); break;
    case UI_ACTION_SYNTH_INST_UP:
        dispatch_synth_inst(app, (app->synth.inst + 1) % SYNTH_INSTRUMENTS);
        break;
    case UI_ACTION_SYNTH_INST_DOWN:
        dispatch_synth_inst(app,
            (app->synth.inst + SYNTH_INSTRUMENTS - 1) % SYNTH_INSTRUMENTS);
        break;
    case UI_ACTION_SYNTH_NOTE_UP:
        dispatch_synth_note(app,
            app->synth.note < SYNTH_NOTES - 1 ? app->synth.note + 1
                                               : app->synth.note);
        break;
    case UI_ACTION_SYNTH_NOTE_DOWN:
        dispatch_synth_note(app,
            app->synth.note > 0 ? app->synth.note - 1 : app->synth.note);
        break;
    case UI_ACTION_SYNTH_INST_SET:
        dispatch_synth_inst(app, app->ui.synth_dd_select);
        break;
    case UI_ACTION_SYNTH_NOTE_SET:
        dispatch_synth_note(app, app->ui.synth_dd_select);
        break;
    case UI_ACTION_GRAPH_MODE:
        /* graph_mode already updated by ui.c; no further state change needed */
        break;
    default:
        break;
    }
}

static void handle_track_done(app_t *app)
{
    assert(app != NULL);
    assert(app->event_track_done != 0);

    /* In synth mode the looped buffer never ends naturally; ignore stray events. */
    if (app->mode == APP_MODE_SYNTH) { return; }

    i32 next = playlist_next(&app->playlist);
    if (next >= 0) {
        load_current_track(app);
        (void)audio_play(&app->audio);
    }
}

static void handle_files_added(app_t *app)
{
    assert(app != NULL);
    assert(app->event_files_added != 0);

    if (s_dialog_count <= 0) { return; }

    const char *ptrs[PLAYLIST_MAX + 1];
    i32 limit = s_dialog_count;
    assert(limit <= PLAYLIST_MAX);
    for (i32 i = 0; i < limit; i++) { ptrs[i] = s_dialog_buf[i]; }
    ptrs[limit] = NULL;

    bool had_tracks = (app->playlist.count > 0);
    (void)playlist_add_paths(&app->playlist, ptrs);
    if (!had_tracks && app->playlist.count > 0) { load_current_track(app); }
    (void)playlist_save(&app->playlist);
}

static void handle_mouse_down(app_t *app, SDL_Event *ev)
{
    assert(app != NULL);
    assert(ev  != NULL);

    SDL_MouseButtonEvent btn = ev->button;
    (void)SDL_RenderCoordinatesFromWindow(g_app.renderer, btn.x, btn.y,
                                          &btn.x, &btn.y);
    ui_action_t action = ui_on_mouse_down(&app->ui, &btn,
                                           &app->audio, &app->playlist, app->mode);
    dispatch_action(app, action);
}

static void handle_mouse_motion(app_t *app, SDL_Event *ev)
{
    assert(app != NULL);
    assert(ev  != NULL);

    bool held = (ev->motion.state & SDL_BUTTON_LMASK) != 0;
    SDL_MouseMotionEvent mot = ev->motion;
    (void)SDL_RenderCoordinatesFromWindow(g_app.renderer, mot.x, mot.y,
                                          &mot.x, &mot.y);
    ui_action_t action = ui_on_mouse_motion(&app->ui, &mot, held, &app->playlist);
    if (action == UI_ACTION_SEEK) {
        (void)audio_seek(&app->audio, app->ui.scrub_pos);
    } else if (action == UI_ACTION_SET_VOLUME) {
        audio_set_volume(&app->audio, vol_pos_to_gain(app->ui.vol_pos));
    }
}

static void handle_mouse_up(app_t *app, SDL_Event *ev)
{
    assert(app != NULL);
    assert(ev  != NULL);

    SDL_MouseButtonEvent btn = ev->button;
    (void)SDL_RenderCoordinatesFromWindow(g_app.renderer, btn.x, btn.y,
                                          &btn.x, &btn.y);
    dispatch_action(app, ui_on_mouse_up(&app->ui, &btn));
}

static void handle_mouse_wheel(app_t *app, SDL_Event *ev)
{
    assert(app != NULL);
    assert(ev  != NULL);

    f32 lx, ly;
    (void)SDL_RenderCoordinatesFromWindow(g_app.renderer,
                                          ev->wheel.mouse_x, ev->wheel.mouse_y,
                                          &lx, &ly);
    dispatch_action(app, ui_on_scroll(&app->ui, lx, ly, ev->wheel.y,
                                      audio_get_pos(&app->audio), &app->playlist,
                                      app->mode));
}

/* ── SDL3 callback: SDL_AppEvent ─────────────────────────────────────────── */

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    app_t *app = (app_t *)appstate;
    assert(app   != NULL);
    assert(event != NULL);

    if (event->type == SDL_EVENT_QUIT)               { return SDL_APP_SUCCESS; }
    if (event->type == app->event_track_done)        { handle_track_done(app);          }
    if (event->type == app->event_files_added)       { handle_files_added(app);         }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)  { handle_mouse_down(app, event);   }
    if (event->type == SDL_EVENT_MOUSE_MOTION)       { handle_mouse_motion(app, event); }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)    { handle_mouse_up(app, event);     }
    if (event->type == SDL_EVENT_MOUSE_WHEEL)        { handle_mouse_wheel(app, event);  }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        dispatch_action(app, ui_on_key(&event->key));
    }

    return SDL_APP_CONTINUE;
}

/* ── SDL3 callback: SDL_AppQuit ──────────────────────────────────────────── */

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;

    app_t *app = (app_t *)appstate;
    assert(appstate == (void *)&g_app || appstate == NULL);
    if (app == NULL) { return; }

    (void)playlist_save(&app->playlist);
    synth_quit(&app->synth);
    audio_quit(&app->audio);
    fft_quit  (&app->fft);
    /* spectrum and playlist have no resources to free beyond what we own. */

    if (app->ui.hdr_title_tex  != NULL) { SDL_DestroyTexture(app->ui.hdr_title_tex);  }
    if (app->ui.hdr_artist_tex != NULL) { SDL_DestroyTexture(app->ui.hdr_artist_tex); }
    if (app->ui.font != NULL) {
        TTF_CloseFont(app->ui.font);
        app->ui.font = NULL;
    }
    TTF_Quit();

    /* SDL destroys window and renderer automatically on SDL_Quit. */
}
