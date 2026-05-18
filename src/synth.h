/*
 * synth.h — TinySoundFont-based GM synthesis module.
 *
 * Renders a 10-second sustain buffer for the currently selected instrument
 * and MIDI note.  The buffer is looped infinitely via audio_play_looped
 * while playing and stopped on toggle.
 *
 * TinySoundFont (MIT) is a single-header library: the implementation is
 * compiled in synth.c via #define TSF_IMPLEMENTATION.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#ifndef CSPLAYER_SYNTH_H
#define CSPLAYER_SYNTH_H

#include "types.h"
#include <stdbool.h>

/* Forward-declare tsf so callers do not need to include tsf.h. */
typedef struct tsf tsf;

/* ── Module state ────────────────────────────────────────────────────────── */

typedef struct {
    tsf *font;                   /* TinySoundFont handle; NULL if .sf2 absent */
    f32  buf[SYNTH_MAX_FRAMES];  /* PCM buffer, looped while playing          */
    i32  buf_frames;             /* valid frames in buf (= SYNTH_MAX_FRAMES)   */
    bool dirty;                  /* true → buf needs to be rebuilt             */
    i32  inst;                   /* GM program [0, SYNTH_INSTRUMENTS)          */
    i32  note;                   /* MIDI note  [0, SYNTH_NOTES)                */
} synth_t;

/* ── Public interface ────────────────────────────────────────────────────── */

/*
 * Load the SoundFont at sf2_path and initialise state with defaults
 * (inst=0, note=69 A4).  Returns false if the .sf2 cannot be loaded;
 * synth mode will produce silence but remain functional.
 */
bool synth_init(synth_t *s, const char *sf2_path);

/*
 * Render a 10-second sustain buffer for the current inst/note.
 * Clears dirty.  Safe to call when font==NULL (fills with silence).
 */
void synth_build(synth_t *s);

/* Free the TinySoundFont handle.  Safe to call when font==NULL. */
void synth_quit(synth_t *s);

/* Return the frequency in Hz for a MIDI note number. */
f32  synth_note_hz(i32 note);

/* Write the note name (e.g. "A4", "C#3") into dst (at least 5 bytes). */
void synth_note_name(i32 note, char *dst, i32 len);

/* Write the GM program name (e.g. "Acoustic Grand Piano") into dst. */
void synth_instrument_name(i32 inst, char *dst, i32 len);

#endif /* CSPLAYER_SYNTH_H */
