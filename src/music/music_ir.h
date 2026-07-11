/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_ir.h (private to src/music/)
 * The generation-side IR (musicgen/ir.py): Meter derivations, the annotated
 * note event, the per-bar HarmonicContext handed to generators, and the
 * Tier-2 parameter block in its prototype-faithful shape. The playable core
 * (AnoNoteEvent) is the public bridge type from anoptic_music.h; everything
 * here wraps or feeds it. All times are quarter-note beats from piece start;
 * pre-modifier events align to the 16th grid.
 */

#ifndef ANO_MUSIC_IR_H
#define ANO_MUSIC_IR_H

#include <anoptic_music.h>

#include "music_theory.h"

#define ANO_MUSIC_GRID 0.25 // 16th-note grid, in quarter-note beats

// ---------------------------------------------------------------------------
// Meter
// ---------------------------------------------------------------------------

#define ANO_METER_MAX_SLOTS 32 // 12/8 needs 24; headroom beyond any real meter

typedef struct AnoMeter
{
    int numerator;
    int denominator;
} AnoMeter;

static inline AnoMeter ano_meter_default(void) { return (AnoMeter){ 4, 4 }; }

// Bar length in quarter-note beats (4/4 -> 4.0, 6/8 -> 3.0).
double ano_meter_bar_quarters(AnoMeter m);

// 0-based bar index containing a beat position (Python float floordiv).
int ano_meter_bar_of(AnoMeter m, double start);

// 1-based musician-style beat position within the bar.
double ano_meter_beat_in_bar(AnoMeter m, double start);

// Grid slots per bar (16 in 4/4, 12 in 3/4 and 6/8).
int ano_meter_slots(AnoMeter m);

// Grid slot within the bar for a beat position.
int ano_meter_slot_of(AnoMeter m, double start);

// Compound meters (6/8, 9/8, 12/8) group in threes: the felt pulse is the
// dotted unit, not the notated denominator beat.
bool ano_meter_is_compound(AnoMeter m);

// Felt beats per bar (4/4 -> 4, 3/4 -> 3, 6/8 -> 2, 12/8 -> 4).
int ano_meter_pulses(AnoMeter m);

// Quarter-note length of one felt beat (1.0 in x/4, 1.5 in 6/8).
double ano_meter_pulse_quarters(AnoMeter m);

// Grid slots per felt beat (4 in x/4, 6 in 6/8).
int ano_meter_pulse_slots(AnoMeter m);

// Accent hierarchy per grid slot: downbeat 4.0, mid-bar pulse 3.5, other
// pulses 3.0, 8ths 2.0, 16ths 1.0. Returns the slot count.
uint32_t ano_meter_metric_weights(AnoMeter m, double out[ANO_METER_MAX_SLOTS]);

// Slots carrying beat-level weight (metric weight >= 3.0); returns the count.
uint32_t ano_meter_strong_slots(AnoMeter m, int out[ANO_METER_MAX_SLOTS]);

// ---------------------------------------------------------------------------
// Annotated events
// ---------------------------------------------------------------------------

// NoteEvent.__post_init__ as a predicate (the prototype raises; callers gate).
bool ano_note_event_valid(const AnoNoteEvent *ev);

// The generation-side event: the playable core plus the inspection
// annotations (no acoustic effect; textdump and the lint oracle read them).
// role is a free string in the prototype ("chord-tone", "drum:kick", ...).
typedef struct AnoMusicEvent
{
    AnoNoteEvent core;
    uint8_t      degree;       // 1..7 within the bar's scale, 0 = None
    char         chordSym[16]; // roman-numeral symbol in context
    char         role[20];
} AnoMusicEvent;

// ---------------------------------------------------------------------------
// HarmonicContext
// ---------------------------------------------------------------------------

// ctx.cadence_slot: "" | "pre-cadence" | "cadence" (open/free bars carry "").
typedef enum AnoCtxSlot
{
    ANO_CTX_SLOT_NONE = 0,
    ANO_CTX_SLOT_PRE_CADENCE,
    ANO_CTX_SLOT_CADENCE,
} AnoCtxSlot;

// ctx.obligation: "" | "cadential64" | "lament" | "tonicize:N".
typedef enum AnoObligation
{
    ANO_OBL_NONE = 0,
    ANO_OBL_CADENTIAL64,
    ANO_OBL_LAMENT,
    ANO_OBL_TONICIZE, // target degree in obligationTarget
} AnoObligation;

// ctx.form: "" | "antecedent" | "consequent" (period contract annotation).
typedef enum AnoPhraseForm
{
    ANO_FORM_NONE = 0,
    ANO_FORM_ANTECEDENT,
    ANO_FORM_CONSEQUENT,
} AnoPhraseForm;

// One intra-bar timeline entry: (beat offset within the bar, chord).
typedef struct AnoChordSpan
{
    double   off;
    AnoChord chord;
} AnoChordSpan;

// Per-bar harmonic state handed from the conductor to generators. chordPcs is
// bass-first: chordPcs[0] is the sounding bass pitch class (respecting
// inversion); the linter's bass-root rule relies on this. The chords timeline
// is populated only for the compressed cadential 6/4 (2 spans max).
typedef struct AnoHarmonicContext
{
    int      bar; // 0-based
    AnoScale scale;
    AnoChord chord; // .valid == false is the prototype's None
    char     chordSym[16];
    uint8_t  chordPcs[5];
    uint32_t chordPcCount;
    AnoChord nextChord;
    char     nextChordSym[16];
    double   tension;
    AnoCtxSlot    cadenceSlot;
    int8_t        cadencePolicy; // AnoCadencePolicy; NONE when slot is empty
    char          modulation[96]; // key-change annotation, inspection only
    AnoObligation obligation;
    uint8_t       obligationTarget; // tonicize resolution degree
    int           phrasePos;  // 0-based bar position within the phrase
    int           phraseBars; // phrase length in bars
    AnoChordSpan  chords[2];
    uint32_t      chordSpanCount;
    int           phraseApex; // bar-in-phrase of the planned apex, -1 = none
    AnoPhraseForm form;
} AnoHarmonicContext;

// The chord in force at a beat offset within the bar: the last timeline entry
// at or before the offset (1e-9 slack), else the downbeat chord.
AnoChord ano_ctx_chord_at(const AnoHarmonicContext *ctx, double beatOffset);

// ---------------------------------------------------------------------------
// Tier-2 parameters (prototype MusicalParams; the public AnoMusicalParams in
// anoptic_music.h is the bridge shape this reduces to at the synth boundary)
// ---------------------------------------------------------------------------

// Tier-2 texture state, phrase-quantized by the conductor's rotation.
typedef enum AnoTexture
{
    ANO_TEX_NONE = 0, // "" — pre-texture-system behavior
    ANO_TEX_MONOPHONIC,
    ANO_TEX_HOMOPHONIC,
    ANO_TEX_DOUBLED,
    ANO_TEX_IMITATIVE,
    ANO_TEX_COUNTER,
} AnoTexture;

// Semantic patch names (the mapper's energy tiers pick among these; the synth
// registry maps them to voice variants at the bridge).
typedef enum AnoPatchName
{
    ANO_PATCH_NONE = 0,
    ANO_PATCH_WARM,
    ANO_PATCH_BRIGHT,
    ANO_PATCH_ROUND,
    ANO_PATCH_DRIVEN,
    ANO_PATCH_SOFT,
    ANO_PATCH_HARD,
    ANO_PATCH_PLUCK,
    ANO_PATCH_GLASS,
    ANO_PATCH_COUNT,
} AnoPatchName;

extern const char *const ANO_PATCH_NAMES[ANO_PATCH_COUNT]; // "" first

// All knobs that multiply into draws or costs are double (never float: the
// parity hazard from music_theory.h applies). layers is ORDERED — the gate
// emits pad, bass, melody, perc, arp and the conductor iterates in that
// order, so a bitmask would lose draw order.
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

    // DSP tier (consumed by the synth backend; inert on the raw-events path)
    double filterCutoff; // Hz, 2500.0
    double reverbSend;   // 0.20
    double delaySend;    // 0.10
    double drive;        // 0.15
    double stereoWidth;  // 0.70
} AnoGenParams;

AnoGenParams ano_gen_params_default(void);

#endif // ANO_MUSIC_IR_H
