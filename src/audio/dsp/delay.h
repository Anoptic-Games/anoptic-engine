/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Variable-time delay primitive + Schroeder allpass + feedback comb.
// Fractional reads clamp delay into [1, mask - 1]. The line holds mask + 1 samples of masked
// history, so any tap <= mask - 1 and its i + 1 neighbour are always written slots.
// Buffer zeroed from mi_heap_calloc.

#ifndef ANO_DSP_DELAY_H
#define ANO_DSP_DELAY_H

#include <stdbool.h>
#include <stdint.h>
#include <mimalloc.h>

// Allocation floor in samples. Power of two >= 2, so mask >= 1 for every line.
#define ANO_DSP_DELAY_MIN_CAP 2u
static_assert(ANO_DSP_DELAY_MIN_CAP >= 2u, "delay floor keeps mask >= 1 so mask - 1 cannot wrap");
static_assert((ANO_DSP_DELAY_MIN_CAP & (ANO_DSP_DELAY_MIN_CAP - 1u)) == 0u,
              "delay floor must be a power of two: mask is cap - 1");

typedef struct AnoDspDelay
{
    float   *buf;
    uint32_t mask; // capacity - 1 (pow2, >= 1)
    uint32_t w;    // write cursor
} AnoDspDelay;

static inline uint32_t ano_dsp_delay_pow2_(uint32_t v)
{
    if (v < ANO_DSP_DELAY_MIN_CAP) return ANO_DSP_DELAY_MIN_CAP;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u;
}

// false on alloc failure. buffer arrives zeroed.
static inline bool ano_dsp_delay_init(AnoDspDelay *d, mi_heap_t *heap, uint32_t maxDelay)
{
    uint32_t cap = ano_dsp_delay_pow2_(maxDelay + 1u);
    d->buf = mi_heap_calloc(heap, cap, sizeof(float));
    if (!d->buf)
        return false;
    d->mask = cap - 1u;
    d->w    = 0u;
    return true;
}

static inline void ano_dsp_delay_write(AnoDspDelay *d, float x)
{
    d->buf[d->w & d->mask] = x;
    d->w++;
}

// Sample written `delay` ago. Caller keeps delay in [1, mask]; the index is masked either way.
static inline float ano_dsp_delay_read_int(const AnoDspDelay *d, uint32_t delay)
{
    return d->buf[(d->w - delay) & d->mask];
}

// Fractional tap, linear interp. Clamps into [1, mask - 1]: mask >= 1 by pow2 init, so the
// ceiling cannot wrap and the (uint32_t) conversion is always in range. A degenerate 2-slot
// line (mask == 1) collapses the tap to 0 -- defined, in-buffer, no interpolation.
static inline float ano_dsp_delay_read_frac(const AnoDspDelay *d, float delay)
{
    float dmax = (float)(d->mask - 1u);
    if (!(delay > 1.0f)) delay = 1.0f; // accept-form: NaN clamps here too
    if (delay > dmax) delay = dmax;
    uint32_t i = (uint32_t)delay;
    float    f = delay - (float)i;
    float a = d->buf[(d->w - i) & d->mask];
    float b = d->buf[(d->w - i - 1u) & d->mask];
    return a + (b - a) * f;
}

// Schroeder allpass: y = -g*x + v. line gets x + g*y.
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

// Feedback comb: y = line out. line in = x + g*y.
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
