/*
 * playlist.c — Track list management and session persistence.
 *
 * Persistence format (plain text, UTF-8):
 *   Line 1: current track index (integer, -1 if none)
 *   Lines 2+: one absolute file path per line
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "playlist.h"

#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Name of the persistence file within the preference directory. */
#define PLAYLIST_FILE "playlist.txt"

/* ── Internal helpers ────────────────────────────────────────────────────── */

/*
 * Extract a display title from an absolute path (the basename without
 * extension).  dst must be at least TITLE_MAX_LEN bytes.
 */
static void path_to_title(const char *path, char *dst)
{
    assert(path != NULL);
    assert(dst  != NULL);

    /* Find the last path separator. */
    const char *slash = SDL_strrchr(path, '/');
    const char *bslash = SDL_strrchr(path, '\\');
    const char *base = path;
    if (slash  != NULL && slash  > base) { base = slash  + 1; }
    if (bslash != NULL && bslash > base) { base = bslash + 1; }

    SDL_strlcpy(dst, base, (size_t)TITLE_MAX_LEN);

    /* Strip the extension (everything from the last dot). */
    char *dot = SDL_strrchr(dst, '.');
    if (dot != NULL && dot != dst) {
        *dot = '\0';
    }
}

/*
 * Check whether path is already present in the playlist.
 * Returns true if it is a duplicate.
 */
static bool is_duplicate(const playlist_t *pl, const char *path)
{
    assert(pl   != NULL);
    assert(path != NULL);

    i32 limit = pl->count;
    assert(limit >= 0 && limit <= PLAYLIST_MAX);

    for (i32 i = 0; i < limit; i++) {
        if (SDL_strcmp(pl->entries[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

/* ── Public interface ────────────────────────────────────────────────────── */

bool playlist_init(playlist_t *pl)
{
    assert(pl != NULL);

    SDL_memset(pl, 0, sizeof(*pl));
    pl->current = -1;

    char *pref = SDL_GetPrefPath(CSPLAYER_ORG, CSPLAYER_APP_NAME);
    if (pref == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "playlist_init: SDL_GetPrefPath failed: %s", SDL_GetError());
        return false;
    }
    SDL_strlcpy(pl->pref_path, pref, (size_t)PATH_MAX_LEN);
    SDL_free(pref);
    return true;
}

bool playlist_load(playlist_t *pl)
{
    assert(pl != NULL);
    assert(pl->pref_path[0] != '\0');

    char filepath[PATH_MAX_LEN * 2];
    if (SDL_snprintf(filepath, sizeof(filepath), "%s%s",
                     pl->pref_path, PLAYLIST_FILE) < 0) {
        return false;
    }

    SDL_IOStream *io = SDL_IOFromFile(filepath, "r");
    if (io == NULL) {
        /* Missing file is not an error; the playlist just starts empty. */
        return true;
    }

    /* Read current index from the first line. */
    char line[PATH_MAX_LEN];
    if (SDL_ReadIO(io, line, 1) == 0) {
        SDL_CloseIO(io);
        return true;
    }

    /* Re-open for line-by-line reading via stdio (SDL_IOStream lacks fgets). */
    SDL_CloseIO(io);

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        return true;
    }

    i32 saved_current = -1;
    if (fgets(line, (i32)sizeof(line), f) != NULL) {
        saved_current = (i32)SDL_strtol(line, NULL, 10);
    }

    i32 loaded = 0;
    while (loaded < PLAYLIST_MAX && fgets(line, (i32)sizeof(line), f) != NULL) {
        /* Strip trailing newline / carriage return. */
        i32 len = (i32)SDL_strlen(line);
        assert(len >= 0);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }
        SDL_strlcpy(pl->entries[loaded].path,  line, (size_t)PATH_MAX_LEN);
        path_to_title(line, pl->entries[loaded].title);
        pl->entries[loaded].duration_sec = 0.0;
        loaded++;
    }
    fclose(f);

    pl->count   = loaded;
    pl->current = (saved_current >= 0 && saved_current < loaded)
                      ? saved_current : (loaded > 0 ? 0 : -1);
    return true;
}

bool playlist_save(const playlist_t *pl)
{
    assert(pl != NULL);
    assert(pl->pref_path[0] != '\0');

    char filepath[PATH_MAX_LEN * 2];
    if (SDL_snprintf(filepath, sizeof(filepath), "%s%s",
                     pl->pref_path, PLAYLIST_FILE) < 0) {
        return false;
    }

    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "playlist_save: cannot open %s for writing", filepath);
        return false;
    }

    fprintf(f, "%d\n", pl->current);

    i32 limit = pl->count;
    assert(limit >= 0 && limit <= PLAYLIST_MAX);

    for (i32 i = 0; i < limit; i++) {
        fprintf(f, "%s\n", pl->entries[i].path);
    }
    fclose(f);
    return true;
}

i32 playlist_add_paths(playlist_t *pl, const char *const *filelist)
{
    assert(pl       != NULL);
    assert(filelist != NULL);

    i32 added = 0;
    for (i32 i = 0; filelist[i] != NULL; i++) {
        assert(i < PLAYLIST_MAX * 2);   /* guard against infinite list */
        if (pl->count >= PLAYLIST_MAX) {
            break;
        }
        if (is_duplicate(pl, filelist[i])) {
            continue;
        }
        SDL_strlcpy(pl->entries[pl->count].path,  filelist[i], (size_t)PATH_MAX_LEN);
        path_to_title(filelist[i], pl->entries[pl->count].title);
        pl->entries[pl->count].duration_sec = 0.0;
        pl->count++;
        added++;
    }
    /* Select first track if none was selected before. */
    if (pl->current < 0 && pl->count > 0) {
        pl->current = 0;
    }
    return added;
}

i32 playlist_next(playlist_t *pl)
{
    assert(pl != NULL);

    if (pl->count <= 0)                  { return -1; }
    if (pl->current >= pl->count - 1)    { return -1; }
    pl->current++;
    return pl->current;
}

i32 playlist_prev(playlist_t *pl)
{
    assert(pl != NULL);

    if (pl->count <= 0)   { return -1; }
    if (pl->current <= 0) { return -1; }
    pl->current--;
    return pl->current;
}

i32 playlist_remove(playlist_t *pl, i32 idx)
{
    assert(pl  != NULL);
    assert(idx >= 0 && idx < pl->count);

    if (idx < 0 || idx >= pl->count) { return pl->current; }

    /* Shift later entries down by one. */
    for (i32 i = idx; i < pl->count - 1; i++) {
        assert(i + 1 < PLAYLIST_MAX);
        pl->entries[i] = pl->entries[i + 1];
    }
    pl->count--;

    /* Keep current pointing at the same logical track (or nearest valid). */
    if (pl->current == idx) {
        /* Removed the current track; point at the same slot (now the next
         * track), clamped down if it was the last entry. */
        if (pl->count == 0) {
            pl->current = -1;
        } else if (pl->current >= pl->count) {
            pl->current = pl->count - 1;
        }
    } else if (pl->current > idx) {
        pl->current--;
    }

    /* Clamp scroll. */
    if (pl->scroll >= pl->count && pl->scroll > 0) {
        pl->scroll = pl->count - 1;
    }

    return pl->current;
}

i32 playlist_set_current(playlist_t *pl, i32 idx)
{
    assert(pl  != NULL);
    assert(idx >= 0);

    if (pl->count <= 0) {
        return -1;
    }
    if (idx >= pl->count) {
        idx = pl->count - 1;
    }
    pl->current = idx;
    return pl->current;
}

const char *playlist_current_path(const playlist_t *pl)
{
    assert(pl != NULL);

    if (pl->current < 0 || pl->current >= pl->count) {
        return NULL;
    }
    return pl->entries[pl->current].path;
}
