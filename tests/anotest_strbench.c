/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Benchmark for anoptic_strings.h: anostr_compare / anostr_eq vs naive strcmp-style memcmp,
 * validating the German-string claim that len + 4-byte prefix answer most compares in-register
 * without dereferencing the heap (docs/string_progress.md 5.3, 6.8).
 *
 * Series:
 *   - inline (<= 12B) keys, random: the no-allocation tier;
 *   - long (32B) keys, random: prefixes differ, the in-register fast path;
 *   - long (32B) keys, shared 16B prefix: the adversarial case, forced memcmp tail;
 * each as anostr_compare vs plain memcmp over the same data.
 *
 * Prints a percentile table (bench.h) and exits 0 always: benchmarks assert nothing, they
 * report. Built always, DISABLED in ctest -- run by hand from a Release (-O3) build (build 7).
 * argv[1] scales the number of compare pairs. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_strings.h"
#include "templates/bench.h"
#include "templates/rng.h"

#define PAIRS_DEFAULT 100000u
#define BATCH 64u   // compares per timed sample: one rdtsc read costs more than one compare

// A volatile sink so the compare results cannot be optimized away.
static volatile int g_sink;

typedef struct {
    anostr_t   a, b;
    const char *rawA, *rawB;    // the same bytes for the memcmp baseline
    uint32_t    lenA, lenB;
} pair_t;

static const char *dup_bytes(mi_heap_t *heap, const char *src, size_t n)
{
    char *p = mi_heap_malloc(heap, n);
    memcpy(p, src, n);
    return p;
}

static pair_t make_pair(mi_heap_t *heap, test_rng *rng, uint32_t len, uint32_t sharedPrefix)
{
    char bufA[64], bufB[64];
    rng_fill_printable(rng, bufA, len, len);
    rng_fill_printable(rng, bufB, len, len);
    memcpy(bufB, bufA, sharedPrefix < len ? sharedPrefix : len);

    pair_t p;
    p.lenA = p.lenB = len;
    p.rawA = dup_bytes(heap, bufA, len);
    p.rawB = dup_bytes(heap, bufB, len);
    p.a = anostr_from(heap, bufA, len);
    p.b = anostr_from(heap, bufB, len);
    return p;
}

// memcmp baseline with the same total-order semantics as anostr_compare.
static inline int baseline_compare(const pair_t *p)
{
    uint32_t n = p->lenA < p->lenB ? p->lenA : p->lenB;
    int c = memcmp(p->rawA, p->rawB, n);
    if (c != 0) return c;
    return p->lenA == p->lenB ? 0 : (p->lenA < p->lenB ? -1 : 1);
}

static bench_stats run_anostr(const pair_t *pairs, uint32_t count, uint64_t *buf, size_t cap)
{
    bench_lat lat;
    bench_lat_init(&lat, buf, cap);
    for (uint32_t i = 0; i + BATCH <= count; i += BATCH) {
        uint64_t t0 = bench_begin();
        int acc = 0;
        for (uint32_t j = 0; j < BATCH; j++)
            acc += anostr_compare(pairs[i + j].a, pairs[i + j].b);
        g_sink = acc;
        bench_lat_add(&lat, bench_end(t0) / BATCH);
    }
    return bench_lat_stats(&lat);
}

static bench_stats run_memcmp(const pair_t *pairs, uint32_t count, uint64_t *buf, size_t cap)
{
    bench_lat lat;
    bench_lat_init(&lat, buf, cap);
    for (uint32_t i = 0; i + BATCH <= count; i += BATCH) {
        uint64_t t0 = bench_begin();
        int acc = 0;
        for (uint32_t j = 0; j < BATCH; j++)
            acc += baseline_compare(&pairs[i + j]);
        g_sink = acc;
        bench_lat_add(&lat, bench_end(t0) / BATCH);
    }
    return bench_lat_stats(&lat);
}

int main(int argc, char **argv)
{
    uint32_t pairs = PAIRS_DEFAULT;
    if (argc > 1) pairs = (uint32_t)strtoul(argv[1], NULL, 10);

    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("mi_heap_new failed\n"); return 1; }

    test_rng rng = rng_make(0xBE5C0123u);
    pair_t *inl   = mi_heap_malloc(heap, pairs * sizeof(pair_t));
    pair_t *lng   = mi_heap_malloc(heap, pairs * sizeof(pair_t));
    pair_t *shared = mi_heap_malloc(heap, pairs * sizeof(pair_t));
    uint64_t *buf = mi_heap_malloc(heap, (pairs / BATCH + 1) * sizeof(uint64_t));
    if (!inl || !lng || !shared || !buf) { printf("alloc failed\n"); return 1; }

    for (uint32_t i = 0; i < pairs; i++) {
        inl[i]    = make_pair(heap, &rng, 8, 0);    // inline tier
        lng[i]    = make_pair(heap, &rng, 32, 0);   // long, prefixes differ
        shared[i] = make_pair(heap, &rng, 32, 16);  // long, 16B shared prefix
    }

    size_t cap = pairs / BATCH + 1;
    printf("anotest_strbench: %u pairs per series, %u compares per sample\n\n", pairs, BATCH);
    bench_lat_header();
    bench_lat_row("inline8  anostr_compare", run_anostr(inl, pairs, buf, cap));
    bench_lat_row("inline8  memcmp",         run_memcmp(inl, pairs, buf, cap));
    bench_lat_row("long32   anostr_compare", run_anostr(lng, pairs, buf, cap));
    bench_lat_row("long32   memcmp",         run_memcmp(lng, pairs, buf, cap));
    bench_lat_row("shared16 anostr_compare", run_anostr(shared, pairs, buf, cap));
    bench_lat_row("shared16 memcmp",         run_memcmp(shared, pairs, buf, cap));
    return 0;
}
