/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_gen.h (private to src/music/)
 * Generation leaves shared by all layer generators: rhythm primitives
 * (musicgen/gen/rhythm.py — Euclidean patterns, the stochastic rough_cell)
 * and phrase structure (musicgen/gen/structure.py — PhrasePos, the
 * within-phrase tension micro-arc, hypermetric weights). rough_cell draws;
 * its short-circuit draw gates are part of the parity contract.
 */

#ifndef ANO_MUSIC_GEN_H
#define ANO_MUSIC_GEN_H

#include "music_theory.h"

// ---------------------------------------------------------------------------
// Rhythm (gen/rhythm.py)
// ---------------------------------------------------------------------------

#define ANO_RHYTHM_MAX 32 // >= any bar's slot count

// Slot indices of the Euclidean rhythm E(k, n), optionally rotated, sorted
// ascending. E(3, 8) -> (0, 3, 6). Returns the hit count (min(max(k,0),n)).
uint32_t ano_euclid(int k, int n, int rotation, uint8_t out[ANO_RHYTHM_MAX]);

typedef struct AnoRhythmNote
{
    int slot;
    int durSlots;
} AnoRhythmNote;

// One bar's rhythm cell as (slot, dur) pairs from an even base_step pulse:
// merges neighbors ~ roughness (draw gated on a successor existing), splits
// long notes at high density (draw gated on dur >= 2), drops notes at low
// density (no draw for the first note). Always >= 2 notes, first on its
// original slot. Returns the note count.
uint32_t ano_rough_cell(AnoMusicRng *rng, double density, double roughness,
                        int slots, int baseStep, AnoRhythmNote out[ANO_RHYTHM_MAX]);

// ---------------------------------------------------------------------------
// Phrase structure (gen/structure.py)
// ---------------------------------------------------------------------------

// D2 segment kinds; regular is the prototype's "".
typedef enum AnoSegKind
{
    ANO_SEG_REGULAR = 0,
    ANO_SEG_CODETTA,
    ANO_SEG_EXTENSION,
    ANO_SEG_ELISION,
} AnoSegKind;

typedef struct AnoPhrasePos
{
    int phrase; // 0-based phrase index
    int pos;    // 0-based bar within the phrase
    int bars;   // phrase length in bars
    AnoSegKind kind;
} AnoPhrasePos;

AnoPhrasePos ano_phrase_position(int bar, int phraseBars);

// PhrasePos.slot: cadence / pre-cadence checked before open (a 2-bar phrase
// is pre-cadence then cadence, never open).
AnoCadenceSlot ano_phrase_slot(AnoPhrasePos p);

// The within-phrase tension micro-arc: rises toward the pre-cadence bar,
// settles at the cadence; codettas sit low throughout. Clamped to [0, 1].
double ano_effective_tension(double base, AnoPhrasePos p);

// Hypermetric weight of a bar within its phrase: bars group in fours, and in
// phrases of 8+ the mid-phrase downbeat is second-strongest.
double ano_hyper_weight(int pos, int bars);

#endif // ANO_MUSIC_GEN_H
