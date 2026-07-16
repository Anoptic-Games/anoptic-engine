/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Voice-led pad chords: M14 ornaments, C2 animation, C4 thinning, D1 tie prep.
// Returned voicing is always the block target (voice-leading memory untouched).

#ifndef ANO_MUSIC_PAD_H
#define ANO_MUSIC_PAD_H

#include "music_ir.h"

typedef enum AnoPadAnimate
{
    ANO_PAD_BLOCK = 0,
    ANO_PAD_CONNECTIVE,
    ANO_PAD_COMPING,
} AnoPadAnimate;

// D1 tie prep: next bar's root-first pcs, voiced-pc set, and scale.
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
    int           voicing[6]; // block target
    uint32_t      voiceCount;
} AnoPadResult;

// C4 monophonic: strip to root+fifth dyad, hold voicer to 2 voices.
// Shared with D3 split path (must thin identically or a monophonic phrase grows a 4-note pad).
uint32_t ano_thin_voicing(uint8_t *pcs, uint32_t pcCount, AnoVoicingConfig *cfg);

// prevVoicing NULL/0 = None. rng draws only for comping. prevTie = ANO_NEAR_NONE or tied pitch.
void ano_generate_pad(const AnoHarmonicContext *ctx, AnoMeter meter,
                      const AnoGenParams *params,
                      const int *prevVoicing, uint32_t prevCount,
                      const AnoVoicingConfig *cfg, bool suspend, bool appoggiatura,
                      const uint8_t *nextPcs, uint32_t nextPcCount,
                      AnoPadAnimate animate, AnoMusicRng *rng, bool thin,
                      const AnoPadTiePrep *tiePrep, int prevTie,
                      AnoPadResult *out);

#endif // ANO_MUSIC_PAD_H
