/*
 * stems.h — Instrument-stem separation job and on-disk cache.
 *
 * Neural source separation is non-causal over multi-second windows, so it can
 * never run inside the audio callback.  Instead a sidecar executable
 * (STEM_ENGINE_EXE, a Demucs v4 inference binary) is launched as a child
 * process and writes six stem files into a per-track cache directory.  The main
 * loop calls stems_poll() once per frame, which never blocks: completion is
 * checked with a non-blocking SDL_WaitProcess and progress is read from the
 * child's redirected stdout log at STEMS_POLL_NS intervals.
 *
 * Progress is read by draining the child's stdout pipe, which SDL exposes as a
 * non-blocking SDL_IOStream.  An earlier design redirected stdout to a file and
 * tailed it; that cannot work, because the child holds the file with a share
 * mode that refuses all readers, so every read failed and the bar sat at 0%.
 * The pipe must be drained on every poll or the child eventually blocks on a
 * full pipe buffer.
 *
 * Cache layout, under <pref>/stems/<hash>/ where <hash> covers the source
 * path, size and mtime (so an edited or replaced file misses the cache):
 *
 *   input.wav      decoded 44.1 kHz stereo source handed to the engine
 *   drums.wav …    the six stems, 16-bit after post-conversion
 *   done.txt       completion marker — written LAST, so a job that is killed
 *                  or crashes mid-way can never be mistaken for complete
 *
 * This module owns no SDL_mixer state and draws nothing; audio.c loads the
 * cached files and ui.c renders the status.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_STEMS_H
#define CSPLAYER_STEMS_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include "types.h"

/* ── Job state ───────────────────────────────────────────────────────────── */

typedef enum {
    STEMS_IDLE = 0,   /* nothing cached for this track, nothing running */
    STEMS_RUNNING,    /* child process active; progress is meaningful   */
    STEMS_READY,      /* six stems cached and ready to load             */
    STEMS_FAILED,     /* last attempt failed; reason in err[]           */
} stems_status_t;

/* ── Module state ────────────────────────────────────────────────────────── */

/*
 * The separation job is deliberately independent of the loaded track.  A job
 * takes many minutes, so it must survive anything else the user does — playing
 * another song, browsing the playlist, switching to synth mode.  The two halves
 * below are therefore tracked separately: `status` describes the *current
 * track*, while the job_* fields describe whatever separation is in flight.
 * They coincide only when the job happens to be for the current track.
 */
typedef struct {
    /* ── Current track ──────────────────────────────────────────────────── */
    stems_status_t status;          /* status of the CURRENT track           */
    bool  notify;                   /* a job finished and has not been seen  */
    bool  enabled[STEM_COUNT];      /* per-stem mute state; all true on load */
    char  src[PATH_MAX_LEN];        /* current track  ("" if none)           */
    char  cache_dir[PATH_MAX_LEN];  /* <pref>/stems/<hash>/  ("" if no track) */
    char  err[TITLE_MAX_LEN];       /* user-facing failure message           */

    /* ── Background job (outlives track changes) ────────────────────────── */
    SDL_Process  *proc;             /* NULL when no job is running           */
    char  job_dir[PATH_MAX_LEN];    /* cache dir the job is writing into     */
    char  job_title[TITLE_MAX_LEN]; /* track name, for the progress readout  */
    i32   progress;                 /* 0..100; monotonic while a job runs    */
    char  last_out[STEM_TAIL_BYTES];/* most recent engine output, for errors  */

    char  root[PATH_MAX_LEN];       /* <pref>/stems/                         */
} stems_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Zero the struct and resolve <pref>/stems/, creating it if absent.
 * pref_path must be a directory path ending in a separator (as returned by
 * SDL_GetPrefPath and stored in playlist_t.pref_path).
 * Returns false if the directory cannot be created; the module is then inert.
 */
bool stems_init(stems_t *s, const char *pref_path);

/*
 * Point the module at a track: compute its cache directory and set status
 * accordingly — STEMS_READY if a complete cache exists, STEMS_RUNNING if the
 * in-flight job happens to be for this track, else STEMS_IDLE.  Resets the mute
 * flags to all-enabled and clears any previous error.
 *
 * A running job is NEVER disturbed, whichever track this is called with.
 * Passing NULL (no current track) leaves an empty cache_dir, and stems_begin()
 * will then refuse to start.
 */
void stems_set_track(stems_t *s, const char *src_path);

/*
 * Start a separation job for the track most recently passed to
 * stems_set_track().  Decodes the source to input.wav, then launches the
 * engine.  Returns false (status STEMS_FAILED, err[] populated) if the engine
 * or model is missing, the source cannot be decoded, or the process cannot be
 * spawned.  Refuses when a job is already running — one engine at a time.
 */
bool stems_begin(stems_t *s);

/*
 * Advance the running job.  Call once per rendered frame; never blocks, and is
 * a no-op when no job is running.  On child exit, verifies the six outputs,
 * converts them to 16-bit and writes done.txt.  notify is set either way so the
 * user finds out; the current track's status is updated only if the job was for
 * that track.
 */
void stems_poll(stems_t *s);

/* True while a separation job is in flight, for any track. */
bool stems_job_active(const stems_t *s);

/*
 * Kill the in-flight job and delete its partial output.  Safe to call in any
 * state.  Only clears the current track's STEMS_RUNNING status if the cancelled
 * job was the one for that track.
 */
void stems_cancel(stems_t *s);

/* Clear the completion notification (the user has seen it). */
void stems_clear_notify(stems_t *s);

/*
 * Write the cached path of stem idx into dst.  Returns false if idx is out of
 * range, no track is set, or the path would not fit.
 */
bool stems_stem_path(const stems_t *s, i32 idx, char *dst, i32 dstsz);

/* Display name of stem idx ("Drums", "Bass", …), or NULL if out of range. */
const char *stems_name(i32 idx);

/* Kill any in-flight job and release the log handle. */
void stems_quit(stems_t *s);

/* ── Pure helpers (exposed for unit test; no I/O, no global state) ───────── */

/*
 * FNV-1a 64-bit hash of the cache key.  Mixing size and mtime in means an
 * edited or replaced file gets a different cache directory rather than silently
 * reusing stems that no longer match the audio.
 */
u64 stems_hash_key(const char *path, i64 size, i64 mtime);

/*
 * Scan a chunk of engine log output backward for the newest "(<num>%)" field
 * and return it clamped to [0, 100], or -1 if the buffer holds no complete
 * percentage.  len is a byte count; buf need not be NUL-terminated.
 */
i32 stems_parse_progress(const char *buf, i32 len);

/*
 * True when dir holds a complete cache: the done.txt marker plus all six stem
 * files.  Because the marker is written last, a partial directory left by a
 * killed job reads as incomplete.
 */
bool stems_cache_complete(const char *dir);

#endif /* CSPLAYER_STEMS_H */
