/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/noise.h (private to src/audio/)
 * Seeded stochastic primitives (TECH_SPEC §12.2, finding 8): every instance
 * takes an explicit seed, no global entropy anywhere. White noise for sources
 * and tests; TPDF dither (±1 LSB triangular) applied ONLY at final 16-bit
 * quantization (the ALSA s16 fallback path).
 */

#ifndef ANO_DSP_NOISE_H
#define ANO_DSP_NOISE_H

#include <stdint.h>

typedef struct AnoDspRng
{
    uint32_t s; // never zero (xorshift32 sticks at zero)
} AnoDspRng;

static inline void ano_dsp_rng_seed(AnoDspRng *r, uint32_t seed)
{
    r->s = seed ? seed : 0xA5A5A5A5u;
}

static inline uint32_t ano_dsp_rng_next(AnoDspRng *r)
{
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (r->s = x);
}

// Uniform white noise in [-1, 1).
static inline float ano_dsp_noise(AnoDspRng *r)
{
    return (float)(int32_t)ano_dsp_rng_next(r) * (1.0f / 2147483648.0f);
}

// Triangular dither in [-1, 1) LSB units: add to the scaled sample right
// before truncation to 16 bits.
static inline float ano_dsp_tpdf(AnoDspRng *r)
{
    float a = (float)(ano_dsp_rng_next(r) & 0xFFFFu) * (1.0f / 65536.0f);
    float b = (float)(ano_dsp_rng_next(r) & 0xFFFFu) * (1.0f / 65536.0f);
    return a - b;
}

#endif // ANO_DSP_NOISE_H
