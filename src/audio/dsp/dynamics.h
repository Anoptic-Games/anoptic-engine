/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Dynamics detectors: peak follower, sliding-window max, linear ramp, comp gain.
// One step = one sample.

#ifndef ANO_DSP_DYNAMICS_H
#define ANO_DSP_DYNAMICS_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <mimalloc.h>

static inline float ano_dsp_pole_ms(float ms, float fs)
{
    if (ms < 0.01f) ms = 0.01f;
    return 1.0f - expf(-1000.0f / (ms * fs));
}

// Instant attack, exponential release.
typedef struct AnoDspFollower
{
    float env;
    float decay; // per-sample release multiplier
} AnoDspFollower;

static inline float ano_dsp_follower_step(AnoDspFollower *f, float mag)
{
    float d = f->decay * f->env;
    f->env = mag > d ? mag : d;
    if (f->env < 1.0e-20f)
        f->env = 0.0f;
    return f->env;
}

// Gapless window max over last `win` samples. Monotonic wedge, amortized O(1).
typedef struct AnoDspWinMax
{
    float    *val;
    uint64_t *stamp;
    uint32_t  mask; // ring capacity - 1
    uint32_t  head, tail;
    uint64_t  n;   // samples pushed
    uint32_t  win;
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

// Linear ramp. Reaches target in exactly `samples` steps (one-pole never converges inside a lookahead window).
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

// (env/thr)^(invRatio - 1) above threshold, else 1. Caller clamps.
static inline float ano_dsp_comp_gain(float env, float threshold, float invRatio)
{
    if (env <= threshold || threshold <= 0.0f)
        return 1.0f;
    return powf(env / threshold, invRatio - 1.0f);
}

#endif // ANO_DSP_DYNAMICS_H
