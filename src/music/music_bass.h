/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Bass lines: root anchoring, density-tiered patterns, approach into next chord.
// At most one draw per bar (weighted approach-kind). pedalDegree holds that scale degree.

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
    int           root; // this bar's root pitch
} AnoBassResult;

// prevRoot / nextBassPc: ANO_NEAR_NONE / -1 for None. pedalDegree != 0 holds.
void ano_generate_bass(const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, int prevRoot, int nextBassPc,
                       const AnoBassConfig *cfg, AnoMusicRng *rng, int pedalDegree,
                       AnoBassResult *out);

#endif // ANO_MUSIC_BASS_H
