/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Voice state and render boundary. No bridge atomics: usable from C and C++.

#ifndef ANO_AUDIO_SOURCE_H
#define ANO_AUDIO_SOURCE_H

#include <stdint.h>

#include "dsp/smooth.h"

// Release ramp retire threshold (-80 dBFS).
#define ANO_AUDIO_RETIRE_EPS 1.0e-4f

// Voice lifecycle. FREE -> PLAYING is a pool state flip.
// RETIRING holds the slot until AEVT_SOURCE_RETIRED lands; only then may the id recycle.
typedef enum AnoAudioSourceState
{
    ANO_AUDIO_SRC_FREE = 0,
    ANO_AUDIO_SRC_PLAYING,
    ANO_AUDIO_SRC_STOPPING, // release ramp
    ANO_AUDIO_SRC_RETIRING, // silent; retirement event not yet delivered
} AnoAudioSourceState;

typedef struct AnoAudioSource
{
    uint32_t state;     // AnoAudioSourceState
    uint32_t source_id;
    uint32_t bus;
    uint32_t kind;      // AnoAudioSourceKind
    uint32_t flags;     // AnoAudioSourceFlags

    // TONE
    double phase; // cycles [0, 1)

    // BUFFER: registry snapshot + read cursor. Block outlives every voice on it (retirement waits silence).
    uint32_t     bufSlot;
    uint32_t     bufChannels;
    uint64_t     bufFrames;
    const float *bufData;
    double       cursor; // fractional frame

    uint64_t remaining; // frames until release. UINT64_MAX = until STOP

    AnoAudioSmooth gain;
    AnoAudioSmooth pan;      // -1 .. +1
    AnoAudioSmooth freq;     // TONE Hz
    AnoAudioSmooth rate;     // BUFFER rate
    AnoAudioSmooth spatGain; // distance attenuation

    float position[3];
    float minDist, maxDist, rolloff;
    AnoAudioSmooth airCutoff; // per-block -> one-pole coef
    float airCoef;
    float airLp;              // mono pre-pan state
} AnoAudioSource;

#ifdef __cplusplus
extern "C" {
#endif

void ano_audio_source_render(AnoAudioSource *source, float *mix, uint32_t frames, float fsInv);

#ifdef __cplusplus
}
#endif

#endif // ANO_AUDIO_SOURCE_H
