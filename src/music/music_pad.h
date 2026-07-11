/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_pad.h (private to src/music/)
 * Sustained voice-led pad chords (musicgen/gen/pad.py) with the M14 cadence
 * ornaments (prepared suspension, else the payoff appoggiatura), C2 inner-
 * voice animation (connective walk / comping figure), C4 thinning, and D1
 * suspension preparation held across the barline. The returned voicing is
 * always the block target, so voice-leading memory is untouched.
 */

#ifndef ANO_MUSIC_PAD_H
#define ANO_MUSIC_PAD_H

#include "music_ir.h"

typedef enum AnoPadAnimate
{
    ANO_PAD_BLOCK = 0, // ""
    ANO_PAD_CONNECTIVE,
    ANO_PAD_COMPING,
} AnoPadAnimate;

// D1 tie preparation: the NEXT bar's root-first pcs, its voiced-pc set, and
// its scale — the same preview the suspension realizer will run.
typedef struct AnoPadTiePrep
{
    uint8_t  pcs[5];
    uint32_t pcCount;
    uint8_t  chordPcs[5];
    uint32_t chordPcCount;
    AnoScale scale;
} AnoPadTiePrep;

typedef struct AnoPadResult
{
    AnoMusicEvent events[16];
    uint32_t      eventCount;
    int           voicing[6]; // the block target (threads voice leading)
    uint32_t      voiceCount;
} AnoPadResult;

// One bar of sustained chord. prevVoicing NULL/0 = None; nextPcs feeds the
// connective preview; rng only draws for comping; prevTie = ANO_NEAR_NONE
// or the pitch the previous bar tied out (closes the D1 loop).
void ano_generate_pad(const AnoHarmonicContext *ctx, AnoMeter meter,
                      const AnoGenParams *params,
                      const int *prevVoicing, uint32_t prevCount,
                      const AnoVoicingConfig *cfg, bool suspend, bool appoggiatura,
                      const uint8_t *nextPcs, uint32_t nextPcCount,
                      AnoPadAnimate animate, AnoMusicRng *rng, bool thin,
                      const AnoPadTiePrep *tiePrep, int prevTie,
                      AnoPadResult *out);

#endif // ANO_MUSIC_PAD_H
