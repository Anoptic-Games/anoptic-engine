/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_theory.h (private to src/music/)
 * The theory kernel, ported line for line from the prototype's musicgen
 * theory package: scales as
 * (tonic pc, mode), chords as symbolic degrees realized against a scale,
 * the RNG-driven functional harmony walk, the minimum-movement voicing
 * search, guide-tone threading, species counterpoint predicates, and pivot
 * modulation. Bit-parity discipline: every RNG draw order, weight iteration
 * order, and float operation order matches the prototype exactly — a 1-ULP
 * or one-draw drift flips musical decisions (TECH_SPEC §3.3, §8.3).
 */

#ifndef ANO_MUSIC_THEORY_H
#define ANO_MUSIC_THEORY_H

#include <stdint.h>
#include <stdbool.h>

#include "music_det.h"

// ---------------------------------------------------------------------------
// Scales (theory/scales.py)
// ---------------------------------------------------------------------------

typedef enum AnoMode
{
    ANO_MODE_IONIAN = 0,
    ANO_MODE_DORIAN,
    ANO_MODE_PHRYGIAN,
    ANO_MODE_LYDIAN,
    ANO_MODE_MIXOLYDIAN,
    ANO_MODE_AEOLIAN,
    ANO_MODE_LOCRIAN,
    ANO_MODE_COUNT,
    ANO_MODE_NONE = -1, // "no source mode" (diatonic chord)
} AnoMode;

extern const char *const ANO_MODE_NAMES[ANO_MODE_COUNT];

// Usable modes bright to dark; locrian deliberately scores like "darkest"
// via the prototype's BRIGHTNESS.get(mode, -1) default.
int ano_mode_brightness(AnoMode mode); // lydian +3 .. phrygian -2, locrian -1

typedef struct AnoScale
{
    uint8_t tonic; // pitch class 0..11
    uint8_t mode;  // AnoMode
} AnoScale;

// Ascending semitone offsets from the tonic (7 entries) for a mode.
const uint8_t *ano_mode_intervals(AnoMode mode);

void ano_scale_pcs(AnoScale s, uint8_t out[7]);
bool ano_scale_contains(AnoScale s, int midi);
int  ano_scale_degree_of(AnoScale s, int midi); // 1-based, 0 if chromatic
int  ano_scale_pitch_at(AnoScale s, int degree, int octave); // degrees wrap past 7
int  ano_snap_to_scale(AnoScale s, int pitch);  // deltas tried (0,1,-1,2,-2)
int  ano_diatonic_shift(AnoScale s, int pitch, int steps);

// "Eb ionian" style display name into buf; returns buf.
const char *ano_scale_name(AnoScale s, char *buf, uint32_t cap);

// ---------------------------------------------------------------------------
// Pitch helpers (theory/pitch.py)
// ---------------------------------------------------------------------------

static inline int ano_pitch_class(int midi) { return midi % 12; }
static inline int ano_octave_of(int midi) { return midi / 12 - 1; } // 60 == C4

// "C#4" style (sharps unless preferFlats) into buf; returns buf.
const char *ano_pitch_name(int midi, bool preferFlats, char *buf, uint32_t cap);

// ---------------------------------------------------------------------------
// Chords (theory/chords.py)
// ---------------------------------------------------------------------------

typedef enum AnoChordExt
{
    ANO_EXT_7    = 1 << 0,
    ANO_EXT_9    = 1 << 1,
    ANO_EXT_SUS2 = 1 << 2,
    ANO_EXT_SUS4 = 1 << 3,
} AnoChordExt;

typedef enum AnoChordQuality
{
    ANO_QUAL_MAJ = 0,
    ANO_QUAL_MIN,
    ANO_QUAL_DIM,
    ANO_QUAL_AUG,
    ANO_QUAL_SUS,
    ANO_QUAL_UNKNOWN,
} AnoChordQuality;

typedef struct AnoChord
{
    uint8_t degree;     // 1..7 root scale degree
    uint8_t extensions; // AnoChordExt mask
    uint8_t inversion;
    int8_t  sourceMode; // AnoMode, ANO_MODE_NONE when diatonic
    uint8_t applied;    // secondary-dominant target degree, 0 = none
    bool    valid;      // false = "no chord" (the prototype's None)
} AnoChord;

static inline AnoChord ano_chord(int degree, uint8_t extensions)
{
    return (AnoChord){ (uint8_t)degree, extensions, 0, ANO_MODE_NONE, 0, true };
}
static inline AnoChord ano_chord_none(void) { return (AnoChord){0}; }

// V/target: major-minor 7th a fifth above the target (chromatic realization).
AnoChord ano_chord_applied_dominant(int target, bool seventh);

char ano_chord_function(AnoChord c); // 'T', 'P' (pre-dominant), 'D'

// Root-first stacked-third member degrees (3..5 entries); returns the count.
uint32_t ano_chord_member_degrees(AnoChord c, int out[5]);

// Member pitch classes root-first (ignores inversion); returns the count.
uint32_t ano_chord_pitch_classes(AnoChord c, AnoScale context, uint8_t out[5]);

// Members rotated so the inversion's bass pc comes first.
uint32_t ano_chord_voiced_pcs(AnoChord c, AnoScale context, uint8_t out[5]);

int ano_chord_bass_pc(AnoChord c, AnoScale context);
AnoChordQuality ano_chord_quality(AnoChord c, AnoScale context);

// Roman-numeral symbol in context ("V7", "iv", "bVI", "V7/vi", "ii65"...).
const char *ano_chord_symbol(AnoChord c, AnoScale context, char *buf, uint32_t cap);

// ---------------------------------------------------------------------------
// The functional walk (theory/harmony.py)
// ---------------------------------------------------------------------------

// doubles, never floats: these constants multiply into choices() weights, and
// (double)1.6f is not Python's 1.6
typedef struct AnoHarmonyConfig
{
    double dominantTensionBias; // 1.6
    double tonicCalmBias;       // 1.2
    double repeatPenalty;       // 0.25
    double borrowProbMax;       // 0.35
    double phraseOpenTonicBoost;// 1.6
    double tonicSuppress;       // 0.05
} AnoHarmonyConfig;

AnoHarmonyConfig ano_harmony_config_default(void);

typedef enum AnoCadenceSlot
{
    ANO_SLOT_OPEN = 0,
    ANO_SLOT_FREE,
    ANO_SLOT_PRE_CADENCE,
    ANO_SLOT_CADENCE,
} AnoCadenceSlot;

typedef enum AnoCadencePolicy
{
    ANO_CADENCE_AUTHENTIC = 0,
    ANO_CADENCE_HALF,
    ANO_CADENCE_DECEPTIVE,
} AnoCadencePolicy;

// One step of the functional walk (prototype next_chord, trace elided).
// prev may be ano_chord_none(). All draws come from rng, in prototype order.
AnoChord ano_next_chord(AnoChord prev, AnoCadenceSlot slot, AnoCadencePolicy policy,
                        double tension, double valence, AnoMode mode,
                        bool phraseStart, bool pieceStart,
                        const AnoHarmonyConfig *cfg, AnoMusicRng *rng,
                        bool suppressTonic, int tonicize, bool forceDominant);

// ---------------------------------------------------------------------------
// Voicing search (theory/voicing.py)
// ---------------------------------------------------------------------------

typedef struct AnoVoicingConfig
{
    uint8_t voices;         // 4
    uint8_t lo, hi;         // 52 (E3), 79 (G5)
    uint8_t maxAdjacentGap; // 12
    double  center;         // 64.0 (double: feeds the float cost)
    uint8_t maxVoiceMove;   // 7
} AnoVoicingConfig;

AnoVoicingConfig ano_voicing_config_default(void);

// Best strictly-ascending voicing of root-first chord pcs given the previous
// voicing (prevLen 0 = none). First minimum wins in candidate order (the
// prototype's min()). Returns the voice count, cost in *outCost.
uint32_t ano_voice_chord(const uint8_t *chordPcs, uint32_t pcCount,
                         const int *prev, uint32_t prevLen,
                         const AnoVoicingConfig *cfg, int out[6], double *outCost);

// ---------------------------------------------------------------------------
// Guide tones (theory/guides.py)
// ---------------------------------------------------------------------------

// The chord's (3rd, 7th-or-5th) candidates.
void ano_guide_pcs(AnoChord c, AnoScale s, int out[2]);

// Continue the thread: nearest candidate by folded pc distance, ties low.
// prevPc -1 = first chord (takes the 3rd).
int ano_next_guide(int prevPc, AnoChord c, AnoScale s);

// ---------------------------------------------------------------------------
// Counterpoint predicates (theory/counterpoint.py)
// ---------------------------------------------------------------------------

typedef enum AnoMotion
{
    ANO_MOTION_OBLIQUE = 0,
    ANO_MOTION_CONTRARY,
    ANO_MOTION_PARALLEL,
    ANO_MOTION_SIMILAR,
} AnoMotion;

static inline int ano_interval_class(int lower, int upper)
{
    return ((upper - lower) % 12 + 12) % 12;
}
bool ano_is_perfect(int lower, int upper);
bool ano_is_consonant(int lower, int upper);
AnoMotion ano_motion(int prevLower, int prevUpper, int lower, int upper);
bool ano_forbidden_parallel(int prevLower, int prevUpper, int lower, int upper);
bool ano_forbidden_direct(int prevLower, int prevUpper, int lower, int upper, int maxStep);

// ---------------------------------------------------------------------------
// Pivot modulation (theory/modulation.py)
// ---------------------------------------------------------------------------

typedef struct AnoPivot
{
    uint8_t oldDegree;
    uint8_t newDegree;
    uint8_t pcs[3]; // root-first in the old key
} AnoPivot;

// Triads diatonic in both scales, best first. Returns the count (<= 7).
uint32_t ano_find_pivots(AnoScale oldKey, AnoScale newKey, AnoPivot out[7]);

// Signed circle-of-fifths steps pc a -> pc b, sharpwards positive, -5..+6.
int ano_fifths_between(int a, int b);

#endif // ANO_MUSIC_THEORY_H
