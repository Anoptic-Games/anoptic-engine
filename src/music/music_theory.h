/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Theory kernel: scales, chords, functional walk, voicing, guides, counterpoint, pivots.
// RNG draw order, weight iteration, and float op order are parity-load-bearing.

#ifndef ANO_MUSIC_THEORY_H
#define ANO_MUSIC_THEORY_H

#include <stdint.h>
#include <stdbool.h>

#include "music_det.h"
#include <anoptic_music.h>


/* Scales */

extern const char *const ANO_MODE_NAMES[ANO_MODE_COUNT];

// Usable modes bright to dark; locrian scores -1 via BRIGHTNESS.get(mode, -1).
int ano_mode_brightness(AnoMode mode); // lydian +3 .. phrygian -2, locrian -1

typedef struct AnoScale
{
    uint8_t tonic; // pitch class 0..11
    uint8_t mode;  // AnoMode
} AnoScale;

const uint8_t *ano_mode_intervals(AnoMode mode); // 7 ascending semitone offsets

void ano_scale_pcs(AnoScale s, uint8_t out[7]);
bool ano_scale_contains(AnoScale s, int midi);
int  ano_scale_degree_of(AnoScale s, int midi); // 1-based, 0 if chromatic
int  ano_scale_pitch_at(AnoScale s, int degree, int octave); // degrees wrap past 7
int  ano_snap_to_scale(AnoScale s, int pitch);  // deltas tried (0,1,-1,2,-2)
int  ano_diatonic_shift(AnoScale s, int pitch, int steps);

// "Eb ionian" into buf; returns buf.
const char *ano_scale_name(AnoScale s, char *buf, uint32_t cap);


/* Pitch */

static inline int ano_pitch_class(int midi) { return midi % 12; }
static inline int ano_octave_of(int midi) { return midi / 12 - 1; } // 60 == C4

// "C#4" into buf; returns buf.
const char *ano_pitch_name(int midi, bool preferFlats, char *buf, uint32_t cap);


/* Chords */

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
    bool    valid;      // false = no chord
} AnoChord;

static inline AnoChord ano_chord(int degree, uint8_t extensions)
{
    return (AnoChord){ (uint8_t)degree, extensions, 0, ANO_MODE_NONE, 0, true };
}
static inline AnoChord ano_chord_none(void) { return (AnoChord){0}; }

// V/target: major-minor 7th a fifth above the target.
AnoChord ano_chord_applied_dominant(int target, bool seventh);

// Spelling scale: context unless sourceMode re-modes on the same tonic.
static inline AnoScale ano_chord_scale_for(AnoChord c, AnoScale context)
{
    if (c.sourceMode == ANO_MODE_NONE || c.sourceMode == (int8_t)context.mode)
        return context;
    return (AnoScale){ context.tonic, (uint8_t)c.sourceMode };
}

char ano_chord_function(AnoChord c); // 'T', 'P', 'D'

uint32_t ano_chord_member_degrees(AnoChord c, int out[5]); // root-first stacked thirds
uint32_t ano_chord_pitch_classes(AnoChord c, AnoScale context, uint8_t out[5]);
uint32_t ano_chord_voiced_pcs(AnoChord c, AnoScale context, uint8_t out[5]); // bass pc first

int ano_chord_bass_pc(AnoChord c, AnoScale context);
AnoChordQuality ano_chord_quality(AnoChord c, AnoScale context);

// Roman-numeral symbol ("V7", "iv", "bVI", "V7/vi", "ii65"...).
const char *ano_chord_symbol(AnoChord c, AnoScale context, char *buf, uint32_t cap);


/* Functional Walk */

// doubles, never floats: these multiply into choices() weights ((double)1.6f != Python 1.6).
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

// One step of the functional walk. prev may be ano_chord_none(). Draws in prototype order.
AnoChord ano_next_chord(AnoChord prev, AnoCadenceSlot slot, AnoCadencePolicy policy,
                        double tension, double valence, AnoMode mode,
                        bool phraseStart, bool pieceStart,
                        const AnoHarmonyConfig *cfg, AnoMusicRng *rng,
                        bool suppressTonic, int tonicize, bool forceDominant);


/* Voicing */

typedef struct AnoVoicingConfig
{
    uint8_t voices;         // 4
    uint8_t lo, hi;         // 52 (E3), 79 (G5)
    uint8_t maxAdjacentGap; // 12
    double  center;         // 64.0
    uint8_t maxVoiceMove;   // 7
} AnoVoicingConfig;

AnoVoicingConfig ano_voicing_config_default(void);

// Best strictly-ascending voicing. First minimum wins. Returns voice count, cost in *outCost.
uint32_t ano_voice_chord(const uint8_t *chordPcs, uint32_t pcCount,
                         const int *prev, uint32_t prevLen,
                         const AnoVoicingConfig *cfg, int out[6], double *outCost);


/* Guide Tones */

void ano_guide_pcs(AnoChord c, AnoScale s, int out[2]); // (3rd, 7th-or-5th)

// Nearest candidate by folded pc distance, ties low. prevPc -1 = take the 3rd.
int ano_next_guide(int prevPc, AnoChord c, AnoScale s);


/* Counterpoint */

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


/* Pivot Modulation */

typedef struct AnoPivot
{
    uint8_t oldDegree;
    uint8_t newDegree;
    uint8_t pcs[3]; // root-first in the old key
} AnoPivot;

// Triads diatonic in both scales, best first. Returns count (<= 7).
uint32_t ano_find_pivots(AnoScale oldKey, AnoScale newKey, AnoPivot out[7]);

// Signed circle-of-fifths steps a -> b, sharpwards positive, -5..+6.
int ano_fifths_between(int a, int b);

#endif // ANO_MUSIC_THEORY_H
