/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_imitation.h (private to src/music/)
 * Imitative entries (musicgen/gen/imitation.py): a second voice restates the
 * motif's head one bar later, via a deterministic retry list — on the bar,
 * half a bar later, transposed a 3rd up/down — taking the first candidate
 * that never clashes (2nds/7ths/tritones at overlapping onsets) with the
 * sounding melody, falling back to the least-clashing. No rng.
 */

#ifndef ANO_MUSIC_IMITATION_H
#define ANO_MUSIC_IMITATION_H

#include "music_ir.h"
#include "music_motif.h"

// The opening cell: the first ceil(n/2) notes.
AnoMotif ano_imitation_cell(const AnoMotif *motif);

typedef struct AnoImitationResult
{
    AnoMusicEvent events[ANO_MOTIF_MAX];
    uint32_t      eventCount;
    AnoMotif      emitted; // cached per phrase; the lint recomputes
    bool          hasEmitted;
    int           candidateIdx; // which retry landed (0 = on the bar); -1 none
    int           clashes;      // tolerated clash count of the landed entry
} AnoImitationResult;

// One imitative entry in hostLayer's register, role "imitation".
void ano_generate_imitation(const AnoHarmonicContext *ctx, AnoMeter meter,
                            const AnoMotif *motif,
                            const AnoMusicEvent *melodyEvents, uint32_t melodyCount,
                            uint8_t hostLayer, int lo, int hi, int velocity,
                            AnoImitationResult *out);

#endif // ANO_MUSIC_IMITATION_H
