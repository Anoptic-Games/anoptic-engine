/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_det.h (private to src/music/)
 * The determinism kernel (TECH_SPEC §8): BLAKE2b-8 stream tagging, CPython-
 * compatible MT19937 with init_by_array seeding and the exact draw semantics
 * of random / getrandbits / randint / choice / choices / shuffle / uniform /
 * sample / gauss, and Python 3 banker's rounding. Everything the generation
 * core consumes randomness or rounding through lives here — bar N's material
 * depends only on (master seed, declared musical state at N), realized as a
 * FRESH generator per colon-joined stream tag. Bit-parity with CPython 3.12
 * is the contract (§8.4 phase 1); it is proven by anotest_music against
 * vectors generated from the prototype's interpreter.
 */

#ifndef ANO_MUSIC_DET_H
#define ANO_MUSIC_DET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// BLAKE2b, keyless, digest size 8 (RFC 7693 reference shape).
// in: message bytes; out: exactly 8 digest bytes.
void ano_music_blake2b8(const void *msg, size_t len, uint8_t out[8]);

// CPython random.Random: MT19937 + a one-slot gauss cache.
typedef struct AnoMusicRng
{
    uint32_t mt[624];
    uint32_t index;
    double   gaussNext;
    bool     hasGauss;
} AnoMusicRng;

// random.Random(seed) for a non-negative int seed (init_by_array; the key
// word count follows the seed's magnitude, exactly as CPython splits it).
void ano_music_rng_seed(AnoMusicRng *r, uint64_t seed);

// The stream factory: tag -> BLAKE2b-8 -> big-endian u64 -> fresh generator.
// Tags are colon-joined decimal ints and verbatim strings ("42:pad:3").
void ano_music_stream(AnoMusicRng *r, const char *tag);

uint32_t ano_music_getrandbits32(AnoMusicRng *r); // raw tempered word
double   ano_music_random(AnoMusicRng *r);        // 53-bit double in [0, 1)

// randint(a, b): both ends inclusive, a <= b (rejection via getrandbits).
int64_t ano_music_randint(AnoMusicRng *r, int64_t a, int64_t b);

// choice: a uniform index into n items (the caller indexes its own array).
uint32_t ano_music_choice(AnoMusicRng *r, uint32_t n);

// choices(weights=..., k=1): one weighted index via cumulative float sums +
// bisect_right — float ORDER is load-bearing, weights sum left to right.
uint32_t ano_music_choices1(AnoMusicRng *r, const double *weights, uint32_t n);

// shuffle: Fisher-Yates over idx[n], CPython order (i from n-1 down to 1).
void ano_music_shuffle(AnoMusicRng *r, uint32_t *idx, uint32_t n);

// uniform(a, b): a + (b-a)*random().
double ano_music_uniform(AnoMusicRng *r, double a, double b);

// sample(range(n), k) without replacement into out[k], CPython's pool /
// selection-set split reproduced. k <= n.
void ano_music_sample(AnoMusicRng *r, uint32_t n, uint32_t k, uint32_t *out);

// gauss(mu, sigma): CPython's cached-pair algorithm (Humanize only).
double ano_music_gauss(AnoMusicRng *r, double mu, double sigma);

// Python 3 round(x, ndigits): decimal round-half-to-even (banker's), exact
// via correctly-rounded decimal conversion. ndigits >= 0.
double ano_music_round(double x, int ndigits);

// Python 3 round(x) -> int: half-to-even to an integer.
int64_t ano_music_round_int(double x);

// Python float floor division (a // b): CPython's float_divmod quotient with
// its snap-to-integral correction — NOT plain floor(a/b), which can differ
// by 1 when fp division and the exact quotient fall on opposite sides of an
// integer. wx != 0.
double ano_music_floordiv(double vx, double wx);

#endif // ANO_MUSIC_DET_H
