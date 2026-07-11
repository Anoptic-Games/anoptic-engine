/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_melody.h (private to src/music/)
 * The motif-based melody engine (musicgen/gen/melody.py, generation half —
 * the Motif type and factories live in music_motif.h). Constraint-first
 * placement: strong beats snap to chord tones, weak beats step, leaps beyond
 * a P4 recover by an opposite step, the register window folds toward center.
 * Signature lifecycle staging, the A3 outer-voice guard, A4 apex, B2 replay,
 * C1 doubling, D1 pickups/ties all ride the same entry point, exactly as the
 * prototype threads them.
 *
 * Draw parity map (per bar, in order): the rest draw (free slots only, never
 * completed / signature-event / apex bars) -> phrase_variant's draws (pos
 * 1,4 = step choice; 6 = ornament's gated choice; 2,5,7 = op choice) -> the
 * anacrusis stream (cadence bars: gate draw, then count choice) -> the
 * syncopate stream (gate chain ends in one draw). Cadence formula and all
 * pitch machinery are draw-free.
 */

#ifndef ANO_MUSIC_MELODY_H
#define ANO_MUSIC_MELODY_H

#include "music_ir.h"
#include "music_motif.h"

typedef struct AnoMelodyState
{
    int prevPitch;  // ANO_NEAR_NONE = None
    int prevAnchor; // ANO_NEAR_NONE
    // the (beat, melody pitch, bass pitch) pair at the last strong-slot
    // melody onset — the A3 frame carried across bars
    bool   hasPrevOuter;
    double outerT;
    int    outerMelody;
    int    outerBass;
    int pendingTie; // ANO_NEAR_NONE = None; a pitch tied out of the last bar
} AnoMelodyState;

static inline AnoMelodyState ano_melody_state_init(void)
{
    return (AnoMelodyState){ .prevPitch = ANO_NEAR_NONE, .prevAnchor = ANO_NEAR_NONE,
                             .pendingTie = ANO_NEAR_NONE };
}

// generate_melody's `lifecycle` strings.
typedef enum AnoMelodyStage
{
    ANO_MEL_PLAIN = 0, // "" — plain sentence form
    ANO_MEL_INTRODUCED,
    ANO_MEL_DEVELOPED,
    ANO_MEL_STATED,    // faithful recurrence of an authored signature (M17)
    ANO_MEL_COMPLETED, // the payoff drive into the cadence-fused statement
} AnoMelodyStage;

#define ANO_MELODY_MAX_EVENTS 64 // placed + pickups + doubles

typedef struct AnoMelodyResult
{
    AnoMusicEvent  events[ANO_MELODY_MAX_EVENTS];
    uint32_t       eventCount;
    AnoMelodyState state; // the successor state
} AnoMelodyResult;

// One bar of melody. Optional inputs use NULL (signature/apex/bass/replay/
// anacrusisRng/syncopateRng) and ANO_NEAR_NONE (prevBass) exactly where the
// prototype uses None; bass is the realized bass bar for the A3 guard
// (active only when cfg->counterpoint and bassCount > 0).
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
