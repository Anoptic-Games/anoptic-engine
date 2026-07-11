/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_signatures.h (private to src/music/)
 * Authored signature motifs (musicgen/gen/signatures.py): the MotifDirector
 * weighs overdue x importance pressure against the best-fitting admissible
 * transform, with leniency trading recurrence against fit. Draw-free and
 * deterministic. Transforms are always measured at 16 slots (the prototype's
 * admissible_transforms default, even in other meters).
 */

#ifndef ANO_MUSIC_SIGNATURES_H
#define ANO_MUSIC_SIGNATURES_H

#include "music_motif.h"

#define ANO_SIG_MAX 16

typedef struct AnoSignatureMotif
{
    const char *tag;        // "hero", "threat", ... — the game's handle
    AnoMotif    motif;
    double      importance; // 0..1: landmark (high) vs secondary colour
} AnoSignatureMotif;

typedef struct AnoMotifDirector
{
    AnoSignatureMotif library[ANO_SIG_MAX];
    uint32_t          libraryCount;
    int barsSince[ANO_SIG_MAX]; // -1 = never stated (maximally overdue)
} AnoMotifDirector;

void ano_director_init(AnoMotifDirector *d, const AnoSignatureMotif *library,
                       uint32_t count);

// A phrase passed; every stated signature grows more overdue.
void ano_director_age(AnoMotifDirector *d, int bars);

// Record that `tag` was just stated: age the rest, reset its clock.
void ano_director_observe(AnoMotifDirector *d, const char *tag, int bars);

// Pick a signature to state this phrase. requested (NULL/"" = none) wins
// outright once it fits at all. Returns false when none is appropriate;
// otherwise fills the library index, transform, and transformed motif.
bool ano_director_select(AnoMotifDirector *d, AnoScale scale,
                         const uint8_t *chordPcs, uint32_t pcCount,
                         int lo, int hi, uint32_t strongMask,
                         double leniency, int near, const char *requested,
                         uint32_t *outSig, AnoTransform *outXform, AnoMotif *outMotif);

#endif // ANO_MUSIC_SIGNATURES_H
