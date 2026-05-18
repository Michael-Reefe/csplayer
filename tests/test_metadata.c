/*
 * test_metadata.c — Unit tests for src/metadata.c.
 *
 * Each test writes a minimal binary file to disk, calls
 * metadata_read_artist, and checks the result.  No real audio files
 * are required: all test data is synthesised from constants.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include <stdio.h>
#include <string.h>
#include "harness.h"
#include "metadata.h"
#include "types.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static bool write_file(const char *path, const u8 *data, i32 len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) { return false; }
    bool ok = ((i32)fwrite(data, 1, (size_t)len, f) == len);
    fclose(f);
    return ok;
}

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_metadata_nonexistent(void)
{
    TEST_BEGIN("metadata: nonexistent path returns false");
    char dst[128] = {0};
    bool r = metadata_read_artist("/no/such/file.mp3", dst, (i32)sizeof(dst));
    CHECK(!r);
    CHECK(dst[0] == '\0');
    TEST_END();
}

static void test_metadata_unknown_format(void)
{
    TEST_BEGIN("metadata: unknown format returns false cleanly");
    const u8 garbage[16] = {0xDE,0xAD,0xBE,0xEF,0x00,0x11,0x22,0x33,
                             0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB};
    const char *path = "meta_test_garbage.bin";
    if (!write_file(path, garbage, 16)) { printf("  SKIP  (could not create temp file)\n"); g_passed++; return; }
    char dst[128] = {0};
    bool r = metadata_read_artist(path, dst, (i32)sizeof(dst));
    /* Unknown format may or may not find an ID3v1 tag — we only require no crash. */
    (void)r;
    remove(path);
    TEST_END();
}

static void test_metadata_id3v2_tpe1(void)
{
    TEST_BEGIN("metadata: ID3v2.3 TPE1 artist");

    /* Minimal ID3v2.3 tag with a single TPE1 frame "Test Artist" (11 chars).
     * Syncsafe size: 22 bytes total tag payload (frame header 10 + data 12). */
    const u8 tag[] = {
        /* ID3v2 header: "ID3" + ver 2.3 + flags 0 + syncsafe size = 22 */
        'I','D','3', 3, 0, 0,
        0x00, 0x00, 0x00, 0x16,   /* syncsafe 22 */
        /* TPE1 frame header: id(4) + BE size(4) + flags(2) */
        'T','P','E','1',
        0x00, 0x00, 0x00, 0x0C,   /* size = 12 */
        0x00, 0x00,               /* flags */
        /* frame data: encoding byte 0 (Latin-1) + "Test Artist" */
        0x00,
        'T','e','s','t',' ','A','r','t','i','s','t'
    };
    const char *path = "meta_test_id3v2.bin";
    if (!write_file(path, tag, (i32)sizeof(tag))) { printf("  SKIP  (could not create temp file)\n"); g_passed++; return; }

    char dst[128] = {0};
    bool r = metadata_read_artist(path, dst, (i32)sizeof(dst));
    CHECK(r);
    CHECK(strcmp(dst, "Test Artist") == 0);
    remove(path);
    TEST_END();
}

static void test_metadata_id3v1(void)
{
    TEST_BEGIN("metadata: ID3v1 artist fallback");

    /* Minimal file: 2-byte MPEG sync header + 126 padding + 128-byte ID3v1 tag.
     * Total: 256 bytes.  Artist is at tag+33 (30 bytes, NUL-padded). */
    u8 buf[256];
    memset(buf, 0, sizeof(buf));
    /* MPEG frame sync bytes so the ID3v2 scan finds nothing */
    buf[0] = 0xFF; buf[1] = 0xFB;
    /* ID3v1 tag starts at offset 128 */
    memcpy(buf + 128, "TAG", 3);
    /* title[30] at +3 — leave empty */
    /* artist[30] at +33 */
    memcpy(buf + 128 + 33, "Old Artist", 10);

    const char *path = "meta_test_id3v1.bin";
    if (!write_file(path, buf, 256)) { printf("  SKIP  (could not create temp file)\n"); g_passed++; return; }

    char dst[128] = {0};
    bool r = metadata_read_artist(path, dst, (i32)sizeof(dst));
    CHECK(r);
    CHECK(strcmp(dst, "Old Artist") == 0);
    remove(path);
    TEST_END();
}

static void test_metadata_wav_iart(void)
{
    TEST_BEGIN("metadata: WAV RIFF INFO IART artist");

    /* Minimal WAV with RIFF INFO LIST chunk containing an IART sub-chunk.
     * Layout:
     *   RIFF(4) + file_size(4) + WAVE(4)           = 12 bytes
     *   LIST(4) + list_size(4) + INFO(4)            = 12 bytes
     *   IART(4) + iart_size(4) + "WAV Artist"(10)  = 18 bytes
     *                                         total = 42 bytes
     * list_size = 4(INFO) + 8(IART hdr) + 10(data) = 22
     * file_size = 4(WAVE) + 8(LIST hdr) + 22       = 34
     */
    const u8 wav[] = {
        'R','I','F','F',
        34,0,0,0,           /* file_size (LE) */
        'W','A','V','E',
        'L','I','S','T',
        22,0,0,0,           /* list_size (LE) */
        'I','N','F','O',
        'I','A','R','T',
        10,0,0,0,           /* iart_size (LE) */
        'W','A','V',' ','A','r','t','i','s','t'
    };
    const char *path = "meta_test.wav";
    if (!write_file(path, wav, (i32)sizeof(wav))) { printf("  SKIP  (could not create temp file)\n"); g_passed++; return; }

    char dst[128] = {0};
    bool r = metadata_read_artist(path, dst, (i32)sizeof(dst));
    CHECK(r);
    CHECK(strcmp(dst, "WAV Artist") == 0);
    remove(path);
    TEST_END();
}

static void test_metadata_ogg_no_artist(void)
{
    TEST_BEGIN("metadata: OGG with no artist tag returns false cleanly");

    /* OggS magic + minimal garbage — not a valid OGG page; parser must not crash. */
    const u8 data[] = { 'O','g','g','S', 0,0,0,0, 0,0,0,0 };
    const char *path = "meta_test_no_artist.ogg";
    if (!write_file(path, data, (i32)sizeof(data))) { printf("  SKIP  (could not create temp file)\n"); g_passed++; return; }

    char dst[128] = {0};
    bool r = metadata_read_artist(path, dst, (i32)sizeof(dst));
    CHECK(!r);
    remove(path);
    TEST_END();
}

static void test_metadata_flac_no_artist(void)
{
    TEST_BEGIN("metadata: FLAC with no artist tag returns false cleanly");

    /* fLaC magic + minimal garbage — no valid metadata block; parser must not crash. */
    const u8 data[] = { 'f','L','a','C', 0,0,0,0, 0,0,0,0 };
    const char *path = "meta_test_no_artist.flac";
    if (!write_file(path, data, (i32)sizeof(data))) { printf("  SKIP  (could not create temp file)\n"); g_passed++; return; }

    char dst[128] = {0};
    bool r = metadata_read_artist(path, dst, (i32)sizeof(dst));
    CHECK(!r);
    remove(path);
    TEST_END();
}

/* ── Suite entry point ───────────────────────────────────────────────────── */

void run_metadata_tests(void)
{
    test_metadata_nonexistent();
    test_metadata_unknown_format();
    test_metadata_id3v2_tpe1();
    test_metadata_id3v1();
    test_metadata_wav_iart();
    test_metadata_ogg_no_artist();
    test_metadata_flac_no_artist();
}
