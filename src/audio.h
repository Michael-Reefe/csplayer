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

/* Userdata for the per-stem capture callback: one instance per stem, so the
 * shared callback can tell which ring buffer it is filling. */
typedef struct audio_s audio_t;
typedef struct {
    audio_t *a;
    i32      idx;
} stem_cb_ud_t;

struct audio_s {
    MIX_Mixer  *mixer;
    MIX_Track  *track;
    MIX_Audio  *audio;           /* currently loaded MIX_Audio object  */

    /* Ring buffer — written by the audio thread, read by the main thread. */
    SDL_Mutex  *ring_mutex;
    f32         ring[RING_SIZE]; /* mono PCM samples, circular          */
    i32         ring_write;      /* next write position (audio thread)  */

    /* ── Instrument-stem playback ───────────────────────────────────────────
     * When stems_active, six tracks play the cached stems instead of the single
     * `track` above.  They are tagged STEMS_TAG and started with MIX_PlayTag,
     * which guarantees all tagged tracks begin at the same mixing instant, so
     * they stay sample-locked.  Muting is a per-track gain of 0 — the mixer's
     * raw callback still runs, so a muted stem keeps its display.
     * stem_track[0] is the reference for position, duration and end-of-track. */
    bool          stems_active;
    MIX_Track    *stem_track[STEM_COUNT];
    MIX_Audio    *stem_audio[STEM_COUNT];
    f32           stem_ring[STEM_COUNT][RING_SIZE];
    i32           stem_ring_write[STEM_COUNT];
    stem_cb_ud_t  stem_cb_ud[STEM_COUNT];

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
};

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
 * Load a raw mono PCM buffer (f32, sample_rate Hz) into the mixer.
 * The buffer must remain valid until audio_unload / audio_quit —
 * SDL does not copy it (NoCopy semantics).  Does NOT register a
 * track-stopped callback; call audio_play_looped after this.
 * Returns true on success.
 */
bool audio_load_raw(audio_t *a, const f32 *buf, i32 nframes, f32 sample_rate);

/*
 * Begin infinite-loop playback of the currently loaded audio.
 * Passes MIX_PROP_PLAY_MAX_LOOPS_NUMBER = -1 to SDL3_mixer.
 * Returns true on success; status becomes AUDIO_STATUS_PLAYING.
 */
bool audio_play_looped(audio_t *a);

/*
 * Unload the current audio file (stop + free), leaving the mixer and track
 * intact.  Safe to call when nothing is loaded.
 */
void audio_unload(audio_t *a);

/* ── Instrument-stem playback ────────────────────────────────────────────── */

/*
 * Load STEM_COUNT stem files and switch to stem playback.  paths must hold
 * STEM_COUNT valid filesystem paths in stem order.  Unloads any single-track
 * audio first, creates the six tracks (once; reused across tracks afterwards),
 * tags them STEMS_TAG, and registers the per-stem capture callbacks.  Every
 * stem starts enabled.  Returns false and unloads cleanly on any failure.
 *
 * Duration, position and the end-of-track callback come from stem 0.
 */
bool audio_load_stems(audio_t *a, const char *const *paths);

/*
 * Begin synchronized playback of all six stems via MIX_PlayTag.
 * Returns true on success; status becomes AUDIO_STATUS_PLAYING.
 */
bool audio_play_stems(audio_t *a);

/*
 * Mute or unmute one stem in the live mix by setting its track gain.
 * idx must be in [0, STEM_COUNT).  Silently ignores calls when stems are not
 * loaded.  The stem's ring buffer keeps filling either way.
 */
void audio_set_stem_enabled(audio_t *a, i32 idx, bool on);

/*
 * Copy the most recent RING_SIZE mono samples of stem idx (oldest first) into
 * dst, which must hold at least RING_SIZE floats.  Thread-safe.  Writes
 * silence if stems are not loaded or idx is out of range.
 */
void audio_snapshot_stem_ring(audio_t *a, i32 idx, f32 *dst);

/*
 * Stop and release the stem tracks, returning to single-track playback.
 * Safe to call when no stems are loaded.
 */
void audio_unload_stems(audio_t *a);

/*
 * Tear down SDL3_mixer.  Safe to call even if audio_init was not completed.
 */
void audio_quit(audio_t *a);

#endif /* CSPLAYER_AUDIO_H */
