/*
 * test_audio.c — Integration tests for src/audio.c.
 *
 * SDL_AUDIODRIVER=dummy is set by test_main.c before SDL_Init so MIX_Init
 * opens a virtual device without real hardware.  SDL failure paths are
 * exercised via the compile-time mock layer (CSPLAYER_TESTING).
 *
 * Expected-failure tests temporarily redirect SDL's log output to a null
 * sink so the intentional SDL_LogError calls inside audio.c do not pollute
 * the test output.  suppress_sdl_log / restore_sdl_log bracket only the
 * specific call that is expected to fail.
 */

#include <math.h>
#include <SDL3/SDL.h>

#include "harness.h"
#include "types.h"
#include "audio.h"
#include "test_mocks.h"

/* ── External globals (defined in test_main.c) ───────────────────────────── */

extern char g_test_wav_path[];

/* ── SDL log suppression for expected-failure tests ─────────────────────── */

static void null_log_fn(void *userdata, int category,
                         SDL_LogPriority priority, const char *message)
{
    (void)userdata; (void)category; (void)priority; (void)message;
}

static SDL_LogOutputFunction g_saved_log_fn       = NULL;
static void                 *g_saved_log_userdata = NULL;

static void suppress_sdl_log(void)
{
    SDL_GetLogOutputFunction(&g_saved_log_fn, &g_saved_log_userdata);
    SDL_SetLogOutputFunction(null_log_fn, NULL);
}

static void restore_sdl_log(void)
{
    SDL_SetLogOutputFunction(g_saved_log_fn, g_saved_log_userdata);
}

/* ── Tests ───────────────────────────────────────────────────────────────── */

/* ---- audio_init ---------------------------------------------------------- */

static void test_audio_init_success(void)
{
    TEST_BEGIN("audio_init_success");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(a.status    == AUDIO_STATUS_STOPPED);
    CHECK(a.mixer     != NULL);
    CHECK(a.track     != NULL);
    CHECK(a.ring_mutex != NULL);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_init_fail_mix_init(void)
{
    TEST_BEGIN("audio_init_fail_mix_init");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    g_mock_MIX_Init_fail = true;
    suppress_sdl_log();
    CHECK(!audio_init(&a));
    restore_sdl_log();
    CHECK(a.mixer == NULL);
    TEST_END();
}

static void test_audio_init_fail_mixer_device(void)
{
    TEST_BEGIN("audio_init_fail_mixer_device");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    g_mock_MIX_CreateMixerDevice_fail = true;
    suppress_sdl_log();
    CHECK(!audio_init(&a));
    restore_sdl_log();
    CHECK(a.mixer == NULL);
    TEST_END();
}

static void test_audio_init_fail_track(void)
{
    TEST_BEGIN("audio_init_fail_track");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    g_mock_MIX_CreateTrack_fail = true;
    suppress_sdl_log();
    CHECK(!audio_init(&a));
    restore_sdl_log();
    CHECK(a.track == NULL);
    TEST_END();
}

static void test_audio_init_fail_mutex(void)
{
    TEST_BEGIN("audio_init_fail_mutex");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    g_mock_SDL_CreateMutex_fail = true;
    suppress_sdl_log();
    CHECK(!audio_init(&a));
    restore_sdl_log();
    CHECK(a.ring_mutex == NULL);
    TEST_END();
}

/* ---- audio_quit with partial/zeroed struct ------------------------------- */

static void test_audio_quit_zeroed(void)
{
    TEST_BEGIN("audio_quit_zeroed");
    /* audio_quit must not crash when all pointers are NULL. */
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    audio_quit(&a);   /* no crash → pass */
    TEST_END();
}

/* ---- audio_load ---------------------------------------------------------- */

static void test_audio_load_bad_path(void)
{
    TEST_BEGIN("audio_load_bad_path");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    suppress_sdl_log();
    CHECK(!audio_load(&a, "/nonexistent/path/that/does/not/exist.wav"));
    restore_sdl_log();
    CHECK(a.audio == NULL);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_load_fail_load_audio(void)
{
    TEST_BEGIN("audio_load_fail_load_audio");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    g_mock_MIX_LoadAudio_fail = true;
    suppress_sdl_log();
    CHECK(!audio_load(&a, g_test_wav_path));
    restore_sdl_log();
    CHECK(a.audio == NULL);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_load_fail_set_track(void)
{
    TEST_BEGIN("audio_load_fail_set_track");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    g_mock_MIX_SetTrackAudio_fail = true;
    suppress_sdl_log();
    CHECK(!audio_load(&a, g_test_wav_path));
    restore_sdl_log();
    CHECK(a.audio == NULL);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_load_success(void)
{
    TEST_BEGIN("audio_load_success");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(a.audio   != NULL);
    CHECK(a.sample_rate > 0.0f);
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_play ---------------------------------------------------------- */

static void test_audio_play_no_audio(void)
{
    TEST_BEGIN("audio_play_no_audio");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    /* No load — audio==NULL → play returns false. */
    suppress_sdl_log();
    CHECK(!audio_play(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

static void test_audio_play_fail_props(void)
{
    TEST_BEGIN("audio_play_fail_props");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    g_mock_SDL_CreateProperties_fail = true;
    suppress_sdl_log();
    CHECK(!audio_play(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

static void test_audio_play_fail_play(void)
{
    TEST_BEGIN("audio_play_fail_play");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    g_mock_MIX_PlayTrack_fail = true;
    suppress_sdl_log();
    CHECK(!audio_play(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

static void test_audio_play_success(void)
{
    TEST_BEGIN("audio_play_success");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    CHECK(a.status == AUDIO_STATUS_PLAYING);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_play_from_paused(void)
{
    TEST_BEGIN("audio_play_from_paused");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    /* Pause then play again: exercises MIX_ResumeTrack path. */
    CHECK(audio_toggle_pause(&a));
    CHECK(a.status == AUDIO_STATUS_PAUSED);
    CHECK(audio_play(&a));
    CHECK(a.status == AUDIO_STATUS_PLAYING);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_play_from_paused_fail(void)
{
    TEST_BEGIN("audio_play_from_paused_fail");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    CHECK(audio_toggle_pause(&a));
    g_mock_MIX_ResumeTrack_fail = true;
    suppress_sdl_log();
    CHECK(!audio_play(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_toggle_pause -------------------------------------------------- */

static void test_audio_toggle_pause_playing(void)
{
    TEST_BEGIN("audio_toggle_pause_playing");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    CHECK(audio_toggle_pause(&a));
    CHECK(a.status == AUDIO_STATUS_PAUSED);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_toggle_pause_fail(void)
{
    TEST_BEGIN("audio_toggle_pause_fail");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    g_mock_MIX_PauseTrack_fail = true;
    suppress_sdl_log();
    CHECK(!audio_toggle_pause(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

static void test_audio_toggle_pause_paused(void)
{
    TEST_BEGIN("audio_toggle_pause_paused");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    CHECK(audio_toggle_pause(&a));
    /* Pause→Play via toggle. */
    CHECK(audio_toggle_pause(&a));
    CHECK(a.status == AUDIO_STATUS_PLAYING);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_toggle_pause_stopped(void)
{
    TEST_BEGIN("audio_toggle_pause_stopped");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    /* status=STOPPED and no audio — toggle returns false. */
    suppress_sdl_log();
    CHECK(!audio_toggle_pause(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_stop ---------------------------------------------------------- */

static void test_audio_stop_success(void)
{
    TEST_BEGIN("audio_stop_success");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    CHECK(audio_stop(&a));
    CHECK(a.status == AUDIO_STATUS_STOPPED);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_stop_fail(void)
{
    TEST_BEGIN("audio_stop_fail");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    g_mock_MIX_StopTrack_fail = true;
    suppress_sdl_log();
    CHECK(!audio_stop(&a));
    restore_sdl_log();
    /* status unchanged after failure */
    CHECK(a.status == AUDIO_STATUS_PLAYING);
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_seek ---------------------------------------------------------- */

static void test_audio_seek_no_audio(void)
{
    TEST_BEGIN("audio_seek_no_audio");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    /* audio==NULL → seek returns false. */
    suppress_sdl_log();
    CHECK(!audio_seek(&a, 0.5f));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

static void test_audio_seek_no_duration(void)
{
    TEST_BEGIN("audio_seek_no_duration");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    /* Force duration_frames=0 to hit the duration<=0 branch. */
    a.duration_frames = 0;
    suppress_sdl_log();
    CHECK(!audio_seek(&a, 0.5f));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

static void test_audio_seek_success(void)
{
    TEST_BEGIN("audio_seek_success");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    CHECK(audio_seek(&a, 0.5f));
    audio_quit(&a);
    TEST_END();
}

static void test_audio_seek_fail(void)
{
    TEST_BEGIN("audio_seek_fail");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    g_mock_MIX_SetTrackPlaybackPosition_fail = true;
    suppress_sdl_log();
    CHECK(!audio_seek(&a, 0.5f));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_get_pos ------------------------------------------------------- */

static void test_audio_get_pos_no_track(void)
{
    TEST_BEGIN("audio_get_pos_no_track");
    /* track==NULL → 0.0f */
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    a.sample_rate = 44100.0f;   /* satisfy assert */
    CHECK(audio_get_pos(&a) == 0.0f);
    TEST_END();
}

static void test_audio_get_pos_zero_duration(void)
{
    TEST_BEGIN("audio_get_pos_zero_duration");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    /* duration_frames defaults to -1 after init → <=0 branch. */
    CHECK(audio_get_pos(&a) == 0.0f);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_get_pos_valid(void)
{
    TEST_BEGIN("audio_get_pos_valid");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    f32 pos = audio_get_pos(&a);
    CHECK(pos >= 0.0f && pos <= 1.0f);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_get_pos_negative_cur(void)
{
    TEST_BEGIN("audio_get_pos_negative_cur");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    /* Mock returns -1 → cur<0 branch → return 0.0f */
    g_mock_MIX_GetTrackPlaybackPosition_neg = true;
    CHECK(audio_get_pos(&a) == 0.0f);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_get_pos_overflow(void)
{
    TEST_BEGIN("audio_get_pos_overflow");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    /* Mock returns INT64_MAX → pos > 1.0f → clamped to 1.0f */
    g_mock_MIX_GetTrackPlaybackPosition_over = true;
    CHECK(audio_get_pos(&a) == 1.0f);
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_snapshot_ring ------------------------------------------------- */

static void test_audio_snapshot_ring(void)
{
    TEST_BEGIN("audio_snapshot_ring");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));

    /* Write known values at a known ring_write position. */
    a.ring_write = 0;
    for (i32 i = 0; i < RING_SIZE; i++) {
        a.ring[i] = (f32)i;
    }

    static f32 snap[RING_SIZE];
    audio_snapshot_ring(&a, snap);

    /* Since ring_write=0, snapshot is ring[0..RING_SIZE-1] in order. */
    bool ok = true;
    for (i32 i = 0; i < RING_SIZE; i++) {
        if (snap[i] != (f32)i) { ok = false; break; }
    }
    CHECK(ok);

    /* Test with ring_write at mid-point: oldest sample is at ring_write. */
    a.ring_write = 10;
    audio_snapshot_ring(&a, snap);
    CHECK(snap[0] == (f32)10);   /* oldest entry is at index 10 */
    CHECK(snap[RING_SIZE - 10] == (f32)0);  /* wraps around */

    audio_quit(&a);
    TEST_END();
}

/* ---- audio_set_volume ---------------------------------------------------- */

static void test_audio_set_volume_no_mixer(void)
{
    TEST_BEGIN("audio_set_volume_no_mixer");
    /* mixer==NULL → early return, no crash. */
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    a.sample_rate = 44100.0f;
    audio_set_volume(&a, 0.5f);   /* must not crash */
    TEST_END();
}

static void test_audio_set_volume_valid(void)
{
    TEST_BEGIN("audio_set_volume_valid");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    audio_set_volume(&a, 0.75f);   /* just checks it doesn't crash */
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_unload -------------------------------------------------------- */

static void test_audio_unload(void)
{
    TEST_BEGIN("audio_unload");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(a.audio != NULL);
    audio_unload(&a);
    CHECK(a.audio  == NULL);
    CHECK(a.status == AUDIO_STATUS_STOPPED);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_unload_null(void)
{
    TEST_BEGIN("audio_unload_null");
    /* unload_audio is safe when audio is already NULL. */
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    audio_unload(&a);   /* audio==NULL → no crash */
    audio_quit(&a);
    TEST_END();
}

/* ---- track_stopped_cb branches (via audio_stop triggering the callback) - */

static void test_audio_track_stopped_event_zero(void)
{
    TEST_BEGIN("audio_track_stopped_event_zero");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    /* event_track_done=0 → callback early-returns without pushing event.
     * audio_stop calls MIX_StopTrack, which may fire track_stopped_cb. */
    a.event_track_done = 0;
    CHECK(audio_stop(&a));
    CHECK(a.status == AUDIO_STATUS_STOPPED);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_track_stopped_event_nonzero(void)
{
    TEST_BEGIN("audio_track_stopped_event_nonzero");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    CHECK(audio_init(&a));
    CHECK(audio_load(&a, g_test_wav_path));
    CHECK(audio_play(&a));
    /* Register a user event id so the callback pushes it. */
    u32 ev_id = SDL_RegisterEvents(1);
    a.event_track_done = ev_id;
    CHECK(audio_stop(&a));
    CHECK(a.status == AUDIO_STATUS_STOPPED);
    /* Drain any pending events (the callback may or may not have fired
     * depending on SDL3_mixer's dummy driver behaviour). */
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) { /* drain */ }
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_load_raw ----------------------------------------------------- */

/* A small silent PCM buffer shared by all audio_load_raw tests. */
static f32 g_raw_buf[SYNTH_SAMPLE_RATE];   /* 1 second of silence at 44100 Hz */

static void test_audio_load_raw_success(void)
{
    TEST_BEGIN("audio_load_raw_success");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    SDL_memset(g_raw_buf, 0, sizeof(g_raw_buf));
    CHECK(audio_init(&a));
    CHECK(audio_load_raw(&a, g_raw_buf, SYNTH_SAMPLE_RATE, 44100.0f));
    CHECK(a.audio          != NULL);
    CHECK(a.duration_frames == SYNTH_SAMPLE_RATE);
    CHECK(a.channels        == 1);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_load_raw_fail_load(void)
{
    TEST_BEGIN("audio_load_raw_fail_load");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    SDL_memset(g_raw_buf, 0, sizeof(g_raw_buf));
    CHECK(audio_init(&a));
    g_mock_MIX_LoadRawAudioNoCopy_fail = true;
    suppress_sdl_log();
    CHECK(!audio_load_raw(&a, g_raw_buf, SYNTH_SAMPLE_RATE, 44100.0f));
    restore_sdl_log();
    CHECK(a.audio == NULL);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_load_raw_fail_set_track(void)
{
    TEST_BEGIN("audio_load_raw_fail_set_track");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    SDL_memset(g_raw_buf, 0, sizeof(g_raw_buf));
    CHECK(audio_init(&a));
    g_mock_MIX_SetTrackAudio_fail = true;
    suppress_sdl_log();
    CHECK(!audio_load_raw(&a, g_raw_buf, SYNTH_SAMPLE_RATE, 44100.0f));
    restore_sdl_log();
    CHECK(a.audio == NULL);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_load_raw_fail_raw_callback(void)
{
    TEST_BEGIN("audio_load_raw_fail_raw_callback");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    SDL_memset(g_raw_buf, 0, sizeof(g_raw_buf));
    CHECK(audio_init(&a));
    g_mock_MIX_SetTrackRawCallback_fail = true;
    suppress_sdl_log();
    CHECK(!audio_load_raw(&a, g_raw_buf, SYNTH_SAMPLE_RATE, 44100.0f));
    restore_sdl_log();
    CHECK(a.audio == NULL);
    audio_quit(&a);
    TEST_END();
}

/* ---- audio_play_looped -------------------------------------------------- */

static void test_audio_play_looped_success(void)
{
    TEST_BEGIN("audio_play_looped_success");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    SDL_memset(g_raw_buf, 0, sizeof(g_raw_buf));
    CHECK(audio_init(&a));
    CHECK(audio_load_raw(&a, g_raw_buf, SYNTH_SAMPLE_RATE, 44100.0f));
    CHECK(audio_play_looped(&a));
    CHECK(a.status == AUDIO_STATUS_PLAYING);
    audio_quit(&a);
    TEST_END();
}

static void test_audio_play_looped_fail_props(void)
{
    TEST_BEGIN("audio_play_looped_fail_props");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    SDL_memset(g_raw_buf, 0, sizeof(g_raw_buf));
    CHECK(audio_init(&a));
    CHECK(audio_load_raw(&a, g_raw_buf, SYNTH_SAMPLE_RATE, 44100.0f));
    g_mock_SDL_CreateProperties_fail = true;
    suppress_sdl_log();
    CHECK(!audio_play_looped(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

static void test_audio_play_looped_fail_play(void)
{
    TEST_BEGIN("audio_play_looped_fail_play");
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    SDL_memset(g_raw_buf, 0, sizeof(g_raw_buf));
    CHECK(audio_init(&a));
    CHECK(audio_load_raw(&a, g_raw_buf, SYNTH_SAMPLE_RATE, 44100.0f));
    g_mock_MIX_PlayTrack_fail = true;
    suppress_sdl_log();
    CHECK(!audio_play_looped(&a));
    restore_sdl_log();
    audio_quit(&a);
    TEST_END();
}

/* ── Suite ───────────────────────────────────────────────────────────────── */

/* ── Instrument-stem playback ────────────────────────────────────────────────
 * audio_t now carries six RING_SIZE stem buffers (~224 KB total), so these
 * tests share one file-scope instance rather than putting it on the stack. */

static audio_t g_sa;

/* Six paths, all the test WAV — audio.c only cares that each one loads. */
static void stem_paths(const char *out[STEM_COUNT])
{
    for (i32 i = 0; i < STEM_COUNT; i++) { out[i] = g_test_wav_path; }
}

static void test_audio_load_stems_success(void)
{
    TEST_BEGIN("audio_load_stems_success");
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));
    CHECK(audio_load_stems(&g_sa, paths));
    CHECK(g_sa.stems_active);
    for (i32 i = 0; i < STEM_COUNT; i++) {
        CHECK(g_sa.stem_track[i] != NULL);
        CHECK(g_sa.stem_audio[i] != NULL);
        CHECK(g_sa.stem_cb_ud[i].idx == i);
        CHECK(g_sa.stem_cb_ud[i].a == &g_sa);
    }
    /* Stem 0 is the reference clock, so its duration must be adopted. */
    CHECK(g_sa.duration_frames > 0);
    CHECK(g_sa.sample_rate > 0.0f);
    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_load_stems_null_path(void)
{
    TEST_BEGIN("audio_load_stems_null_path");
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    paths[4] = NULL;
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));
    suppress_sdl_log();
    CHECK(!audio_load_stems(&g_sa, paths));
    restore_sdl_log();
    /* A partial load must leave nothing active or half-attached. */
    CHECK(!g_sa.stems_active);
    for (i32 i = 0; i < STEM_COUNT; i++) { CHECK(g_sa.stem_audio[i] == NULL); }
    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_load_stems_failure_branches(void)
{
    TEST_BEGIN("audio_load_stems_failure_branches");
    const char *paths[STEM_COUNT];
    stem_paths(paths);

    /* Each mock is one-shot, so it trips on the first stem and the whole load
     * must unwind.  Every failure point inside load_one_stem is covered. */
    bool *flags[4] = {
        &g_mock_MIX_LoadAudio_fail,
        &g_mock_MIX_SetTrackAudio_fail,
        &g_mock_MIX_TagTrack_fail,
        &g_mock_MIX_SetTrackRawCallback_fail,
    };
    for (i32 i = 0; i < 4; i++) {
        SDL_memset(&g_sa, 0, sizeof(g_sa));
        CHECK(audio_init(&g_sa));
        *flags[i] = true;
        suppress_sdl_log();
        CHECK(!audio_load_stems(&g_sa, paths));
        restore_sdl_log();
        CHECK(!g_sa.stems_active);
        *flags[i] = false;
        audio_quit(&g_sa);
    }
    TEST_END();
}

static void test_audio_play_stems(void)
{
    TEST_BEGIN("audio_play_stems");
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));

    /* Not loaded → refuses rather than starting an empty tag. */
    CHECK(!audio_play_stems(&g_sa));

    CHECK(audio_load_stems(&g_sa, paths));
    CHECK(audio_play_stems(&g_sa));
    CHECK(g_sa.status == AUDIO_STATUS_PLAYING);
    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_play_stems_failures(void)
{
    TEST_BEGIN("audio_play_stems_failures");
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));
    CHECK(audio_load_stems(&g_sa, paths));

    g_mock_SDL_CreateProperties_fail = true;
    suppress_sdl_log();
    CHECK(!audio_play_stems(&g_sa));
    restore_sdl_log();
    g_mock_SDL_CreateProperties_fail = false;

    g_mock_MIX_PlayTag_fail = true;
    suppress_sdl_log();
    CHECK(!audio_play_stems(&g_sa));
    restore_sdl_log();
    g_mock_MIX_PlayTag_fail = false;

    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_stem_transport(void)
{
    TEST_BEGIN("audio_stem_transport");
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));
    CHECK(audio_load_stems(&g_sa, paths));

    /* audio_play routes to the tag when stems are active. */
    CHECK(audio_play(&g_sa));
    CHECK(g_sa.status == AUDIO_STATUS_PLAYING);
    /* Pause and resume must both go through the tag, not the idle single track. */
    CHECK(audio_toggle_pause(&g_sa));
    CHECK(g_sa.status == AUDIO_STATUS_PAUSED);
    CHECK(audio_play(&g_sa));
    CHECK(g_sa.status == AUDIO_STATUS_PLAYING);
    /* Seek and position come from the stem tracks. */
    CHECK(audio_seek(&g_sa, 0.5f));
    f32 pos = audio_get_pos(&g_sa);
    CHECK(pos >= 0.0f && pos <= 1.0f);
    CHECK(audio_stop(&g_sa));
    CHECK(g_sa.status == AUDIO_STATUS_STOPPED);

    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_set_stem_enabled(void)
{
    TEST_BEGIN("audio_set_stem_enabled");
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));

    /* Before load: must be a silent no-op, not a crash. */
    audio_set_stem_enabled(&g_sa, 0, false);
    CHECK(!g_sa.stems_active);

    CHECK(audio_load_stems(&g_sa, paths));
    for (i32 i = 0; i < STEM_COUNT; i++) {
        audio_set_stem_enabled(&g_sa, i, false);
        audio_set_stem_enabled(&g_sa, i, true);
    }
    CHECK(g_sa.stems_active);
    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_snapshot_stem_ring(void)
{
    TEST_BEGIN("audio_snapshot_stem_ring");
    static f32 dst[RING_SIZE];
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));

    /* Not loaded → silence, so callers never read uninitialised audio. */
    for (i32 i = 0; i < RING_SIZE; i++) { dst[i] = 1.0f; }
    audio_snapshot_stem_ring(&g_sa, 2, dst);
    CHECK(dst[0] == 0.0f);
    CHECK(dst[RING_SIZE - 1] == 0.0f);

    CHECK(audio_load_stems(&g_sa, paths));
    /* Seed a known ramp and check the snapshot is rotated oldest-first. */
    for (i32 i = 0; i < RING_SIZE; i++) { g_sa.stem_ring[3][i] = (f32)i; }
    g_sa.stem_ring_write[3] = 10;
    audio_snapshot_stem_ring(&g_sa, 3, dst);
    CHECK(dst[0] == 10.0f);
    CHECK(dst[RING_SIZE - 11] == (f32)(RING_SIZE - 1));
    CHECK(dst[RING_SIZE - 10] == 0.0f);

    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_unload_stems_and_switch_back(void)
{
    TEST_BEGIN("audio_unload_stems_and_switch_back");
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));

    /* Unloading when nothing is loaded is harmless. */
    audio_unload_stems(&g_sa);
    CHECK(!g_sa.stems_active);

    CHECK(audio_load_stems(&g_sa, paths));
    CHECK(g_sa.stems_active);
    audio_unload_stems(&g_sa);
    CHECK(!g_sa.stems_active);
    CHECK(g_sa.duration_frames == -1);
    audio_unload_stems(&g_sa);        /* idempotent */
    CHECK(!g_sa.stems_active);

    /* Loading a plain track must also end stem playback. */
    CHECK(audio_load_stems(&g_sa, paths));
    CHECK(g_sa.stems_active);
    CHECK(audio_load(&g_sa, g_test_wav_path));
    CHECK(!g_sa.stems_active);
    CHECK(g_sa.audio != NULL);

    audio_quit(&g_sa);
    TEST_END();
}

/* ── PCM capture callbacks ──────────────────────────────────────────────────
 * Driven through the test seams in audio.c, since SDL's dummy audio driver
 * never fires them.  What matters here is the channel down-mix and the ring
 * wrap-around, both of which feed every waveform the app draws. */

static void test_audio_capture_downmix(void)
{
    TEST_BEGIN("audio_capture_downmix");
    static f32 pcm[8];
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));

    /* Stereo pairs average to mono: (1.0 + 0.0)/2 = 0.5 each frame. */
    for (i32 i = 0; i < 8; i += 2) { pcm[i] = 1.0f; pcm[i + 1] = 0.0f; }
    csp_test_feed_raw(&g_sa, pcm, 8, 2);
    CHECK(g_sa.ring_write == 4);
    for (i32 i = 0; i < 4; i++) { CHECK_NEAR(g_sa.ring[i], 0.5f, 1e-6f); }
    /* The spec's rate and channel count are adopted from the callback. */
    CHECK(g_sa.channels == 2);
    CHECK(g_sa.sample_rate > 0.0f);

    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_capture_ring_wraps(void)
{
    TEST_BEGIN("audio_capture_ring_wraps");
    static f32 pcm[16];
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));

    for (i32 i = 0; i < 16; i++) { pcm[i] = 1.0f; }
    /* Start near the end so the write wraps to the front mid-batch. */
    g_sa.ring_write = RING_SIZE - 4;
    csp_test_feed_raw(&g_sa, pcm, 16, 1);
    CHECK(g_sa.ring_write == 12);
    CHECK_NEAR(g_sa.ring[RING_SIZE - 1], 1.0f, 1e-6f);
    CHECK_NEAR(g_sa.ring[0],  1.0f, 1e-6f);
    CHECK_NEAR(g_sa.ring[11], 1.0f, 1e-6f);

    audio_quit(&g_sa);
    TEST_END();
}

static void test_audio_capture_per_stem(void)
{
    TEST_BEGIN("audio_capture_per_stem");
    static f32 pcm[4];
    const char *paths[STEM_COUNT];
    stem_paths(paths);
    SDL_memset(&g_sa, 0, sizeof(g_sa));
    CHECK(audio_init(&g_sa));
    CHECK(audio_load_stems(&g_sa, paths));

    /* Each stem must land in its own ring and nowhere else — a mixed-up index
     * would draw one instrument's waveform in another's panel. */
    for (i32 idx = 0; idx < STEM_COUNT; idx++) {
        for (i32 i = 0; i < 4; i++) { pcm[i] = (f32)(idx + 1); }
        csp_test_feed_stem(&g_sa, idx, pcm, 4, 2);
        CHECK(g_sa.stem_ring_write[idx] == 2);
        CHECK_NEAR(g_sa.stem_ring[idx][0], (f32)(idx + 1), 1e-6f);
        CHECK_NEAR(g_sa.stem_ring[idx][1], (f32)(idx + 1), 1e-6f);
    }
    for (i32 idx = 0; idx < STEM_COUNT; idx++) {
        CHECK_NEAR(g_sa.stem_ring[idx][0], (f32)(idx + 1), 1e-6f);
    }

    /* A muted stem still captures — that is what keeps its panel animating. */
    audio_set_stem_enabled(&g_sa, 0, false);
    for (i32 i = 0; i < 4; i++) { pcm[i] = 9.0f; }
    csp_test_feed_stem(&g_sa, 0, pcm, 4, 2);
    CHECK_NEAR(g_sa.stem_ring[0][2], 9.0f, 1e-6f);

    audio_quit(&g_sa);
    TEST_END();
}

void run_audio_tests(void)
{
    test_audio_init_success();
    test_audio_init_fail_mix_init();
    test_audio_init_fail_mixer_device();
    test_audio_init_fail_track();
    test_audio_init_fail_mutex();
    test_audio_quit_zeroed();
    test_audio_load_bad_path();
    test_audio_load_fail_load_audio();
    test_audio_load_fail_set_track();
    test_audio_load_success();
    test_audio_play_no_audio();
    test_audio_play_fail_props();
    test_audio_play_fail_play();
    test_audio_play_success();
    test_audio_play_from_paused();
    test_audio_play_from_paused_fail();
    test_audio_toggle_pause_playing();
    test_audio_toggle_pause_fail();
    test_audio_toggle_pause_paused();
    test_audio_toggle_pause_stopped();
    test_audio_stop_success();
    test_audio_stop_fail();
    test_audio_seek_no_audio();
    test_audio_seek_no_duration();
    test_audio_seek_success();
    test_audio_seek_fail();
    test_audio_get_pos_no_track();
    test_audio_get_pos_zero_duration();
    test_audio_get_pos_valid();
    test_audio_get_pos_negative_cur();
    test_audio_get_pos_overflow();
    test_audio_snapshot_ring();
    test_audio_set_volume_no_mixer();
    test_audio_set_volume_valid();
    test_audio_unload();
    test_audio_unload_null();
    test_audio_track_stopped_event_zero();
    test_audio_track_stopped_event_nonzero();
    test_audio_load_raw_success();
    test_audio_load_raw_fail_load();
    test_audio_load_raw_fail_set_track();
    test_audio_load_raw_fail_raw_callback();
    test_audio_play_looped_success();
    test_audio_play_looped_fail_props();
    test_audio_play_looped_fail_play();

    /* Instrument-stem playback */
    test_audio_load_stems_success();
    test_audio_load_stems_null_path();
    test_audio_load_stems_failure_branches();
    test_audio_play_stems();
    test_audio_play_stems_failures();
    test_audio_stem_transport();
    test_audio_set_stem_enabled();
    test_audio_snapshot_stem_ring();
    test_audio_unload_stems_and_switch_back();

    /* PCM capture callbacks */
    test_audio_capture_downmix();
    test_audio_capture_ring_wraps();
    test_audio_capture_per_stem();
}
