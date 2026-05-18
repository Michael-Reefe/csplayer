/*
 * audio.c — SDL3_mixer playback engine and PCM ring buffer.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "audio.h"

#include <SDL3/SDL.h>
#include <assert.h>

#ifdef CSPLAYER_TESTING
#include "test_mocks.h"
#endif

/* ── Internal callbacks (file-scope, not exported) ───────────────────────── */

/*
 * raw_audio_cb — PCM capture callback (audio thread).
 *
 * Fires before gain/effects processing.  Down-mixes all channels to mono
 * and writes the result into the circular ring buffer.  Runs on the audio
 * thread; uses ring_mutex for synchronisation with the main thread.
 *
 * NOTE: This is a documented exception to NASA Rule 9 (function pointers).
 * SDL3_mixer requires a function pointer for MIX_SetTrackRawCallback.
 */
static void raw_audio_cb(void *userdata, MIX_Track *t,
                          const SDL_AudioSpec *spec, f32 *pcm, i32 samples)
{
    /* Suppress unused-parameter warning — t is provided by the API. */
    (void)t;

    audio_t *a  = (audio_t *)userdata;
    assert(a    != NULL);
    assert(spec != NULL);
    assert(pcm  != NULL);
    assert(samples >= 0);

    SDL_LockMutex(a->ring_mutex);

    a->sample_rate = (f32)spec->freq;
    a->channels    = (i32)spec->channels;

    i32 ch     = a->channels;
    i32 frames = samples / ch;

    /* Bound: frames can be at most RING_SIZE (SDL3_mixer buffer sizes are
     * typically 512–4096, well below RING_SIZE = FFT_SIZE = 8192). */
    assert(frames <= RING_SIZE);

    for (i32 i = 0; i < frames; i++) {
        f32 mono = 0.0f;
        for (i32 c = 0; c < ch; c++) {
            mono += pcm[i * ch + c];
        }
        a->ring[a->ring_write] = mono / (f32)ch;
        a->ring_write = (a->ring_write + 1) % RING_SIZE;
    }

    SDL_UnlockMutex(a->ring_mutex);
}

/*
 * track_stopped_cb — fires when the track reaches its natural end
 * (audio thread).  Pushes a user event to the main thread so it can
 * advance to the next track.
 *
 * NOTE: Documented exception to NASA Rule 9 (function pointer).
 */
static void track_stopped_cb(void *userdata, MIX_Track *t)
{
    (void)t;

    audio_t *a = (audio_t *)userdata;
    assert(a != NULL);
    assert(a->event_track_done == 0 || a->event_track_done >= SDL_EVENT_USER);

    a->status = AUDIO_STATUS_STOPPED;

    if (a->event_track_done == 0) {
        return;
    }

    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = a->event_track_done;
    (void)SDL_PushEvent(&ev);
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/*
 * Unload the current audio object without destroying the mixer or track.
 * Safe to call when audio is already NULL.
 */
static void unload_audio(audio_t *a)
{
    assert(a != NULL);
    assert(a->duration_frames >= -1);

    if (a->track != NULL) {
        /* Clear the stopped callback before stopping to prevent re-entrancy. */
        (void)MIX_SetTrackStoppedCallback(a->track, NULL, NULL);
        if (a->audio != NULL) {
            (void)MIX_StopTrack(a->track, 0);
            (void)MIX_SetTrackAudio(a->track, NULL);
        }
    }
    if (a->audio != NULL) {
        MIX_DestroyAudio(a->audio);
        a->audio = NULL;
    }
    a->status          = AUDIO_STATUS_STOPPED;
    a->duration_frames = -1;
    a->ring_write      = 0;

    /* Zero the ring buffer so the spectrum shows silence while loading. */
    SDL_memset(a->ring, 0, sizeof(a->ring));
}

/* ── Public interface ────────────────────────────────────────────────────── */

bool audio_init(audio_t *a)
{
    assert(a != NULL);
    assert(RING_SIZE > 0);

    SDL_memset(a, 0, sizeof(*a));
    a->status          = AUDIO_STATUS_STOPPED;
    a->duration_frames = -1;
    a->sample_rate     = 44100.0f;
    a->channels        = 2;

    if (!MIX_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_init: MIX_Init failed: %s", SDL_GetError());
        return false;
    }

    a->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (a->mixer == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_init: MIX_CreateMixerDevice failed: %s", SDL_GetError());
        MIX_Quit();
        return false;
    }

    a->track = MIX_CreateTrack(a->mixer);
    if (a->track == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_init: MIX_CreateTrack failed: %s", SDL_GetError());
        MIX_DestroyMixer(a->mixer);
        a->mixer = NULL;
        MIX_Quit();
        return false;
    }

    a->ring_mutex = SDL_CreateMutex();
    if (a->ring_mutex == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_init: SDL_CreateMutex failed: %s", SDL_GetError());
        MIX_DestroyTrack(a->track);
        a->track = NULL;
        MIX_DestroyMixer(a->mixer);
        a->mixer = NULL;
        MIX_Quit();
        return false;
    }

    return true;
}

bool audio_load(audio_t *a, const char *path)
{
    assert(a    != NULL);
    assert(path != NULL);
    assert(a->mixer != NULL);
    assert(a->track != NULL);

    unload_audio(a);

    a->audio = MIX_LoadAudio(a->mixer, path, false);
    if (a->audio == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load: MIX_LoadAudio('%s') failed: %s",
                     path, SDL_GetError());
        return false;
    }

    /* Query duration for seek/position calculations. */
    a->duration_frames = MIX_GetAudioDuration(a->audio);

    /* Get the audio's native sample rate for duration/seek calculations. */
    SDL_AudioSpec fmt;
    if (MIX_GetAudioFormat(a->audio, &fmt)) {
        a->sample_rate = (f32)fmt.freq;
        a->channels    = (i32)fmt.channels;
    }

    if (!MIX_SetTrackAudio(a->track, a->audio)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load: MIX_SetTrackAudio failed: %s", SDL_GetError());
        MIX_DestroyAudio(a->audio);
        a->audio = NULL;
        return false;
    }

    /* Register callbacks — documented exceptions to NASA Rule 9. */
    if (!MIX_SetTrackRawCallback(a->track, raw_audio_cb, a)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load: MIX_SetTrackRawCallback failed: %s", SDL_GetError());
    }
    if (!MIX_SetTrackStoppedCallback(a->track, track_stopped_cb, a)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load: MIX_SetTrackStoppedCallback failed: %s", SDL_GetError());
    }

    return true;
}

bool audio_play(audio_t *a)
{
    assert(a       != NULL);
    assert(a->track != NULL);

    if (a->audio == NULL) {
        return false;
    }
    if (a->status == AUDIO_STATUS_PAUSED) {
        if (!MIX_ResumeTrack(a->track)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "audio_play: MIX_ResumeTrack failed: %s", SDL_GetError());
            return false;
        }
        a->status = AUDIO_STATUS_PLAYING;
        return true;
    }

    SDL_PropertiesID opts = SDL_CreateProperties();
    if (opts == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_play: SDL_CreateProperties failed: %s", SDL_GetError());
        return false;
    }
    bool ok = MIX_PlayTrack(a->track, opts);
    SDL_DestroyProperties(opts);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_play: MIX_PlayTrack failed: %s", SDL_GetError());
        return false;
    }
    a->status = AUDIO_STATUS_PLAYING;
    return true;
}

bool audio_toggle_pause(audio_t *a)
{
    assert(a        != NULL);
    assert(a->track != NULL);

    if (a->status == AUDIO_STATUS_PLAYING) {
        if (!MIX_PauseTrack(a->track)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "audio_toggle_pause: MIX_PauseTrack failed: %s", SDL_GetError());
            return false;
        }
        a->status = AUDIO_STATUS_PAUSED;
        return true;
    }
    if (a->status == AUDIO_STATUS_PAUSED) {
        return audio_play(a);
    }
    return false;
}

bool audio_stop(audio_t *a)
{
    assert(a        != NULL);
    assert(a->track != NULL);

    if (!MIX_StopTrack(a->track, 0)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_stop: MIX_StopTrack failed: %s", SDL_GetError());
        return false;
    }
    a->status = AUDIO_STATUS_STOPPED;
    return true;
}

bool audio_seek(audio_t *a, f32 pos)
{
    assert(a        != NULL);
    assert(a->track != NULL);
    assert(pos >= 0.0f && pos <= 1.0f);

    if (a->audio == NULL || a->duration_frames <= 0) {
        return false;
    }

    i64 target = (i64)((f64)pos * (f64)a->duration_frames);
    if (target < 0) { target = 0; }
    if (target > a->duration_frames) { target = a->duration_frames; }

    if (!MIX_SetTrackPlaybackPosition(a->track, target)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_seek: MIX_SetTrackPlaybackPosition failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

f32 audio_get_pos(const audio_t *a)
{
    assert(a != NULL);
    assert(a->sample_rate >= 0.0f);

    if (a->track == NULL || a->duration_frames <= 0) {
        return 0.0f;
    }
    i64 cur = MIX_GetTrackPlaybackPosition(a->track);
    if (cur < 0) { return 0.0f; }

    f32 pos = (f32)((f64)cur / (f64)a->duration_frames);
    if (pos < 0.0f) { return 0.0f; }
    if (pos > 1.0f) { return 1.0f; }
    return pos;
}

void audio_snapshot_ring(audio_t *a, f32 *dst)
{
    assert(a   != NULL);
    assert(dst != NULL);

    SDL_LockMutex(a->ring_mutex);

    /* Copy RING_SIZE samples oldest-first by rotating around ring_write. */
    i32 write_at = a->ring_write;
    for (i32 i = 0; i < RING_SIZE; i++) {
        dst[i] = a->ring[(write_at + i) % RING_SIZE];
    }

    SDL_UnlockMutex(a->ring_mutex);
}

bool audio_load_raw(audio_t *a, const f32 *buf, i32 nframes, f32 sample_rate)
{
    assert(a           != NULL);
    assert(buf         != NULL);
    assert(nframes     > 0);
    assert(a->mixer    != NULL);
    assert(a->track    != NULL);

    unload_audio(a);

    SDL_AudioSpec spec;
    SDL_memset(&spec, 0, sizeof(spec));
    spec.format   = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq     = (i32)sample_rate;

    a->audio = MIX_LoadRawAudioNoCopy(a->mixer, buf, (size_t)nframes * sizeof(f32), &spec, false);
    if (a->audio == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_raw: MIX_LoadRawAudioNoCopy failed: %s",
                     SDL_GetError());
        return false;
    }

    a->duration_frames = nframes;
    a->sample_rate     = sample_rate;
    a->channels        = 1;

    if (!MIX_SetTrackAudio(a->track, a->audio)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_raw: MIX_SetTrackAudio failed: %s", SDL_GetError());
        MIX_DestroyAudio(a->audio);
        a->audio = NULL;
        return false;
    }

    if (!MIX_SetTrackRawCallback(a->track, raw_audio_cb, a)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_raw: MIX_SetTrackRawCallback failed: %s", SDL_GetError());
        MIX_DestroyAudio(a->audio);
        a->audio = NULL;
        return false;
    }

    return true;
}

bool audio_play_looped(audio_t *a)
{
    assert(a        != NULL);
    assert(a->track != NULL);
    assert(a->audio != NULL);

    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_play_looped: SDL_CreateProperties failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
    bool ok = MIX_PlayTrack(a->track, props);
    SDL_DestroyProperties(props);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_play_looped: MIX_PlayTrack failed: %s", SDL_GetError());
        return false;
    }
    a->status = AUDIO_STATUS_PLAYING;
    return true;
}

void audio_unload(audio_t *a)
{
    assert(a != NULL);
    assert(a->duration_frames >= -1);
    unload_audio(a);
}

void audio_set_volume(audio_t *a, f32 volume)
{
    assert(a != NULL);
    assert(volume >= 0.0f && volume <= 1.0f);
    if (a->mixer == NULL) { return; }
    (void)MIX_SetMixerGain(a->mixer, volume);
}

void audio_quit(audio_t *a)
{
    assert(a != NULL);
    assert(RING_SIZE > 0);

    if (a->ring_mutex != NULL) {
        SDL_DestroyMutex(a->ring_mutex);
        a->ring_mutex = NULL;
    }
    if (a->track != NULL) {
        (void)MIX_StopTrack(a->track, 0);
        MIX_DestroyTrack(a->track);
        a->track = NULL;
    }
    if (a->audio != NULL) {
        MIX_DestroyAudio(a->audio);
        a->audio = NULL;
    }
    if (a->mixer != NULL) {
        MIX_DestroyMixer(a->mixer);
        a->mixer = NULL;
        MIX_Quit();
    }
}
