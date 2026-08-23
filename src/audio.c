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
 * Down-mix interleaved PCM to mono and append it to a circular ring buffer.
 * Shared by the single-track and per-stem capture callbacks; the caller holds
 * ring_mutex and owns the write index.
 */
static void ring_write_mono(f32 *ring, i32 *write_at, const f32 *pcm,
                             i32 frames, i32 ch)
{
    assert(ring     != NULL);
    assert(write_at != NULL);
    assert(pcm      != NULL);
    assert(ch       >  0);
    /* Bound: frames can be at most RING_SIZE (SDL3_mixer buffer sizes are
     * typically 512–4096, well below RING_SIZE = FFT_SIZE = 8192). */
    assert(frames >= 0 && frames <= RING_SIZE);

    i32 w = *write_at;
    for (i32 i = 0; i < frames; i++) {
        f32 mono = 0.0f;
        for (i32 c = 0; c < ch; c++) {
            mono += pcm[i * ch + c];
        }
        ring[w] = mono / (f32)ch;
        w = (w + 1) % RING_SIZE;
    }
    *write_at = w;
}

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

    i32 ch = a->channels;
    ring_write_mono(a->ring, &a->ring_write, pcm, samples / ch, ch);

    SDL_UnlockMutex(a->ring_mutex);
}

/*
 * stem_audio_cb — per-stem PCM capture callback (audio thread).
 *
 * Fires before gain, so a stem muted with MIX_SetTrackGain(0) still delivers
 * PCM here and its waveform panel keeps animating — the user can see exactly
 * what they have removed from the mix.  userdata identifies which stem.
 *
 * NOTE: Documented exception to NASA Rule 9 (function pointer), like the
 * three callbacks above.
 */
static void stem_audio_cb(void *userdata, MIX_Track *t,
                           const SDL_AudioSpec *spec, f32 *pcm, i32 samples)
{
    (void)t;

    stem_cb_ud_t *ud = (stem_cb_ud_t *)userdata;
    assert(ud   != NULL);
    assert(spec != NULL);
    assert(pcm  != NULL);
    assert(samples >= 0);
    assert(ud->idx >= 0 && ud->idx < STEM_COUNT);

    audio_t *a = ud->a;
    assert(a != NULL);

    SDL_LockMutex(a->ring_mutex);
    i32 ch = (i32)spec->channels;
    if (ch > 0) {
        ring_write_mono(a->stem_ring[ud->idx], &a->stem_ring_write[ud->idx],
                        pcm, samples / ch, ch);
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

#ifdef CSPLAYER_TESTING
/*
 * Test seams for the two capture callbacks above.
 *
 * Those run only on SDL's audio thread, which never fires under the dummy
 * driver, so their channel down-mix and ring wrap-around logic would otherwise
 * go unexercised.  These call the real callbacks with a synthesised spec —
 * production behaviour, driven from the test rather than the mixer.  Declared
 * in test_mocks.h; compiled out of shipping builds.
 */
void csp_test_feed_raw(audio_t *a, const f32 *pcm, i32 samples, i32 channels)
{
    assert(a   != NULL);
    assert(pcm != NULL);
    assert(channels > 0);

    SDL_AudioSpec spec;
    SDL_memset(&spec, 0, sizeof(spec));
    spec.format   = SDL_AUDIO_F32;
    spec.channels = channels;
    spec.freq     = (i32)STEM_RATE;
    raw_audio_cb(a, NULL, &spec, (f32 *)pcm, samples);
}

void csp_test_feed_stem(audio_t *a, i32 idx, const f32 *pcm, i32 samples,
                         i32 channels)
{
    assert(a   != NULL);
    assert(pcm != NULL);
    assert(idx >= 0 && idx < STEM_COUNT);
    assert(channels > 0);

    SDL_AudioSpec spec;
    SDL_memset(&spec, 0, sizeof(spec));
    spec.format   = SDL_AUDIO_F32;
    spec.channels = channels;
    spec.freq     = (i32)STEM_RATE;
    a->stem_cb_ud[idx].a   = a;
    a->stem_cb_ud[idx].idx = idx;
    stem_audio_cb(&a->stem_cb_ud[idx], NULL, &spec, (f32 *)pcm, samples);
}
#endif /* CSPLAYER_TESTING */

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

    /* Loading a plain track ends any stem playback for the previous one. */
    audio_unload_stems(a);
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

    if (a->stems_active) {
        if (a->status == AUDIO_STATUS_PAUSED) {
            if (!MIX_ResumeTag(a->mixer, STEMS_TAG)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "audio_play: MIX_ResumeTag failed: %s", SDL_GetError());
                return false;
            }
            a->status = AUDIO_STATUS_PLAYING;
            return true;
        }
        return audio_play_stems(a);
    }
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
        bool ok = a->stems_active ? MIX_PauseTag(a->mixer, STEMS_TAG)
                                  : MIX_PauseTrack(a->track);
        if (!ok) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "audio_toggle_pause: pause failed: %s", SDL_GetError());
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

    bool ok = a->stems_active ? MIX_StopTag(a->mixer, STEMS_TAG, 0)
                              : MIX_StopTrack(a->track, 0);
    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_stop: stop failed: %s", SDL_GetError());
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

    if ((a->audio == NULL && !a->stems_active) || a->duration_frames <= 0) {
        return false;
    }

    i64 target = (i64)((f64)pos * (f64)a->duration_frames);
    if (target < 0) { target = 0; }
    if (target > a->duration_frames) { target = a->duration_frames; }

    /* Seek every stem to the same frame so they stay sample-locked. */
    if (a->stems_active) {
        bool ok = true;
        for (i32 i = 0; i < STEM_COUNT; i++) {
            if (a->stem_track[i] == NULL) { continue; }
            ok = MIX_SetTrackPlaybackPosition(a->stem_track[i], target) && ok;
        }
        if (!ok) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "audio_seek: stem seek failed: %s", SDL_GetError());
        }
        return ok;
    }

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

    /* Stem 0 is the reference clock while stems play; all six advance together. */
    MIX_Track *ref = a->stems_active ? a->stem_track[0] : a->track;
    if (ref == NULL || a->duration_frames <= 0) {
        return 0.0f;
    }
    i64 cur = MIX_GetTrackPlaybackPosition(ref);
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
    (void)SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
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

/* ── Instrument-stem playback ────────────────────────────────────────────── */

/* Load one stem onto its dedicated track, creating the track on first use. */
static bool load_one_stem(audio_t *a, i32 i, const char *path)
{
    assert(a    != NULL);
    assert(path != NULL);
    assert(i >= 0 && i < STEM_COUNT);
    assert(a->mixer != NULL);

    a->stem_audio[i] = MIX_LoadAudio(a->mixer, path, false);
    if (a->stem_audio[i] == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_stems: MIX_LoadAudio('%s') failed: %s",
                     path, SDL_GetError());
        return false;
    }
    if (a->stem_track[i] == NULL) {
        a->stem_track[i] = MIX_CreateTrack(a->mixer);
        if (a->stem_track[i] == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "audio_load_stems: MIX_CreateTrack failed: %s",
                         SDL_GetError());
            return false;
        }
    }
    if (!MIX_SetTrackAudio(a->stem_track[i], a->stem_audio[i])) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_stems: MIX_SetTrackAudio failed: %s", SDL_GetError());
        return false;
    }
    /* The tag is what lets MIX_PlayTag start all six at one mixing instant. */
    if (!MIX_TagTrack(a->stem_track[i], STEMS_TAG)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_stems: MIX_TagTrack failed: %s", SDL_GetError());
        return false;
    }
    a->stem_cb_ud[i].a   = a;
    a->stem_cb_ud[i].idx = i;
    if (!MIX_SetTrackRawCallback(a->stem_track[i], stem_audio_cb,
                                 &a->stem_cb_ud[i])) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_stems: MIX_SetTrackRawCallback failed: %s",
                     SDL_GetError());
        return false;
    }
    (void)MIX_SetTrackGain(a->stem_track[i], 1.0f);
    return true;
}

bool audio_load_stems(audio_t *a, const char *const *paths)
{
    assert(a     != NULL);
    assert(paths != NULL);
    assert(a->mixer != NULL);
    assert(a->track != NULL);

    audio_unload_stems(a);
    unload_audio(a);

    for (i32 i = 0; i < STEM_COUNT; i++) {
        if (paths[i] == NULL || !load_one_stem(a, i, paths[i])) {
            audio_unload_stems(a);
            return false;
        }
    }

    /* Stem 0 is the reference: the six files are the same length and rate by
     * construction, having been produced from one source in one pass. */
    a->duration_frames = MIX_GetAudioDuration(a->stem_audio[0]);
    SDL_AudioSpec fmt;
    if (MIX_GetAudioFormat(a->stem_audio[0], &fmt)) {
        a->sample_rate = (f32)fmt.freq;
        a->channels    = (i32)fmt.channels;
    }
    if (!MIX_SetTrackStoppedCallback(a->stem_track[0], track_stopped_cb, a)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_load_stems: MIX_SetTrackStoppedCallback failed: %s",
                     SDL_GetError());
    }
    a->stems_active = true;
    a->status       = AUDIO_STATUS_STOPPED;
    return true;
}

bool audio_play_stems(audio_t *a)
{
    assert(a != NULL);
    assert(a->mixer != NULL);

    if (!a->stems_active) { return false; }

    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_play_stems: SDL_CreateProperties failed: %s",
                     SDL_GetError());
        return false;
    }
    bool ok = MIX_PlayTag(a->mixer, STEMS_TAG, props);
    SDL_DestroyProperties(props);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "audio_play_stems: MIX_PlayTag failed: %s", SDL_GetError());
        return false;
    }
    a->status = AUDIO_STATUS_PLAYING;
    return true;
}

void audio_set_stem_enabled(audio_t *a, i32 idx, bool on)
{
    assert(a != NULL);
    assert(idx >= 0 && idx < STEM_COUNT);

    if (!a->stems_active || a->stem_track[idx] == NULL) { return; }
    /* Gain 0 removes the stem from the mix but leaves the raw callback firing,
     * so its waveform panel keeps animating while muted. */
    (void)MIX_SetTrackGain(a->stem_track[idx], on ? 1.0f : 0.0f);
}

void audio_snapshot_stem_ring(audio_t *a, i32 idx, f32 *dst)
{
    assert(a   != NULL);
    assert(dst != NULL);
    assert(idx >= 0 && idx < STEM_COUNT);

    if (!a->stems_active) {
        SDL_memset(dst, 0, (size_t)RING_SIZE * sizeof(f32));
        return;
    }

    SDL_LockMutex(a->ring_mutex);
    i32 write_at = a->stem_ring_write[idx];
    for (i32 i = 0; i < RING_SIZE; i++) {
        dst[i] = a->stem_ring[idx][(write_at + i) % RING_SIZE];
    }
    SDL_UnlockMutex(a->ring_mutex);
}

void audio_unload_stems(audio_t *a)
{
    assert(a != NULL);
    assert(STEM_COUNT > 0);

    for (i32 i = 0; i < STEM_COUNT; i++) {
        if (a->stem_track[i] != NULL) {
            /* Clear the callback before stopping to prevent re-entrancy. */
            (void)MIX_SetTrackStoppedCallback(a->stem_track[i], NULL, NULL);
            (void)MIX_StopTrack(a->stem_track[i], 0);
            (void)MIX_SetTrackAudio(a->stem_track[i], NULL);
        }
        if (a->stem_audio[i] != NULL) {
            MIX_DestroyAudio(a->stem_audio[i]);
            a->stem_audio[i] = NULL;
        }
        a->stem_ring_write[i] = 0;
        SDL_memset(a->stem_ring[i], 0, sizeof(a->stem_ring[i]));
    }
    if (a->stems_active) {
        a->stems_active    = false;
        a->status          = AUDIO_STATUS_STOPPED;
        a->duration_frames = -1;
    }
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

    for (i32 i = 0; i < STEM_COUNT; i++) {
        if (a->stem_track[i] != NULL) {
            (void)MIX_SetTrackStoppedCallback(a->stem_track[i], NULL, NULL);
            (void)MIX_StopTrack(a->stem_track[i], 0);
            MIX_DestroyTrack(a->stem_track[i]);
            a->stem_track[i] = NULL;
        }
        if (a->stem_audio[i] != NULL) {
            MIX_DestroyAudio(a->stem_audio[i]);
            a->stem_audio[i] = NULL;
        }
    }
    a->stems_active = false;

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
