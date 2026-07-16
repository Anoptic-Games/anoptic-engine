/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Authored signature motifs. MotifDirector: overdue x importance vs best-fit transform.
// Draw-free. Transforms measured at 16 slots always.

#ifndef ANO_MUSIC_SIGNATURES_H
#define ANO_MUSIC_SIGNATURES_H

#include "music_motif.h"

typedef struct AnoMotifDirector
{
    AnoSignatureMotif library[ANO_SIG_MAX];
    uint32_t          libraryCount;
    int barsSince[ANO_SIG_MAX]; // -1 = never stated
} AnoMotifDirector;

void ano_director_init(AnoMotifDirector *d, const AnoSignatureMotif *library,
                       uint32_t count);

void ano_director_age(AnoMotifDirector *d, int bars);

void ano_director_observe(AnoMotifDirector *d, const char *tag, int bars);

// requested (NULL/"" = none) wins once it fits. false = none appropriate.
bool ano_director_select(AnoMotifDirector *d, AnoScale scale,
                         const uint8_t *chordPcs, uint32_t pcCount,
                         int lo, int hi, uint32_t strongMask,
                         double leniency, int near, const char *requested,
                         uint32_t *outSig, AnoTransform *outXform, AnoMotif *outMotif);

#endif // ANO_MUSIC_SIGNATURES_H
