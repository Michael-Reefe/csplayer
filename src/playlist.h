/*
 * playlist.h — Track list management and session persistence.
 *
 * The playlist is a fixed-size array of entries (NASA Rule 3: no dynamic
 * allocation).  It is loaded from and saved to a plain-text file in the
 * SDL preference directory so that the track list survives between sessions.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_PLAYLIST_H
#define CSPLAYER_PLAYLIST_H

#include <stdbool.h>
#include "types.h"

/* ── Data types ──────────────────────────────────────────────────────────── */

typedef struct {
    char path[PATH_MAX_LEN];     /* absolute filesystem path   */
    char title[TITLE_MAX_LEN];   /* display title (basename)   */
    char artist[TITLE_MAX_LEN];  /* artist tag (empty if none) */
    f64  duration_sec;           /* track length in seconds    */
} playlist_entry_t;

typedef struct {
    playlist_entry_t entries[PLAYLIST_MAX];
    i32  count;                        /* number of valid entries   */
    i32  current;                      /* index of loaded track, -1 = none */
    i32  scroll;                       /* first visible row in the UI      */
    char pref_path[PATH_MAX_LEN];      /* SDL_GetPrefPath result           */
} playlist_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Initialise the playlist struct and resolve the preference directory via
 * SDL_GetPrefPath.  Returns true on success.
 */
bool playlist_init(playlist_t *pl);

/*
 * Load the saved playlist from the preference directory.  Missing files are
 * silently ignored (returns true).  Returns false only on I/O errors.
 */
bool playlist_load(playlist_t *pl);

/*
 * Save the current playlist to the preference directory.
 * Returns true on success.
 */
bool playlist_save(const playlist_t *pl);

/*
 * Add paths from a NULL-terminated file list (e.g. from SDL_ShowOpenFileDialog).
 * Duplicate paths are skipped.  Returns the number of entries actually added.
 */
i32 playlist_add_paths(playlist_t *pl, const char *const *filelist);

/*
 * Advance to the next track.  Returns the new current index, or -1 if
 * already at the last track or the playlist is empty (does NOT wrap).
 */
i32 playlist_next(playlist_t *pl);

/*
 * Go back to the previous track.  Returns the new current index, or -1 if
 * already at the first track or the playlist is empty (does NOT wrap).
 */
i32 playlist_prev(playlist_t *pl);

/*
 * Remove the track at idx, shifting later entries down.  Adjusts current and
 * scroll to remain valid.  Returns the new current index (or -1 if empty).
 */
i32 playlist_remove(playlist_t *pl, i32 idx);

/*
 * Set the current track index explicitly.  idx is clamped to [0, count-1].
 * Returns the new current index, or -1 if the playlist is empty.
 */
i32 playlist_set_current(playlist_t *pl, i32 idx);

/*
 * Return the filesystem path of the current track, or NULL if none.
 * The returned pointer is valid until the next playlist mutation.
 */
const char *playlist_current_path(const playlist_t *pl);

#endif /* CSPLAYER_PLAYLIST_H */
