/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/svf.h (private to src/audio/)
 * TPT (topology-preserving transform) state-variable filter — the DSP
 * library's workhorse (TECH_SPEC §12.2), first primitive of the Phase 3 set.
 * Coefficients are recomputed per block from smoothed cutoff/Q (one tanf);
 * state advances per sample. Stereo = two states sharing one coefficient set.
 * Protocol-free: callers map their own mode enums onto ANO_DSP_SVF_*.
 */

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
    float a1; // 1 / (1 + g*(g + k))
    float a2; // g * a1
    float a3; // g * a2
} AnoDspSvfCoef;

typedef struct AnoDspSvfState
{
    float ic1; // first integrator state
    float ic2; // second integrator state
} AnoDspSvfState;

// in:  cutoff Hz (clamped [10, 0.45*rate]), Q (clamped >= 0.1), sample rate
// out: coefficient set for the block
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

// Flush decayed integrator state to true zero so silent buses never grind
// through denormals. Call once per block.
static inline void ano_dsp_svf_flush(AnoDspSvfState *s)
{
    if (s->ic1 < 1.0e-20f && s->ic1 > -1.0e-20f) s->ic1 = 0.0f;
    if (s->ic2 < 1.0e-20f && s->ic2 > -1.0e-20f) s->ic2 = 0.0f;
}

// One sample; returns the selected response.
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
