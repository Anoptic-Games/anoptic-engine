/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/osc.h (private to src/audio/)
 * Band-limited voice oscillators (TECH_SPEC §12.2): sine, polyBLEP saw and
 * square, and triangle via a leaky integration of the polyBLEP square. Each is
 * a phase accumulator in cycles [0,1) advanced by freq/rate per sample; the
 * caller owns the phase and may frequency-modulate it freely (vibrato, FM).
 * All state must be init-zeroed (finding 8).
 */

#ifndef ANO_DSP_OSC_H
#define ANO_DSP_OSC_H

#include <math.h>

#define ANO_DSP_TWO_PI 6.28318530717958647692f

// Advance a phase accumulator by dt = freq/sampleRate; wraps into [0, 1).
// Negative dt (extreme FM) wraps correctly too.
static inline float ano_dsp_phase_step(float *phase, float dt)
{
    float p = *phase + dt;
    p -= floorf(p);
    *phase = p;
    return p;
}

// in: phase cycles [0,1). out: sine sample [-1, 1].
static inline float ano_dsp_sine(float phase)
{
    return sinf(ANO_DSP_TWO_PI * phase);
}

// Two-sided polyBLEP residual at phase t with per-sample increment dt:
// smooths the unit step a naive discontinuity would alias on.
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

// in: phase cycles [0,1), dt = freq/rate. out: band-limited saw [-1, 1].
static inline float ano_dsp_saw(float phase, float dt)
{
    return 2.0f * phase - 1.0f - ano_dsp_polyblep(phase, dt);
}

// in: phase cycles [0,1), dt = freq/rate. out: band-limited square [-1, 1].
static inline float ano_dsp_square(float phase, float dt)
{
    float v = phase < 0.5f ? 1.0f : -1.0f;
    v += ano_dsp_polyblep(phase, dt);
    float t2 = phase + 0.5f;
    t2 -= floorf(t2);
    v -= ano_dsp_polyblep(t2, dt);
    return v;
}

// Triangle integrator state (one per oscillator; init-zero).
typedef struct AnoDspTri
{
    float y; // leaky-integrated square
} AnoDspTri;

// in: phase cycles [0,1), dt = freq/rate. out: band-limited triangle [-1, 1].
// Leak keeps the integrator DC-free; gain 4*dt scales the ramp to unit peak.
static inline float ano_dsp_triangle(AnoDspTri *s, float phase, float dt)
{
    float sq = ano_dsp_square(phase, dt);
    s->y = 0.999f * s->y + 4.0f * dt * sq;
    return s->y;
}

#endif // ANO_DSP_OSC_H
