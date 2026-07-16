/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Band-limited oscillators: sine, polyBLEP saw/square, leaky-integrated triangle.
// Phase in cycles [0,1). Caller owns phase. Init-zero state.

#ifndef ANO_DSP_OSC_H
#define ANO_DSP_OSC_H

#include <math.h>

#define ANO_DSP_TWO_PI 6.28318530717958647692f

// Advance phase by dt = freq/sampleRate. Wraps [0, 1). Negative dt ok.
static inline float ano_dsp_phase_step(float *phase, float dt)
{
    float p = *phase + dt;
    p -= floorf(p);
    *phase = p;
    return p;
}

static inline float ano_dsp_sine(float phase)
{
    return sinf(ANO_DSP_TWO_PI * phase);
}

// polyBLEP residual at phase t with increment dt.
static inline float ano_dsp_polyblep(float t, float dt)
{
    if (dt <= 0.0f)
        return 0.0f;
    if (t < dt) {
        float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

static inline float ano_dsp_saw(float phase, float dt)
{
    return 2.0f * phase - 1.0f - ano_dsp_polyblep(phase, dt);
}

static inline float ano_dsp_square(float phase, float dt)
{
    float v = phase < 0.5f ? 1.0f : -1.0f;
    v += ano_dsp_polyblep(phase, dt);
    float t2 = phase + 0.5f;
    t2 -= floorf(t2);
    v -= ano_dsp_polyblep(t2, dt);
    return v;
}

typedef struct AnoDspTri
{
    float y;
} AnoDspTri;

// Leak keeps DC-free. gain 4*dt scales to unit peak.
static inline float ano_dsp_triangle(AnoDspTri *s, float phase, float dt)
{
    float sq = ano_dsp_square(phase, dt);
    s->y = 0.999f * s->y + 4.0f * dt * sq;
    return s->y;
}

#endif // ANO_DSP_OSC_H
