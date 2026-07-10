/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/env.h (private to src/audio/)
 * One-shot ASR envelope with curve shaping (TECH_SPEC §12.2): attack ramps
 * 0 -> 1 as t^curve, sustain holds 1, release falls 1 -> 0 as (1-t)^curve.
 * curve > 1 gives a soft attack and a percussive, exponential-feeling decay
 * (the prototype's drums live at curve 3-4). One-shot-per-event lifecycle:
 * init at voice allocation, step to completion, never retargeted. Segment
 * lengths are frames, fixed at init — the voice's exact end frame is
 * attack + sustain + release, known at allocation (finding 9's state-flip
 * voice pool depends on that).
 */

#ifndef ANO_DSP_ENV_H
#define ANO_DSP_ENV_H

#include <stdint.h>
#include <math.h>

typedef struct AnoDspAsr
{
    uint64_t attack;  // frames
    uint64_t sustain; // frames
    uint64_t release; // frames
    float    curve;
    uint64_t n; // frames elapsed
} AnoDspAsr;

// in: segment lengths in seconds (clamped >= 0), curve (clamped >= 0.1), rate.
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

// One sample; 0 forever once the release has completed.
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

// True once the envelope has fully decayed (the voice's retirement test).
static inline bool ano_dsp_asr_done(const AnoDspAsr *e)
{
    return e->n >= ano_dsp_asr_total(e);
}

#endif // ANO_DSP_ENV_H
