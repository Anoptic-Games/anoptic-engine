/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// RBJ biquad: low/high shelf and peaking. Transposed DF-II. Coefs per block.

#ifndef ANO_DSP_BIQUAD_H
#define ANO_DSP_BIQUAD_H

#include <math.h>
#include <stdint.h>

typedef struct AnoDspBiquad
{
    float b0, b1, b2; // feedforward / a0
    float a1, a2;     // feedback / a0
} AnoDspBiquad;

typedef struct AnoDspBiquadState
{
    float z1, z2;
} AnoDspBiquadState;

static inline float ano_dsp_bq_clampf_(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline void ano_dsp_biquad_peak(AnoDspBiquad *c, float freq, float q, float gainDb, float fs)
{
    freq = ano_dsp_bq_clampf_(freq, 10.0f, 0.45f * fs);
    q    = ano_dsp_bq_clampf_(q, 0.1f, 12.0f);
    float A     = powf(10.0f, gainDb / 40.0f);
    float w     = 6.28318530717959f * freq / fs;
    float cs    = cosf(w), sn = sinf(w);
    float alpha = sn / (2.0f * q);
    float a0    = 1.0f + alpha / A;
    c->b0 = (1.0f + alpha * A) / a0;
    c->b1 = (-2.0f * cs) / a0;
    c->b2 = (1.0f - alpha * A) / a0;
    c->a1 = (-2.0f * cs) / a0;
    c->a2 = (1.0f - alpha / A) / a0;
}

// Shelf. slope S = 1.
static inline void ano_dsp_biquad_shelf(AnoDspBiquad *c, float freq, float gainDb, float fs, int high)
{
    freq = ano_dsp_bq_clampf_(freq, 10.0f, 0.45f * fs);
    float A    = powf(10.0f, gainDb / 40.0f);
    float w    = 6.28318530717959f * freq / fs;
    float cs   = cosf(w), sn = sinf(w);
    float alpha = 0.5f * sn * sqrtf(2.0f); // S = 1
    float sqA2a = 2.0f * sqrtf(A) * alpha;
    float b0, b1, b2, a0, a1, a2;
    if (high) {
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cs + sqA2a);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cs - sqA2a);
        a0 = (A + 1.0f) - (A - 1.0f) * cs + sqA2a;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
        a2 = (A + 1.0f) - (A - 1.0f) * cs - sqA2a;
    } else {
        b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + sqA2a);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - sqA2a);
        a0 = (A + 1.0f) + (A - 1.0f) * cs + sqA2a;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
        a2 = (A + 1.0f) + (A - 1.0f) * cs - sqA2a;
    }
    c->b0 = b0 / a0;
    c->b1 = b1 / a0;
    c->b2 = b2 / a0;
    c->a1 = a1 / a0;
    c->a2 = a2 / a0;
}

static inline void ano_dsp_biquad_lowshelf(AnoDspBiquad *c, float freq, float gainDb, float fs)
{
    ano_dsp_biquad_shelf(c, freq, gainDb, fs, 0);
}

static inline void ano_dsp_biquad_highshelf(AnoDspBiquad *c, float freq, float gainDb, float fs)
{
    ano_dsp_biquad_shelf(c, freq, gainDb, fs, 1);
}

static inline void ano_dsp_biquad_flush(AnoDspBiquadState *s)
{
    if (s->z1 < 1.0e-20f && s->z1 > -1.0e-20f) s->z1 = 0.0f;
    if (s->z2 < 1.0e-20f && s->z2 > -1.0e-20f) s->z2 = 0.0f;
}

static inline float ano_dsp_biquad_step(const AnoDspBiquad *c, AnoDspBiquadState *s, float x)
{
    float y = c->b0 * x + s->z1;
    s->z1 = c->b1 * x - c->a1 * y + s->z2;
    s->z2 = c->b2 * x - c->a2 * y;
    return y;
}

#endif // ANO_DSP_BIQUAD_H
