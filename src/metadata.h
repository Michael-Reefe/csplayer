#ifndef CSPLAYER_METADATA_H
#define CSPLAYER_METADATA_H

#include "types.h"
#include <stdbool.h>

/*
 * Read the artist tag from an audio file into dst (NUL-terminated, max len bytes).
 * Supports MP3 (ID3v2 TPE1 / ID3v1), Ogg Vorbis, FLAC, and WAV (RIFF INFO IART).
 * Returns true if an artist was found, false otherwise (dst is left unchanged).
 * Format is detected from magic bytes, not the file extension.
 */
bool metadata_read_artist(const char *path, char *dst, i32 len);

#endif /* CSPLAYER_METADATA_H */
