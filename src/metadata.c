/*
 * metadata.c — Artist-tag extraction from audio files.
 *
 * Detects the container from leading magic bytes (not the file extension) and
 * reads the artist string from the matching tag format:
 *   • MP3  — ID3v2.3/2.4 (TPE1 frame), falling back to ID3v1 (offset -128).
 *   • OGG  — Vorbis comment (ARTIST=) in the second Ogg page.
 *   • FLAC — Vorbis comment in the VORBIS_COMMENT metadata block.
 *   • WAV  — RIFF LIST/INFO IART sub-chunk.
 * All parsing uses fixed-size stack buffers and bounded loops (NASA Rule 2/3).
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#include "metadata.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

/* ── Internal helpers ────────────────────────────────────────────────────── */

static u32 read_u32_be(const u8 *p)
{
    assert(p != NULL);
    assert(CHAR_BIT == 8);  /* platform must use 8-bit bytes */
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u32 read_u32_le(const u8 *p)
{
    assert(p != NULL);
    assert(CHAR_BIT == 8);  /* platform must use 8-bit bytes */
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Decode a 4-byte ID3v2 syncsafe integer. */
static u32 syncsafe_to_u32(const u8 *p)
{
    assert(p != NULL);
    assert(CHAR_BIT == 8);  /* syncsafe integers assume 8-bit bytes */
    return ((u32)(p[0] & 0x7f) << 21) |
           ((u32)(p[1] & 0x7f) << 14) |
           ((u32)(p[2] & 0x7f) <<  7) |
            (u32)(p[3] & 0x7f);
}

/* Copy at most (len-1) bytes from src, NUL-terminate, strip trailing spaces. */
static void copy_strip(char *dst, const char *src, i32 src_len, i32 dst_len)
{
    assert(dst     != NULL);
    assert(src     != NULL);
    assert(src_len >= 0);
    assert(dst_len >  0);

    i32 n = src_len < dst_len - 1 ? src_len : dst_len - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
    /* Strip trailing spaces / NULs (common in ID3v1 fixed-width fields). */
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\0')) {
        dst[--n] = '\0';
    }
}

/* ── Vorbis comment parser (shared by OGG and FLAC) ─────────────────────── */

/*
 * Parse a Vorbis comment block already loaded into buf[buf_len].
 * Returns true and fills dst if an ARTIST comment is found.
 */
static bool parse_vorbis_comments(const u8 *buf, i32 buf_len,
                                   char *dst, i32 dst_len)
{
    assert(buf != NULL && dst != NULL);
    assert(buf_len >= 0 && dst_len > 0);

    i32 pos = 0;
    if (buf_len < 4) { return false; }

    /* Skip vendor string: 4-byte LE length + chars. */
    u32 vendor_len = read_u32_le(buf + pos);
    pos += 4;
    if (pos + (i32)vendor_len > buf_len) { return false; }
    pos += (i32)vendor_len;

    if (pos + 4 > buf_len) { return false; }
    u32 count = read_u32_le(buf + pos);
    pos += 4;

    for (u32 i = 0; i < count && i < 512; i++) {
        if (pos + 4 > buf_len) { break; }
        u32 clen = read_u32_le(buf + pos);
        pos += 4;
        if (clen == 0 || pos + (i32)clen > buf_len) { break; }

        const char *comment = (const char *)(buf + pos);
        pos += (i32)clen;

        /* Case-insensitive prefix match for "ARTIST=". */
        if (clen > 7) {
            char prefix[8];
            memcpy(prefix, comment, 7);
            prefix[7] = '\0';
            /* Uppercase the prefix for comparison. */
            for (i32 k = 0; k < 7; k++) {
                if (prefix[k] >= 'a' && prefix[k] <= 'z') {
                    prefix[k] = (char)(prefix[k] - 32);
                }
            }
            if (memcmp(prefix, "ARTIST=", 7) == 0) {
                copy_strip(dst, comment + 7, (i32)clen - 7, dst_len);
                return dst[0] != '\0';
            }
        }
    }
    return false;
}

/* ── MP3 / ID3 ───────────────────────────────────────────────────────────── */

static bool read_id3v2(FILE *f, char *dst, i32 dst_len)
{
    assert(f       != NULL);
    assert(dst     != NULL);
    assert(dst_len >  0);

    u8 hdr[10];
    if (fread(hdr, 1, 10, f) != 10)       { return false; }
    if (memcmp(hdr, "ID3", 3) != 0)        { return false; }
    u8 ver = hdr[3];
    if (ver != 3 && ver != 4)              { return false; } /* only 2.3 / 2.4 */

    u32 tag_size = syncsafe_to_u32(hdr + 6);
    if (tag_size > 8 * 1024 * 1024)       { return false; } /* sanity limit 8 MB */

    u32 consumed = 0;
    u8  frame_hdr[10];

    while (consumed + 10 < tag_size) {
        if (fread(frame_hdr, 1, 10, f) != 10) { break; }
        consumed += 10;

        /* Padding / end of frames. */
        if (frame_hdr[0] == 0) { break; }

        u32 frame_size = read_u32_be(frame_hdr + 4);
        if (frame_size == 0 || frame_size > tag_size - consumed) { break; }

        if (memcmp(frame_hdr, "TPE1", 4) == 0 && frame_size >= 2) {
            /* Skip 1-byte text-encoding flag. */
            u8 enc;
            if (fread(&enc, 1, 1, f) != 1) { break; }
            u32 text_len = frame_size - 1;
            if (text_len >= (u32)dst_len) { text_len = (u32)dst_len - 1; }
            if (fread(dst, 1, text_len, f) == text_len) {
                dst[text_len] = '\0';
                /* Strip BOM for UTF-16 (enc==1) — not fully decoded, but
                 * Latin-1 (enc==0) and UTF-8 (enc==3) are used as-is. */
                if (enc == 1 && text_len > 1) { dst[0] = '\0'; } /* skip UTF-16 */
                while (text_len > 0 && (dst[text_len - 1] == '\0' || dst[text_len - 1] == ' ')) {
                    dst[--text_len] = '\0';
                }
                return dst[0] != '\0';
            }
            break;
        }

        if (fseek(f, (long)frame_size, SEEK_CUR) != 0) { break; }
        consumed += frame_size;
    }
    return false;
}

static bool read_id3v1(FILE *f, char *dst, i32 dst_len)
{
    assert(f       != NULL);
    assert(dst     != NULL);
    assert(dst_len >  0);

    if (fseek(f, -128, SEEK_END) != 0)  { return false; }
    u8 tag[128];
    if (fread(tag, 1, 128, f) != 128)   { return false; }
    if (memcmp(tag, "TAG", 3) != 0)     { return false; }

    /* Artist field: bytes 33–62 (30 bytes). */
    copy_strip(dst, (const char *)(tag + 33), 30, dst_len);
    return dst[0] != '\0';
}

static bool read_artist_mp3(FILE *f, char *dst, i32 dst_len)
{
    assert(f       != NULL);
    assert(dst     != NULL);
    assert(dst_len >  0);
    if (read_id3v2(f, dst, dst_len)) { return true; }
    rewind(f);
    return read_id3v1(f, dst, dst_len);
}

/* ── OGG Vorbis ──────────────────────────────────────────────────────────── */

/*
 * Extract the data payload from OGG page at the current file position.
 * Returns the number of bytes read into buf, or -1 on error.
 * Advances f to the next page boundary.
 */
static i32 read_ogg_page_data(FILE *f, u8 *buf, i32 buf_cap)
{
    assert(f       != NULL);
    assert(buf     != NULL);
    assert(buf_cap >  0);

    u8 page_hdr[27];
    if (fread(page_hdr, 1, 27, f) != 27) { return -1; }
    if (memcmp(page_hdr, "OggS", 4) != 0)   { return -1; }

    u8  nseg   = page_hdr[26];
    u8  segtab[255];
    if (nseg == 0 || fread(segtab, 1, nseg, f) != nseg) { return -1; }

    i32 total = 0;
    for (u8 s = 0; s < nseg; s++) { total += segtab[s]; }
    if (total <= 0 || total > buf_cap) {
        if (fseek(f, (long)total, SEEK_CUR) != 0) { return -1; }
        return 0;
    }
    if ((i32)fread(buf, 1, (size_t)total, f) != total) { return -1; }
    return total;
}

static bool read_artist_ogg(FILE *f, char *dst, i32 dst_len)
{
    assert(f       != NULL);
    assert(dst     != NULL);
    assert(dst_len >  0);

    u8 buf[65536];
    /* Page 1: identification header (skip). */
    if (read_ogg_page_data(f, buf, (i32)sizeof(buf)) <= 0) { return false; }
    /* Page 2: comment header. */
    i32 len = read_ogg_page_data(f, buf, (i32)sizeof(buf));
    if (len < 7) { return false; }
    if (memcmp(buf, "\x03vorbis", 7) != 0) { return false; }
    return parse_vorbis_comments(buf + 7, len - 7, dst, dst_len);
}

/* ── FLAC ────────────────────────────────────────────────────────────────── */

static bool read_artist_flac(FILE *f, char *dst, i32 dst_len)
{
    assert(f       != NULL);
    assert(dst     != NULL);
    assert(dst_len >  0);

    /* Skip the 4-byte "fLaC" magic already confirmed by caller. */
    if (fseek(f, 4, SEEK_SET) != 0) { return false; }

    for (i32 iter = 0; iter < 64; iter++) {
        u8 blk_hdr[4];
        if (fread(blk_hdr, 1, 4, f) != 4) { break; }

        bool last       = (blk_hdr[0] & 0x80) != 0;
        u8   block_type = blk_hdr[0] & 0x7f;
        u32  block_len  = ((u32)blk_hdr[1] << 16) |
                          ((u32)blk_hdr[2] <<  8) |
                           (u32)blk_hdr[3];

        if (block_type == 4 && block_len > 0 && block_len < 65536) {
            /* VORBIS_COMMENT — use stack buffer (block_len already < 65536). */
            u8 buf[65536];
            if (fread(buf, 1, block_len, f) == block_len) {
                return parse_vorbis_comments(buf, (i32)block_len, dst, dst_len);
            }
            return false;
        }

        if (fseek(f, (long)block_len, SEEK_CUR) != 0) { break; }
        if (last) { break; }
    }
    return false;
}

/* ── WAV (RIFF INFO) ─────────────────────────────────────────────────────── */

static bool read_artist_wav(FILE *f, char *dst, i32 dst_len)
{
    assert(f       != NULL);
    assert(dst     != NULL);
    assert(dst_len >  0);

    /* Skip "RIFF" (4) + size (4) + "WAVE" (4) = 12 bytes. */
    if (fseek(f, 12, SEEK_SET) != 0) { return false; }

    u8 chunk_hdr[8];
    for (i32 iter = 0; iter < 256; iter++) {
        if (fread(chunk_hdr, 1, 8, f) != 8) { break; }

        u32 chunk_size = read_u32_le(chunk_hdr + 4);

        if (memcmp(chunk_hdr, "LIST", 4) == 0 && chunk_size >= 4) {
            u8 list_type[4];
            if (fread(list_type, 1, 4, f) != 4) { break; }
            if (memcmp(list_type, "INFO", 4) == 0) {
                u32 remaining = chunk_size - 4;
                while (remaining >= 8) {
                    u8 sub_hdr[8];
                    if (fread(sub_hdr, 1, 8, f) != 8) { return false; }
                    u32 sub_size = read_u32_le(sub_hdr + 4);
                    remaining -= 8;

                    if (memcmp(sub_hdr, "IART", 4) == 0 && sub_size > 0) {
                        u32 read_len = sub_size < (u32)(dst_len - 1)
                                       ? sub_size : (u32)(dst_len - 1);
                        if (fread(dst, 1, read_len, f) == read_len) {
                            dst[read_len] = '\0';
                            while (read_len > 0 && (dst[read_len - 1] == '\0' || dst[read_len - 1] == ' ')) {
                                dst[--read_len] = '\0';
                            }
                            return dst[0] != '\0';
                        }
                        return false;
                    }
                    /* Skip sub-chunk + padding byte if size is odd. */
                    u32 skip = sub_size + (sub_size & 1u);
                    if (skip > remaining) { break; }
                    if (fseek(f, (long)skip, SEEK_CUR) != 0) { return false; }
                    remaining -= skip;
                }
            } else {
                /* Not INFO — skip rest of LIST. */
                i64 skip = (i64)chunk_size - 4;
                if (skip > 0 && fseek(f, (long)skip, SEEK_CUR) != 0) { break; }
            }
        } else {
            /* Skip non-LIST chunk (pad to even size). */
            i64 skip = (i64)chunk_size + (chunk_size & 1u);
            if (skip > 0 && fseek(f, (long)skip, SEEK_CUR) != 0) { break; }
        }
    }
    return false;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool metadata_read_artist(const char *path, char *dst, i32 len)
{
    assert(path != NULL);
    assert(dst  != NULL);
    assert(len  >  0);

    FILE *f = fopen(path, "rb");
    if (f == NULL) { return false; }

    u8   magic[12];
    bool result = false;
    i32  n      = (i32)fread(magic, 1, 12, f);
    rewind(f);

    if (n >= 4) {
        if (memcmp(magic, "OggS", 4) == 0) {
            result = read_artist_ogg(f, dst, len);
        } else if (memcmp(magic, "fLaC", 4) == 0) {
            result = read_artist_flac(f, dst, len);
        } else if (n >= 12 && memcmp(magic, "RIFF", 4) == 0 &&
                   memcmp(magic + 8, "WAVE", 4) == 0) {
            result = read_artist_wav(f, dst, len);
        } else {
            result = read_artist_mp3(f, dst, len);
        }
    }

    (void)fclose(f);
    return result;
}
