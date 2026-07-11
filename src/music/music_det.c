/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * music_det.c
 * The determinism kernel: BLAKE2b (RFC 7693 reference, keyless, 8-byte
 * digests), MT19937 with CPython's init_by_array seeding, the exact CPython
 * 3.12 draw algorithms, and Python 3 banker's rounding. No allocation, no
 * state beyond the caller's AnoMusicRng. This TU must build with
 * -ffp-contract=off: choices' cumulative sums and gauss' transcendentals are
 * bit-parity surfaces (TECH_SPEC §3.3, §8.3).
 */

#include "music_det.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// BLAKE2b (RFC 7693), unkeyed, digest_size 8
// ---------------------------------------------------------------------------

static const uint64_t B2B_IV[8] = {
    0x6A09E667F3BCC908ull, 0xBB67AE8584CAA73Bull, 0x3C6EF372FE94F82Bull,
    0xA54FF53A5F1D36F1ull, 0x510E527FADE682D1ull, 0x9B05688C2B3E6C1Full,
    0x1F83D9ABFB41BD6Bull, 0x5BE0CD19137E2179ull,
};

static const uint8_t B2B_SIGMA[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
};

static inline uint64_t b2b_rotr(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64u - n));
}

static inline uint64_t b2b_load64(const uint8_t *p)
{
    return (uint64_t)p[0] | (uint64_t)p[1] << 8 | (uint64_t)p[2] << 16
         | (uint64_t)p[3] << 24 | (uint64_t)p[4] << 32 | (uint64_t)p[5] << 40
         | (uint64_t)p[6] << 48 | (uint64_t)p[7] << 56;
}

#define B2B_G(a, b, c, d, x, y) do { \
        v[a] += v[b] + (x); v[d] = b2b_rotr(v[d] ^ v[a], 32); \
        v[c] += v[d];       v[b] = b2b_rotr(v[b] ^ v[c], 24); \
        v[a] += v[b] + (y); v[d] = b2b_rotr(v[d] ^ v[a], 16); \
        v[c] += v[d];       v[b] = b2b_rotr(v[b] ^ v[c], 63); \
    } while (0)

// One compression: h in/out, one 128-byte block, byte counter t, final flag.
static void b2b_compress(uint64_t h[8], const uint8_t block[128], uint64_t t, bool last)
{
    uint64_t v[16], m[16];
    for (int i = 0; i < 8; ++i) {
        v[i]      = h[i];
        v[i + 8]  = B2B_IV[i];
    }
    v[12] ^= t; // low counter word (messages here never reach 2^64 bytes)
    if (last)
        v[14] = ~v[14];
    for (int i = 0; i < 16; ++i)
        m[i] = b2b_load64(block + 8 * i);
    for (int r = 0; r < 12; ++r) {
        const uint8_t *s = B2B_SIGMA[r];
        B2B_G(0, 4, 8, 12, m[s[0]], m[s[1]]);
        B2B_G(1, 5, 9, 13, m[s[2]], m[s[3]]);
        B2B_G(2, 6, 10, 14, m[s[4]], m[s[5]]);
        B2B_G(3, 7, 11, 15, m[s[6]], m[s[7]]);
        B2B_G(0, 5, 10, 15, m[s[8]], m[s[9]]);
        B2B_G(1, 6, 11, 12, m[s[10]], m[s[11]]);
        B2B_G(2, 7, 8, 13, m[s[12]], m[s[13]]);
        B2B_G(3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; ++i)
        h[i] ^= v[i] ^ v[i + 8];
}

void ano_music_blake2b8(const void *msg, size_t len, uint8_t out[8])
{
    uint64_t h[8];
    memcpy(h, B2B_IV, sizeof h);
    h[0] ^= 0x01010000ull ^ 8ull; // param block: digest_length 8, fanout 1, depth 1

    const uint8_t *p = msg;
    uint64_t t = 0;
    // full blocks, except the last block is always compressed with the final flag
    while (len > 128) {
        t += 128;
        b2b_compress(h, p, t, false);
        p += 128;
        len -= 128;
    }
    uint8_t block[128] = {0};
    memcpy(block, p, len);
    t += len;
    b2b_compress(h, block, t, true);

    for (int i = 0; i < 8; ++i)
        out[i] = (uint8_t)(h[0] >> (8 * i)); // little-endian first state word
}

// ---------------------------------------------------------------------------
// MT19937, CPython seeding and draws
// ---------------------------------------------------------------------------

#define MT_N 624
#define MT_M 397

static void mt_init_genrand(AnoMusicRng *r, uint32_t s)
{
    r->mt[0] = s;
    for (uint32_t i = 1; i < MT_N; ++i)
        r->mt[i] = 1812433253u * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + i;
    r->index = MT_N;
}

static void mt_init_by_array(AnoMusicRng *r, const uint32_t *key, uint32_t keyLen)
{
    mt_init_genrand(r, 19650218u);
    uint32_t i = 1, j = 0;
    uint32_t k = MT_N > keyLen ? MT_N : keyLen;
    for (; k; --k) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1664525u))
                 + key[j] + j;
        if (++i >= MT_N) { r->mt[0] = r->mt[MT_N - 1]; i = 1; }
        if (++j >= keyLen) j = 0;
    }
    for (k = MT_N - 1; k; --k) {
        r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1566083941u)) - i;
        if (++i >= MT_N) { r->mt[0] = r->mt[MT_N - 1]; i = 1; }
    }
    r->mt[0] = 0x80000000u;
    r->index = MT_N;
}

uint32_t ano_music_getrandbits32(AnoMusicRng *r)
{
    if (r->index >= MT_N) {
        uint32_t *mt = r->mt;
        for (uint32_t i = 0; i < MT_N; ++i) {
            uint32_t y = (mt[i] & 0x80000000u) | (mt[(i + 1) % MT_N] & 0x7FFFFFFFu);
            uint32_t next = mt[(i + MT_M) % MT_N] ^ (y >> 1);
            if (y & 1u)
                next ^= 0x9908B0DFu;
            mt[i] = next;
        }
        r->index = 0;
    }
    uint32_t y = r->mt[r->index++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= y >> 18;
    return y;
}

// random.Random(int): the key word count follows the seed's magnitude —
// values below 2^32 seed with ONE word, not a zero-padded pair.
void ano_music_rng_seed(AnoMusicRng *r, uint64_t seed)
{
    uint32_t key[2] = { (uint32_t)seed, (uint32_t)(seed >> 32) };
    mt_init_by_array(r, key, key[1] ? 2u : 1u);
    r->hasGauss = false;
    r->gaussNext = 0.0;
}

void ano_music_stream(AnoMusicRng *r, const char *tag)
{
    uint8_t d[8];
    ano_music_blake2b8(tag, strlen(tag), d);
    uint64_t seed = 0; // big-endian digest -> int (TECH_SPEC §8.1)
    for (int i = 0; i < 8; ++i)
        seed = seed << 8 | d[i];
    ano_music_rng_seed(r, seed);
}

double ano_music_random(AnoMusicRng *r)
{
    uint32_t a = ano_music_getrandbits32(r) >> 5;
    uint32_t b = ano_music_getrandbits32(r) >> 6;
    return ((double)a * 67108864.0 + (double)b) * (1.0 / 9007199254740992.0);
}

// getrandbits(k), k in [1, 32]
static uint32_t mt_getrandbits(AnoMusicRng *r, uint32_t k)
{
    return ano_music_getrandbits32(r) >> (32u - k);
}

// CPython _randbelow_with_getrandbits: rejection sampling at bit_length(n)
static uint64_t mt_randbelow(AnoMusicRng *r, uint64_t n)
{
    if (n == 0)
        return 0;
    uint32_t k = 64u - (uint32_t)__builtin_clzll(n); // n.bit_length()
    if (k <= 32u) {
        uint32_t v;
        do
            v = mt_getrandbits(r, k);
        while (v >= n);
        return v;
    }
    // getrandbits(k > 32): CPython fills 32-bit words LEAST significant first
    uint64_t v;
    do {
        uint64_t lo = ano_music_getrandbits32(r);
        uint64_t hi = mt_getrandbits(r, k - 32u);
        v = hi << 32 | lo;
    } while (v >= n);
    return v;
}

int64_t ano_music_randint(AnoMusicRng *r, int64_t a, int64_t b)
{
    return a + (int64_t)mt_randbelow(r, (uint64_t)(b - a + 1));
}

uint32_t ano_music_choice(AnoMusicRng *r, uint32_t n)
{
    return (uint32_t)mt_randbelow(r, n);
}

uint32_t ano_music_choices1(AnoMusicRng *r, const double *weights, uint32_t n)
{
    // cumulative sums left to right, then bisect_right with hi = n - 1
    double cum[64];
    if (n > 64u) n = 64u; // no prototype weight list approaches this
    double acc = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        acc += weights[i];
        cum[i] = acc;
    }
    double x = ano_music_random(r) * (cum[n - 1] + 0.0);
    uint32_t lo = 0, hi = n - 1;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if (x < cum[mid])
            hi = mid;
        else
            lo = mid + 1u;
    }
    return lo;
}

void ano_music_shuffle(AnoMusicRng *r, uint32_t *idx, uint32_t n)
{
    if (n < 2u)
        return;
    for (uint32_t i = n - 1u; i >= 1u; --i) {
        uint32_t j = (uint32_t)mt_randbelow(r, (uint64_t)i + 1u);
        uint32_t t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
    }
}

double ano_music_uniform(AnoMusicRng *r, double a, double b)
{
    return a + (b - a) * ano_music_random(r);
}

// CPython sample(): pool method below the setsize heuristic, selection-set
// tracking above it. Reproduced exactly, including the log-based threshold.
void ano_music_sample(AnoMusicRng *r, uint32_t n, uint32_t k, uint32_t *out)
{
    uint32_t setsize = 21u;
    if (k > 5u)
        setsize += (uint32_t)pow(4.0, ceil(log((double)k * 3.0) / log(4.0)));
    if (n <= setsize) {
        // pool method: pick from a shrinking copy
        uint32_t pool[256];
        uint32_t m = n < 256u ? n : 256u;
        for (uint32_t i = 0; i < m; ++i)
            pool[i] = i;
        for (uint32_t i = 0; i < k; ++i) {
            uint32_t j = (uint32_t)mt_randbelow(r, n - i);
            out[i] = pool[j];
            pool[j] = pool[n - i - 1u];
        }
        return;
    }
    // selection set: re-draw on collision (n here is small; linear scan)
    uint32_t seen[256];
    uint32_t seenCount = 0;
    for (uint32_t i = 0; i < k; ++i) {
        uint32_t j;
        bool dup;
        do {
            j = (uint32_t)mt_randbelow(r, n);
            dup = false;
            for (uint32_t s = 0; s < seenCount; ++s)
                if (seen[s] == j) { dup = true; break; }
        } while (dup);
        if (seenCount < 256u)
            seen[seenCount++] = j;
        out[i] = j;
    }
}

double ano_music_gauss(AnoMusicRng *r, double mu, double sigma)
{
    double z;
    if (r->hasGauss) {
        z = r->gaussNext;
        r->hasGauss = false;
    } else {
        double x2pi  = ano_music_random(r) * (2.0 * 3.141592653589793238462643383279502884);
        double g2rad = sqrt(-2.0 * log(1.0 - ano_music_random(r)));
        z = cos(x2pi) * g2rad;
        r->gaussNext = sin(x2pi) * g2rad;
        r->hasGauss = true;
    }
    return mu + z * sigma;
}

// ---------------------------------------------------------------------------
// Python 3 banker's rounding
// ---------------------------------------------------------------------------
// CPython rounds floats to ndigits through correctly-rounded decimal
// conversion (David Gay), ties to even. glibc's printf/strtod pair performs
// the identical correctly-rounded conversion in both directions, so a
// round-trip through the shortest sufficient decimal string reproduces it.

double ano_music_round(double x, int ndigits)
{
    if (!isfinite(x))
        return x;
    char buf[512]; // %.10f of any double fits with room to spare
    snprintf(buf, sizeof buf, "%.*f", ndigits, x);
    return strtod(buf, NULL);
}

int64_t ano_music_round_int(double x)
{
    // binary halves are exact, so native round-to-nearest-even is Python's
    return (int64_t)nearbyint(x);
}
