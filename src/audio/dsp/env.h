/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// One-shot ASR. Attack 0->1 as t^curve. Release 1->0 as (1-t)^curve.
// Segment lengths fixed at init. Never retargeted. Total frames = A+S+R known at allocation.

#ifndef ANO_DSP_ENV_H
#define ANO_DSP_ENV_H

#include <stdint.h>
#include <math.h>

typedef struct AnoDspAsr
{
    uint64_t attack;
    uint64_t sustain;
    uint64_t release;
    float    curve;
    uint64_t n; // frames elapsed
} AnoDspAsr;

// Segment lengths in seconds (>= 0). curve >= 0.1.
static inline void ano_dsp_asr_init(AnoDspAsr *e, float attackS, float sustainS,
                                    float releaseS, float curve, float sampleRate)
{
    if (attackS < 0.0f) attackS = 0.0f;
    if (sustainS < 0.0f) sustainS = 0.0f;
    if (releaseS < 0.0f) releaseS = 0.0f;
    e->attack  = (uint64_t)(attackS * sampleRate);
    e->sustain = (uint64_t)(sustainS * sampleRate);
    e->release = (uint64_t)(releaseS * sampleRate);
    e->curve   = curve < 0.1f ? 0.1f : curve;
    e->n       = 0;
}

static inline uint64_t ano_dsp_asr_total(const AnoDspAsr *e)
{
    return e->attack + e->sustain + e->release;
}

// 0 forever after release completes.
static inline float ano_dsp_asr_step(AnoDspAsr *e)
{
    uint64_t n = e->n++;
    if (n < e->attack)
        return powf((float)n / (float)e->attack, e->curve);
    n -= e->attack;
    if (n < e->sustain)
        return 1.0f;
    n -= e->sustain;
    if (n < e->release)
        return powf(1.0f - (float)n / (float)e->release, e->curve);
    return 0.0f;
}

static inline bool ano_dsp_asr_done(const AnoDspAsr *e)
{
    return e->n >= ano_dsp_asr_total(e);
}

#endif // ANO_DSP_ENV_H
