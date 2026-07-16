/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Granular engine over a live history ring. Seeded Bernoulli spawn clock.
// Hann grains, random delay/rate/pan. Read cursors clamped. Caller owns ring (pow2).

#ifndef ANO_DSP_GRAIN_H
#define ANO_DSP_GRAIN_H

#include <stdint.h>
#include <math.h>
#include "noise.h"
#include "osc.h"

#define ANO_DSP_GRAIN_SLOTS 16

typedef struct AnoDspGrain
{
    bool     active;
    double   src;   // fractional frame in ring timeline
    double   rate;  // cursor advance per output frame
    uint32_t age;
    uint32_t dur;
    float    gl, gr; // constant-power pan
} AnoDspGrain;

typedef struct AnoDspGrainEngine
{
    float   *ring;
    uint32_t mask;    // capacity - 1
    uint64_t write;
    float    minDelayS, maxDelayS;
    float    grainS;
    float    rate;
    float    density; // mean grains/sec
    AnoDspRng rng;
    AnoDspGrain g[ANO_DSP_GRAIN_SLOTS];
} AnoDspGrainEngine;

static inline void ano_dsp_grain_init(AnoDspGrainEngine *e, float *ring, uint32_t capacity,
                                      float minDelayS, float maxDelayS, float grainS,
                                      float rate, uint32_t seed)
{
    e->ring = ring;
    e->mask = capacity - 1u;
    e->write = 0;
    e->minDelayS = minDelayS;
    e->maxDelayS = maxDelayS;
    e->grainS = grainS;
    e->rate = rate;
    e->density = 0.0f;
    ano_dsp_rng_seed(&e->rng, seed);
    for (uint32_t i = 0; i < ANO_DSP_GRAIN_SLOTS; ++i)
        e->g[i] = (AnoDspGrain){0};
}

// Write in. Maybe spawn. Accumulate live grains into *l/*r (caller zeroes).
static inline void ano_dsp_grain_step(AnoDspGrainEngine *e, float in, float fs,
                                      float *l, float *r)
{
    e->ring[e->write & e->mask] = in;
    e->write++;

    if (e->density > 0.0f && ano_dsp_noise(&e->rng) * 0.5f + 0.5f < e->density / fs) {
        for (uint32_t i = 0; i < ANO_DSP_GRAIN_SLOTS; ++i) {
            AnoDspGrain *g = &e->g[i];
            if (g->active)
                continue;
            float u = ano_dsp_noise(&e->rng) * 0.5f + 0.5f;
            float delay = e->minDelayS + u * (e->maxDelayS - e->minDelayS);
            float pan   = ano_dsp_noise(&e->rng); // [-1, 1]
            float th    = (pan + 1.0f) * 0.78539816f;
            g->active = true;
            g->src    = (double)e->write - (double)(delay * fs);
            g->rate   = e->rate;
            g->age    = 0;
            g->dur    = (uint32_t)(e->grainS * fs);
            if (g->dur == 0u) g->dur = 1u;
            g->gl = cosf(th);
            g->gr = sinf(th);
            break; // full slots: drop grain
        }
    }

    for (uint32_t i = 0; i < ANO_DSP_GRAIN_SLOTS; ++i) {
        AnoDspGrain *g = &e->g[i];
        if (!g->active)
            continue;
        // clamp into [write - capacity + 2, write - 1]
        double lo = (double)e->write - (double)e->mask;
        if (lo < 0.0) lo = 0.0;
        double hi = (double)(e->write - 1u);
        double src = g->src;
        if (src < lo) src = lo;
        if (src > hi) src = hi;
        uint64_t i0 = (uint64_t)src;
        float frac = (float)(src - (double)i0);
        float s0 = e->ring[i0 & e->mask];
        float s1 = e->ring[(i0 + 1u) & e->mask];
        float v  = s0 + (s1 - s0) * frac;
        float w  = 0.5f - 0.5f * cosf(ANO_DSP_TWO_PI * (float)g->age / (float)g->dur);
        v *= w;
        *l += v * g->gl;
        *r += v * g->gr;
        g->src += g->rate;
        if (++g->age >= g->dur)
            g->active = false;
    }
}

#endif // ANO_DSP_GRAIN_H
