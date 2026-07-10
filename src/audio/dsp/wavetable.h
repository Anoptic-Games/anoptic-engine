/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/wavetable.h (private to src/audio/)
 * 2D-morphing wavetable reader (TECH_SPEC §12.2): a bank of single-cycle
 * frames, bilinear interpolation across phase and morph. The caller owns the
 * bank memory (frames * frameLen floats, frame-major) and its content; this
 * header only reads it. Morph is clamped below 1.0 in the node (finding 7 —
 * the prototype's library segfaulted past it; ours just refuses).
 */

#ifndef ANO_DSP_WAVETABLE_H
#define ANO_DSP_WAVETABLE_H

#include <stdint.h>
#include <stddef.h>

typedef struct AnoDspWavetable
{
    const float *bank;     // frameCount * frameLen, frame-major
    uint32_t     frameLen; // samples per single-cycle frame (>= 2)
    uint32_t     frameCount;
} AnoDspWavetable;

// in: phase cycles [0,1), morph [0,1) (clamped [0, 0.999] here).
// out: bilinear sample across (phase within frame) x (morph across frames).
static inline float ano_dsp_wavetable_read(const AnoDspWavetable *w, float phase, float morph)
{
    if (morph < 0.0f) morph = 0.0f;
    if (morph > 0.999f) morph = 0.999f;
    float fpos = morph * (float)(w->frameCount - 1u);
    uint32_t f0 = (uint32_t)fpos;
    float ff = fpos - (float)f0;
    uint32_t f1 = f0 + 1u >= w->frameCount ? f0 : f0 + 1u;

    float spos = phase * (float)w->frameLen;
    uint32_t s0 = (uint32_t)spos;
    float sf = spos - (float)s0;
    if (s0 >= w->frameLen) s0 = w->frameLen - 1u; // phase 1.0 epsilon guard
    uint32_t s1 = s0 + 1u >= w->frameLen ? 0u : s0 + 1u;

    const float *a = w->bank + (size_t)f0 * w->frameLen;
    const float *b = w->bank + (size_t)f1 * w->frameLen;
    float va = a[s0] + (a[s1] - a[s0]) * sf;
    float vb = b[s0] + (b[s1] - b[s0]) * sf;
    return va + (vb - va) * ff;
}

#endif // ANO_DSP_WAVETABLE_H
