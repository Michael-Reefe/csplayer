/*
 * test_ui.c — Integration tests for src/ui.c.
 *
 * Two sub-suites:
 *   1. Input-handler tests: synthesise SDL event structs and verify the
 *      returned ui_action_t values.  No renderer required.
 *   2. Draw smoke tests: call ui_draw with an offscreen SDL_Renderer and
 *      varied app state to exercise every branch in the static draw helpers.
 *      Tests pass as long as no assertion fires and the process does not crash.
 *
 * Layout constants (mirrored from ui.c private #defines):
 *   VOL_X = 4 + 3*BTN_SZ + 2*BTN_GAP + 4 = 72
 *   VOL_W = 36,  VOL_Y = CTRL_Y+3 = 275,  VOL_H = 18   (CTRL_Y=272)
 *   SEEK_X = VOL_X+VOL_W+4 = 112
 *   SEEK_Y = CTRL_Y+2 = 274,  SEEK_W = LOG_W-SEEK_X-4 = 396,  SEEK_H = 20
 *   PL_SB_X = LOG_W-5 = 507,  PL_SB_W = 5               (PL_Y=202)
 *   PL_DEL_X = PL_SB_X-12-2 = 493,  PL_DEL_W = 12
 *   PL_ADD_X = 2,  PL_ADD_SZ = PL_ROW-2 = 14
 *   GBTN_X0 = SPEC_X+SPEC_W-3*GRAPH_BTN_W-2*GRAPH_BTN_GAP-2 = 376
 *   GBTN_Y  = SPEC_Y+2 = 30,  each button 38×10 px, gap=2
 */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <assert.h>
#include <string.h>

#include "harness.h"
#include "ui.h"
#include "audio.h"
#include "spectrum.h"
#include "playlist.h"
#include "test_mocks.h"

/* Absolute path injected by CMake; fall back to local name if not set. */
#ifndef UNIFONT_PATH
#define UNIFONT_PATH "unifont.otf"
#endif

/* ── Private geometry constants (duplicated from ui.c for test use) ──────── */

#define T_BTN_SZ    BTN_SZ
#define T_BTN_GAP   BTN_GAP
#define T_VOL_X     (4 + 3*T_BTN_SZ + 2*T_BTN_GAP + 4)  /* 72 */
#define T_VOL_W     36
#define T_VOL_Y     (CTRL_Y + 3)
#define T_VOL_H     (CTRL_H - 6)
#define T_SEEK_X    (T_VOL_X + T_VOL_W + 4)              /* 112 */
#define T_SEEK_Y    (CTRL_Y + 2)
#define T_SEEK_W    (LOG_W - T_SEEK_X - 4)               /* 396 */
#define T_SEEK_H    (CTRL_H - 4)
#define T_PL_SB_W   5
#define T_PL_SB_X   (LOG_W - T_PL_SB_W)                 /* 507 */
#define T_PL_DEL_W  12
#define T_PL_DEL_X  (T_PL_SB_X - T_PL_DEL_W - 2)        /* 493 */
#define T_PL_ADD_X  PL_ADD_X                              /* 2   */
#define T_PL_ADD_SZ PL_ADD_SZ                             /* 14  */
/* Graph-mode toggle button strip (top-right interior of spectrum panel). */
#define T_GBTN_X0  (SPEC_X + SPEC_W - 3*GRAPH_BTN_W - 2*GRAPH_BTN_GAP - 2)  /* 376 */
#define T_GBTN_BY  (SPEC_Y + 2)                                               /* 30  */

/* ── Globals ─────────────────────────────────────────────────────────────── */

static SDL_Window   *g_window   = NULL;
static SDL_Renderer *g_renderer = NULL;
static synth_t       g_synth;          /* zeroed; font=NULL → silence */

/* ── Helpers: synthesise minimal SDL event structs ───────────────────────── */

static SDL_MouseButtonEvent make_btn(float x, float y, Uint8 btn)
{
    SDL_MouseButtonEvent e;
    SDL_memset(&e, 0, sizeof(e));
    e.button = btn;
    e.x      = x;
    e.y      = y;
    return e;
}

static SDL_MouseMotionEvent make_motion(float x, float y)
{
    SDL_MouseMotionEvent e;
    SDL_memset(&e, 0, sizeof(e));
    e.x = x;
    e.y = y;
    return e;
}

static SDL_KeyboardEvent make_key(SDL_Scancode sc)
{
    SDL_KeyboardEvent e;
    SDL_memset(&e, 0, sizeof(e));
    e.type     = SDL_EVENT_KEY_DOWN;
    e.scancode = sc;
    return e;
}

/* Zeroed audio_t suitable for input-handler tests (no real mixer). */
static audio_t dummy_audio(void)
{
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    a.sample_rate = 44100.0f;
    return a;
}

/* Playlist with N synthetic tracks (no real files). */
static playlist_t make_pl(i32 count, i32 current)
{
    playlist_t pl;
    SDL_memset(&pl, 0, sizeof(pl));
    pl.current = current;
    for (i32 i = 0; i < count && i < PLAYLIST_MAX; i++) {
        SDL_snprintf(pl.entries[i].path, (size_t)PATH_MAX_LEN, "/t/%d.mp3", i);
        SDL_snprintf(pl.entries[i].title, (size_t)TITLE_MAX_LEN, "Track %d", i);
        pl.entries[i].duration_sec = 90.0 * (i + 1);
    }
    pl.count = count;
    return pl;
}

/* ── Input-handler tests ─────────────────────────────────────────────────── */

/* ---- ui_on_key ----------------------------------------------------------- */

static void test_ui_key_space(void)
{
    TEST_BEGIN("ui_key_space");
    ui_t ui; SDL_memset(&ui, 0, sizeof(ui));
    SDL_KeyboardEvent e = make_key(SDL_SCANCODE_SPACE);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_PLAY_PAUSE);
    CHECK(e.scancode == SDL_SCANCODE_SPACE);
    TEST_END();
}

static void test_ui_key_left(void)
{
    TEST_BEGIN("ui_key_left");
    ui_t ui; SDL_memset(&ui, 0, sizeof(ui));
    SDL_KeyboardEvent e = make_key(SDL_SCANCODE_LEFT);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_REWIND);
    CHECK(e.scancode == SDL_SCANCODE_LEFT);
    TEST_END();
}

static void test_ui_key_right(void)
{
    TEST_BEGIN("ui_key_right");
    ui_t ui; SDL_memset(&ui, 0, sizeof(ui));
    SDL_KeyboardEvent e = make_key(SDL_SCANCODE_RIGHT);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_NEXT);
    CHECK(e.scancode == SDL_SCANCODE_RIGHT);
    TEST_END();
}

static void test_ui_key_other(void)
{
    TEST_BEGIN("ui_key_other");
    ui_t ui; SDL_memset(&ui, 0, sizeof(ui));
    SDL_KeyboardEvent e = make_key(SDL_SCANCODE_ESCAPE);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_NONE);
    CHECK(e.scancode == SDL_SCANCODE_ESCAPE);
    TEST_END();
}

static void test_ui_key_graph_shortcuts(void)
{
    TEST_BEGIN("ui_key_graph_shortcuts");
    ui_t ui; SDL_memset(&ui, 0, sizeof(ui));
    SDL_KeyboardEvent e;

    e = make_key(SDL_SCANCODE_S);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_FREQ);

    e = make_key(SDL_SCANCODE_W);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_TIME);

    e = make_key(SDL_SCANCODE_B);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_BOTH);
    TEST_END();
}

static void test_ui_key_decompose_toggle(void)
{
    TEST_BEGIN("ui_key_decompose_toggle");
    ui_t ui; SDL_memset(&ui, 0, sizeof(ui));
    SDL_KeyboardEvent e = make_key(SDL_SCANCODE_D);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_WAVE_DECOMPOSE);
    CHECK(ui.wave_decompose == true);
    CHECK(ui_on_key(&ui, &e, APP_MODE_PLAYER) == UI_ACTION_WAVE_DECOMPOSE);
    CHECK(ui.wave_decompose == false);
    TEST_END();
}

static void test_ui_key_mode_shortcuts(void)
{
    TEST_BEGIN("ui_key_mode_shortcuts");
    ui_t ui; SDL_memset(&ui, 0, sizeof(ui));
    SDL_KeyboardEvent ep = make_key(SDL_SCANCODE_P);
    SDL_KeyboardEvent es = make_key(SDL_SCANCODE_S);
    ep.mod = SDL_KMOD_LSHIFT;
    es.mod = SDL_KMOD_LSHIFT;

    /* Shift+P while already in PLAYER → no action */
    CHECK(ui_on_key(&ui, &ep, APP_MODE_PLAYER) == UI_ACTION_NONE);
    /* Shift+P while in SYNTH → toggle */
    CHECK(ui_on_key(&ui, &ep, APP_MODE_SYNTH) == UI_ACTION_MODE_TOGGLE);

    /* Shift+S while already in SYNTH → no action */
    CHECK(ui_on_key(&ui, &es, APP_MODE_SYNTH) == UI_ACTION_NONE);
    /* Shift+S while in PLAYER → toggle */
    CHECK(ui_on_key(&ui, &es, APP_MODE_PLAYER) == UI_ACTION_MODE_TOGGLE);
    TEST_END();
}

/* ---- ui_on_mouse_down ---------------------------------------------------- */

static void test_ui_mouse_down_non_left(void)
{
    TEST_BEGIN("ui_mouse_down_non_left");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    SDL_MouseButtonEvent e = make_btn(10.0f, 270.0f, SDL_BUTTON_RIGHT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_NONE);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_down_rewind_btn(void)
{
    TEST_BEGIN("ui_mouse_down_rewind_btn");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    SDL_MouseButtonEvent e = make_btn(10.0f, (f32)(CTRL_Y + 4), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_REWIND);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_down_play_btn(void)
{
    TEST_BEGIN("ui_mouse_down_play_btn");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    SDL_MouseButtonEvent e = make_btn(30.0f, (f32)(CTRL_Y + 4), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_PLAY_PAUSE);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_down_next_btn(void)
{
    TEST_BEGIN("ui_mouse_down_next_btn");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    SDL_MouseButtonEvent e = make_btn(55.0f, (f32)(CTRL_Y + 4), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_NEXT);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_down_add_open(void)
{
    TEST_BEGIN("ui_mouse_down_add_open");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.dialog_open = false;
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Click inside the [+] button area with dialog not yet open. */
    SDL_MouseButtonEvent e = make_btn(5.0f, (f32)(PL_Y + 4), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_OPEN_DIALOG);
    CHECK(!ui.dialog_open);
    TEST_END();
}

static void test_ui_mouse_down_add_blocked(void)
{
    TEST_BEGIN("ui_mouse_down_add_blocked");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.dialog_open = true;
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);   /* empty playlist so no row click */
    SDL_MouseButtonEvent e = make_btn(5.0f, (f32)(PL_Y + 4), SDL_BUTTON_LEFT);
    /* dialog_open=true → add button check fails; no rows → NONE */
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_NONE);
    CHECK(ui.dialog_open);
    TEST_END();
}

static void test_ui_mouse_down_vol_slider(void)
{
    TEST_BEGIN("ui_mouse_down_vol_slider");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* x=85 is inside vol slider [VOL_X=72, VOL_X+VOL_W=108). */
    SDL_MouseButtonEvent e = make_btn(85.0f, (f32)T_VOL_Y + 2.0f, SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_SET_VOLUME);
    CHECK(ui.vol_dragging);
    CHECK(ui.vol_pos > 0.0f && ui.vol_pos < 1.0f);
    TEST_END();
}

static void test_ui_mouse_down_vol_clamp_lo(void)
{
    TEST_BEGIN("ui_mouse_down_vol_clamp_lo");
    /* The clamp inside on_mouse_down's vol branch is dead code (in_vol_slider
     * guarantees lx >= VOL_X), but the slider should still set vol_pos to a
     * valid value at the left edge of the slider. */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    SDL_MouseButtonEvent e = make_btn((f32)T_VOL_X, (f32)T_VOL_Y + 2.0f, SDL_BUTTON_LEFT);
    ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(ui.vol_pos >= 0.0f);
    CHECK(!ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_down_seek_bar(void)
{
    TEST_BEGIN("ui_mouse_down_seek_bar");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    SDL_MouseButtonEvent e = make_btn(200.0f, (f32)T_SEEK_Y + 2.0f, SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_SEEK);
    CHECK(ui.scrubbing);
    CHECK(ui.scrub_pos > 0.0f && ui.scrub_pos < 1.0f);
    TEST_END();
}

static void test_ui_mouse_down_scrollbar(void)
{
    TEST_BEGIN("ui_mouse_down_scrollbar");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    /* Need count > 0 for sb_pos_to_scroll to be called (count branch). */
    playlist_t pl = make_pl(8, 0);
    /* x=637 is in [PL_SB_X=635, 640), y=260 is in [PL_Y=242, 338). */
    SDL_MouseButtonEvent e = make_btn(637.0f, 260.0f, SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_NONE);
    CHECK(ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_down_scrollbar_nospace(void)
{
    TEST_BEGIN("ui_mouse_down_scrollbar_nospace");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    /* count=3 <= visible=4 → max_scroll<=0 → sb_pos_to_scroll returns 0 */
    playlist_t pl = make_pl(3, 0);
    SDL_MouseButtonEvent e = make_btn(637.0f, 260.0f, SDL_BUTTON_LEFT);
    ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 0);
    CHECK(ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_down_pl_delete(void)
{
    TEST_BEGIN("ui_mouse_down_pl_delete");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(2, 0);
    /* x=625 is in delete column [PL_DEL_X=621, 633); y maps to row 0. */
    SDL_MouseButtonEvent e = make_btn(625.0f, (f32)(PL_Y + 4), SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_PL_REMOVE);
    CHECK(ui.pl_hover == 0);
    TEST_END();
}

static void test_ui_mouse_down_pl_row(void)
{
    TEST_BEGIN("ui_mouse_down_pl_row");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(2, 0);
    /* x=100 is in title area (not delete col); y maps to row 0. */
    SDL_MouseButtonEvent e = make_btn(100.0f, (f32)(PL_Y + 4), SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_PL_SELECT);
    CHECK(ui.pl_hover == 0);
    TEST_END();
}

static void test_ui_mouse_down_nowhere(void)
{
    TEST_BEGIN("ui_mouse_down_nowhere");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Click in the header area — no region matches. */
    SDL_MouseButtonEvent e = make_btn(200.0f, 5.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_NONE);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

/* ---- ui_on_mouse_motion -------------------------------------------------- */

static void test_ui_mouse_motion_hover(void)
{
    TEST_BEGIN("ui_mouse_motion_hover");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(2, 0);
    SDL_MouseMotionEvent e = make_motion(100.0f, (f32)(PL_Y + 4));
    ui_on_mouse_motion(&ui, &e, false, &pl);
    CHECK(ui.pl_hover == 0);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_hover_oob(void)
{
    TEST_BEGIN("ui_mouse_motion_hover_oob");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);   /* empty: pl_row_at returns -1 */
    SDL_MouseMotionEvent e = make_motion(100.0f, 200.0f);
    ui_on_mouse_motion(&ui, &e, false, &pl);
    CHECK(ui.pl_hover == -1);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_sb_held(void)
{
    TEST_BEGIN("ui_mouse_motion_sb_held");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.sb_dragging = true;
    playlist_t pl = make_pl(8, 0);
    SDL_MouseMotionEvent e = make_motion(508.0f, 210.0f);
    ui_on_mouse_motion(&ui, &e, true, &pl);
    /* scroll should be updated */
    CHECK(pl.scroll >= 0);
    CHECK(ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_sb_not_held(void)
{
    TEST_BEGIN("ui_mouse_motion_sb_not_held");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.sb_dragging = true;
    playlist_t pl = make_pl(8, 0);
    pl.scroll = 3;
    SDL_MouseMotionEvent e = make_motion(508.0f, 210.0f);
    ui_on_mouse_motion(&ui, &e, false, &pl);   /* held=false → no change */
    CHECK(pl.scroll == 3);
    CHECK(ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_sb_above(void)
{
    TEST_BEGIN("ui_mouse_motion_sb_above");
    /* sb_dragging + held + ly < PL_Y → t clamped to 0 → scroll=0 */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.sb_dragging = true;
    playlist_t pl = make_pl(8, 0);
    pl.scroll = 4;
    SDL_MouseMotionEvent e = make_motion(508.0f, 10.0f);   /* ly=10 < PL_Y=194 */
    ui_on_mouse_motion(&ui, &e, true, &pl);
    CHECK(pl.scroll == 0);
    CHECK(ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_sb_below(void)
{
    TEST_BEGIN("ui_mouse_motion_sb_below");
    /* sb_dragging + held + ly > PL_Y+PL_H → t clamped to 1 → max_scroll */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.sb_dragging = true;
    playlist_t pl = make_pl(10, 0);
    SDL_MouseMotionEvent e = make_motion(508.0f, 345.0f);  /* > PL_Y+PL_H=338 */
    ui_on_mouse_motion(&ui, &e, true, &pl);
    /* max_scroll = 10 - 6 = 4; scroll clamped to 4 */
    CHECK(pl.scroll == 4);
    CHECK(ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_vol_drag_lo(void)
{
    TEST_BEGIN("ui_mouse_motion_vol_drag_lo");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_dragging = true;
    ui.vol_pos = 0.5f;
    playlist_t pl = make_pl(0, -1);
    /* lx=50 < VOL_X=72 → vol_pos < 0 → clamped to 0 */
    SDL_MouseMotionEvent e = make_motion(50.0f, 270.0f);
    ui_on_mouse_motion(&ui, &e, true, &pl);
    CHECK(ui.vol_pos == 0.0f);
    CHECK(ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_vol_drag_hi(void)
{
    TEST_BEGIN("ui_mouse_motion_vol_drag_hi");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_dragging = true;
    ui.vol_pos = 0.5f;
    playlist_t pl = make_pl(0, -1);
    /* lx=150 > VOL_X+VOL_W=108 → vol_pos > 1 → clamped to 1 */
    SDL_MouseMotionEvent e = make_motion(150.0f, 270.0f);
    ui_on_mouse_motion(&ui, &e, true, &pl);
    CHECK(ui.vol_pos == 1.0f);
    CHECK(ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_motion_seek_drag_lo(void)
{
    TEST_BEGIN("ui_mouse_motion_seek_drag_lo");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing = true;
    ui.scrub_pos = 0.5f;
    playlist_t pl = make_pl(0, -1);
    /* lx=50 < SEEK_X=112 → scrub_pos < 0 → clamped to 0 */
    SDL_MouseMotionEvent e = make_motion(50.0f, 268.0f);
    ui_on_mouse_motion(&ui, &e, true, &pl);
    CHECK(ui.scrub_pos == 0.0f);
    CHECK(ui.scrubbing);
    TEST_END();
}

static void test_ui_mouse_motion_seek_drag_hi(void)
{
    TEST_BEGIN("ui_mouse_motion_seek_drag_hi");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing = true;
    ui.scrub_pos = 0.5f;
    playlist_t pl = make_pl(0, -1);
    /* lx=640 > SEEK_X+SEEK_W=636 → scrub_pos > 1 → clamped to 1 */
    SDL_MouseMotionEvent e = make_motion(640.0f, 268.0f);
    ui_on_mouse_motion(&ui, &e, true, &pl);
    CHECK(ui.scrub_pos == 1.0f);
    CHECK(ui.scrubbing);
    TEST_END();
}

/* ---- ui_on_mouse_up ------------------------------------------------------ */

static void test_ui_mouse_up_non_left(void)
{
    TEST_BEGIN("ui_mouse_up_non_left");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    SDL_MouseButtonEvent e = make_btn(200.0f, 268.0f, SDL_BUTTON_RIGHT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_NONE);
    CHECK(!ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_up_sb(void)
{
    TEST_BEGIN("ui_mouse_up_sb");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.sb_dragging = true;
    SDL_MouseButtonEvent e = make_btn(508.0f, 210.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_NONE);
    CHECK(!ui.sb_dragging);
    TEST_END();
}

static void test_ui_mouse_up_vol(void)
{
    TEST_BEGIN("ui_mouse_up_vol");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_dragging = true;
    SDL_MouseButtonEvent e = make_btn(85.0f, 270.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_SET_VOLUME);
    CHECK(!ui.vol_dragging);
    CHECK(ui.vol_pos >= 0.0f && ui.vol_pos <= 1.0f);
    TEST_END();
}

static void test_ui_mouse_up_vol_clamp_lo(void)
{
    TEST_BEGIN("ui_mouse_up_vol_clamp_lo");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_dragging = true;
    /* Release left of slider: vol_pos clamps to 0. */
    SDL_MouseButtonEvent e = make_btn(10.0f, 270.0f, SDL_BUTTON_LEFT);
    ui_on_mouse_up(&ui, &e);
    CHECK(ui.vol_pos == 0.0f);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_up_vol_clamp_hi(void)
{
    TEST_BEGIN("ui_mouse_up_vol_clamp_hi");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_dragging = true;
    /* Release right of slider: vol_pos clamps to 1. */
    SDL_MouseButtonEvent e = make_btn(200.0f, 270.0f, SDL_BUTTON_LEFT);
    ui_on_mouse_up(&ui, &e);
    CHECK(ui.vol_pos == 1.0f);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_mouse_up_seek(void)
{
    TEST_BEGIN("ui_mouse_up_seek");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing = true;
    SDL_MouseButtonEvent e = make_btn(200.0f, 268.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_SEEK);
    CHECK(!ui.scrubbing);
    CHECK(ui.scrub_pos >= 0.0f && ui.scrub_pos <= 1.0f);
    TEST_END();
}

static void test_ui_mouse_up_seek_clamp_lo(void)
{
    TEST_BEGIN("ui_mouse_up_seek_clamp_lo");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing = true;
    /* Release left of seek bar: scrub_pos clamps to 0. */
    SDL_MouseButtonEvent e = make_btn(10.0f, 268.0f, SDL_BUTTON_LEFT);
    ui_on_mouse_up(&ui, &e);
    CHECK(ui.scrub_pos == 0.0f);
    CHECK(!ui.scrubbing);
    TEST_END();
}

static void test_ui_mouse_up_seek_clamp_hi(void)
{
    TEST_BEGIN("ui_mouse_up_seek_clamp_hi");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing = true;
    /* Release right of seek bar: scrub_pos clamps to 1. */
    SDL_MouseButtonEvent e = make_btn(640.0f, 268.0f, SDL_BUTTON_LEFT);
    ui_on_mouse_up(&ui, &e);
    CHECK(ui.scrub_pos == 1.0f);
    CHECK(!ui.scrubbing);
    TEST_END();
}

static void test_ui_mouse_up_nodrag(void)
{
    TEST_BEGIN("ui_mouse_up_nodrag");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    SDL_MouseButtonEvent e = make_btn(200.0f, 268.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_NONE);
    CHECK(!ui.sb_dragging);
    TEST_END();
}

/* ---- ui_on_scroll -------------------------------------------------------- */

static void test_ui_scroll_vol_up(void)
{
    TEST_BEGIN("ui_scroll_vol_up");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_pos = 0.5f;
    playlist_t pl = make_pl(0, -1);
    ui_action_t act = ui_on_scroll(&ui, (f32)T_VOL_X + 5.0f,
                                    (f32)T_VOL_Y + 2.0f, 1.0f, 0.0f, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_SET_VOLUME);
    CHECK(ui.vol_pos > 0.5f);
    TEST_END();
}

static void test_ui_scroll_vol_down_clamp(void)
{
    TEST_BEGIN("ui_scroll_vol_down_clamp");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_pos = 0.0f;
    playlist_t pl = make_pl(0, -1);
    /* dy=-1 → vol_pos -= 0.05 → negative → clamped to 0 */
    ui_on_scroll(&ui, (f32)T_VOL_X + 5.0f, (f32)T_VOL_Y + 2.0f,
                 -1.0f, 0.0f, &pl, APP_MODE_PLAYER);
    CHECK(ui.vol_pos == 0.0f);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_scroll_vol_up_clamp(void)
{
    TEST_BEGIN("ui_scroll_vol_up_clamp");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_pos = 1.0f;
    playlist_t pl = make_pl(0, -1);
    /* dy=1 → vol_pos += 0.05 → > 1 → clamped to 1 */
    ui_on_scroll(&ui, (f32)T_VOL_X + 5.0f, (f32)T_VOL_Y + 2.0f,
                 1.0f, 0.0f, &pl, APP_MODE_PLAYER);
    CHECK(ui.vol_pos == 1.0f);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_scroll_seek(void)
{
    TEST_BEGIN("ui_scroll_seek");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    ui_action_t act = ui_on_scroll(&ui, (f32)T_SEEK_X + 10.0f,
                                    (f32)T_SEEK_Y + 2.0f, 1.0f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_SEEK);
    CHECK(ui.scrub_pos > 0.5f);
    TEST_END();
}

static void test_ui_scroll_seek_clamp_hi(void)
{
    TEST_BEGIN("ui_scroll_seek_clamp_hi");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    /* current_seek_pos=1.0 + dy=1 → scrub_pos > 1 → clamped */
    ui_on_scroll(&ui, (f32)T_SEEK_X + 10.0f, (f32)T_SEEK_Y + 2.0f,
                 1.0f, 1.0f, &pl, APP_MODE_PLAYER);
    CHECK(ui.scrub_pos == 1.0f);
    CHECK(!ui.scrubbing);
    TEST_END();
}

static void test_ui_scroll_seek_clamp_lo(void)
{
    TEST_BEGIN("ui_scroll_seek_clamp_lo");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    /* current_seek_pos=0.0 + dy=-1 → scrub_pos < 0 → clamped */
    ui_on_scroll(&ui, (f32)T_SEEK_X + 10.0f, (f32)T_SEEK_Y + 2.0f,
                 -1.0f, 0.0f, &pl, APP_MODE_PLAYER);
    CHECK(ui.scrub_pos == 0.0f);
    CHECK(!ui.scrubbing);
    TEST_END();
}

static void test_ui_scroll_playlist(void)
{
    TEST_BEGIN("ui_scroll_playlist");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(8, 0);
    pl.scroll = 2;
    /* dy=1 → scroll -= 1 = 1 */
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, 1.0f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 1);
    /* dy=-1 → scroll += 1 = 2 */
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, -1.0f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 2);
    TEST_END();
}

static void test_ui_scroll_playlist_clamp(void)
{
    TEST_BEGIN("ui_scroll_playlist_clamp");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(10, 0);
    pl.scroll = 0;
    /* dy=10 → scroll -= 10 = -10 → clamped to 0 */
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, 10.0f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 0);
    /* dy=-10 → scroll += 10 = 10 → clamped to max_scroll=4 (10-6=4) */
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, -10.0f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 4);
    TEST_END();
}

static void test_ui_scroll_playlist_nospace(void)
{
    TEST_BEGIN("ui_scroll_playlist_nospace");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    /* count=3 <= visible=4 → max_scroll<=0 → no scroll change */
    playlist_t pl = make_pl(3, 0);
    pl.scroll = 0;
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, -1.0f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 0);
    CHECK(!ui.sb_dragging);
    TEST_END();
}

static void test_ui_scroll_playlist_trackpad(void)
{
    TEST_BEGIN("ui_scroll_playlist_trackpad");
    /* Trackpad: sub-row deltas accumulate instead of truncating to zero, and
     * the remainder carries across events. */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(20, 0);   /* max_scroll = 20 - 6 = 14 */
    pl.scroll = 5;

    /* Two small downward deltas (dy<0) total -0.6 → no whole step yet. */
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, -0.3f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 5);
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, -0.3f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 5);

    /* A third delta pushes the accumulator past 1.0 → exactly one row moves. */
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, -0.5f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 6);

    /* Remainder (-0.1) carries: another -0.95 crosses the next whole step. */
    ui_on_scroll(&ui, 100.0f, (f32)PL_Y + 5.0f, -0.95f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(pl.scroll == 7);
    TEST_END();
}

static void test_ui_scroll_synth_row_trackpad(void)
{
    TEST_BEGIN("ui_scroll_synth_row_trackpad");
    /* Over the instrument row, a sub-step trackpad delta yields no action until
     * a full step accumulates (so a flick does not race through instruments). */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);

    ui_action_t act = ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 4),
                                    0.4f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_NONE);
    act = ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 4),
                       0.7f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_SYNTH_INST_UP);
    TEST_END();
}

static void test_ui_scroll_none(void)
{
    TEST_BEGIN("ui_scroll_none");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    /* Click in the header — no region matches → NONE */
    ui_action_t act = ui_on_scroll(&ui, 100.0f, 5.0f, 1.0f, 0.5f, &pl, APP_MODE_PLAYER);
    CHECK(act == UI_ACTION_NONE);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

/* ── Draw smoke tests ────────────────────────────────────────────────────── */

/* Build an audio_t suitable for draw tests (no real mixer, safe for ui_draw). */
static audio_t make_draw_audio(audio_status_t status)
{
    audio_t a;
    SDL_memset(&a, 0, sizeof(a));
    a.sample_rate     = 44100.0f;
    a.duration_frames = 44100;   /* 1 second at 44100 Hz */
    a.status          = status;
    return a;
}

/* Build a flat-line spectrum_t. */
static spectrum_t make_flat_spectrum(void)
{
    spectrum_t s;
    SDL_memset(&s, 0, sizeof(s));
    for (i32 i = 0; i < SPEC_W; i++) {
        s.pts[i].x = (f32)(SPEC_X + i);
        s.pts[i].y = (f32)(SPEC_Y + SPEC_H / 2);
    }
    return s;
}

/* ---- status / scrubbing branches ---------------------------------------- */

static void test_ui_draw_status_playing(void)
{
    TEST_BEGIN("ui_draw_status_playing");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_status_paused(void)
{
    TEST_BEGIN("ui_draw_status_paused");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PAUSED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_status_stopped(void)
{
    TEST_BEGIN("ui_draw_status_stopped");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_scrubbing(void)
{
    TEST_BEGIN("ui_draw_scrubbing");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing  = true;
    ui.scrub_pos  = 0.5f;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

/* ---- vol/seek fill clamp branches --------------------------------------- */

static void test_ui_draw_vol_clamp_lo(void)
{
    TEST_BEGIN("ui_draw_vol_clamp_lo");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    /* vol_pos < 0 → vfill < 0 → clamped to 0 */
    ui.vol_pos = -0.1f;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_vol_clamp_hi(void)
{
    TEST_BEGIN("ui_draw_vol_clamp_hi");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.vol_pos = 1.1f;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_seek_clamp_lo(void)
{
    TEST_BEGIN("ui_draw_seek_clamp_lo");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing = true;
    ui.scrub_pos = -0.1f;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_seek_clamp_hi(void)
{
    TEST_BEGIN("ui_draw_seek_clamp_hi");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.scrubbing = true;
    ui.scrub_pos = 1.1f;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

/* ---- header branches ----------------------------------------------------- */

static void test_ui_draw_no_current_track(void)
{
    TEST_BEGIN("ui_draw_no_current_track");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);   /* current=-1 → no title branch */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_with_current_track(void)
{
    TEST_BEGIN("ui_draw_with_current_track");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(2, 0);   /* current=0 → title + clip branch */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

/* ---- playlist panel branches -------------------------------------------- */

static void test_ui_draw_empty_playlist(void)
{
    TEST_BEGIN("ui_draw_empty_playlist");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_full_playlist(void)
{
    TEST_BEGIN("ui_draw_full_playlist");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(8, 0);   /* count>visible → scrollbar drawn */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_thumb_min(void)
{
    TEST_BEGIN("ui_draw_thumb_min");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    /* very large count → thumb_h < 4 → clamped to 4 */
    playlist_t pl = make_pl(PLAYLIST_MAX, 0);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_hover_row(void)
{
    TEST_BEGIN("ui_draw_hover_row");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(3, 0);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    /* pl_hover=1, current=0 → hover highlight branch (idx==hover && idx!=current) */
    ui.pl_hover = 1;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_current_row(void)
{
    TEST_BEGIN("ui_draw_current_row");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(3, 1);   /* current=1 → ">" indicator branch */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_hover_equals_current(void)
{
    TEST_BEGIN("ui_draw_hover_equals_current");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(3, 0);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    /* pl_hover==current → first if true, second if false */
    ui.pl_hover = 0;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_duration_positive(void)
{
    TEST_BEGIN("ui_draw_duration_positive");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(1, 0);
    pl.entries[0].duration_sec = 90.0;   /* > 0 → duration rendered */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_duration_zero(void)
{
    TEST_BEGIN("ui_draw_duration_zero");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(1, 0);
    pl.entries[0].duration_sec = 0.0;   /* <= 0 → no duration rendered */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_dialog_open(void)
{
    TEST_BEGIN("ui_draw_dialog_open");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.dialog_open = true;   /* add button drawn hot */
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

/* ---- render_utf8 branches ------------------------------------------------ */

static void test_ui_draw_empty_string(void)
{
    TEST_BEGIN("ui_draw_empty_string");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    /* Empty title → render_utf8 early-returns on text[0]=='\0'. */
    playlist_t pl = make_pl(1, 0);
    pl.entries[0].title[0] = '\0';
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_font_null(void)
{
    TEST_BEGIN("ui_draw_font_null");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(1, 0);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.font = NULL;   /* → SDL_RenderDebugText fallback */
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_ttf_render_fail(void)
{
    TEST_BEGIN("ui_draw_ttf_render_fail");
    /* Try to load a TTF font; skip if unavailable. */
    TTF_Font *font = TTF_OpenFont(UNIFONT_PATH, 8);
    if (font == NULL) {
        printf("  SKIP  ui_draw_ttf_render_fail (UNIFONT_PATH not found)\n");
        TEST_END();
        return;
    }

    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(1, 0);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.font = font;

    /* Mock makes TTF_RenderText_Solid return NULL → render_utf8 silently skips. */
    g_mock_TTF_RenderText_Solid_fail = true;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);

    TTF_CloseFont(font);
    TEST_END();
}

static void test_ui_draw_ttf_texture_fail(void)
{
    TEST_BEGIN("ui_draw_ttf_texture_fail");
    TTF_Font *font = TTF_OpenFont(UNIFONT_PATH, 8);
    if (font == NULL) {
        printf("  SKIP  ui_draw_ttf_texture_fail (UNIFONT_PATH not found)\n");
        TEST_END();
        return;
    }

    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(1, 0);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.font = font;

    /* Mock makes SDL_CreateTextureFromSurface return NULL → silent skip. */
    g_mock_SDL_CreateTextureFromSurface_fail = true;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);

    TTF_CloseFont(font);
    TEST_END();
}

static void test_ui_draw_font_valid(void)
{
    TEST_BEGIN("ui_draw_font_valid");
    TTF_Font *font = TTF_OpenFont(UNIFONT_PATH, 8);
    if (font == NULL) {
        printf("  SKIP  ui_draw_font_valid (UNIFONT_PATH not found)\n");
        TEST_END();
        return;
    }

    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(1, 0);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.font = font;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);

    TTF_CloseFont(font);
    TEST_END();
}

/* ── Synth mode input-handler tests ─────────────────────────────────────── */

static void test_ui_mode_toggle_click(void)
{
    TEST_BEGIN("ui_mode_toggle_click");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Mode button is now at MODE_BTN_X=78, w=96, so centre is x=126. */
    SDL_MouseButtonEvent e = make_btn(120.0f, 10.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_MODE_TOGGLE);
    CHECK(!ui.vol_dragging);
    TEST_END();
}

static void test_ui_synth_inst_row_click(void)
{
    TEST_BEGIN("ui_synth_inst_row_click");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_dd_hover = -1;
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Synth inst row: y in [PL_Y, PL_Y+16) = [194, 210).
     * Now opens the dropdown → returns NONE, sets synth_inst_open. */
    SDL_MouseButtonEvent e = make_btn(100.0f, (f32)(PL_Y + 4), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_SYNTH) == UI_ACTION_NONE);
    CHECK(ui.synth_inst_open);
    TEST_END();
}

static void test_ui_synth_note_row_click(void)
{
    TEST_BEGIN("ui_synth_note_row_click");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_dd_hover = -1;
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Synth note row: y in [PL_Y+16, PL_Y+32) = [210, 226).
     * Now opens the dropdown → returns NONE, sets synth_note_open. */
    SDL_MouseButtonEvent e = make_btn(100.0f, (f32)(PL_Y + 18), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_SYNTH) == UI_ACTION_NONE);
    CHECK(ui.synth_note_open);
    TEST_END();
}

static void test_ui_synth_panel_empty_area(void)
{
    TEST_BEGIN("ui_synth_panel_empty_area");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Area below note row (y=PL_Y+40) has no button — returns NONE. */
    SDL_MouseButtonEvent e = make_btn(100.0f, (f32)(PL_Y + 40), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_SYNTH) == UI_ACTION_NONE);
    CHECK(!ui.synth_inst_open);
    TEST_END();
}

static void test_ui_synth_dd_sb_drag_start(void)
{
    TEST_BEGIN("ui_synth_dd_sb_drag_start");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_inst_open = true;
    ui.synth_dd_scroll = 0;
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* x=633 is in [LOG_W-4-DD_SB_W=631, LOG_W-4=636) — scrollbar strip.
     * y=DD_INST_Y+8 is inside the dropdown rect. */
    SDL_MouseButtonEvent e = make_btn(633.0f, (f32)(PL_Y + 16 + 8), SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_SYNTH) == UI_ACTION_NONE);
    CHECK(ui.synth_dd_sb_dragging);
    CHECK(ui.synth_inst_open);   /* dropdown stays open during scroll drag */
    TEST_END();
}

static void test_ui_synth_dd_sb_drag_motion(void)
{
    TEST_BEGIN("ui_synth_dd_sb_drag_motion");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_inst_open     = true;
    ui.synth_dd_sb_dragging = true;
    ui.synth_dd_scroll     = 0;
    playlist_t pl = make_pl(0, -1);
    /* Move to y = DD_INST_Y + DD_H/2 = 210 + 32 = 242 → t=0.5 → scroll≈62. */
    SDL_MouseMotionEvent e = make_motion(505.0f, (f32)(PL_Y + 16 + 32));
    ui_on_mouse_motion(&ui, &e, true, &pl);
    CHECK(ui.synth_dd_scroll > 0);
    CHECK(ui.synth_inst_open);
    TEST_END();
}

static void test_ui_synth_dd_sb_drag_end(void)
{
    TEST_BEGIN("ui_synth_dd_sb_drag_end");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_dd_sb_dragging = true;
    SDL_MouseButtonEvent e = make_btn(505.0f, 220.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_NONE);
    CHECK(!ui.synth_dd_sb_dragging);
    TEST_END();
}

static void test_ui_synth_dropdown_select_inst(void)
{
    TEST_BEGIN("ui_synth_dropdown_select_inst");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_dd_hover  = -1;
    ui.synth_inst_open = true;
    ui.synth_dd_scroll = 5;   /* first visible item = 5 */
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Click row 0 of inst dropdown (DD_INST_Y = PL_Y+16 = 210, row_h=16).
     * Item area: x in [4, LOG_W-4-DD_SB_W) = [4, 503). */
    SDL_MouseButtonEvent e = make_btn(100.0f, (f32)(PL_Y + 16 + 4), SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_SYNTH_INST_SET);
    CHECK(ui.synth_dd_select == 5);   /* scroll=5, row=0 → item=5 */
    CHECK(!ui.synth_inst_open);
    TEST_END();
}

static void test_ui_synth_dropdown_close_outside(void)
{
    TEST_BEGIN("ui_synth_dropdown_close_outside");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_dd_hover  = -1;
    ui.synth_inst_open = true;
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Click in spectrum area (x=10, y=100) — outside dropdown AND mode button. */
    SDL_MouseButtonEvent e = make_btn(10.0f, 100.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_SYNTH) == UI_ACTION_NONE);
    CHECK(!ui.synth_inst_open);
    TEST_END();
}

static void test_ui_synth_scroll_dropdown_open(void)
{
    TEST_BEGIN("ui_synth_scroll_dropdown_open");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_inst_open  = true;
    ui.synth_dd_scroll  = 5;
    playlist_t pl = make_pl(0, -1);
    /* dy=1 (scroll up) → dd_scroll decreases by 1. */
    ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 4), 1.0f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(ui.synth_dd_scroll == 4);
    /* dy=-1 (scroll down) → dd_scroll increases by 1. */
    ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 4), -1.0f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(ui.synth_dd_scroll == 5);
    TEST_END();
}

static void test_ui_synth_hold_mouse_up_playing(void)
{
    TEST_BEGIN("ui_synth_hold_mouse_up_playing");
    /* With toggle model, mouse_up no longer stops play; NONE is returned. */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_playing = true;
    SDL_MouseButtonEvent e = make_btn(100.0f, 234.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_NONE);
    CHECK(!ui.sb_dragging);
    TEST_END();
}

static void test_ui_synth_hold_mouse_up_not_playing(void)
{
    TEST_BEGIN("ui_synth_hold_mouse_up_not_playing");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_playing = false;
    SDL_MouseButtonEvent e = make_btn(100.0f, 234.0f, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_up(&ui, &e) == UI_ACTION_NONE);
    CHECK(!ui.sb_dragging);
    TEST_END();
}

static void test_ui_synth_scroll_inst_up(void)
{
    TEST_BEGIN("ui_synth_scroll_inst_up");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    ui_action_t act = ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 4),
                                    1.0f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_SYNTH_INST_UP);
    CHECK(!ui.synth_inst_open);
    TEST_END();
}

static void test_ui_synth_scroll_inst_down(void)
{
    TEST_BEGIN("ui_synth_scroll_inst_down");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    ui_action_t act = ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 4),
                                    -1.0f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_SYNTH_INST_DOWN);
    CHECK(!ui.synth_inst_open);
    TEST_END();
}

static void test_ui_synth_scroll_note_up(void)
{
    TEST_BEGIN("ui_synth_scroll_note_up");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    ui_action_t act = ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 18),
                                    1.0f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_SYNTH_NOTE_UP);
    CHECK(!ui.synth_note_open);
    TEST_END();
}

static void test_ui_synth_scroll_note_down(void)
{
    TEST_BEGIN("ui_synth_scroll_note_down");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    ui_action_t act = ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 18),
                                    -1.0f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_SYNTH_NOTE_DOWN);
    CHECK(!ui.synth_note_open);
    TEST_END();
}

static void test_ui_synth_scroll_hold_none(void)
{
    TEST_BEGIN("ui_synth_scroll_hold_none");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    playlist_t pl = make_pl(0, -1);
    /* Scroll over HOLD button area (y=PL_Y+40) in SYNTH mode → NONE */
    ui_action_t act = ui_on_scroll(&ui, 100.0f, (f32)(PL_Y + 40),
                                    1.0f, 0.0f, &pl, APP_MODE_SYNTH);
    CHECK(act == UI_ACTION_NONE);
    CHECK(!ui.synth_inst_open);
    TEST_END();
}

/* ── Synth mode draw smoke tests ─────────────────────────────────────────── */

static void test_ui_draw_mode_btn_synth(void)
{
    TEST_BEGIN("ui_draw_mode_btn_synth");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    /* mode=SYNTH → "SYN" label on mode button, synth panel shown */
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

static void test_ui_draw_synth_playing(void)
{
    TEST_BEGIN("ui_draw_synth_playing");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_playing = true;   /* HOLD button drawn hot */
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

static void test_ui_draw_synth_not_playing(void)
{
    TEST_BEGIN("ui_draw_synth_not_playing");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_playing = false;  /* HOLD button drawn normal */
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

static void test_ui_draw_synth_all_instruments(void)
{
    TEST_BEGIN("ui_draw_synth_all_instruments");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    /* Spot-check representative instruments — no crash. */
    i32 insts[] = {0, 63, 127};
    for (i32 i = 0; i < 3; i++) {
        ui.synth_inst = insts[i];
        ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    }
    TEST_END();
}

static void test_ui_draw_synth_all_notes(void)
{
    TEST_BEGIN("ui_draw_synth_all_notes");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    /* Spot-check note extremes and middle — no crash. */
    i32 notes[] = {0, 64, 127};
    for (i32 i = 0; i < 3; i++) {
        ui.synth_note = notes[i];
        ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    }
    TEST_END();
}

static void test_ui_draw_synth_inst_dropdown(void)
{
    TEST_BEGIN("ui_draw_synth_inst_dropdown");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_dd_hover  = -1;
    ui.synth_inst      = 10;
    ui.synth_inst_open = true;
    ui.synth_dd_scroll = 8;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

static void test_ui_draw_synth_note_dropdown(void)
{
    TEST_BEGIN("ui_draw_synth_note_dropdown");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.synth_dd_hover  = -1;
    ui.synth_note      = 69;
    ui.synth_note_open = true;
    ui.synth_dd_scroll = 65;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

static void test_ui_draw_header_artist(void)
{
    TEST_BEGIN("ui_draw_header_artist");
    audio_t a    = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(1, 0);
    SDL_strlcpy(pl.entries[0].artist, "Test Artist", (size_t)TITLE_MAX_LEN);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_header_synth_no_title(void)
{
    TEST_BEGIN("ui_draw_header_synth_no_title");
    audio_t a    = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    /* Has a current track, but synth mode — title must be suppressed. */
    playlist_t pl = make_pl(1, 0);
    SDL_strlcpy(pl.entries[0].artist, "SomeArtist", (size_t)TITLE_MAX_LEN);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

/* ── Graph-mode button tests ─────────────────────────────────────────────── */

static void test_graph_btn_center_each(void)
{
    TEST_BEGIN("graph_btn_center_each");
    /* Click the centre of each of the three graph-mode toggle buttons. */
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    f32 cy = (f32)(T_GBTN_BY + GRAPH_BTN_H / 2);
    i32 expected_modes[3] = { GRAPH_MODE_FREQ, GRAPH_MODE_TIME, GRAPH_MODE_BOTH };
    for (i32 b = 0; b < 3; b++) {
        ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
        f32 cx = (f32)(T_GBTN_X0 + b * (GRAPH_BTN_W + GRAPH_BTN_GAP) + GRAPH_BTN_W / 2);
        SDL_MouseButtonEvent e = make_btn(cx, cy, SDL_BUTTON_LEFT);
        ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
        CHECK(act == UI_ACTION_GRAPH_MODE);
        CHECK((i32)ui.graph_mode == expected_modes[b]);
    }
    TEST_END();
}

static void test_graph_btn_wrong_idx(void)
{
    TEST_BEGIN("graph_btn_wrong_idx");
    /* Clicking btn 0 centre sets GRAPH_MODE_FREQ, not GRAPH_MODE_TIME. */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_TIME;   /* pre-set to time so we can detect a change */
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    f32 cx = (f32)(T_GBTN_X0 + GRAPH_BTN_W / 2);
    f32 cy = (f32)(T_GBTN_BY + GRAPH_BTN_H / 2);
    SDL_MouseButtonEvent e = make_btn(cx, cy, SDL_BUTTON_LEFT);
    ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(ui.graph_mode == GRAPH_MODE_FREQ);
    CHECK(ui.graph_mode != GRAPH_MODE_TIME);
    TEST_END();
}

static void test_graph_btn_outside(void)
{
    TEST_BEGIN("graph_btn_outside");
    /* A click well outside the button strip must not trigger GRAPH_MODE. */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* y=200 is far below the button strip at y=[SPEC_Y+2, SPEC_Y+12). */
    SDL_MouseButtonEvent e = make_btn((f32)(T_GBTN_X0 + 19), 200.0f, SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(act != UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_FREQ);
    TEST_END();
}

static void test_graph_btn_between(void)
{
    TEST_BEGIN("graph_btn_between");
    /* A click in the 2-px gap between btn 0 and btn 1 must not match either. */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    /* Gap: btn0 ends at T_GBTN_X0+GRAPH_BTN_W, btn1 starts 2 px later. */
    f32 gap_x = (f32)(T_GBTN_X0 + GRAPH_BTN_W + 1);   /* middle of 2-px gap */
    f32 cy    = (f32)(T_GBTN_BY + GRAPH_BTN_H / 2);
    SDL_MouseButtonEvent e = make_btn(gap_x, cy, SDL_BUTTON_LEFT);
    ui_action_t act = ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER);
    CHECK(act != UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_FREQ);
    TEST_END();
}

static void test_graph_click_freq(void)
{
    TEST_BEGIN("graph_click_freq");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_BOTH;   /* start non-zero to verify the change */
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    f32 cx = (f32)(T_GBTN_X0 + GRAPH_BTN_W / 2);
    f32 cy = (f32)(T_GBTN_BY + GRAPH_BTN_H / 2);
    SDL_MouseButtonEvent e = make_btn(cx, cy, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_FREQ);
    TEST_END();
}

static void test_graph_click_wave(void)
{
    TEST_BEGIN("graph_click_wave");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    f32 cx = (f32)(T_GBTN_X0 + (GRAPH_BTN_W + GRAPH_BTN_GAP) + GRAPH_BTN_W / 2);
    f32 cy = (f32)(T_GBTN_BY + GRAPH_BTN_H / 2);
    SDL_MouseButtonEvent e = make_btn(cx, cy, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_TIME);
    TEST_END();
}

static void test_graph_click_both(void)
{
    TEST_BEGIN("graph_click_both");
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    f32 cx = (f32)(T_GBTN_X0 + 2*(GRAPH_BTN_W + GRAPH_BTN_GAP) + GRAPH_BTN_W / 2);
    f32 cy = (f32)(T_GBTN_BY + GRAPH_BTN_H / 2);
    SDL_MouseButtonEvent e = make_btn(cx, cy, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_PLAYER) == UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_BOTH);
    TEST_END();
}

static void test_graph_click_synth_mode(void)
{
    TEST_BEGIN("graph_click_synth_mode");
    /* Graph buttons work identically in APP_MODE_SYNTH (graph is always visible). */
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    audio_t a = dummy_audio();
    playlist_t pl = make_pl(0, -1);
    f32 cx = (f32)(T_GBTN_X0 + (GRAPH_BTN_W + GRAPH_BTN_GAP) + GRAPH_BTN_W / 2);
    f32 cy = (f32)(T_GBTN_BY + GRAPH_BTN_H / 2);
    SDL_MouseButtonEvent e = make_btn(cx, cy, SDL_BUTTON_LEFT);
    CHECK(ui_on_mouse_down(&ui, &e, &a, &pl, APP_MODE_SYNTH) == UI_ACTION_GRAPH_MODE);
    CHECK(ui.graph_mode == GRAPH_MODE_TIME);
    TEST_END();
}

/* ── Graph-mode draw smoke tests ─────────────────────────────────────────── */

static void test_ui_draw_freq_player(void)
{
    TEST_BEGIN("ui_draw_freq_player");
    audio_t a = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_FREQ;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_freq_synth(void)
{
    TEST_BEGIN("ui_draw_freq_synth");
    audio_t a = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_FREQ;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

static void test_ui_draw_time_player(void)
{
    TEST_BEGIN("ui_draw_time_player");
    audio_t a = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_TIME;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_time_synth(void)
{
    TEST_BEGIN("ui_draw_time_synth");
    audio_t a = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_TIME;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

static void test_ui_draw_both_player(void)
{
    TEST_BEGIN("ui_draw_both_player");
    audio_t a = make_draw_audio(AUDIO_STATUS_PLAYING);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_BOTH;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_PLAYER, &g_synth);
    TEST_END();
}

static void test_ui_draw_both_synth(void)
{
    TEST_BEGIN("ui_draw_both_synth");
    audio_t a = make_draw_audio(AUDIO_STATUS_STOPPED);
    spectrum_t s = make_flat_spectrum();
    playlist_t pl = make_pl(0, -1);
    ui_t ui;   SDL_memset(&ui, 0, sizeof(ui));
    ui.graph_mode = GRAPH_MODE_BOTH;
    ui_draw(g_renderer, &a, &s, &pl, &ui, APP_MODE_SYNTH, &g_synth);
    TEST_END();
}

/* ── Suite ───────────────────────────────────────────────────────────────── */

void run_ui_tests(void)
{
    /* Create an offscreen window + software renderer for draw smoke tests. */
    g_window = SDL_CreateWindow("csplayer_tests", PHYS_W, PHYS_H,
                                SDL_WINDOW_HIDDEN);
    if (g_window == NULL) {
        printf("  SKIP  all draw smoke tests (SDL_CreateWindow failed: %s)\n",
               SDL_GetError());
    } else {
        g_renderer = SDL_CreateRenderer(g_window, SDL_SOFTWARE_RENDERER);
        if (g_renderer == NULL) {
            printf("  SKIP  all draw smoke tests (SDL_CreateRenderer failed: %s)\n",
                   SDL_GetError());
            SDL_DestroyWindow(g_window);
            g_window = NULL;
        } else {
            SDL_SetRenderLogicalPresentation(g_renderer, LOG_W, LOG_H,
                                             SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        }
    }

    /* Initialise TTF once for the suite (needed by mock tests). */
    if (!TTF_Init()) {
        printf("  NOTE  TTF_Init failed: %s\n", SDL_GetError());
    }

    /* ── Input handler tests (no renderer needed) ── */
    test_ui_key_space();
    test_ui_key_left();
    test_ui_key_right();
    test_ui_key_other();
    test_ui_key_graph_shortcuts();
    test_ui_key_decompose_toggle();
    test_ui_key_mode_shortcuts();
    test_ui_mouse_down_non_left();
    test_ui_mouse_down_rewind_btn();
    test_ui_mouse_down_play_btn();
    test_ui_mouse_down_next_btn();
    test_graph_btn_center_each();
    test_graph_btn_wrong_idx();
    test_graph_btn_outside();
    test_graph_btn_between();
    test_graph_click_freq();
    test_graph_click_wave();
    test_graph_click_both();
    test_graph_click_synth_mode();
    test_ui_mode_toggle_click();
    test_ui_mouse_down_add_open();
    test_ui_mouse_down_add_blocked();
    test_ui_mouse_down_vol_slider();
    test_ui_mouse_down_vol_clamp_lo();
    test_ui_mouse_down_seek_bar();
    test_ui_mouse_down_scrollbar();
    test_ui_mouse_down_scrollbar_nospace();
    test_ui_mouse_down_pl_delete();
    test_ui_mouse_down_pl_row();
    test_ui_mouse_down_nowhere();
    test_ui_synth_inst_row_click();
    test_ui_synth_note_row_click();
    test_ui_synth_panel_empty_area();
    test_ui_synth_dd_sb_drag_start();
    test_ui_synth_dd_sb_drag_motion();
    test_ui_synth_dd_sb_drag_end();
    test_ui_synth_dropdown_select_inst();
    test_ui_synth_dropdown_close_outside();
    test_ui_synth_scroll_dropdown_open();
    test_ui_synth_hold_mouse_up_playing();
    test_ui_synth_hold_mouse_up_not_playing();
    test_ui_synth_scroll_inst_up();
    test_ui_synth_scroll_inst_down();
    test_ui_synth_scroll_note_up();
    test_ui_synth_scroll_note_down();
    test_ui_synth_scroll_hold_none();
    test_ui_mouse_motion_hover();
    test_ui_mouse_motion_hover_oob();
    test_ui_mouse_motion_sb_held();
    test_ui_mouse_motion_sb_not_held();
    test_ui_mouse_motion_sb_above();
    test_ui_mouse_motion_sb_below();
    test_ui_mouse_motion_vol_drag_lo();
    test_ui_mouse_motion_vol_drag_hi();
    test_ui_mouse_motion_seek_drag_lo();
    test_ui_mouse_motion_seek_drag_hi();
    test_ui_mouse_up_non_left();
    test_ui_mouse_up_sb();
    test_ui_mouse_up_vol();
    test_ui_mouse_up_vol_clamp_lo();
    test_ui_mouse_up_vol_clamp_hi();
    test_ui_mouse_up_seek();
    test_ui_mouse_up_seek_clamp_lo();
    test_ui_mouse_up_seek_clamp_hi();
    test_ui_mouse_up_nodrag();
    test_ui_scroll_vol_up();
    test_ui_scroll_vol_down_clamp();
    test_ui_scroll_vol_up_clamp();
    test_ui_scroll_seek();
    test_ui_scroll_seek_clamp_hi();
    test_ui_scroll_seek_clamp_lo();
    test_ui_scroll_playlist();
    test_ui_scroll_playlist_clamp();
    test_ui_scroll_playlist_nospace();
    test_ui_scroll_playlist_trackpad();
    test_ui_scroll_synth_row_trackpad();
    test_ui_scroll_none();

    /* ── Draw smoke tests (require renderer) ── */
    if (g_renderer != NULL) {
        test_ui_draw_status_playing();
        test_ui_draw_status_paused();
        test_ui_draw_status_stopped();
        test_ui_draw_scrubbing();
        test_ui_draw_vol_clamp_lo();
        test_ui_draw_vol_clamp_hi();
        test_ui_draw_seek_clamp_lo();
        test_ui_draw_seek_clamp_hi();
        test_ui_draw_no_current_track();
        test_ui_draw_with_current_track();
        test_ui_draw_empty_playlist();
        test_ui_draw_full_playlist();
        test_ui_draw_thumb_min();
        test_ui_draw_hover_row();
        test_ui_draw_current_row();
        test_ui_draw_hover_equals_current();
        test_ui_draw_duration_positive();
        test_ui_draw_duration_zero();
        test_ui_draw_dialog_open();
        test_ui_draw_empty_string();
        test_ui_draw_font_null();
        test_ui_draw_ttf_render_fail();
        test_ui_draw_ttf_texture_fail();
        test_ui_draw_font_valid();
        test_ui_draw_mode_btn_synth();
        test_ui_draw_synth_playing();
        test_ui_draw_synth_not_playing();
        test_ui_draw_synth_all_instruments();
        test_ui_draw_synth_all_notes();
        test_ui_draw_synth_inst_dropdown();
        test_ui_draw_synth_note_dropdown();
        test_ui_draw_header_artist();
        test_ui_draw_header_synth_no_title();
        test_ui_draw_freq_player();
        test_ui_draw_freq_synth();
        test_ui_draw_time_player();
        test_ui_draw_time_synth();
        test_ui_draw_both_player();
        test_ui_draw_both_synth();
    }

    TTF_Quit();
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = NULL; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window   = NULL; }
}
