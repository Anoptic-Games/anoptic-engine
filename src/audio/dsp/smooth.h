/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// One-pole parameter smoother (~30 ms). Snap-on-converge kills denormals.
// Cadence (per-sample or per-block) is baked into coef.

#ifndef ANO_DSP_SMOOTH_H
#define ANO_DSP_SMOOTH_H

typedef struct AnoAudioSmooth
{
    float y;      // current
    float target;
    float coef;   // pole for caller's step cadence
} AnoAudioSmooth;

static inline float ano_audio_smooth_step(AnoAudioSmooth *s)
{
    float d = s->y - s->target;
    if (d < 1.0e-7f && d > -1.0e-7f) { s->y = s->target; return s->y; }
    s->y = s->target + d * s->coef;
    return s->y;
}

static inline void ano_audio_smooth_snap(AnoAudioSmooth *s, float v)
{
    s->y = v;
    s->target = v;
}

#endif // ANO_DSP_SMOOTH_H
