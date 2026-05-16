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

/* ── Suite ───────────────────────────────────────────────────────────────── */

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
}
