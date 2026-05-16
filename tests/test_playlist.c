/*
 * test_playlist.c — Unit tests for src/playlist.c.
 *
 * Tests bypass playlist_init (which needs SDL_GetPrefPath) by zeroing the
 * struct and setting current=-1.  All branches in path_to_title,
 * playlist_add_paths, playlist_next, playlist_prev, playlist_remove,
 * playlist_set_current, playlist_current_path, playlist_load, and
 * playlist_save are covered.
 */

#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "harness.h"
#include "playlist.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static playlist_t g_pl;

static void reset_pl(void)
{
    SDL_memset(&g_pl, 0, sizeof(g_pl));
    g_pl.current = -1;
}

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_playlist_empty_state(void)
{
    TEST_BEGIN("playlist_empty_state");
    reset_pl();
    CHECK(playlist_next(&g_pl)        == -1);
    CHECK(playlist_prev(&g_pl)        == -1);
    CHECK(playlist_set_current(&g_pl, 0) == -1);
    CHECK(playlist_current_path(&g_pl)   == NULL);
    TEST_END();
}

static void test_playlist_title_unix_path(void)
{
    TEST_BEGIN("playlist_title_unix_path");
    reset_pl();
    const char *paths[] = { "/path/to/Song.flac", NULL };
    playlist_add_paths(&g_pl, paths);
    CHECK(SDL_strcmp(g_pl.entries[0].title, "Song") == 0);
    TEST_END();
}

static void test_playlist_title_win_path(void)
{
    TEST_BEGIN("playlist_title_win_path");
    reset_pl();
    const char *paths[] = { "C:\\Users\\test\\Song.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    CHECK(SDL_strcmp(g_pl.entries[0].title, "Song") == 0);
    TEST_END();
}

static void test_playlist_title_no_ext(void)
{
    TEST_BEGIN("playlist_title_no_ext");
    reset_pl();
    const char *paths[] = { "/path/track", NULL };
    playlist_add_paths(&g_pl, paths);
    CHECK(SDL_strcmp(g_pl.entries[0].title, "track") == 0);
    TEST_END();
}

static void test_playlist_title_dotfile(void)
{
    TEST_BEGIN("playlist_title_dotfile");
    reset_pl();
    /* dot == dst guard: the dot IS the first char, so extension not stripped. */
    const char *paths[] = { "/path/.hidden", NULL };
    playlist_add_paths(&g_pl, paths);
    CHECK(SDL_strcmp(g_pl.entries[0].title, ".hidden") == 0);
    TEST_END();
}

static void test_playlist_add_normal(void)
{
    TEST_BEGIN("playlist_add_normal");
    reset_pl();
    const char *paths[] = { "/a/track1.mp3", "/a/track2.flac", NULL };
    i32 added = playlist_add_paths(&g_pl, paths);
    CHECK(added == 2);
    CHECK(g_pl.count == 2);
    /* current auto-set to 0 on first add. */
    CHECK(g_pl.current == 0);
    TEST_END();
}

static void test_playlist_add_duplicate(void)
{
    TEST_BEGIN("playlist_add_duplicate");
    reset_pl();
    const char *paths[] = { "/a/track1.mp3", "/a/track1.mp3", NULL };
    i32 added = playlist_add_paths(&g_pl, paths);
    CHECK(added == 1);
    CHECK(g_pl.count == 1);
    TEST_END();
}

static void test_playlist_add_overflow(void)
{
    TEST_BEGIN("playlist_add_overflow");
    reset_pl();

    /* Build PLAYLIST_MAX + 2 unique paths on the heap. */
    static char   bufs[PLAYLIST_MAX + 2][PATH_MAX_LEN];
    static const char *ptrs[PLAYLIST_MAX + 3];

    for (i32 i = 0; i < PLAYLIST_MAX + 2; i++) {
        SDL_snprintf(bufs[i], (size_t)PATH_MAX_LEN, "/track/%d.mp3", i);
        ptrs[i] = bufs[i];
    }
    ptrs[PLAYLIST_MAX + 2] = NULL;

    playlist_add_paths(&g_pl, ptrs);
    CHECK(g_pl.count == PLAYLIST_MAX);
    TEST_END();
}

static void test_playlist_next_normal(void)
{
    TEST_BEGIN("playlist_next_normal");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 0;
    CHECK(playlist_next(&g_pl) == 1);
    CHECK(playlist_next(&g_pl) == 2);
    TEST_END();
}

static void test_playlist_next_at_end(void)
{
    TEST_BEGIN("playlist_next_at_end");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 1;   /* already at last track */
    CHECK(playlist_next(&g_pl) == -1);
    CHECK(g_pl.current == 1);   /* unchanged */
    TEST_END();
}

static void test_playlist_prev_normal(void)
{
    TEST_BEGIN("playlist_prev_normal");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 2;
    CHECK(playlist_prev(&g_pl) == 1);
    CHECK(playlist_prev(&g_pl) == 0);
    TEST_END();
}

static void test_playlist_prev_at_start(void)
{
    TEST_BEGIN("playlist_prev_at_start");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 0;
    CHECK(playlist_prev(&g_pl) == -1);
    CHECK(g_pl.current == 0);   /* unchanged */
    TEST_END();
}

static void test_playlist_remove_before_current(void)
{
    TEST_BEGIN("playlist_remove_before_current");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 2;
    /* Remove idx=0 (before current): current must shift from 2 to 1. */
    i32 cur = playlist_remove(&g_pl, 0);
    CHECK(cur == 1);
    CHECK(g_pl.count == 2);
    TEST_END();
}

static void test_playlist_remove_after_current(void)
{
    TEST_BEGIN("playlist_remove_after_current");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 0;
    /* Remove idx=2 (after current): current must stay at 0. */
    i32 cur = playlist_remove(&g_pl, 2);
    CHECK(cur == 0);
    CHECK(g_pl.count == 2);
    TEST_END();
}

static void test_playlist_remove_current_middle(void)
{
    TEST_BEGIN("playlist_remove_current_middle");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 1;
    /* Remove idx=1 (current, not last): current stays at 1 (now the next entry). */
    i32 cur = playlist_remove(&g_pl, 1);
    CHECK(cur == 1);
    CHECK(g_pl.count == 2);
    /* Verify the entry at current=1 is now the old idx=2. */
    CHECK(SDL_strcmp(g_pl.entries[1].path, "/a/2.mp3") == 0);
    TEST_END();
}

static void test_playlist_remove_current_last(void)
{
    TEST_BEGIN("playlist_remove_current_last");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 2;   /* last track */
    /* Remove it: current must clamp to count-1 = 1. */
    i32 cur = playlist_remove(&g_pl, 2);
    CHECK(cur == 1);
    CHECK(g_pl.count == 2);
    TEST_END();
}

static void test_playlist_remove_to_empty(void)
{
    TEST_BEGIN("playlist_remove_to_empty");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    i32 cur = playlist_remove(&g_pl, 0);
    CHECK(cur == -1);
    CHECK(g_pl.count == 0);
    TEST_END();
}

static void test_playlist_remove_scroll_clamp(void)
{
    TEST_BEGIN("playlist_remove_scroll_clamp");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 1;
    g_pl.scroll  = 2;   /* will be out-of-range after remove */
    /* Remove idx=2 (after current, count goes 3→2, scroll=2 >= 2 → clamped). */
    playlist_remove(&g_pl, 2);
    CHECK(g_pl.count  == 2);
    CHECK(g_pl.scroll == 1);
    TEST_END();
}

static void test_playlist_set_current_valid(void)
{
    TEST_BEGIN("playlist_set_current_valid");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", "/a/2.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    CHECK(playlist_set_current(&g_pl, 2) == 2);
    CHECK(g_pl.current == 2);
    TEST_END();
}

static void test_playlist_set_current_clamp(void)
{
    TEST_BEGIN("playlist_set_current_clamp");
    reset_pl();
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    /* idx=5 is out of range → clamped to count-1 = 1. */
    CHECK(playlist_set_current(&g_pl, 5) == 1);
    TEST_END();
}

static void test_playlist_load_no_file(void)
{
    TEST_BEGIN("playlist_load_no_file");
    reset_pl();
    /* Point pref_path at a directory that won't have playlist.txt. */
    SDL_strlcpy(g_pl.pref_path,
                "C:\\nonexistent_csplayer_test_dir_xyz\\",
                (size_t)PATH_MAX_LEN);
    /* On non-Windows the path is still a non-existent dir. */
    bool ok = playlist_load(&g_pl);
    CHECK(ok);
    CHECK(g_pl.count == 0);
    TEST_END();
}

static void test_playlist_load_with_file(void)
{
    TEST_BEGIN("playlist_load_with_file");
    reset_pl();

    /* Write a minimal playlist file in the current directory. */
    FILE *f = fopen("csplayer_test_pl.txt", "w");
    if (f == NULL) {
        /* If we can't create the file, skip this test gracefully. */
        printf("  SKIP  playlist_load_with_file (cannot create temp file)\n");
        TEST_END();
        return;
    }
    fprintf(f, "1\n");                    /* saved current index */
    fprintf(f, "/test/track0.mp3\n");
    fprintf(f, "/test/track1.flac\n");
    fclose(f);

    /* Load from "./" — the file we just wrote is "./csplayer_test_pl.txt",
     * but playlist_load appends "playlist.txt".  Rename to match. */
    remove("playlist.txt");
    rename("csplayer_test_pl.txt", "playlist.txt");

    SDL_strlcpy(g_pl.pref_path, "./", (size_t)PATH_MAX_LEN);
    bool ok = playlist_load(&g_pl);
    CHECK(ok);
    CHECK(g_pl.count == 2);
    CHECK(g_pl.current == 1);
    CHECK(SDL_strcmp(g_pl.entries[0].path, "/test/track0.mp3")  == 0);
    CHECK(SDL_strcmp(g_pl.entries[0].title, "track0")           == 0);
    CHECK(SDL_strcmp(g_pl.entries[1].path, "/test/track1.flac") == 0);

    remove("playlist.txt");
    TEST_END();
}

static void test_playlist_save_basic(void)
{
    TEST_BEGIN("playlist_save_basic");
    reset_pl();
    SDL_strlcpy(g_pl.pref_path, "./", (size_t)PATH_MAX_LEN);
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 1;

    CHECK(playlist_save(&g_pl));

    /* Verify the saved file can be loaded back. */
    playlist_t loaded;
    SDL_memset(&loaded, 0, sizeof(loaded));
    loaded.current = -1;
    SDL_strlcpy(loaded.pref_path, "./", (size_t)PATH_MAX_LEN);
    CHECK(playlist_load(&loaded));
    CHECK(loaded.count   == 2);
    CHECK(loaded.current == 1);

    remove("playlist.txt");
    TEST_END();
}

static void test_playlist_current_path(void)
{
    TEST_BEGIN("playlist_current_path");
    reset_pl();

    /* Empty playlist → NULL. */
    CHECK(playlist_current_path(&g_pl) == NULL);

    /* After adding tracks, current_path returns the right entry. */
    const char *paths[] = { "/a/0.mp3", "/a/1.mp3", NULL };
    playlist_add_paths(&g_pl, paths);
    g_pl.current = 1;
    const char *p = playlist_current_path(&g_pl);
    CHECK(p != NULL);
    CHECK(SDL_strcmp(p, "/a/1.mp3") == 0);
    TEST_END();
}

/* ── Suite ───────────────────────────────────────────────────────────────── */

void run_playlist_tests(void)
{
    test_playlist_empty_state();
    test_playlist_title_unix_path();
    test_playlist_title_win_path();
    test_playlist_title_no_ext();
    test_playlist_title_dotfile();
    test_playlist_add_normal();
    test_playlist_add_duplicate();
    test_playlist_add_overflow();
    test_playlist_next_normal();
    test_playlist_next_at_end();
    test_playlist_prev_normal();
    test_playlist_prev_at_start();
    test_playlist_remove_before_current();
    test_playlist_remove_after_current();
    test_playlist_remove_current_middle();
    test_playlist_remove_current_last();
    test_playlist_remove_to_empty();
    test_playlist_remove_scroll_clamp();
    test_playlist_set_current_valid();
    test_playlist_set_current_clamp();
    test_playlist_load_no_file();
    test_playlist_load_with_file();
    test_playlist_save_basic();
    test_playlist_current_path();
}
