/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Generation IR: meter, annotated events, HarmonicContext, Tier-2 params.
// Times are quarter-note beats from piece start; pre-modifier events align to the 16th grid.

#ifndef ANO_MUSIC_IR_H
#define ANO_MUSIC_IR_H

#include <anoptic_music.h>

#include "music_theory.h"

#define ANO_MUSIC_GRID 0.25 // 16th-note grid, in quarter-note beats


/* Meter */

#define ANO_METER_MAX_SLOTS 32 // 12/8 needs 24

static inline AnoMeter ano_meter_default(void) { return (AnoMeter){ 4, 4 }; }

double ano_meter_bar_quarters(AnoMeter m); // 4/4 -> 4.0, 6/8 -> 3.0
int    ano_meter_bar_of(AnoMeter m, double start); // Python float floordiv
double ano_meter_beat_in_bar(AnoMeter m, double start); // 1-based within bar
int    ano_meter_slots(AnoMeter m); // 16 in 4/4, 12 in 3/4 and 6/8
int    ano_meter_slot_of(AnoMeter m, double start);

bool   ano_meter_is_compound(AnoMeter m); // 6/8, 9/8, 12/8 group in threes
int    ano_meter_pulses(AnoMeter m); // 4/4 -> 4, 6/8 -> 2
double ano_meter_pulse_quarters(AnoMeter m); // 1.0 in x/4, 1.5 in 6/8
int    ano_meter_pulse_slots(AnoMeter m); // 4 in x/4, 6 in 6/8

// Accent hierarchy: downbeat 4.0, mid-bar 3.5, pulses 3.0, 8ths 2.0, 16ths 1.0.
uint32_t ano_meter_metric_weights(AnoMeter m, double out[ANO_METER_MAX_SLOTS]);
uint32_t ano_meter_strong_slots(AnoMeter m, int out[ANO_METER_MAX_SLOTS]); // weight >= 3.0


/* Annotated Events */

extern const char *const ANO_LAYER_NAMES[ANO_MUSIC_LAYER_COUNT]; // "pad", ...

bool ano_note_event_valid(const AnoNoteEvent *ev);

// Playable core + inspection annotations (textdump / lint). role is free string.
typedef struct AnoMusicEvent
{
    AnoNoteEvent core;
    uint8_t      degree;       // 1..7 within the bar's scale, 0 = None
    char         chordSym[16];
    char         role[20];
} AnoMusicEvent;

// Collapse out -> both... -> in chains into one note. Orphan out -> plain; orphan in struck.
// Input: chronological per-layer emission order. in == out legal. Returns merged count, clamped to cap.
uint32_t ano_merge_ties(const AnoMusicEvent *in, uint32_t n,
                        AnoMusicEvent *out, uint32_t cap);


/* HarmonicContext */

typedef enum AnoCtxSlot
{
    ANO_CTX_SLOT_NONE = 0,
    ANO_CTX_SLOT_PRE_CADENCE,
    ANO_CTX_SLOT_CADENCE,
} AnoCtxSlot;

typedef enum AnoObligation
{
    ANO_OBL_NONE = 0,
    ANO_OBL_CADENTIAL64,
    ANO_OBL_LAMENT,
    ANO_OBL_TONICIZE, // target degree in obligationTarget
} AnoObligation;

typedef enum AnoPhraseForm
{
    ANO_FORM_NONE = 0,
    ANO_FORM_ANTECEDENT,
    ANO_FORM_CONSEQUENT,
} AnoPhraseForm;

typedef struct AnoChordSpan
{
    double   off; // beat offset within the bar
    AnoChord chord;
} AnoChordSpan;

// Per-bar harmonic state for generators. chordPcs[0] = sounding bass pc (inversion).
// chords timeline populated only for compressed cadential 6/4 (2 spans max).
typedef struct AnoHarmonicContext
{
    int      bar; // 0-based
    AnoScale scale;
    AnoChord chord; // .valid == false = no chord
    char     chordSym[16];
    uint8_t  chordPcs[5];
    uint32_t chordPcCount;
    AnoChord nextChord;
    char     nextChordSym[16];
    double   tension;
    AnoCtxSlot    cadenceSlot;
    int8_t        cadencePolicy; // AnoCadencePolicy; NONE when slot empty
    char          modulation[96]; // key-change annotation, inspection only
    AnoObligation obligation;
    uint8_t       obligationTarget; // tonicize resolution degree
    int           phrasePos;
    int           phraseBars;
    AnoChordSpan  chords[2];
    uint32_t      chordSpanCount;
    int           phraseApex; // bar-in-phrase of planned apex, -1 = none
    AnoPhraseForm form;
} AnoHarmonicContext;

// Chord in force at beat offset: last timeline entry at or before offset (1e-9 slack).
AnoChord ano_ctx_chord_at(const AnoHarmonicContext *ctx, double beatOffset);


/* Tier-2 Parameters */

extern const char *const ANO_PATCH_NAMES[ANO_PATCH_COUNT]; // "" first

// Knobs that multiply into draws/costs are double (never float; see music_theory.h).
// layers is ORDERED — gate emits pad/bass/melody/perc/arp; conductor iterates that order (not a bitmask).
typedef struct AnoGenParams
{
    double  tempoBpm;      // 100.0
    double  noteDensity;   // 0.5
    double  roughness;     // 0.0
    double  articulation;  // 0.9 gate ratio: staccato 0.45 .. legato 1.05
    int     velocityCenter; // 80
    int     accentDepth;    // 12
    int     registerCenter; // 72, melody center (C5)
    uint8_t layers[ANO_MUSIC_LAYER_COUNT]; // AnoMusicLayer ids, gate order
    uint32_t layerCount;                   // default 2: pad, bass
    double  harmonicRhythm;   // 1.0 chords per bar
    double  dissonanceBudget; // 0.0
    int8_t  cadencePolicy;    // AnoCadencePolicy, default AUTHENTIC
    AnoTexture texture;       // default NONE
    uint8_t instruments[ANO_MUSIC_LAYER_COUNT]; // AnoPatchName per layer

    /* DSP tier */
    double filterCutoff; // Hz, 2500.0
    double reverbSend;   // 0.20
    double delaySend;    // 0.10
    double drive;        // 0.15
    double stereoWidth;  // 0.70
} AnoGenParams;

AnoGenParams ano_gen_params_default(void);

// Gen params -> public bridge: ordered layers -> bitmask, doubles -> float.
AnoMusicalParams ano_gen_params_bridge(const AnoGenParams *p);

static inline AnoMusicAffect ano_affect_bridge(const double a[3])
{
    return (AnoMusicAffect){ (float)a[0], (float)a[1], (float)a[2] };
}

#endif // ANO_MUSIC_IR_H
