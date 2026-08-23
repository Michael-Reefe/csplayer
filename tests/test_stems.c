/*
 * test_stems.c — Unit tests for src/stems.c.
 *
 * Two groups:
 *   1. Pure helpers (hash, progress parsing, path building) — no I/O.
 *   2. Cache and job state — driven with real files in a scratch directory and
 *      mocked process/decoder calls, so every status transition and failure
 *      branch is reachable without a separation engine installed.
 */

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <assert.h>

#include "harness.h"
#include "stems.h"
#include "test_mocks.h"

/* Scratch directory for cache tests, created and torn down per test. */
#define T_ROOT  "csplayer_stemtest/"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Write a small file, creating nothing else.  Returns true on success. */
static bool touch(const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (io == NULL) { return false; }
    (void)SDL_WriteIO(io, "x", 1);
    return SDL_CloseIO(io);
}

/* Does path exist on disk? */
static bool path_there(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info);
}

/* Build "<dir><name>" into dst. */
static void join(char *dst, size_t dstsz, const char *dir, const char *name)
{
    (void)SDL_snprintf(dst, dstsz, "%s%s", dir, name);
}

static const char *const k_stem_files[STEM_COUNT] = {
    "drums.wav", "bass.wav", "other.wav", "vocals.wav", "guitar.wav", "piano.wav"
};

/* Raw output names the engine writes, which finish_job then converts. */
static const char *const k_engine_files[STEM_COUNT] = {
    "target_0_drums.wav",  "target_1_bass.wav",   "target_2_other.wav",
    "target_3_vocals.wav", "target_4_guitar.wav", "target_5_piano.wav"
};

/* Byte-copy src to dst.  Used to stage decodable audio, since finish_job runs
 * the engine's outputs back through the decoder rather than just stat-ing them. */
static bool copy_file(const char *src, const char *dst)
{
    SDL_IOStream *in = SDL_IOFromFile(src, "rb");
    if (in == NULL) { return false; }
    SDL_IOStream *out = SDL_IOFromFile(dst, "wb");
    if (out == NULL) { (void)SDL_CloseIO(in); return false; }

    static u8 buf[8192];
    bool ok = true;
    for (;;) {
        size_t got = SDL_ReadIO(in, buf, sizeof(buf));
        if (got == 0) { break; }
        if (SDL_WriteIO(out, buf, got) != got) { ok = false; break; }
    }
    ok = SDL_CloseIO(out) && ok;
    (void)SDL_CloseIO(in);
    return ok;
}

/* Stage a plausible set of engine outputs: six real, decodable WAV files under
 * the names the engine produces. */
static bool fill_engine_outputs(const char *dir)
{
    extern char g_test_wav_path[512];
    char p[PATH_MAX_LEN];
    for (i32 i = 0; i < STEM_COUNT; i++) {
        join(p, sizeof(p), dir, k_engine_files[i]);
        if (!copy_file(g_test_wav_path, p)) { return false; }
    }
    return true;
}

/* Populate dir with the six stems and, optionally, the done marker. */
static void fill_cache(const char *dir, bool with_marker)
{
    char p[PATH_MAX_LEN];
    for (i32 i = 0; i < STEM_COUNT; i++) {
        join(p, sizeof(p), dir, k_stem_files[i]);
        (void)touch(p);
    }
    if (with_marker) {
        join(p, sizeof(p), dir, STEMS_DONE_FILE);
        (void)touch(p);
    }
}

/* Remove every file this suite may have created in dir, then dir itself. */
static void wipe_dir(const char *dir)
{
    static const char *const extras[] = {
        STEMS_DONE_FILE, STEMS_INPUT_WAV
    };
    char p[PATH_MAX_LEN];
    for (i32 i = 0; i < STEM_COUNT; i++) {
        join(p, sizeof(p), dir, k_stem_files[i]);
        (void)SDL_RemovePath(p);
        join(p, sizeof(p), dir, k_engine_files[i]);
        (void)SDL_RemovePath(p);
    }
    for (i32 i = 0; i < (i32)SDL_arraysize(extras); i++) {
        join(p, sizeof(p), dir, extras[i]);
        (void)SDL_RemovePath(p);
    }
    (void)SDL_RemovePath(dir);
}

/* ── Pure helpers: hash ──────────────────────────────────────────────────── */

static void test_hash_deterministic(void)
{
    TEST_BEGIN("stems_hash_deterministic");
    u64 a = stems_hash_key("/music/a.mp3", 1234, 9999);
    u64 b = stems_hash_key("/music/a.mp3", 1234, 9999);
    CHECK(a == b);
    CHECK(a != 0);
    TEST_END();
}

static void test_hash_sensitive_to_each_input(void)
{
    TEST_BEGIN("stems_hash_sensitive_to_each_input");
    u64 base = stems_hash_key("/music/a.mp3", 1234, 9999);
    /* Every component of the key must change the result — otherwise an edited
     * file could silently reuse stems that no longer match its audio. */
    CHECK(stems_hash_key("/music/b.mp3", 1234, 9999) != base);   /* path  */
    CHECK(stems_hash_key("/music/a.mp3", 1235, 9999) != base);   /* size  */
    CHECK(stems_hash_key("/music/a.mp3", 1234, 10000) != base);  /* mtime */
    TEST_END();
}

static void test_hash_empty_path(void)
{
    TEST_BEGIN("stems_hash_empty_path");
    u64 a = stems_hash_key("", 0, 0);
    u64 b = stems_hash_key("", 0, 1);
    CHECK(a != b);
    CHECK(a != 0);
    TEST_END();
}

/* ── Pure helpers: progress parsing ──────────────────────────────────────── */

static void test_progress_basic(void)
{
    TEST_BEGIN("stems_progress_basic");
    const char *s = "( 42.123%) processing segment";
    CHECK(stems_parse_progress(s, (i32)SDL_strlen(s)) == 42);
    const char *z = "(  0.000%) start";
    CHECK(stems_parse_progress(z, (i32)SDL_strlen(z)) == 0);
    TEST_END();
}

static void test_progress_takes_last(void)
{
    TEST_BEGIN("stems_progress_takes_last");
    /* The log grows; the newest percentage is the one at the end. */
    const char *s = "( 10.0%) a\n( 55.0%) b\n( 91.5%) c\n";
    CHECK(stems_parse_progress(s, (i32)SDL_strlen(s)) == 91);
    TEST_END();
}

static void test_progress_clamps_and_completes(void)
{
    TEST_BEGIN("stems_progress_clamps_and_completes");
    const char *hundred = "(100.000%) done";
    CHECK(stems_parse_progress(hundred, (i32)SDL_strlen(hundred)) == 100);
    /* A malformed over-range value must saturate, never wrap or overflow. */
    const char *over = "(99999%) bogus";
    CHECK(stems_parse_progress(over, (i32)SDL_strlen(over)) == 100);
    TEST_END();
}

static void test_progress_malformed(void)
{
    TEST_BEGIN("stems_progress_malformed");
    const char *bare = "no percentage here";
    CHECK(stems_parse_progress(bare, (i32)SDL_strlen(bare)) == -1);
    /* A '%' with no digits in front of it is not a reading. */
    const char *sign = "loading % done";
    CHECK(stems_parse_progress(sign, (i32)SDL_strlen(sign)) == -1);
    CHECK(stems_parse_progress("", 0) == -1);
    /* A stray '%' must be stepped over to reach the real reading behind it,
     * rather than aborting the scan and losing the percentage entirely. */
    const char *stray = "( 88.0%) working %";
    CHECK(stems_parse_progress(stray, (i32)SDL_strlen(stray)) == 88);
    TEST_END();
}

static void test_progress_truncated_tail(void)
{
    TEST_BEGIN("stems_progress_truncated_tail");
    /* Reading the tail of a file the child is still writing can cut a line in
     * half; a partial reading must be ignored, not misparsed. */
    const char *s = "( 33.0%) ok\n( 44.";
    CHECK(stems_parse_progress(s, (i32)SDL_strlen(s)) == 33);
    /* len shorter than the buffer must be honoured (no NUL scan past it). */
    CHECK(stems_parse_progress(s, 11) == 33);
    CHECK(stems_parse_progress(s, 2) == -1);
    TEST_END();
}

/* ── Cache completeness ──────────────────────────────────────────────────── */

static void test_cache_complete_states(void)
{
    TEST_BEGIN("stems_cache_complete_states");
    CHECK(!stems_cache_complete(""));                 /* no track set        */
    CHECK(!stems_cache_complete("no_such_dir_xyz/")); /* nothing on disk     */

    CHECK(SDL_CreateDirectory(T_ROOT));

    /* Six stems but no marker: a job that died before publishing must not be
     * mistaken for a finished one. */
    fill_cache(T_ROOT, false);
    CHECK(!stems_cache_complete(T_ROOT));

    /* Marker plus all six: complete. */
    fill_cache(T_ROOT, true);
    CHECK(stems_cache_complete(T_ROOT));

    /* Marker present but a stem missing: still incomplete. */
    char p[PATH_MAX_LEN];
    join(p, sizeof(p), T_ROOT, k_stem_files[3]);
    CHECK(SDL_RemovePath(p));
    CHECK(!stems_cache_complete(T_ROOT));

    wipe_dir(T_ROOT);
    TEST_END();
}

/* ── init / set_track / stem_path / name ─────────────────────────────────── */

static void test_init_and_defaults(void)
{
    TEST_BEGIN("stems_init_and_defaults");
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    CHECK(s.status == STEMS_IDLE);
    CHECK(s.progress == 0);
    CHECK(!s.notify);
    CHECK(s.proc == NULL);
    CHECK(s.cache_dir[0] == '\0');
    for (i32 i = 0; i < STEM_COUNT; i++) { CHECK(s.enabled[i]); }
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_init_mkdir_fail(void)
{
    TEST_BEGIN("stems_init_mkdir_fail");
    stems_t s;
    g_mock_SDL_CreateDirectory_fail = true;
    CHECK(!stems_init(&s, T_ROOT));
    /* An inert module must not carry a usable root, so nothing later writes
     * into a directory that does not exist. */
    CHECK(s.root[0] == '\0');
    g_mock_SDL_CreateDirectory_fail = false;
    TEST_END();
}

static void test_set_track_idle_and_ready(void)
{
    TEST_BEGIN("stems_set_track_idle_and_ready");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));

    /* A real file with no cache yet → IDLE with a cache path assigned. */
    stems_set_track(&s, g_test_wav_path);
    CHECK(s.status == STEMS_IDLE);
    CHECK(s.cache_dir[0] != '\0');
    char dir[PATH_MAX_LEN];
    (void)SDL_strlcpy(dir, s.cache_dir, sizeof(dir));

    /* The cache directory is <root>/<hash>/ over path+size+mtime.  Deriving it
     * here independently pins that contract, and lets the cache be populated
     * before a module ever looks at the track. */
    SDL_PathInfo info;
    CHECK(SDL_GetPathInfo(g_test_wav_path, &info));
    u64 key = stems_hash_key(g_test_wav_path, (i64)info.size,
                             (i64)info.modify_time);
    char expect[PATH_MAX_LEN];
    (void)SDL_snprintf(expect, sizeof(expect), "%s%s/%016llx/",
                       T_ROOT, STEMS_DIR, (unsigned long long)key);
    CHECK(SDL_strcmp(dir, expect) == 0);

    /* Populate it, then discover the track fresh: status must come up READY. */
    CHECK(SDL_CreateDirectory(dir));
    fill_cache(dir, true);
    stems_t s2;
    CHECK(stems_init(&s2, T_ROOT));
    stems_set_track(&s2, g_test_wav_path);
    CHECK(s2.status == STEMS_READY);
    stems_quit(&s2);

    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_set_track_null_and_missing(void)
{
    TEST_BEGIN("stems_set_track_null_and_missing");
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));

    stems_set_track(&s, NULL);
    CHECK(s.status == STEMS_IDLE);
    CHECK(s.cache_dir[0] == '\0');

    /* A path that does not exist yields no cache dir, so begin() will refuse. */
    stems_set_track(&s, "definitely_not_a_file_98765.mp3");
    CHECK(s.cache_dir[0] == '\0');
    CHECK(!stems_begin(&s));
    CHECK(s.status == STEMS_FAILED);

    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_stem_path_and_name(void)
{
    TEST_BEGIN("stems_stem_path_and_name");
    stems_t s;
    SDL_memset(&s, 0, sizeof(s));
    char buf[PATH_MAX_LEN];

    /* No track set → no path. */
    CHECK(!stems_stem_path(&s, 0, buf, (i32)sizeof(buf)));

    (void)SDL_strlcpy(s.cache_dir, "cache/", sizeof(s.cache_dir));
    for (i32 i = 0; i < STEM_COUNT; i++) {
        CHECK(stems_stem_path(&s, i, buf, (i32)sizeof(buf)));
        CHECK(SDL_strstr(buf, "cache/") == buf);
        CHECK(stems_name(i) != NULL);
    }
    /* Out-of-range indices are rejected rather than reading off the tables. */
    CHECK(!stems_stem_path(&s, -1, buf, (i32)sizeof(buf)));
    CHECK(!stems_stem_path(&s, STEM_COUNT, buf, (i32)sizeof(buf)));
    CHECK(stems_name(-1) == NULL);
    CHECK(stems_name(STEM_COUNT) == NULL);
    /* A destination too small to hold the result must fail, not truncate. */
    CHECK(!stems_stem_path(&s, 0, buf, 4));
    TEST_END();
}

/* ── Job lifecycle ───────────────────────────────────────────────────────── */

/* Drive a track to STEMS_RUNNING with a mocked child.  Returns false if the
 * environment could not get there (then the caller skips). */
static bool start_fake_job(stems_t *s, char *dir, size_t dirsz)
{
    extern char g_test_wav_path[512];
    if (!stems_init(s, T_ROOT)) { return false; }
    stems_set_track(s, g_test_wav_path);
    if (s->cache_dir[0] == '\0') { return false; }
    (void)SDL_strlcpy(dir, s->cache_dir, dirsz);

    /* Bypass the real engine/model existence checks and process spawn. */
    g_mock_stems_skip_tools  = true;
    g_mock_process_fake      = true;
    bool ok = stems_begin(s);
    g_mock_stems_skip_tools  = false;
    return ok;
}

static void test_begin_no_track(void)
{
    TEST_BEGIN("stems_begin_no_track");
    stems_t s;
    SDL_memset(&s, 0, sizeof(s));
    CHECK(!stems_begin(&s));
    CHECK(s.status == STEMS_FAILED);
    CHECK(s.err[0] != '\0');
    TEST_END();
}

static void test_begin_missing_engine(void)
{
    TEST_BEGIN("stems_begin_missing_engine");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    stems_set_track(&s, g_test_wav_path);
    /* The engine is not installed in the test tree, so this is the real path a
     * user without the sidecar binary hits: a clean message, no crash. */
    CHECK(!stems_begin(&s));
    CHECK(s.status == STEMS_FAILED);
    CHECK(s.err[0] != '\0');
    CHECK(s.proc == NULL);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_begin_spawn_fail(void)
{
    TEST_BEGIN("stems_begin_spawn_fail");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    stems_set_track(&s, g_test_wav_path);
    char dir[PATH_MAX_LEN];
    (void)SDL_strlcpy(dir, s.cache_dir, sizeof(dir));

    g_mock_stems_skip_tools           = true;
    g_mock_SDL_CreateProcess_fail     = true;
    CHECK(!stems_begin(&s));
    CHECK(s.status == STEMS_FAILED);
    CHECK(s.proc == NULL);
    g_mock_SDL_CreateProcess_fail     = false;
    g_mock_stems_skip_tools           = false;

    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_begin_decode_fail(void)
{
    TEST_BEGIN("stems_begin_decode_fail");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    stems_set_track(&s, g_test_wav_path);
    char dir[PATH_MAX_LEN];
    (void)SDL_strlcpy(dir, s.cache_dir, sizeof(dir));

    g_mock_stems_skip_tools             = true;
    g_mock_MIX_CreateAudioDecoder_fail  = true;
    CHECK(!stems_begin(&s));
    CHECK(s.status == STEMS_FAILED);
    g_mock_MIX_CreateAudioDecoder_fail  = false;
    g_mock_stems_skip_tools             = false;

    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_begin_missing_model(void)
{
    TEST_BEGIN("stems_begin_missing_model");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    stems_set_track(&s, g_test_wav_path);

    /* Engine installed, weights not — exactly the state before the ~53 MB
     * model download, and it must be reported distinctly from a missing engine. */
    g_mock_stems_engine_only = true;
    CHECK(!stems_begin(&s));
    g_mock_stems_engine_only = false;
    CHECK(s.status == STEMS_FAILED);
    CHECK(SDL_strstr(s.err, "model") != NULL);
    CHECK(!stems_job_active(&s));

    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_begin_cache_mkdir_fail(void)
{
    TEST_BEGIN("stems_begin_cache_mkdir_fail");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    stems_set_track(&s, g_test_wav_path);

    /* The per-track cache directory cannot be created (read-only disk, quota). */
    g_mock_stems_skip_tools         = true;
    g_mock_SDL_CreateDirectory_fail = true;
    CHECK(!stems_begin(&s));
    g_mock_stems_skip_tools         = false;
    CHECK(s.status == STEMS_FAILED);
    CHECK(!stems_job_active(&s));
    CHECK(s.err[0] != '\0');

    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_init_root_path_too_long(void)
{
    TEST_BEGIN("stems_init_root_path_too_long");
    /* A pref path that leaves no room for the "stems/" suffix must be rejected
     * outright rather than silently truncated into the wrong directory. */
    static char longpref[PATH_MAX_LEN + 8];
    for (i32 i = 0; i < (i32)sizeof(longpref) - 1; i++) { longpref[i] = 'x'; }
    longpref[sizeof(longpref) - 1] = '\0';

    stems_t s;
    CHECK(!stems_init(&s, longpref));
    CHECK(s.root[0] == '\0');
    /* An inert module refuses to start work. */
    CHECK(!stems_begin(&s));
    CHECK(!stems_job_active(&s));
    TEST_END();
}

static void test_set_track_cache_path_too_long(void)
{
    TEST_BEGIN("stems_set_track_cache_path_too_long");
    extern char g_test_wav_path[512];
    /* A root just under the limit leaves no room for the hash subdirectory, so
     * set_track must end up with no cache_dir rather than a truncated one. */
    static char deep[PATH_MAX_LEN];
    (void)SDL_snprintf(deep, sizeof(deep), "%s", T_ROOT);
    i32 used = (i32)SDL_strlen(deep);
    for (i32 i = used; i < (i32)sizeof(deep) - 12; i++) { deep[i] = 'd'; }
    deep[sizeof(deep) - 12] = '/';
    deep[sizeof(deep) - 11] = '\0';

    stems_t s;
    SDL_memset(&s, 0, sizeof(s));
    /* Bypass stems_init (which would reject this) to reach the set_track path. */
    (void)SDL_strlcpy(s.root, deep, sizeof(s.root));
    stems_set_track(&s, g_test_wav_path);
    CHECK(s.cache_dir[0] == '\0');
    CHECK(s.status == STEMS_IDLE);
    /* And with no cache dir, begin refuses. */
    CHECK(!stems_begin(&s));
    CHECK(s.status == STEMS_FAILED);
    TEST_END();
}

static void test_decode_output_unwritable(void)
{
    TEST_BEGIN("stems_decode_output_unwritable");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    stems_set_track(&s, g_test_wav_path);
    char dir[PATH_MAX_LEN];
    (void)SDL_strlcpy(dir, s.cache_dir, sizeof(dir));

    /* The source decodes fine, but the destination cannot be opened for
     * writing: input.wav is occupied by a *directory* of that name.
     * stems_begin must fail cleanly rather than launch the engine on an input
     * that does not exist. */
    char inp[PATH_MAX_LEN];
    CHECK(SDL_CreateDirectory(dir));
    join(inp, sizeof(inp), dir, STEMS_INPUT_WAV);
    CHECK(SDL_CreateDirectory(inp));

    g_mock_stems_skip_tools = true;
    CHECK(!stems_begin(&s));
    g_mock_stems_skip_tools = false;
    CHECK(s.status == STEMS_FAILED);
    CHECK(!stems_job_active(&s));

    (void)SDL_RemovePath(inp);
    (void)SDL_RemovePath(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_begin_basepath_unavailable(void)
{
    TEST_BEGIN("stems_begin_basepath_unavailable");
    extern char g_test_wav_path[512];
    stems_t s;
    CHECK(stems_init(&s, T_ROOT));
    stems_set_track(&s, g_test_wav_path);

    /* Without a base path the sidecar cannot be located at all. */
    g_mock_SDL_GetBasePath_null = true;
    CHECK(!stems_begin(&s));
    g_mock_SDL_GetBasePath_null = false;
    CHECK(s.status == STEMS_FAILED);
    CHECK(!stems_job_active(&s));

    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_publish_marker_unwritable(void)
{
    TEST_BEGIN("stems_publish_marker_unwritable");
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_publish_marker_unwritable\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    /* Conversion succeeds but the completion marker cannot be written, because
     * done.txt is occupied by a directory.  The job must report failure and
     * leave no cache that would later read as complete. */
    CHECK(fill_engine_outputs(dir));
    char donep[PATH_MAX_LEN];
    join(donep, sizeof(donep), dir, STEMS_DONE_FILE);
    CHECK(SDL_CreateDirectory(donep));

    g_mock_process_exited   = true;
    g_mock_process_exitcode = 0;
    stems_poll(&s);

    CHECK(!stems_job_active(&s));
    CHECK(s.status == STEMS_FAILED);
    CHECK(!stems_cache_complete(dir));

    g_mock_process_fake   = false;
    g_mock_process_exited = false;
    (void)SDL_RemovePath(donep);
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_poll_idle_is_noop(void)
{
    TEST_BEGIN("stems_poll_idle_is_noop");
    stems_t s;
    SDL_memset(&s, 0, sizeof(s));
    stems_poll(&s);                    /* must not touch s.proc / assert */
    CHECK(s.status == STEMS_IDLE);
    CHECK(s.progress == 0);
    TEST_END();
}

static void test_running_to_failed_on_exit_code(void)
{
    TEST_BEGIN("stems_running_to_failed_on_exit_code");
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_running_to_failed_on_exit_code\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    CHECK(s.status == STEMS_RUNNING);

    /* Child reports failure → FAILED with a message, and no stale handles. */
    g_mock_process_exited   = true;
    g_mock_process_exitcode = 1;
    stems_poll(&s);
    CHECK(s.status == STEMS_FAILED);
    CHECK(s.err[0] != '\0');
    CHECK(s.proc == NULL);

    g_mock_process_fake = false;
    g_mock_process_exited = false;
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_running_to_failed_on_missing_outputs(void)
{
    TEST_BEGIN("stems_running_to_failed_on_missing_outputs");
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_running_to_failed_on_missing_outputs\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    /* Clean exit but the engine wrote nothing: must fail rather than publish an
     * empty cache that would later read as READY. */
    g_mock_process_exited   = true;
    g_mock_process_exitcode = 0;
    stems_poll(&s);
    CHECK(s.status == STEMS_FAILED);
    CHECK(!stems_cache_complete(dir));

    g_mock_process_fake = false;
    g_mock_process_exited = false;
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_progress_advances_while_running(void)
{
    TEST_BEGIN("stems_progress_advances_while_running");
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_progress_advances_while_running\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    CHECK(s.progress == 0);
    g_mock_process_exited = false;

    /* Progress must come from draining the child's stdout pipe.  The earlier
     * design redirected stdout to a file and tailed it, which silently never
     * worked: the child holds that file with a share mode that refuses every
     * reader, so the bar sat at 0% for whole jobs. */
    csp_mock_stage_process_output("[THREAD 0] ( 67.500%) segment 3\n");
    stems_poll(&s);
    CHECK(s.status == STEMS_RUNNING);
    CHECK(s.progress == 67);

    /* Monotonic: the engine's chunks report independently down one pipe, so an
     * older, lower reading arriving later must not walk the bar backwards. */
    csp_mock_stage_process_output("[THREAD 2] ( 12.000%) segment 1\n");
    stems_poll(&s);
    CHECK(s.progress == 67);

    /* ...but genuine forward progress is taken. */
    csp_mock_stage_process_output("[THREAD 1] ( 88.250%) segment 9\n");
    stems_poll(&s);
    CHECK(s.progress == 88);

    /* An empty pipe is not an error and leaves the reading alone. */
    csp_mock_stage_process_output(NULL);
    stems_poll(&s);
    CHECK(s.progress == 88);
    CHECK(s.status == STEMS_RUNNING);

    g_mock_process_fake = false;
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_cancel_from_running_and_idle(void)
{
    TEST_BEGIN("stems_cancel_from_running_and_idle");
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_cancel_from_running_and_idle\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    CHECK(s.status == STEMS_RUNNING);
    stems_cancel(&s);
    CHECK(s.status == STEMS_IDLE);
    CHECK(s.proc == NULL);
    CHECK(s.progress == 0);
    /* Partial output must be gone, so a later run does not see a bogus cache. */
    CHECK(!stems_cache_complete(dir));

    /* Cancelling again from IDLE is harmless. */
    stems_cancel(&s);
    CHECK(s.status == STEMS_IDLE);

    g_mock_process_fake = false;
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_job_survives_track_changes(void)
{
    TEST_BEGIN("stems_job_survives_track_changes");
    extern char g_test_wav_path[512];
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_job_survives_track_changes\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    CHECK(s.status == STEMS_RUNNING);
    CHECK(stems_job_active(&s));
    CHECK(SDL_strcmp(s.job_dir, dir) == 0);
    CHECK(s.job_title[0] != '\0');       /* named for the progress readout */

    /* Re-selecting the same track: still running, still this track's status. */
    stems_set_track(&s, g_test_wav_path);
    CHECK(stems_job_active(&s));
    CHECK(s.status == STEMS_RUNNING);

    /* Switching to a different track must NOT disturb the job — separation
     * takes minutes and has to outlive whatever else the user does.  The new
     * track simply has no stems of its own yet. */
    stems_set_track(&s, "some_other_track_that_does_not_exist.mp3");
    CHECK(stems_job_active(&s));
    CHECK(s.proc != NULL);
    CHECK(SDL_strcmp(s.job_dir, dir) == 0);   /* job keeps its own directory */
    CHECK(s.status == STEMS_IDLE);            /* ...but this track is idle    */

    /* Coming back to the separating track shows its progress again. */
    stems_set_track(&s, g_test_wav_path);
    CHECK(s.status == STEMS_RUNNING);

    /* And a second job cannot be started while one is in flight. */
    CHECK(!stems_begin(&s));
    CHECK(stems_job_active(&s));

    g_mock_process_fake = false;
    stems_cancel(&s);
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_job_completes_for_current_track(void)
{
    TEST_BEGIN("stems_job_completes_for_current_track");
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_job_completes_for_current_track\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    CHECK(fill_engine_outputs(dir));
    g_mock_process_exited   = true;
    g_mock_process_exitcode = 0;
    stems_poll(&s);

    /* The happy path: converted, published, and adopted by the current track. */
    CHECK(!stems_job_active(&s));
    CHECK(s.status == STEMS_READY);
    CHECK(s.notify);
    CHECK(s.progress == 100);
    CHECK(stems_cache_complete(dir));
    /* Conversion consumes the raw outputs and the decoded input. */
    char p[PATH_MAX_LEN];
    join(p, sizeof(p), dir, k_engine_files[0]);
    CHECK(!path_there(p));
    join(p, sizeof(p), dir, STEMS_INPUT_WAV);
    CHECK(!path_there(p));

    g_mock_process_fake   = false;
    g_mock_process_exited = false;
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_job_completes_for_other_track(void)
{
    TEST_BEGIN("stems_job_completes_for_other_track");
    stems_t s;
    char dir[PATH_MAX_LEN];
    if (!start_fake_job(&s, dir, sizeof(dir))) {
        printf("  SKIP  stems_job_completes_for_other_track\n");
        g_mock_process_fake = false;
        TEST_END();
        return;
    }
    /* Move to another track, then let the job finish.  The current track must
     * not be marked READY — its stems do not exist — but the user still has to
     * be told the background job landed. */
    stems_set_track(&s, "unrelated_track_xyz.mp3");
    CHECK(s.status == STEMS_IDLE);

    CHECK(fill_engine_outputs(dir));   /* engine wrote its raw outputs */
    g_mock_process_exited   = true;
    g_mock_process_exitcode = 0;
    stems_poll(&s);

    CHECK(!stems_job_active(&s));
    CHECK(s.status != STEMS_READY);    /* not this track's cache */
    CHECK(s.notify);                   /* but completion is announced */
    /* The cache is complete on disk, so selecting that track later finds it. */
    CHECK(stems_cache_complete(dir));

    g_mock_process_fake   = false;
    g_mock_process_exited = false;
    wipe_dir(dir);
    stems_quit(&s);
    (void)SDL_RemovePath(T_ROOT "stems/");
    (void)SDL_RemovePath(T_ROOT);
    TEST_END();
}

static void test_clear_notify(void)
{
    TEST_BEGIN("stems_clear_notify");
    stems_t s;
    SDL_memset(&s, 0, sizeof(s));
    s.notify = true;
    stems_clear_notify(&s);
    CHECK(!s.notify);
    stems_clear_notify(&s);      /* idempotent */
    CHECK(!s.notify);
    TEST_END();
}

/* ── Suite ───────────────────────────────────────────────────────────────── */

void run_stems_tests(void)
{
    /* stems.c decodes through SDL_mixer, which needs MIX_Init.  Left
     * initialised afterwards; the audio suite does its own init/quit. */
    if (!MIX_Init()) {
        printf("  NOTE  MIX_Init failed: %s\n", SDL_GetError());
    }

    test_hash_deterministic();
    test_hash_sensitive_to_each_input();
    test_hash_empty_path();

    test_progress_basic();
    test_progress_takes_last();
    test_progress_clamps_and_completes();
    test_progress_malformed();
    test_progress_truncated_tail();

    test_cache_complete_states();

    test_init_and_defaults();
    test_init_mkdir_fail();
    test_set_track_idle_and_ready();
    test_set_track_null_and_missing();
    test_stem_path_and_name();

    test_begin_no_track();
    test_begin_missing_engine();
    test_begin_spawn_fail();
    test_begin_decode_fail();
    test_begin_missing_model();
    test_begin_cache_mkdir_fail();
    test_init_root_path_too_long();
    test_set_track_cache_path_too_long();
    test_decode_output_unwritable();
    test_begin_basepath_unavailable();
    test_publish_marker_unwritable();

    test_poll_idle_is_noop();
    test_running_to_failed_on_exit_code();
    test_running_to_failed_on_missing_outputs();
    test_progress_advances_while_running();
    test_cancel_from_running_and_idle();
    test_job_survives_track_changes();
    test_job_completes_for_current_track();
    test_job_completes_for_other_track();
    test_clear_notify();
}
