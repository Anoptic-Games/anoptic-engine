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
    uint64_t n; // frames elapsed
} AnoDspAsr;

enum class AnoDspCurve : uint8_t
{
    none = 0,
    linear,
    three_halves,
    nine_fifths,
    square,
    five_halves,
    cube,
    fourth,
};

template<AnoDspCurve Curve>
static inline float ano_dsp_curve(float t)
{
    static_assert(Curve != AnoDspCurve::none);
    if constexpr (Curve == AnoDspCurve::linear) return t;
    else if constexpr (Curve == AnoDspCurve::three_halves) return t * sqrtf(t);
    else if constexpr (Curve == AnoDspCurve::nine_fifths) return powf(t, 1.8f);
    else if constexpr (Curve == AnoDspCurve::square) return t * t;
    else if constexpr (Curve == AnoDspCurve::five_halves) return t * t * sqrtf(t);
    else if constexpr (Curve == AnoDspCurve::cube) return t * t * t;
    else if constexpr (Curve == AnoDspCurve::fourth) {
        float square = t * t;
        return square * square;
    } else {
        static_assert(Curve != Curve, "unhandled ASR curve");
    }
}

// Segment lengths in seconds (>= 0). Curve is a compile-time render contract.
static inline void ano_dsp_asr_init(AnoDspAsr *e, float attackS, float sustainS,
                                    float releaseS, float sampleRate)
{
    if (attackS < 0.0f) attackS = 0.0f;
    if (sustainS < 0.0f) sustainS = 0.0f;
    if (releaseS < 0.0f) releaseS = 0.0f;
    e->attack  = (uint64_t)(attackS * sampleRate);
    e->sustain = (uint64_t)(sustainS * sampleRate);
    e->release = (uint64_t)(releaseS * sampleRate);
    e->n       = 0;
}

static inline uint64_t ano_dsp_asr_total(const AnoDspAsr *e)
{
    return e->attack + e->sustain + e->release;
}

// 0 forever after release completes.
template<AnoDspCurve Curve>
static inline float ano_dsp_asr_step(AnoDspAsr *e)
{
    uint64_t n = e->n++;
    if (n < e->attack)
        return ano_dsp_curve<Curve>((float)n / (float)e->attack);
    n -= e->attack;
    if (n < e->sustain)
        return 1.0f;
    n -= e->sustain;
    if (n < e->release)
        return ano_dsp_curve<Curve>(1.0f - (float)n / (float)e->release);
    return 0.0f;
}

static inline bool ano_dsp_asr_done(const AnoDspAsr *e)
{
    return e->n >= ano_dsp_asr_total(e);
}

#endif // ANO_DSP_ENV_H
