/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// TPT state-variable filter. Coefs per block from cutoff/Q. State per sample.
// Stereo = two states, one coef set. Callers map modes onto ANO_DSP_SVF_*.

#ifndef ANO_DSP_SVF_H
#define ANO_DSP_SVF_H

#include <math.h>
#include <stdint.h>

#define ANO_DSP_SVF_BYPASS   0
#define ANO_DSP_SVF_LOWPASS  1
#define ANO_DSP_SVF_HIGHPASS 2
#define ANO_DSP_SVF_BANDPASS 3

typedef struct AnoDspSvfCoef
{
    float k;  // 1/Q
    float a1;
    float a2;
    float a3;
} AnoDspSvfCoef;

typedef struct AnoDspSvfState
{
    float ic1;
    float ic2;
} AnoDspSvfState;

// cutoff Hz [10, 0.45*rate], Q >= 0.1.
static inline void ano_dsp_svf_coef(AnoDspSvfCoef *c, float cutoffHz, float q, float sampleRate)
{
    float lim = 0.45f * sampleRate;
    if (cutoffHz < 10.0f) cutoffHz = 10.0f;
    if (cutoffHz > lim) cutoffHz = lim;
    if (q < 0.1f) q = 0.1f;
    float g = tanf(3.14159265358979f * cutoffHz / sampleRate);
    c->k  = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

// Denormal flush. Once per block.
static inline void ano_dsp_svf_flush(AnoDspSvfState *s)
{
    if (s->ic1 < 1.0e-20f && s->ic1 > -1.0e-20f) s->ic1 = 0.0f;
    if (s->ic2 < 1.0e-20f && s->ic2 > -1.0e-20f) s->ic2 = 0.0f;
}

static inline float ano_dsp_svf_step(const AnoDspSvfCoef *c, AnoDspSvfState *s,
                                     float in, uint32_t mode)
{
    float v3 = in - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    switch (mode) {
    case ANO_DSP_SVF_LOWPASS:  return v2;
    case ANO_DSP_SVF_BANDPASS: return v1;
    case ANO_DSP_SVF_HIGHPASS: return in - c->k * v1 - v2;
    default:                   return in;
    }
}

#endif // ANO_DSP_SVF_H
