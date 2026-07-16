/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Imitative entries: restate motif head one bar later. No rng.
// Retry: on-bar, +half bar, ±3rd (and ±3rd on half); first non-clash, else least-clash.

#ifndef ANO_MUSIC_IMITATION_H
#define ANO_MUSIC_IMITATION_H

#include "music_ir.h"
#include "music_motif.h"

AnoMotif ano_imitation_cell(const AnoMotif *motif); // first ceil(n/2) notes

typedef struct AnoImitationResult
{
    AnoMusicEvent events[ANO_MOTIF_MAX];
    uint32_t      eventCount;
    AnoMotif      emitted;
    bool          hasEmitted;
    int           candidateIdx; // 0 = on the bar; -1 none
    int           clashes;
} AnoImitationResult;

void ano_generate_imitation(const AnoHarmonicContext *ctx, AnoMeter meter,
                            const AnoMotif *motif,
                            const AnoMusicEvent *melodyEvents, uint32_t melodyCount,
                            uint8_t hostLayer, int lo, int hi, int velocity,
                            AnoImitationResult *out);

#endif // ANO_MUSIC_IMITATION_H
