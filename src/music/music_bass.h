/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_bass.h (private to src/music/)
 * Bass lines (musicgen/gen/bass.py): root anchoring with density-tiered
 * patterns (sustained root -> root + approach -> root/fifth/approach) and
 * approach tones into the next chord. One draw at most per bar (the weighted
 * approach-kind choice), gated exactly as the prototype gates it. A pedal
 * degree abandons the walk and holds (dramaturg withholding).
 */

#ifndef ANO_MUSIC_BASS_H
#define ANO_MUSIC_BASS_H

#include "music_ir.h"

typedef struct AnoBassConfig
{
    int    lo;             // 28 (E1)
    int    hi;             // 50 (D3)
    int    velocityOffset; // 8
    double approachBeats;  // 1.0
} AnoBassConfig;

AnoBassConfig ano_bass_config_default(void);

typedef struct AnoBassResult
{
    AnoMusicEvent events[4];
    uint32_t      eventCount;
    int           root; // this bar's root pitch (feeds the next bar's near)
} AnoBassResult;

// One bar of bass. prevRoot = ANO_NEAR_NONE for None; nextBassPc = -1 for
// None. pedalDegree != 0 holds that scale degree instead of walking.
void ano_generate_bass(const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, int prevRoot, int nextBassPc,
                       const AnoBassConfig *cfg, AnoMusicRng *rng, int pedalDegree,
                       AnoBassResult *out);

#endif // ANO_MUSIC_BASS_H
