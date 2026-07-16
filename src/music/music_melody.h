/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Motif-based melody. Strong beats -> chord tones, weak -> step, leaps recover, register folds.
// Draw order per bar: rest (free slots; never completed/signature/apex) ->
// phrase_variant (pos 1,4 step; 6 ornament; 2,5,7 op) -> anacrusis stream -> syncopate stream.
// Cadence formula and pitch machinery are draw-free.

#ifndef ANO_MUSIC_MELODY_H
#define ANO_MUSIC_MELODY_H

#include "music_ir.h"
#include "music_motif.h"

typedef struct AnoMelodyState
{
    int prevPitch;  // ANO_NEAR_NONE = None
    int prevAnchor; // ANO_NEAR_NONE
    // A3 outer-voice frame at last strong-slot melody onset
    bool   hasPrevOuter;
    double outerT;
    int    outerMelody;
    int    outerBass;
    int pendingTie; // ANO_NEAR_NONE; pitch tied out of last bar
} AnoMelodyState;

static inline AnoMelodyState ano_melody_state_init(void)
{
    return (AnoMelodyState){ .prevPitch = ANO_NEAR_NONE, .prevAnchor = ANO_NEAR_NONE,
                             .pendingTie = ANO_NEAR_NONE };
}

typedef enum AnoMelodyStage
{
    ANO_MEL_PLAIN = 0,
    ANO_MEL_INTRODUCED,
    ANO_MEL_DEVELOPED,
    ANO_MEL_STATED,    // authored signature (M17)
    ANO_MEL_COMPLETED, // cadence-fused payoff
} AnoMelodyStage;

#define ANO_MELODY_MAX_EVENTS 64

typedef struct AnoMelodyResult
{
    AnoMusicEvent  events[ANO_MELODY_MAX_EVENTS];
    uint32_t       eventCount;
    AnoMelodyState state;
} AnoMelodyResult;

// Optional inputs: NULL / ANO_NEAR_NONE where prototype uses None.
// A3 guard active when cfg->counterpoint and bassCount > 0.
void ano_generate_melody(const AnoHarmonicContext *ctx, AnoMeter meter,
                         const AnoGenParams *params, AnoPhrasePos pos,
                         const AnoMotif *motif, const AnoMelodyState *state,
                         const AnoMelodyConfig *cfg, AnoMusicRng *rng,
                         AnoMelodyStage lifecycle, const AnoMotif *signature,
                         const AnoApexPlan *apex,
                         const AnoMusicEvent *bass, uint32_t bassCount,
                         const AnoPlacedNote *replay, uint32_t replayCount,
                         bool doubleLine, int prevBass,
                         AnoMusicRng *anacrusisRng, AnoMusicRng *syncopateRng,
                         AnoMelodyResult *out);

#endif // ANO_MUSIC_MELODY_H
