/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/dynamics.h (private to src/audio/)
 * Detector primitives (TECH_SPEC §12.2, finding 6): the asymmetric one-pole
 * peak follower (env = max(|x|, a*env)), a gapless sliding-window max, a
 * linear ramp-to-target-in-T, and the compressor gain computer. Detector
 * timing is sample-accurate — every step advances exactly one sample.
 */

#ifndef ANO_DSP_DYNAMICS_H
#define ANO_DSP_DYNAMICS_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <mimalloc.h>

// One-pole coefficient for a time constant in ms at fs.
static inline float ano_dsp_pole_ms(float ms, float fs)
{
    if (ms < 0.01f) ms = 0.01f;
    return 1.0f - expf(-1000.0f / (ms * fs));
}

// Asymmetric peak follower: instant attack, exponential release.
typedef struct AnoDspFollower
{
    float env;
    float decay; // per-sample release multiplier, e.g. expf(-1/(ms*fs/1000))
} AnoDspFollower;

static inline float ano_dsp_follower_step(AnoDspFollower *f, float mag)
{
    float d = f->decay * f->env;
    f->env = mag > d ? mag : d;
    if (f->env < 1.0e-20f)
        f->env = 0.0f;
    return f->env;
}

// Gapless sliding-window max over the last `win` samples (monotonic wedge:
// amortized O(1) per sample, exact — never a stale or missed peak).
typedef struct AnoDspWinMax
{
    float    *val;
    uint64_t *stamp;
    uint32_t  mask; // ring capacity - 1
    uint32_t  head, tail;
    uint64_t  n;   // samples pushed
    uint32_t  win; // window length
} AnoDspWinMax;

static inline bool ano_dsp_winmax_init(AnoDspWinMax *w, mi_heap_t *heap, uint32_t window)
{
    uint32_t cap = 2u;
    while (cap < window + 1u)
        cap <<= 1;
    w->val   = mi_heap_calloc(heap, cap, sizeof(float));
    w->stamp = mi_heap_calloc(heap, cap, sizeof(uint64_t));
    if (!w->val || !w->stamp)
        return false;
    w->mask = cap - 1u;
    w->head = w->tail = 0u;
    w->n    = 0u;
    w->win  = window < 1u ? 1u : window;
    return true;
}

// Push one magnitude; returns max over the trailing window (inclusive).
static inline float ano_dsp_winmax_push(AnoDspWinMax *w, float mag)
{
    while (w->tail != w->head && w->val[(w->tail - 1u) & w->mask] <= mag)
        w->tail--;
    w->val[w->tail & w->mask]   = mag;
    w->stamp[w->tail & w->mask] = w->n;
    w->tail++;
    if (w->stamp[w->head & w->mask] + w->win <= w->n)
        w->head++;
    w->n++;
    return w->val[w->head & w->mask];
}

// Linear ramp that reaches its target in exactly `samples` steps — a one-pole
// never converges inside a lookahead window (finding 6).
typedef struct AnoDspRamp
{
    float    y;
    float    step;
    uint32_t left;
} AnoDspRamp;

static inline void ano_dsp_ramp_to(AnoDspRamp *r, float target, uint32_t samples)
{
    if (samples == 0u) {
        r->y = target;
        r->step = 0.0f;
        r->left = 0u;
        return;
    }
    r->step = (target - r->y) / (float)samples;
    r->left = samples;
}

static inline float ano_dsp_ramp_step(AnoDspRamp *r)
{
    if (r->left) {
        r->y += r->step;
        r->left--;
    }
    return r->y;
}

// Compressor gain for an envelope above threshold: (env/thr)^(1/ratio - 1),
// 1 below. Caller clamps the result — gain reduction is bounded, not implicit.
static inline float ano_dsp_comp_gain(float env, float threshold, float invRatio)
{
    if (env <= threshold || threshold <= 0.0f)
        return 1.0f;
    return powf(env / threshold, invRatio - 1.0f);
}

#endif // ANO_DSP_DYNAMICS_H
