/*
 * audio.h — SDL3_mixer playback engine and PCM ring buffer.
 *
 * Responsibilities:
 *   • Load and play audio files via SDL3_mixer.
 *   • Capture raw decoded PCM samples (before gain/effects) into a
 *     circular ring buffer for use by the FFT spectrum analyser.
 *   • Expose play/pause/seek/stop controls.
 *
 * Thread safety: the PCM callback (raw_audio_cb) runs on the audio thread
 * and writes to ring[].  All other functions run on the main thread and
 * may only read ring[] after acquiring ring_mutex.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_AUDIO_H
#define CSPLAYER_AUDIO_H

#include <stdbool.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "types.h"

/* ── Playback state ──────────────────────────────────────────────────────── */

typedef enum {
    AUDIO_STATUS_STOPPED = 0,
    AUDIO_STATUS_PLAYING,
    AUDIO_STATUS_PAUSED,
} audio_status_t;

/* ── Module state ────────────────────────────────────────────────────────── */

typedef struct {
    MIX_Mixer  *mixer;
    MIX_Track  *track;
    MIX_Audio  *audio;           /* currently loaded MIX_Audio object  */

    /* Ring buffer — written by the audio thread, read by the main thread. */
    SDL_Mutex  *ring_mutex;
    f32         ring[RING_SIZE]; /* mono PCM samples, circular          */
    i32         ring_write;      /* next write position (audio thread)  */

    /* Format info captured from the PCM callback spec. */
    f32         sample_rate;
    i32         channels;

    audio_status_t status;

    /* Duration in audio-native frames (-1 if unknown).
     * Divide by sample_rate for seconds. */
    i64         duration_frames;

    /* User event type registered by main.c; pushed when a track ends.
     * Set by main.c before calling audio_load (can remain 0 if unused). */
    u32         event_track_done;
} audio_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Initialise SDL3_mixer and create a mixer device and an empty track.
 * Call this once at startup.  Returns true on success.
 */
bool audio_init(audio_t *a);

/*
 * Load an audio file.  Stops and unloads any previously loaded audio.
 * Registers the PCM callback and the track-stopped callback.
 * Returns true on success.
 */
bool audio_load(audio_t *a, const char *path);

/*
 * Start playback from the beginning (or current position if paused).
 * Returns true on success.
 */
bool audio_play(audio_t *a);

/*
 * Toggle between paused and playing.  Returns true on success.
 */
bool audio_toggle_pause(audio_t *a);

/*
 * Stop playback immediately (position reset to 0).
 * Returns true on success.
 */
bool audio_stop(audio_t *a);

/*
 * Seek to a fractional position [0, 1] within the current track.
 * Returns true on success.
 */
bool audio_seek(audio_t *a, f32 pos);

/*
 * Return the current playback position as a fraction [0, 1].
 * Returns 0 if no track is loaded or duration is unknown.
 */
f32 audio_get_pos(const audio_t *a);

/*
 * Copy the most recent RING_SIZE mono PCM samples (oldest-first) into dst.
 * dst must point to an array of at least RING_SIZE f32 values.
 * Thread-safe (acquires ring_mutex briefly).
 */
void audio_snapshot_ring(audio_t *a, f32 *dst);

/*
 * Set the master playback volume.  volume is a linear gain in [0, 1] where
 * 1.0 = 0 dB (full volume) and 0.0 = silence.  Applies to the mixer device
 * so the setting persists across track changes.
 */
void audio_set_volume(audio_t *a, f32 volume);

/*
 * Unload the current audio file (stop + free), leaving the mixer and track
 * intact.  Safe to call when nothing is loaded.
 */
void audio_unload(audio_t *a);

/*
 * Tear down SDL3_mixer.  Safe to call even if audio_init was not completed.
 */
void audio_quit(audio_t *a);

#endif /* CSPLAYER_AUDIO_H */
