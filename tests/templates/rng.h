/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Deterministic RNG for tests. xorshift32, one instance per thread, no shared state.
// Never libc rand() across threads: it is not thread-safe and kills reproducibility.

#ifndef ANOPTIC_TEST_TEMPLATES_RNG_H
#define ANOPTIC_TEST_TEMPLATES_RNG_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t s; } test_rng;

// Fixed seeds make runs reproducible. Zero state would stick, so it is remapped.
static inline test_rng rng_make(uint32_t seed)
{
    test_rng r = { seed ? seed : 0xA5A5A5A5u };
    return r;
}

static inline uint32_t rng_next(test_rng *r)
{
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (r->s = x);
}

// [0, n). Modulo bias is fine for test inputs.
static inline uint32_t rng_below(test_rng *r, uint32_t n)
{
    return rng_next(r) % n;
}

// Printable ASCII 0x20..0x7e. Never '\n' or '\0', so one record stays one line.
static inline char rng_printable(test_rng *r)
{
    return (char)(0x20 + rng_below(r, 0x7e - 0x20 + 1));
}

// NUL-terminated printable string, length in [minLen, maxLen]. buf holds maxLen + 1.
static inline size_t rng_fill_printable(test_rng *r, char *buf, size_t minLen, size_t maxLen)
{
    size_t len = minLen + rng_below(r, (uint32_t)(maxLen - minLen + 1));
    for (size_t i = 0; i < len; i++)
        buf[i] = rng_printable(r);
    buf[len] = '\0';
    return len;
}

#endif // ANOPTIC_TEST_TEMPLATES_RNG_H
