/*
 * stems.c — Instrument-stem separation job and on-disk cache.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "stems.h"

#include <SDL3_mixer/SDL_mixer.h>
#include <assert.h>

#ifdef CSPLAYER_TESTING
#include "test_mocks.h"
#endif

/* ── File-private constants ──────────────────────────────────────────────── */

/* FNV-1a 64-bit parameters (Fowler / Noll / Vo, public domain). */
#define FNV64_BASIS  1469598103934665603ULL
#define FNV64_PRIME  1099511628211ULL

/* Canonical PCM WAV header size: RIFF + fmt (16) + data headers. */
#define WAV_HDR_BYTES  44

/*
 * Shared staging buffer for the two streaming operations, which never overlap:
 * decode_to_wav runs inside stems_begin / finish_job, progress reads only while
 * a job is RUNNING.  Static so neither path allocates (NASA Rule 3).
 */
#define SCRATCH_BYTES  32768
static u8 s_scratch[SCRATCH_BYTES];

/* ── File-private tables ─────────────────────────────────────────────────── */

/* Display names, in the engine's fixed source order. */
static const char *const s_stem_name[STEM_COUNT] = {
    "Drums", "Bass", "Other", "Vocals", "Guitar", "Piano"
};

/* Final cached filenames (16-bit, produced by convert_outputs). */
static const char *const s_stem_file[STEM_COUNT] = {
    "drums.wav", "bass.wav", "other.wav", "vocals.wav", "guitar.wav", "piano.wav"
};

/* Raw float32 filenames written by the engine. */
static const char *const s_engine_file[STEM_COUNT] = {
    "target_0_drums.wav",  "target_1_bass.wav",   "target_2_other.wav",
    "target_3_vocals.wav", "target_4_guitar.wav", "target_5_piano.wav"
};

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Concatenate a directory (which must end in a separator) and a filename. */
static bool path_join(char *dst, i32 dstsz, const char *dir, const char *name)
{
    assert(dst  != NULL);
    assert(dir  != NULL);
    assert(name != NULL);
    assert(dstsz > 0);

    i32 n = SDL_snprintf(dst, (size_t)dstsz, "%s%s", dir, name);
    return (n > 0 && n < dstsz);
}

/* Build the path of a file shipped next to the executable. */
static bool asset_path(char *dst, i32 dstsz, const char *name)
{
    assert(dst  != NULL);
    assert(name != NULL);
    assert(dstsz > 0);

    const char *base = SDL_GetBasePath();
    if (base == NULL) { return false; }
    i32 n = SDL_snprintf(dst, (size_t)dstsz, "%s%s", base, name);
    return (n > 0 && n < dstsz);
}

/* Basename without extension, for the progress readout.  Mirrors what
 * playlist.c derives for display, but stems.c must not depend on it. */
static void path_to_title(const char *path, char *dst, i32 dstsz)
{
    assert(path != NULL);
    assert(dst  != NULL);
    assert(dstsz > 0);

    const char *base   = path;
    const char *slash  = SDL_strrchr(path, '/');
    const char *bslash = SDL_strrchr(path, '\\');
    if (slash  != NULL && slash  > base) { base = slash  + 1; }
    if (bslash != NULL && bslash > base) { base = bslash + 1; }
    (void)SDL_strlcpy(dst, base, (size_t)dstsz);

    char *dot = SDL_strrchr(dst, '.');
    if (dot != NULL && dot != dst) { *dot = '\0'; }
}

/* Record a user-facing failure and move to STEMS_FAILED. */
static void set_err(stems_t *s, const char *msg)
{
    assert(s   != NULL);
    assert(msg != NULL);

    (void)SDL_strlcpy(s->err, msg, sizeof(s->err));
    s->status = STEMS_FAILED;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "stems: %s", msg);
}

/*
 * True when path names an existing, non-empty regular file.
 *
 * Deliberately stricter than "the path resolves": a directory of the right name
 * would otherwise pass as a cached stem or as the installed engine, and a
 * zero-length file left behind by a truncated write would pass as valid audio.
 */
static bool path_is_file(const char *path)
{
    assert(path != NULL);
    assert(path[0] != '\0');

    SDL_PathInfo info;
    SDL_memset(&info, 0, sizeof(info));
    if (!SDL_GetPathInfo(path, &info)) { return false; }
    return (info.type == SDL_PATHTYPE_FILE && info.size > 0);
}

/*
 * Write a canonical 44-byte PCM WAV header.  data_bytes may be zero here and
 * patched by patch_wav_sizes once the payload length is known.
 */
static bool wav_write_header(SDL_IOStream *io, u32 data_bytes)
{
    assert(io != NULL);
    assert(STEM_CHANNELS > 0 && STEM_BITS > 0);

    u32 block = (u32)STEM_CHANNELS * ((u32)STEM_BITS / 8u);
    bool ok = (SDL_WriteIO(io, "RIFF", 4) == 4);
    ok = ok && SDL_WriteU32LE(io, 36u + data_bytes);
    ok = ok && (SDL_WriteIO(io, "WAVEfmt ", 8) == 8);
    ok = ok && SDL_WriteU32LE(io, 16u);                  /* fmt chunk size */
    ok = ok && SDL_WriteU16LE(io, 1u);                   /* PCM            */
    ok = ok && SDL_WriteU16LE(io, (u16)STEM_CHANNELS);
    ok = ok && SDL_WriteU32LE(io, (u32)STEM_RATE);
    ok = ok && SDL_WriteU32LE(io, (u32)STEM_RATE * block);
    ok = ok && SDL_WriteU16LE(io, (u16)block);
    ok = ok && SDL_WriteU16LE(io, (u16)STEM_BITS);
    ok = ok && (SDL_WriteIO(io, "data", 4) == 4);
    ok = ok && SDL_WriteU32LE(io, data_bytes);
    return ok;
}

/* Patch the RIFF and data length fields of an already-written header. */
static bool patch_wav_sizes(SDL_IOStream *io, u32 data_bytes)
{
    assert(io != NULL);
    assert(WAV_HDR_BYTES == 44);

    if (SDL_SeekIO(io, 4, SDL_IO_SEEK_SET) < 0)      { return false; }
    if (!SDL_WriteU32LE(io, 36u + data_bytes))       { return false; }
    if (SDL_SeekIO(io, 40, SDL_IO_SEEK_SET) < 0)     { return false; }
    return SDL_WriteU32LE(io, data_bytes);
}

/*
 * Stream-decode any supported audio file into a 44.1 kHz stereo 16-bit WAV.
 *
 * Used for both halves of the pipeline: normalising the source track into
 * input.wav for the engine (which resamples 48 kHz material on the way, so the
 * stems' rate is authoritative afterwards), and converting the engine's
 * float32 output into the compact cached form.  Letting SDL_mixer do the
 * reading means no hand-rolled WAV chunk parsing anywhere in this module.
 */
static bool decode_to_wav(const char *src, const char *dst)
{
    assert(src != NULL);
    assert(dst != NULL);

    MIX_AudioDecoder *dec = MIX_CreateAudioDecoder(src, 0);
    if (dec == NULL) { return false; }

    SDL_IOStream *io = SDL_IOFromFile(dst, "wb");
    if (io == NULL) {
        MIX_DestroyAudioDecoder(dec);
        return false;
    }

    SDL_AudioSpec want;
    SDL_memset(&want, 0, sizeof(want));
    want.format   = SDL_AUDIO_S16;
    want.channels = STEM_CHANNELS;
    want.freq     = STEM_RATE;

    /* Bounded by construction (NASA Rule 2): a decoder that never reports EOF
     * cannot spin here.  The cap also keeps `total` clear of the 4 GB ceiling
     * that the WAV size fields impose. */
    bool ok    = wav_write_header(io, 0u);
    u32  total = 0;
    bool eof   = false;
    for (i32 chunk = 0; ok && chunk < STEM_DECODE_MAX_CHUNKS; chunk++) {
        i32 got = MIX_DecodeAudio(dec, s_scratch, SCRATCH_BYTES, &want);
        if (got < 0)  { ok = false; break; }        /* unrecoverable decode error */
        if (got == 0) { eof = true; break; }        /* end of file               */
        ok = (SDL_WriteIO(io, s_scratch, (size_t)got) == (size_t)got);
        total += (u32)got;
    }
    /* Refuse a truncated result rather than caching a silently shortened stem. */
    ok = ok && eof && (total > 0) && patch_wav_sizes(io, total);

    (void)SDL_CloseIO(io);
    MIX_DestroyAudioDecoder(dec);
    if (!ok) { (void)SDL_RemovePath(dst); }
    return ok;
}

/* Delete the engine's intermediates from dir, leaving a finished cache alone. */
static void wipe_partial(const char *dir)
{
    assert(dir != NULL);
    assert(STEM_COUNT > 0);

    if (dir[0] == '\0') { return; }

    char p[PATH_MAX_LEN];
    for (i32 i = 0; i < STEM_COUNT; i++) {
        if (path_join(p, (i32)sizeof(p), dir, s_engine_file[i])) {
            (void)SDL_RemovePath(p);
        }
    }
    if (path_join(p, (i32)sizeof(p), dir, STEMS_INPUT_WAV)) {
        (void)SDL_RemovePath(p);
    }
}

/* Convert every engine output in dir to the cached 16-bit form. */
static bool convert_outputs(const char *dir)
{
    assert(dir != NULL);
    assert(STEM_COUNT > 0);

    char src[PATH_MAX_LEN];
    char dst[PATH_MAX_LEN];
    for (i32 i = 0; i < STEM_COUNT; i++) {
        if (!path_join(src, (i32)sizeof(src), dir, s_engine_file[i]) ||
            !path_join(dst, (i32)sizeof(dst), dir, s_stem_file[i])) {
            return false;
        }
        if (!decode_to_wav(src, dst)) { return false; }
        (void)SDL_RemovePath(src);
    }
    /* input.wav exists only for the engine's benefit. */
    if (path_join(src, (i32)sizeof(src), dir, STEMS_INPUT_WAV)) {
        (void)SDL_RemovePath(src);
    }
    return true;
}

/* Release the child handle.  Safe when none is held.  Destroying the process
 * also closes the stdout pipe SDL created for it. */
static void release_child(stems_t *s)
{
    assert(s != NULL);
    assert(STEM_COUNT > 0);

    if (s->proc != NULL) {
        SDL_DestroyProcess(s->proc);
        s->proc = NULL;
    }
}

/* Publish a finished cache: done.txt is written LAST, which is what lets a
 * killed or crashed job be recognised as incomplete on the next run. */
static bool publish_cache(const char *dir)
{
    assert(dir != NULL);
    assert(dir[0] != '\0');

    char p[PATH_MAX_LEN];
    if (!path_join(p, (i32)sizeof(p), dir, STEMS_DONE_FILE)) { return false; }
    SDL_IOStream *io = SDL_IOFromFile(p, "wb");
    if (io == NULL) { return false; }
    (void)SDL_WriteIO(io, "ok\n", 3);
    return SDL_CloseIO(io);
}

/*
 * Drain everything the engine has written to its stdout pipe and update the
 * progress reading from it.
 *
 * This must run on every poll, not on a timer: the pipe buffer is small and a
 * child that fills it blocks mid-inference until someone reads.  SDL's process
 * pipes are non-blocking, so a read with nothing waiting simply returns 0.
 */
static void drain_child_output(stems_t *s)
{
    assert(s != NULL);
    assert(s->proc != NULL);

    SDL_IOStream *out = SDL_GetProcessOutput(s->proc);
    if (out == NULL) { return; }

    for (i32 i = 0; i < STEM_DRAIN_MAX; i++) {
        size_t got = SDL_ReadIO(out, s_scratch, SCRATCH_BYTES);
        if (got == 0) { break; }   /* NOT_READY or EOF — nothing more for now */

        i32 pct = stems_parse_progress((const char *)s_scratch, (i32)got);
        /* Clamp monotonically.  The engine separates several chunks in parallel
         * and each reports its own percentage down the same pipe, so the newest
         * value is whichever chunk happened to write last — taking it raw would
         * make the bar jump backwards.  Never going down reads as honest
         * progress and needs no per-chunk bookkeeping. */
        if (pct > s->progress) { s->progress = pct; }

        /* Keep the tail of the most recent chunk so a failure can say what the
         * engine was last doing instead of just "it failed". */
        i32 keep = (i32)got;
        if (keep > STEM_TAIL_BYTES - 1) { keep = STEM_TAIL_BYTES - 1; }
        SDL_memcpy(s->last_out, s_scratch + ((i32)got - keep), (size_t)keep);
        s->last_out[keep] = '\0';
        for (i32 k = 0; k < keep; k++) {
            if (s->last_out[k] == '\n' || s->last_out[k] == '\r') {
                s->last_out[k] = ' ';
            }
        }
        if (got < SCRATCH_BYTES) { break; }   /* pipe emptied */
    }
}

/*
 * The child has exited.  Validate and post-process its output, then publish.
 *
 * The job's directory is used throughout, not the current track's: by now the
 * user may well have moved on to a different song, and the results still belong
 * to whatever was being separated.  The current track's status is only touched
 * when the two happen to coincide.
 */
static void finish_job(stems_t *s, i32 exitcode)
{
    assert(s != NULL);
    assert(s->proc != NULL);

    /* One last drain before letting go of the pipe: a child that dies with a
     * message writes it just before exiting, which is exactly the output most
     * worth having and exactly what the poll before this one could not see. */
    drain_child_output(s);
    release_child(s);

    /* Does the finished job belong to the track on screen right now? */
    bool is_current = (s->cache_dir[0] != '\0' &&
                       SDL_strcmp(s->job_dir, s->cache_dir) == 0);

    const char *failure = NULL;
    if (exitcode != 0) {
        failure = "Separation engine failed";
    } else if (!convert_outputs(s->job_dir)) {
        failure = "Separation produced no usable output";
    } else if (!publish_cache(s->job_dir)) {
        failure = "Cannot write cache marker";
    }

    if (failure != NULL) {
        wipe_partial(s->job_dir);
        /* Quote what the engine was last saying — far more useful than a bare
         * "it failed" when diagnosing a crash or a bad model file. */
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "stems: %s (engine: %s)",
                     failure, s->last_out);
        if (is_current) { set_err(s, failure); }
    } else {
        s->progress = 100;
        if (is_current) { s->status = STEMS_READY; }
        /* Announce completion whichever track it was for, so a job finishing
         * in the background does not go unnoticed. */
        s->notify = true;
    }
    s->job_dir[0]   = '\0';
    s->job_title[0] = '\0';
}

/* Locate the engine executable and model weights, checking both exist. */
static bool resolve_tools(stems_t *s, char *engine, char *model, i32 sz)
{
    assert(s      != NULL);
    assert(engine != NULL);
    assert(model  != NULL);
    assert(sz > 0);

    if (!asset_path(engine, sz, STEM_ENGINE_EXE) ||
        !asset_path(model,  sz, STEM_MODEL_FILE)) {
        set_err(s, "Cannot resolve engine path");
        return false;
    }
    if (!path_is_file(engine)) {
        set_err(s, "Separation engine not installed");
        return false;
    }
    if (!path_is_file(model)) {
        set_err(s, "Separation model not installed");
        return false;
    }
    return true;
}

/*
 * Spawn the engine with its stdout (and stderr) redirected to the cache log, so
 * progress can be polled from a file rather than a pipe that could block the
 * render loop.  The background flag keeps Windows from opening a console window.
 */
static bool launch_engine(stems_t *s, char *engine, char *model, char *input)
{
    assert(s      != NULL);
    assert(engine != NULL);
    assert(model  != NULL);
    assert(input  != NULL);

    /* Thread count for the multi-threaded engine: it splits the track into this
     * many chunks and separates them in parallel, which is where nearly all the
     * speed comes from.  Leave a couple of cores for the UI and the OS. */
    i32 threads = SDL_GetNumLogicalCPUCores() - 2;
    if (threads < 1)               { threads = 1; }
    if (threads > STEM_MAX_THREADS) { threads = STEM_MAX_THREADS; }
    char nthreads[16];
    (void)SDL_snprintf(nthreads, sizeof(nthreads), "%d", threads);

    /* Non-const on purpose: SDL takes a plain char *const * here, and every
     * element already points at a mutable local or struct buffer. */
    char *args[6];
    args[0] = engine;
    args[1] = model;
    args[2] = input;
    args[3] = s->job_dir;
    args[4] = nthreads;
    args[5] = NULL;

    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) { return false; }

    (void)SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    /* STDIO_APP gives us a readable pipe rather than a file the child would
     * lock us out of; stderr joins it so failures land in the same stream. */
    (void)SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                SDL_PROCESS_STDIO_APP);
    (void)SDL_SetBooleanProperty(props,
                                 SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    /* Do NOT set SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN here.  It is
     * tempting — it is what suppresses the console window on Windows — but SDL
     * documents that a background process's exit code "is not available, and
     * will always be 0", and on POSIX it is worse than that: SDL polls with
     * kill(pid, 0) instead of waitpid, so the child is never reaped, lingers as
     * a zombie that kill() keeps reporting as alive, and the job is never seen
     * to finish.  The separation would run to completion and the UI would sit
     * at 100% for ever.  A console window is the lesser evil. */
    s->proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);

    return (s->proc != NULL);
}

/* ── Pure helpers ────────────────────────────────────────────────────────── */

u64 stems_hash_key(const char *path, i64 size, i64 mtime)
{
    assert(path != NULL);
    assert(FNV64_PRIME != 0);

    u64 h = FNV64_BASIS;
    for (const char *p = path; *p != '\0'; p++) {
        h ^= (u64)(u8)*p;
        h *= FNV64_PRIME;
    }
    /* Fold size and mtime through the same mixing so a file edited in place
     * lands in a different cache directory instead of reusing stale stems. */
    u64 tail[2] = { (u64)size, (u64)mtime };
    for (i32 i = 0; i < 2; i++) {
        for (i32 b = 0; b < 8; b++) {
            h ^= (tail[i] >> (b * 8)) & 0xFFu;
            h *= FNV64_PRIME;
        }
    }
    return h;
}

i32 stems_parse_progress(const char *buf, i32 len)
{
    assert(buf != NULL);
    assert(len >= 0);

    for (i32 i = len - 1; i >= 0; i--) {
        if (buf[i] != '%') { continue; }

        /* Walk back over the numeric field preceding the sign. */
        i32 b = i;
        while (b > 0 && ((buf[b - 1] >= '0' && buf[b - 1] <= '9') ||
                          buf[b - 1] == '.')) {
            b--;
        }
        i32  v   = 0;
        bool any = false;
        for (i32 k = b; k < i; k++) {
            if (buf[k] == '.') { break; }   /* integer part is enough for a bar */
            v   = v * 10 + (buf[k] - '0');
            any = true;
            if (v > 100) { v = 100; break; }
        }
        if (!any) { continue; }             /* a bare '%', keep scanning back */
        return v;
    }
    return -1;
}

bool stems_cache_complete(const char *dir)
{
    assert(dir != NULL);
    assert(STEM_COUNT > 0);

    if (dir[0] == '\0') { return false; }

    char p[PATH_MAX_LEN];
    /* The marker is written last, so checking it first rejects partial caches
     * with a single stat in the common "never separated" case. */
    if (!path_join(p, (i32)sizeof(p), dir, STEMS_DONE_FILE)) { return false; }
    if (!path_is_file(p)) { return false; }

    for (i32 i = 0; i < STEM_COUNT; i++) {
        if (!path_join(p, (i32)sizeof(p), dir, s_stem_file[i])) { return false; }
        if (!path_is_file(p)) { return false; }
    }
    return true;
}

/* ── Public interface ────────────────────────────────────────────────────── */

bool stems_init(stems_t *s, const char *pref_path)
{
    assert(s         != NULL);
    assert(pref_path != NULL);

    SDL_memset(s, 0, sizeof(*s));
    for (i32 i = 0; i < STEM_COUNT; i++) { s->enabled[i] = true; }

    i32 n = SDL_snprintf(s->root, sizeof(s->root), "%s%s/", pref_path, STEMS_DIR);
    if (n <= 0 || n >= (i32)sizeof(s->root)) {
        s->root[0] = '\0';
        return false;
    }
    if (!SDL_CreateDirectory(s->root)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "stems_init: cannot create '%s': %s", s->root, SDL_GetError());
        s->root[0] = '\0';
        return false;
    }
    return true;
}

void stems_set_track(stems_t *s, const char *src_path)
{
    assert(s != NULL);
    assert(STEM_COUNT > 0);

    /* Note what is deliberately absent here: nothing touches the running job.
     * Separation takes many minutes, so it has to survive the user playing
     * another song, reordering the playlist, or switching modes. */
    s->src[0]       = '\0';
    s->cache_dir[0] = '\0';
    s->err[0]       = '\0';
    s->status       = STEMS_IDLE;
    for (i32 i = 0; i < STEM_COUNT; i++) { s->enabled[i] = true; }

    if (src_path == NULL || s->root[0] == '\0') { return; }

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(src_path, &info)) { return; }

    (void)SDL_strlcpy(s->src, src_path, sizeof(s->src));
    u64 key = stems_hash_key(src_path, (i64)info.size, (i64)info.modify_time);
    i32 n = SDL_snprintf(s->cache_dir, sizeof(s->cache_dir), "%s%016llx/",
                         s->root, (unsigned long long)key);
    if (n <= 0 || n >= (i32)sizeof(s->cache_dir)) {
        s->cache_dir[0] = '\0';
        return;
    }

    if (stems_cache_complete(s->cache_dir)) {
        s->status = STEMS_READY;
    } else if (stems_job_active(s) &&
               SDL_strcmp(s->job_dir, s->cache_dir) == 0) {
        /* Returning to the track currently being separated. */
        s->status = STEMS_RUNNING;
    }
}

bool stems_job_active(const stems_t *s)
{
    assert(s != NULL);
    assert(s->progress >= 0 && s->progress <= 100);
    return (s->proc != NULL);
}

bool stems_begin(stems_t *s)
{
    assert(s != NULL);
    assert(STEM_COUNT > 0);

    /* One engine at a time: it already saturates the machine. */
    if (stems_job_active(s)) { return false; }
    if (s->cache_dir[0] == '\0' || s->src[0] == '\0') {
        set_err(s, "No track loaded");
        return false;
    }

    char engine[PATH_MAX_LEN];
    char model[PATH_MAX_LEN];
    if (!resolve_tools(s, engine, model, (i32)sizeof(engine))) { return false; }

    if (!SDL_CreateDirectory(s->cache_dir)) {
        set_err(s, "Cannot create cache directory");
        return false;
    }

    /* The job takes ownership of this directory for its lifetime; s->cache_dir
     * is free to move on to another track from here. */
    (void)SDL_strlcpy(s->job_dir, s->cache_dir, sizeof(s->job_dir));
    path_to_title(s->src, s->job_title, (i32)sizeof(s->job_title));

    char input[PATH_MAX_LEN];
    if (!path_join(input, (i32)sizeof(input), s->job_dir, STEMS_INPUT_WAV)) {
        set_err(s, "Cache path too long");
        s->job_dir[0] = '\0';
        return false;
    }
    if (!decode_to_wav(s->src, input)) {
        set_err(s, "Cannot decode this track");
        s->job_dir[0] = '\0';
        return false;
    }
    if (!launch_engine(s, engine, model, input)) {
        wipe_partial(s->job_dir);
        set_err(s, "Cannot start separation engine");
        s->job_dir[0] = '\0';
        return false;
    }

    s->err[0]      = '\0';
    s->last_out[0] = '\0';
    s->progress    = 0;
    s->status      = STEMS_RUNNING;
    return true;
}

void stems_poll(stems_t *s)
{
    assert(s != NULL);
    assert(s->progress >= 0 && s->progress <= 100);

    if (!stems_job_active(s)) { return; }
    assert(s->job_dir[0] != '\0');

    /* Drain first, and every time: the child stalls on a full pipe, and the
     * last of its output must be collected before the exit is acted on. */
    drain_child_output(s);

    int exitcode = 0;
    if (SDL_WaitProcess(s->proc, false, &exitcode)) {
        finish_job(s, (i32)exitcode);
    }
}

void stems_cancel(stems_t *s)
{
    assert(s != NULL);
    assert(STEM_COUNT > 0);

    if (!stems_job_active(s)) { return; }

    bool was_current = (s->cache_dir[0] != '\0' &&
                        SDL_strcmp(s->job_dir, s->cache_dir) == 0);

    (void)SDL_KillProcess(s->proc, true);

    /* Reap with a bounded poll rather than a blocking wait.  A blocking
     * SDL_WaitProcess here runs inside the render loop, so anything that delays
     * the child's death freezes the whole UI — and with the old background flag
     * it never returned at all, which is what turned "cancel" into a hang the
     * OS eventually killed.  A forced kill makes the child collectable almost
     * immediately, so a few milliseconds is plenty; if it somehow outlives that,
     * pressing on and leaking one zombie beats stalling the app.  SDL's POSIX
     * destroy does not reap, so this loop is the only thing that does. */
    for (i32 i = 0; i < STEM_REAP_TRIES; i++) {
        if (SDL_WaitProcess(s->proc, false, NULL)) { break; }
        SDL_Delay(STEM_REAP_DELAY_MS);
    }
    release_child(s);

    wipe_partial(s->job_dir);
    s->job_dir[0]   = '\0';
    s->job_title[0] = '\0';
    s->progress     = 0;
    if (was_current) { s->status = STEMS_IDLE; }
}

void stems_clear_notify(stems_t *s)
{
    assert(s != NULL);
    assert(s->progress >= 0);
    s->notify = false;
}

bool stems_stem_path(const stems_t *s, i32 idx, char *dst, i32 dstsz)
{
    assert(s   != NULL);
    assert(dst != NULL);
    assert(dstsz > 0);

    if (idx < 0 || idx >= STEM_COUNT)  { return false; }
    if (s->cache_dir[0] == '\0')       { return false; }
    return path_join(dst, dstsz, s->cache_dir, s_stem_file[idx]);
}

const char *stems_name(i32 idx)
{
    assert(STEM_COUNT > 0);
    assert(idx > INT32_MIN);
    if (idx < 0 || idx >= STEM_COUNT) { return NULL; }
    return s_stem_name[idx];
}

void stems_quit(stems_t *s)
{
    assert(s != NULL);
    assert(STEM_COUNT > 0);
    stems_cancel(s);
}
