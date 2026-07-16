/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Countermelody under the melody. One rough_cell draw per bar.
// Strong beats: chord members consonant with melody (3rds/6ths preferred). Never above melody.

#ifndef ANO_MUSIC_COUNTER_H
#define ANO_MUSIC_COUNTER_H

#include "music_ir.h"
#include "music_motif.h" // ANO_NEAR_NONE

typedef struct AnoCounterConfig
{
    int    lo;             // 55 (G3)
    int    hi;             // 79 (G5)
    int    velocityOffset; // -10
    double densityScale;   // 0.6
} AnoCounterConfig;

AnoCounterConfig ano_counter_config_default(void);

typedef struct AnoCounterState
{
    int prevPitch; // ANO_NEAR_NONE = None
    int guidePc;   // -1 = None
    // Motion chains the species rules judge; one-bar expiry each.
    bool   hasVsMelody;
    double vsMelodyT;
    int    vsMelodyC, vsMelodyM;
    bool   hasVsBass;
    double vsBassT;
    int    vsBassC, vsBassB;
} AnoCounterState;

static inline AnoCounterState ano_counter_state_init(void)
{
    return (AnoCounterState){ .prevPitch = ANO_NEAR_NONE, .guidePc = -1 };
}

typedef struct AnoCounterResult
{
    AnoMusicEvent   events[ANO_METER_MAX_SLOTS];
    uint32_t        eventCount;
    AnoCounterState state;
} AnoCounterResult;

// melodyEvents: this bar's realized melody (doubling excluded internally).
void ano_generate_counter(const AnoHarmonicContext *ctx, AnoMeter meter,
                          const AnoGenParams *params,
                          const AnoMusicEvent *melodyEvents, uint32_t melodyCount,
                          const AnoMusicEvent *bassEvents, uint32_t bassCount,
                          const AnoCounterState *state, const AnoCounterConfig *cfg,
                          AnoMusicRng *rng, AnoCounterResult *out);

#endif // ANO_MUSIC_COUNTER_H
