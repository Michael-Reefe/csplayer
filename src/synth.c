/*
 * synth.c — TinySoundFont GM synthesis.
 *
 * Copyright (C) 2026 Michael Reefe.  Licensed under GPL-3.0.
 */

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include "synth.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <math.h>

/* ── Static tables ───────────────────────────────────────────────────────── */

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* Standard General MIDI program names (programs 0–127). */
static const char *const g_inst_names[SYNTH_INSTRUMENTS] = {
    /* 0–7   Piano      */ "Acoustic Grand Piano", "Bright Acoustic Piano",
                           "Electric Grand Piano", "Honky-tonk Piano",
                           "Electric Piano 1", "Electric Piano 2",
                           "Harpsichord", "Clavinet",
    /* 8–15  Chrom Perc */ "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
                           "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    /* 16–23 Organ      */ "Drawbar Organ", "Percussive Organ", "Rock Organ",
                           "Church Organ", "Reed Organ", "Accordion",
                           "Harmonica", "Tango Accordion",
    /* 24–31 Guitar     */ "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
                           "Electric Guitar (jazz)", "Electric Guitar (clean)",
                           "Electric Guitar (muted)", "Overdriven Guitar",
                           "Distortion Guitar", "Guitar harmonics",
    /* 32–39 Bass       */ "Acoustic Bass", "Electric Bass (finger)",
                           "Electric Bass (pick)", "Fretless Bass",
                           "Slap Bass 1", "Slap Bass 2",
                           "Synth Bass 1", "Synth Bass 2",
    /* 40–47 Strings    */ "Violin", "Viola", "Cello", "Contrabass",
                           "Tremolo Strings", "Pizzicato Strings",
                           "Orchestral Harp", "Timpani",
    /* 48–55 Ensemble   */ "String Ensemble 1", "String Ensemble 2",
                           "SynthStrings 1", "SynthStrings 2",
                           "Choir Aahs", "Voice Oohs",
                           "Synth Voice", "Orchestra Hit",
    /* 56–63 Brass      */ "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
                           "French Horn", "Brass Section",
                           "SynthBrass 1", "SynthBrass 2",
    /* 64–71 Reed       */ "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
                           "Oboe", "English Horn", "Bassoon", "Clarinet",
    /* 72–79 Pipe       */ "Piccolo", "Flute", "Recorder", "Pan Flute",
                           "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    /* 80–87 Synth Lead */ "Lead 1 (square)", "Lead 2 (sawtooth)",
                           "Lead 3 (calliope)", "Lead 4 (chiff)",
                           "Lead 5 (charang)", "Lead 6 (voice)",
                           "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    /* 88–95 Synth Pad  */ "Pad 1 (new age)", "Pad 2 (warm)",
                           "Pad 3 (polysynth)", "Pad 4 (choir)",
                           "Pad 5 (bowed)", "Pad 6 (metallic)",
                           "Pad 7 (halo)", "Pad 8 (sweep)",
    /* 96–103 Synth FX  */ "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)",
                           "FX 4 (atmosphere)", "FX 5 (brightness)",
                           "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    /* 104–111 Ethnic   */ "Sitar", "Banjo", "Shamisen", "Koto",
                           "Kalimba", "Bag pipe", "Fiddle", "Shanai",
    /* 112–119 Perc     */ "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
                           "Taiko Drum", "Melodic Tom",
                           "Synth Drum", "Reverse Cymbal",
    /* 120–127 Sound FX */ "Guitar Fret Noise", "Breath Noise",
                           "Seashore", "Bird Tweet",
                           "Telephone Ring", "Helicopter",
                           "Applause", "Gunshot",
};

/* ── Public interface ────────────────────────────────────────────────────── */

bool synth_init(synth_t *s, const char *sf2_path)
{
    assert(s        != NULL);
    assert(sf2_path != NULL);

    SDL_memset(s, 0, sizeof(*s));
    s->inst  = 0;
    s->note  = 69;   /* A4 */
    s->dirty = true;

    s->font = tsf_load_filename(sf2_path);
    if (s->font == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "synth_init: could not load '%s' — synth produces silence",
                    sf2_path);
        return false;
    }

    tsf_set_output(s->font, TSF_MONO, SYNTH_SAMPLE_RATE, 0);
    return true;
}

void synth_build(synth_t *s)
{
    assert(s != NULL);
    assert(s->inst >= 0 && s->inst < SYNTH_INSTRUMENTS);
    assert(s->note >= 0 && s->note < SYNTH_NOTES);

    SDL_memset(s->buf, 0, sizeof(s->buf));

    if (s->font != NULL) {
        /* Render the note without releasing it first, so the buffer captures
         * the sustain phase.  note_off_all is called AFTER rendering to reset
         * the internal voice state without affecting the captured audio. */
        /* tsf_note_on preset_index selects the GM program directly (0–127). */
        tsf_note_on(s->font, s->inst, s->note, 1.0f);
        tsf_render_float(s->font, s->buf, SYNTH_MAX_FRAMES, 0);
        tsf_note_off_all(s->font);
        tsf_reset(s->font);
    }

    s->buf_frames = SYNTH_MAX_FRAMES;
    s->dirty      = false;
}

void synth_quit(synth_t *s)
{
    assert(s != NULL);
    assert(SYNTH_INSTRUMENTS > 0);
    if (s->font != NULL) {
        tsf_close(s->font);
        s->font = NULL;
    }
}

f32 synth_note_hz(i32 note)
{
    assert(note >= 0 && note < SYNTH_NOTES);
    assert(SYNTH_NOTES == 128);  /* MIDI standard: 128 notes */
    return 440.0f * powf(2.0f, (f32)(note - 69) / 12.0f);
}

void synth_note_name(i32 note, char *dst, i32 len)
{
    assert(note >= 0 && note < SYNTH_NOTES);
    assert(dst  != NULL);
    assert(len  >= 5);
    (void)SDL_snprintf(dst, (size_t)len, "%s%d",
                      NOTE_NAMES[note % 12], note / 12 - 1);
}

void synth_instrument_name(i32 inst, char *dst, i32 len)
{
    assert(inst >= 0 && inst < SYNTH_INSTRUMENTS);
    assert(dst  != NULL);
    assert(len  >= 4);
    (void)SDL_strlcpy(dst, g_inst_names[inst], (size_t)len);
}
