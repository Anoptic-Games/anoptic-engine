/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * dsp/delay.h (private to src/audio/)
 * The one delay primitive everything variable-time is built on (TECH_SPEC
 * §12.2): predelay, chorus taps, ping-pong, limiter lookahead, FDN lines, and
 * the Schroeder allpass diffuser. Buffer-position inputs declare CLAMP
 * semantics (finding 7): every read clamps its delay into [1, cap] — an
 * out-of-range musical parameter can detune, never read wild memory. Range
 * validation is loud at the parameter-set layer (audio_fx.c), not per sample.
 * Buffers come zeroed from mi_heap_calloc — initialized state (finding 8).
 */

#ifndef ANO_DSP_DELAY_H
#define ANO_DSP_DELAY_H

#include <stdbool.h>
#include <stdint.h>
#include <mimalloc.h>

typedef struct AnoDspDelay
{
    float   *buf;
    uint32_t mask; // capacity - 1 (power of two)
    uint32_t cap;  // usable max delay in samples
    uint32_t w;    // write cursor (monotonic, masked on access)
} AnoDspDelay;

static inline uint32_t ano_dsp_delay_pow2_(uint32_t v)
{
    if (v < 2u) return 2u;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u;
}

// in:  heap, maxDelay in samples (> 0)
// out: false on allocation failure; buffer arrives zeroed
static inline bool ano_dsp_delay_init(AnoDspDelay *d, mi_heap_t *heap, uint32_t maxDelay)
{
    uint32_t cap = ano_dsp_delay_pow2_(maxDelay + 1u);
    d->buf = mi_heap_calloc(heap, cap, sizeof(float));
    if (!d->buf)
        return false;
    d->mask = cap - 1u;
    d->cap  = maxDelay;
    d->w    = 0u;
    return true;
}

static inline void ano_dsp_delay_write(AnoDspDelay *d, float x)
{
    d->buf[d->w & d->mask] = x;
    d->w++;
}

// The sample written `delay` writes ago. CLAMPS delay into [1, cap].
static inline float ano_dsp_delay_read_int(const AnoDspDelay *d, uint32_t delay)
{
    if (delay < 1u) delay = 1u;
    if (delay > d->cap) delay = d->cap;
    return d->buf[(d->w - delay) & d->mask];
}

// Fractional tap with linear interpolation. CLAMPS into [1, cap - 1].
static inline float ano_dsp_delay_read_frac(const AnoDspDelay *d, float delay)
{
    if (delay < 1.0f) delay = 1.0f;
    if (delay > (float)(d->cap - 1u)) delay = (float)(d->cap - 1u);
    uint32_t i = (uint32_t)delay;
    float    f = delay - (float)i;
    float a = d->buf[(d->w - i) & d->mask];
    float b = d->buf[(d->w - i - 1u) & d->mask];
    return a + (b - a) * f;
}

// Schroeder allpass diffuser over a fixed-length line: y = -g*x + v,
// where v is the line output and the line receives x + g*y.
typedef struct AnoDspAllpass
{
    AnoDspDelay d;
    uint32_t    len;
    float       g;
} AnoDspAllpass;

static inline bool ano_dsp_allpass_init(AnoDspAllpass *ap, mi_heap_t *heap, uint32_t len, float g)
{
    ap->len = len < 1u ? 1u : len;
    ap->g   = g;
    return ano_dsp_delay_init(&ap->d, heap, ap->len);
}

static inline float ano_dsp_allpass_step(AnoDspAllpass *ap, float x)
{
    float v = ano_dsp_delay_read_int(&ap->d, ap->len);
    float y = v - ap->g * x;
    ano_dsp_delay_write(&ap->d, x + ap->g * y);
    return y;
}

// Feedback comb over a fixed-length line: y = line out; line in = x + g*y.
typedef struct AnoDspComb
{
    AnoDspDelay d;
    uint32_t    len;
    float       g;
} AnoDspComb;

static inline bool ano_dsp_comb_init(AnoDspComb *cb, mi_heap_t *heap, uint32_t len, float g)
{
    cb->len = len < 1u ? 1u : len;
    cb->g   = g;
    return ano_dsp_delay_init(&cb->d, heap, cb->len);
}

static inline float ano_dsp_comb_step(AnoDspComb *cb, float x)
{
    float y = ano_dsp_delay_read_int(&cb->d, cb->len);
    ano_dsp_delay_write(&cb->d, x + cb->g * y);
    return y;
}

#endif // ANO_DSP_DELAY_H
