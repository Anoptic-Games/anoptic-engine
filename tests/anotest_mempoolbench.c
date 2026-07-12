/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* The Lakos grid for anoptic_memory_pools.h, vs the raw mi_heap baseline.
 * Three shapes, each timed as per-op latency percentiles plus wall ops/sec:
 *   churn    steady-state alloc/free with a bounded working set and varied sizes --
 *            the multipool's home turf (AS7-AS10);
 *   batch    allocate a storm, tear it all down at once -- monotonic + wink-out vs
 *            per-object free vs heap wink-out (AS1-AS6);
 *   compose  the same churn through Multipool<Monotonic> (AS11-AS14).
 * Merge bar (plan step 0): multipool >= mi_heap on churn shapes; monotonic+wink >= both
 * on batch shapes. Run by hand from a -O3 build (build.sh 8); DISABLED in ctest. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "anoptic_memory_pools.h"
#include "templates/bench.h"
#include "templates/rng.h"

#define CHURN_OPS   400000u
#define CHURN_SLOTS 1024u
#define BATCH_N     200000u
#define BATCH_REPS  8u

static uint64_t g_sink;     // defeat dead-code elimination

// ---------------------------------------------------------------------------------------------
// Churn: mixed sizes, bounded working set. One sample per op (alloc or free).

typedef void *(*alloc_fn)(void *ctx, size_t size);
typedef void  (*free_fn)(void *ctx, void *p, size_t size);

static void *heap_alloc_cb(void *ctx, size_t size)          { return mi_heap_malloc((mi_heap_t *)ctx, size); }
static void  heap_free_cb(void *ctx, void *p, size_t size)  { (void)ctx; (void)size; mi_free(p); }
static void *mp_alloc_cb(void *ctx, size_t size)            { return ano_mem_multipool_alloc(ctx, size); }
static void  mp_free_cb(void *ctx, void *p, size_t size)    { ano_mem_multipool_free(ctx, p, size); }

static void run_churn(const char *label, void *ctx, alloc_fn af, free_fn ff,
                      uint32_t maxSize, uint64_t *sampleBuf)
{
    static void  *slotPtr[CHURN_SLOTS];
    static size_t slotSize[CHURN_SLOTS];
    memset(slotPtr, 0, sizeof slotPtr);
    test_rng rng = rng_make(0x1776u);
    bench_lat lat;
    bench_lat_init(&lat, sampleBuf, CHURN_OPS);

    uint64_t w0 = bench_begin();
    for (uint32_t it = 0; it < CHURN_OPS; it++) {
        uint32_t slot = rng_below(&rng, CHURN_SLOTS);
        if (slotPtr[slot]) {
            uint64_t t0 = bench_begin();
            ff(ctx, slotPtr[slot], slotSize[slot]);
            bench_lat_add(&lat, bench_end(t0));
            slotPtr[slot] = NULL;
        } else {
            size_t size = 1 + rng_below(&rng, maxSize);
            uint64_t t0 = bench_begin();
            void *p = af(ctx, size);
            bench_lat_add(&lat, bench_end(t0));
            if (!p) { printf("%s: alloc failed mid-churn\n", label); return; }
            ((uint8_t *)p)[0] = (uint8_t)it;            // touch first line only
            ((uint8_t *)p)[size - 1] = (uint8_t)it;
            slotPtr[slot]  = p;
            slotSize[slot] = size;
        }
    }
    uint64_t wall = bench_end(w0);
    for (uint32_t s = 0; s < CHURN_SLOTS; s++)
        if (slotPtr[s]) ff(ctx, slotPtr[s], slotSize[s]);

    bench_stats st = bench_lat_stats(&lat);
    bench_lat_row(label, st);
    printf("%-28s   wall %.1f ms, %.2f Mops/s\n", "",
           (double)ano_ticks_to_ns(wall) / 1e6,
           bench_ops_per_sec(CHURN_OPS, ano_ticks_to_ns(wall)) / 1e6);
}

// ---------------------------------------------------------------------------------------------
// Batch-and-wink: N allocations, then one teardown. Wall time only (teardown included).

static void run_batch(const char *label, uint32_t maxSize, int mode)
{
    // mode 0: mi_heap per-object free; 1: mi_heap wink-out; 2: monotonic + destroy;
    //      3: monotonic reset-reuse (amortized slabs, the per-ingest staging shape).
    static void *ptrs[BATCH_N];
    double bestMs = 1e30;
    ano_mem_monotonic *keepArena = NULL;
    mi_heap_t *keepHeap = NULL;
    if (mode == 3) {
        keepHeap = mi_heap_new();
        keepArena = ano_mem_monotonic_make(ano_mem_parent_heap(keepHeap), 1u << 20);
    }

    for (uint32_t rep = 0; rep < BATCH_REPS; rep++) {
        test_rng rng = rng_make(0xBEEFu + rep);
        uint64_t t0 = bench_begin();
        if (mode == 0 || mode == 1) {
            mi_heap_t *heap = mi_heap_new();
            for (uint32_t i = 0; i < BATCH_N; i++) {
                size_t size = 1 + rng_below(&rng, maxSize);
                ptrs[i] = mi_heap_malloc(heap, size);
                ((uint8_t *)ptrs[i])[0] = (uint8_t)i;
            }
            if (mode == 0)
                for (uint32_t i = 0; i < BATCH_N; i++) mi_free(ptrs[i]);
            mi_heap_destroy(heap);
        } else {
            ano_mem_monotonic *arena = keepArena;
            mi_heap_t *heap = NULL;
            if (mode == 2) {
                heap = mi_heap_new();
                arena = ano_mem_monotonic_make(ano_mem_parent_heap(heap), 1u << 20);
            }
            for (uint32_t i = 0; i < BATCH_N; i++) {
                size_t size = 1 + rng_below(&rng, maxSize);
                void *p = ano_mem_monotonic_alloc(arena, size, 0);
                ((uint8_t *)p)[0] = (uint8_t)i;
                g_sink += (uintptr_t)p;
            }
            if (mode == 2) {
                ano_mem_monotonic_destroy(arena);
                mi_heap_destroy(heap);
            } else {
                ano_mem_monotonic_reset(arena);
            }
        }
        double ms = (double)ano_ticks_to_ns(bench_end(t0)) / 1e6;
        if (ms < bestMs) bestMs = ms;
    }
    if (mode == 3) {
        ano_mem_monotonic_destroy(keepArena);
        mi_heap_destroy(keepHeap);
    }
    printf("%-44s best of %u: %8.2f ms  (%.2f Mops/s)\n",
           label, BATCH_REPS, bestMs, (double)BATCH_N / bestMs / 1e3);
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    static uint64_t samples[CHURN_OPS];

    printf("== churn: %u ops, %u-slot working set ==\n", CHURN_OPS, CHURN_SLOTS);
    bench_lat_header();
    for (int pass = 0; pass < 2; pass++) {      // pass 0 warms, pass 1 reports
        uint32_t sizes[2] = { 4096, 64 * 1024 };
        for (int si = 0; si < 2; si++) {
            char label[64];
            mi_heap_t *h1 = mi_heap_new();
            snprintf(label, sizeof label, "mi_heap      <=%uK", sizes[si] / 1024);
            if (pass) run_churn(label, h1, heap_alloc_cb, heap_free_cb, sizes[si], samples);
            else      run_churn("(warm)", h1, heap_alloc_cb, heap_free_cb, sizes[si], samples);
            mi_heap_destroy(h1);

            mi_heap_t *h2 = mi_heap_new();
            ano_mem_multipool *mp = ano_mem_multipool_make(ano_mem_parent_heap(h2), NULL);
            snprintf(label, sizeof label, "multipool    <=%uK", sizes[si] / 1024);
            if (pass) run_churn(label, mp, mp_alloc_cb, mp_free_cb, sizes[si], samples);
            mi_heap_destroy(h2);

            mi_heap_t *h3 = mi_heap_new();
            ano_mem_monotonic *arena = ano_mem_monotonic_make(ano_mem_parent_heap(h3), 1u << 20);
            ano_mem_multipool *cmp = ano_mem_multipool_make(ano_mem_parent_monotonic(arena), NULL);
            snprintf(label, sizeof label, "mp<monotonic><=%uK", sizes[si] / 1024);
            if (pass) run_churn(label, cmp, mp_alloc_cb, mp_free_cb, sizes[si], samples);
            mi_heap_destroy(h3);
        }
        if (!pass) printf("(warm-up pass done)\n");
    }

    printf("\n== batch-and-wink: %u allocs <= 1 KiB, teardown included ==\n", BATCH_N);
    run_batch("mi_heap malloc + per-object free", 1024, 0);
    run_batch("mi_heap malloc + heap wink-out", 1024, 1);
    run_batch("monotonic + destroy (cold slabs)", 1024, 2);
    run_batch("monotonic + reset (warm slabs)", 1024, 3);

    printf("\nsink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
