/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/smooth.h (private to src/audio/)
 * One-pole parameter smoother: every audible parameter glides through one of
 * these (~30 ms), so retargets never zipper (TECH_SPEC §12.5, the second
 * smoothing tier). Snap-on-converge kills denormals. Two step cadences exist:
 * per-sample (voice/bus gains, pan, freq, rate) and per-block (anything that
 * feeds a coefficient recompute) — the cadence is baked into `coef`.
 */

#ifndef ANO_DSP_SMOOTH_H
#define ANO_DSP_SMOOTH_H

typedef struct AnoAudioSmooth
{
    float y;      // current value
    float target; // retarget destination
    float coef;   // pole for the caller's step cadence
} AnoAudioSmooth;

// Advance one step (sample or block, per the coef baked in) and return the value.
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
