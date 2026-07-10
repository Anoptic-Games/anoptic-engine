/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/grain.h (private to src/audio/)
 * Granular engine over a live history ring (TECH_SPEC §12.2): a mono input
 * feeds a rolling window; a seeded stochastic grain clock (Bernoulli per
 * sample at density/rate — the prototype's RandomImpulse) spawns Hann-windowed
 * grains that read the history at a random delay and their own playback rate,
 * each panned constant-power to a random bearing. Read cursors are clamped
 * into the valid window (finding 7). Everything is caller-allocated and
 * init-zeroed; the ring capacity is a power of two.
 */

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
    double   src;   // absolute fractional frame in ring timeline
    double   rate;  // cursor advance per output frame (2.0 = octave up)
    uint32_t age;   // frames rendered
    uint32_t dur;   // grain length in frames
    float    gl, gr; // constant-power pan gains
} AnoDspGrain;

typedef struct AnoDspGrainEngine
{
    float   *ring;    // capacity floats, caller-allocated, zeroed
    uint32_t mask;    // capacity - 1 (capacity pow2)
    uint64_t write;   // absolute frames written
    float    minDelayS, maxDelayS; // grain start delay behind the write head
    float    grainS;  // grain duration seconds
    float    rate;    // grain playback rate
    float    density; // mean grains per second (clock probability density/fs)
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

// One sample: write `in` into the history, maybe spawn a grain, sum the live
// grains into *l/*r (accumulated, caller zeroes). fs = sample rate.
static inline void ano_dsp_grain_step(AnoDspGrainEngine *e, float in, float fs,
                                      float *l, float *r)
{
    e->ring[e->write & e->mask] = in;
    e->write++;

    // seeded grain clock: expectation density grains/second
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
            break; // all slots busy = grain dropped, deterministically
        }
    }

    for (uint32_t i = 0; i < ANO_DSP_GRAIN_SLOTS; ++i) {
        AnoDspGrain *g = &e->g[i];
        if (!g->active)
            continue;
        // clamp the cursor into the valid window [write - capacity + 2, write - 1]
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
