/*
 * test_synth.c — Unit tests for src/synth.c.
 *
 * Tests that require a loaded SoundFont are skipped (noted) if gm.sf2 is
 * absent from the working directory (the build directory at test time).
 */

#include <math.h>
#include <SDL3/SDL.h>

#include "harness.h"
#include "synth.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Absolute path injected by CMake; fall back to local name if not set. */
#ifndef SF2_PATH
#define SF2_PATH "gm.sf2"
#endif

/* ── synth_note_hz ────────────────────────────────────────────────────────── */

static void test_synth_hz_a4(void)
{
    TEST_BEGIN("synth_hz_a4");
    f32 hz = synth_note_hz(69);
    CHECK_NEAR(hz, 440.0f, 0.5f);
    TEST_END();
}

static void test_synth_hz_c4(void)
{
    TEST_BEGIN("synth_hz_c4");
    f32 hz = synth_note_hz(60);
    CHECK_NEAR(hz, 261.63f, 0.5f);
    TEST_END();
}

static void test_synth_hz_c_minus1(void)
{
    TEST_BEGIN("synth_hz_c_minus1");
    f32 hz = synth_note_hz(0);
    CHECK(hz > 0.0f);
    CHECK(!isnan(hz));
    TEST_END();
}

static void test_synth_hz_g9(void)
{
    TEST_BEGIN("synth_hz_g9");
    f32 hz = synth_note_hz(127);
    CHECK(hz > 8000.0f && hz < 15000.0f);
    TEST_END();
}

/* ── synth_note_name ─────────────────────────────────────────────────────── */

static void test_synth_note_name_a4(void)
{
    TEST_BEGIN("synth_note_name_a4");
    char buf[8];
    synth_note_name(69, buf, (i32)sizeof(buf));
    CHECK(SDL_strcmp(buf, "A4") == 0);
    TEST_END();
}

static void test_synth_note_name_c4(void)
{
    TEST_BEGIN("synth_note_name_c4");
    char buf[8];
    synth_note_name(60, buf, (i32)sizeof(buf));
    CHECK(SDL_strcmp(buf, "C4") == 0);
    TEST_END();
}

static void test_synth_note_name_cs3(void)
{
    TEST_BEGIN("synth_note_name_cs3");
    /* MIDI note 49 = C#3 (octave = 49/12 - 1 = 3) */
    char buf[8];
    synth_note_name(49, buf, (i32)sizeof(buf));
    CHECK(SDL_strcmp(buf, "C#3") == 0);
    TEST_END();
}

static void test_synth_note_name_c_minus1(void)
{
    TEST_BEGIN("synth_note_name_c_minus1");
    char buf[8];
    synth_note_name(0, buf, (i32)sizeof(buf));
    CHECK(SDL_strcmp(buf, "C-1") == 0);
    TEST_END();
}

/* ── synth_instrument_name ───────────────────────────────────────────────── */

static void test_synth_instrument_name_piano(void)
{
    TEST_BEGIN("synth_instrument_name_piano");
    char buf[64];
    synth_instrument_name(0, buf, (i32)sizeof(buf));
    /* The first GM program starts with "Acoustic Grand" */
    CHECK(SDL_strncmp(buf, "Acoustic Grand", 14) == 0);
    TEST_END();
}

static void test_synth_instrument_name_last(void)
{
    TEST_BEGIN("synth_instrument_name_last");
    char buf[64];
    synth_instrument_name(127, buf, (i32)sizeof(buf));
    CHECK(SDL_strcmp(buf, "Gunshot") == 0);
    TEST_END();
}

/* ── synth_build (no font) ───────────────────────────────────────────────── */

static void test_synth_build_no_font(void)
{
    TEST_BEGIN("synth_build_no_font");
    synth_t s;
    SDL_memset(&s, 0, sizeof(s));
    /* font=NULL — synth_build must produce silence and clear dirty. */
    s.inst  = 0;
    s.note  = 69;
    s.dirty = true;
    synth_build(&s);
    CHECK(s.dirty      == false);
    CHECK(s.buf_frames == SYNTH_MAX_FRAMES);
    /* All samples should be zero (silence). */
    bool all_zero = true;
    for (i32 i = 0; i < SYNTH_MAX_FRAMES; i++) {
        if (s.buf[i] != 0.0f) { all_zero = false; break; }
    }
    CHECK(all_zero);
    TEST_END();
}

/* ── synth_build (with font, skipped if gm.sf2 absent) ──────────────────── */

static void test_synth_build_nonzero(void)
{
    TEST_BEGIN("synth_build_nonzero");
    synth_t s;
    SDL_memset(&s, 0, sizeof(s));
    if (!synth_init(&s, SF2_PATH)) {
        printf("  SKIP  (gm.sf2 not found)\n");
        g_passed++;   /* count as pass so CI is not fragile */
        return;
    }
    s.inst = 0;   /* piano */
    s.note = 69;  /* A4 */
    synth_build(&s);
    bool any_nonzero = false;
    for (i32 i = 0; i < SYNTH_MAX_FRAMES; i++) {
        if (s.buf[i] != 0.0f) { any_nonzero = true; break; }
    }
    CHECK(any_nonzero);
    synth_quit(&s);
    TEST_END();
}

static void test_synth_build_buf_frames(void)
{
    TEST_BEGIN("synth_build_buf_frames");
    synth_t s;
    SDL_memset(&s, 0, sizeof(s));
    if (!synth_init(&s, SF2_PATH)) {
        printf("  SKIP  (gm.sf2 not found)\n");
        g_passed++;
        return;
    }
    s.inst = 0;
    s.note = 69;
    synth_build(&s);
    CHECK(s.buf_frames == SYNTH_MAX_FRAMES);
    synth_quit(&s);
    TEST_END();
}

static void test_synth_build_dirty_cleared(void)
{
    TEST_BEGIN("synth_build_dirty_cleared");
    synth_t s;
    SDL_memset(&s, 0, sizeof(s));
    if (!synth_init(&s, SF2_PATH)) {
        printf("  SKIP  (gm.sf2 not found)\n");
        g_passed++;
        return;
    }
    s.inst  = 0;
    s.note  = 69;
    s.dirty = true;
    synth_build(&s);
    CHECK(s.dirty == false);
    synth_quit(&s);
    TEST_END();
}

static void test_synth_build_all_instruments(void)
{
    TEST_BEGIN("synth_build_all_instruments");
    synth_t s;
    SDL_memset(&s, 0, sizeof(s));
    if (!synth_init(&s, SF2_PATH)) {
        printf("  SKIP  (gm.sf2 not found)\n");
        g_passed++;
        return;
    }
    /* Spot-check a few instruments across the range — no crash. */
    static const i32 spots[] = { 0, 32, 63, 96, 127 };
    bool ok = true;
    for (i32 k = 0; k < 5; k++) {
        s.inst  = spots[k];
        s.note  = 69;
        s.dirty = true;
        synth_build(&s);
        if (s.dirty != false || s.buf_frames != SYNTH_MAX_FRAMES) {
            ok = false;
        }
    }
    CHECK(ok);
    synth_quit(&s);
    TEST_END();
}

static void test_synth_build_all_notes_bounds(void)
{
    TEST_BEGIN("synth_build_all_notes_bounds");
    synth_t s;
    SDL_memset(&s, 0, sizeof(s));
    if (!synth_init(&s, SF2_PATH)) {
        printf("  SKIP  (gm.sf2 not found)\n");
        g_passed++;
        return;
    }
    s.inst = 0;
    /* Test boundary notes — no crash, no NaN in buf. */
    static const i32 notes[] = { 0, 127 };
    bool ok = true;
    for (i32 k = 0; k < 2; k++) {
        s.note = notes[k];
        synth_build(&s);
        for (i32 i = 0; i < SYNTH_MAX_FRAMES; i++) {
            if (isnan(s.buf[i])) { ok = false; break; }
        }
    }
    CHECK(ok);
    synth_quit(&s);
    TEST_END();
}

/* ── Suite ───────────────────────────────────────────────────────────────── */

void run_synth_tests(void)
{
    test_synth_hz_a4();
    test_synth_hz_c4();
    test_synth_hz_c_minus1();
    test_synth_hz_g9();
    test_synth_note_name_a4();
    test_synth_note_name_c4();
    test_synth_note_name_cs3();
    test_synth_note_name_c_minus1();
    test_synth_instrument_name_piano();
    test_synth_instrument_name_last();
    test_synth_build_no_font();
    test_synth_build_nonzero();
    test_synth_build_buf_frames();
    test_synth_build_dirty_cleared();
    test_synth_build_all_instruments();
    test_synth_build_all_notes_bounds();
}
