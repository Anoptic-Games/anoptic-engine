/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Determinism kernel: BLAKE2b-8 stream tags, CPython MT19937 draws, banker's round.
// Bar N depends only on (master seed, musical state at N): fresh RNG per colon-joined tag.

#ifndef ANO_MUSIC_DET_H
#define ANO_MUSIC_DET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Hash */

// BLAKE2b, keyless, digest size 8 (RFC 7693).
void ano_music_blake2b8(const void *msg, size_t len, uint8_t out[8]);

// Incremental form: init -> update* -> final == ano_music_blake2b8 over concatenated updates.
typedef struct AnoBlake2b8
{
    uint64_t h[8];
    uint8_t  buf[128];
    size_t   len; // bytes buffered
    uint64_t t;   // bytes already compressed
} AnoBlake2b8;

void ano_blake2b8_init(AnoBlake2b8 *s);
void ano_blake2b8_update(AnoBlake2b8 *s, const void *msg, size_t len);
void ano_blake2b8_final(AnoBlake2b8 *s, uint8_t out[8]);

/* RNG */

// CPython random.Random: MT19937 + one-slot gauss cache.
typedef struct AnoMusicRng
{
    uint32_t mt[624];
    uint32_t index;
    double   gaussNext;
    bool     hasGauss;
} AnoMusicRng;

// random.Random(seed) for non-negative int (init_by_array; key word count follows magnitude).
void ano_music_rng_seed(AnoMusicRng *r, uint64_t seed);

// tag -> BLAKE2b-8 -> big-endian u64 -> fresh generator. Tags: "42:pad:3".
void ano_music_stream(AnoMusicRng *r, const char *tag);

uint32_t ano_music_getrandbits32(AnoMusicRng *r); // raw tempered word
double   ano_music_random(AnoMusicRng *r);        // 53-bit double in [0, 1)

// randint(a, b): both ends inclusive, a <= b.
int64_t ano_music_randint(AnoMusicRng *r, int64_t a, int64_t b);

// Uniform index into n items.
uint32_t ano_music_choice(AnoMusicRng *r, uint32_t n);

// choices(weights=..., k=1): cumulative float sums + bisect_right. Float ORDER is load-bearing; weights sum L->R.
uint32_t ano_music_choices1(AnoMusicRng *r, const double *weights, uint32_t n);

// Fisher-Yates over idx[n], CPython order (i from n-1 down to 1).
void ano_music_shuffle(AnoMusicRng *r, uint32_t *idx, uint32_t n);

// uniform(a, b): a + (b-a)*random().
double ano_music_uniform(AnoMusicRng *r, double a, double b);

// sample(range(n), k) without replacement into out[k]. Pool below setsize heuristic, selection-set above. k <= n.
void ano_music_sample(AnoMusicRng *r, uint32_t n, uint32_t k, uint32_t *out);

// gauss(mu, sigma): CPython cached-pair (Humanize only).
double ano_music_gauss(AnoMusicRng *r, double mu, double sigma);

/* Rounding */

// Python 3 round(x, ndigits): banker's half-to-even via correctly-rounded decimal. ndigits >= 0.
double ano_music_round(double x, int ndigits);

// Python 3 round(x) -> int: half-to-even.
int64_t ano_music_round_int(double x);

// Python float floor division (a // b): float_divmod snap-to-integral. wx != 0. Not plain floor(a/b).
double ano_music_floordiv(double vx, double wx);

#endif // ANO_MUSIC_DET_H
