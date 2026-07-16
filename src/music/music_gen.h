/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Rhythm primitives + phrase structure leaves. rough_cell draw gates are parity-load-bearing.

#ifndef ANO_MUSIC_GEN_H
#define ANO_MUSIC_GEN_H

#include "music_theory.h"


/* Rhythm */

// Euclidean E(k, n), optionally rotated, sorted ascending. Returns hit count.
uint32_t ano_euclid(int k, int n, int rotation, uint8_t out[ANO_RHYTHM_MAX]);

// One bar as (slot, dur) from even base_step: merge ~ roughness (draw gated on successor),
// split at high density (draw gated on dur >= 2), drop at low (no draw for first note).
// Always >= 2 notes, first on original slot. Skipped draws shift later stream decisions. Returns note count.
uint32_t ano_rough_cell(AnoMusicRng *rng, double density, double roughness,
                        int slots, int baseStep, AnoRhythmNote out[ANO_RHYTHM_MAX]);


/* Phrase Structure */

// D2 segment kinds; REGULAR is the empty default.
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

// Cadence / pre-cadence before open (2-bar phrase: pre-cadence then cadence).
AnoCadenceSlot ano_phrase_slot(AnoPhrasePos p);

// Within-phrase tension micro-arc, clamped [0, 1]. Codettas sit low.
double ano_effective_tension(double base, AnoPhrasePos p);

// Hypermetric weight: bars group in fours; phrases of 8+ mid-phrase second-strongest.
double ano_hyper_weight(int pos, int bars);

#endif // ANO_MUSIC_GEN_H
