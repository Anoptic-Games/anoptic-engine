/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Latency + throughput instrumentation for tests and benchmarks. Header-only, no allocation:
// samples are raw ticks in a caller-owned buffer, stats sort in place and convert to ns once.
// Report percentiles, not just means. A mean cannot falsify a tail claim.

#ifndef ANOPTIC_TEST_TEMPLATES_BENCH_H
#define ANOPTIC_TEST_TEMPLATES_BENCH_H

#include <anoptic_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Timed section: t0 = bench_begin(); ...work...; bench_lat_add(&lat, bench_end(t0));
static inline uint64_t bench_begin(void)          { return ano_timestamp_ticks(); }
static inline uint64_t bench_end(uint64_t t0)     { return ano_timestamp_ticks() - t0; }

// Sample recorder over a caller-owned buffer. Overflow drops the sample and counts it.
typedef struct {
    uint64_t *ticks;    // caller's buffer, one raw tick delta per op
    size_t    cap;
    size_t    n;
    uint64_t  lost;     // samples dropped at cap, so truncation is visible in the report
} bench_lat;

static inline void bench_lat_init(bench_lat *l, uint64_t *buf, size_t cap)
{
    l->ticks = buf; l->cap = cap; l->n = 0; l->lost = 0;
}

// Hot path: one compare and one store.
static inline void bench_lat_add(bench_lat *l, uint64_t tickDelta)
{
    if (l->n < l->cap) l->ticks[l->n++] = tickDelta;
    else               l->lost++;
}

typedef struct {
    size_t   n;
    uint64_t lost;
    double   mean_ns;
    uint64_t min_ns, p50_ns, p90_ns, p99_ns, p999_ns, max_ns;
} bench_stats;

static inline int bench_cmp_u64_(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

// Nearest-rank percentile of a sorted array. Permille, so 999 = p99.9.
static inline uint64_t bench_pctl_(const uint64_t *sorted, size_t n, unsigned permille)
{
    size_t rank = (size_t)(((uint64_t)permille * n + 999) / 1000);  // ceil
    if (rank == 0) rank = 1;
    if (rank > n)  rank = n;
    return sorted[rank - 1];
}

// Sorts the samples in place and converts them to ns via toNs. Call once, after recording.
// For custom tick sources (eg: a calibrated rdtsc), pass their own converter.
static inline bench_stats bench_lat_stats_with(bench_lat *l, uint64_t (*toNs)(uint64_t))
{
    bench_stats s;
    memset(&s, 0, sizeof s);
    s.n = l->n; s.lost = l->lost;
    if (l->n == 0)
        return s;
    for (size_t i = 0; i < l->n; i++)
        l->ticks[i] = toNs(l->ticks[i]);    // divisions here, off the timed loop
    qsort(l->ticks, l->n, sizeof *l->ticks, bench_cmp_u64_);
    double sum = 0.0;
    for (size_t i = 0; i < l->n; i++)
        sum += (double)l->ticks[i];
    s.mean_ns = sum / (double)l->n;
    s.min_ns  = l->ticks[0];
    s.max_ns  = l->ticks[l->n - 1];
    s.p50_ns  = bench_pctl_(l->ticks, l->n, 500);
    s.p90_ns  = bench_pctl_(l->ticks, l->n, 900);
    s.p99_ns  = bench_pctl_(l->ticks, l->n, 990);
    s.p999_ns = bench_pctl_(l->ticks, l->n, 999);
    return s;
}

// The common case: samples are ano_timestamp_ticks deltas.
static inline bench_stats bench_lat_stats(bench_lat *l)
{
    return bench_lat_stats_with(l, ano_ticks_to_ns);
}

// Aligned table: header once, one row per series. All values ns.
static inline void bench_lat_header(void)
{
    printf("%-28s %9s %10s %8s %8s %8s %8s %8s %10s %6s\n",
           "series (ns)", "n", "mean", "min", "p50", "p90", "p99", "p99.9", "max", "lost");
}

static inline void bench_lat_row(const char *label, bench_stats s)
{
    printf("%-28s %9llu %10.1f %8llu %8llu %8llu %8llu %8llu %10llu %6llu\n",
           label, (unsigned long long)s.n, s.mean_ns,
           (unsigned long long)s.min_ns,  (unsigned long long)s.p50_ns,
           (unsigned long long)s.p90_ns,  (unsigned long long)s.p99_ns,
           (unsigned long long)s.p999_ns, (unsigned long long)s.max_ns,
           (unsigned long long)s.lost);
}

// ops/sec over an elapsed-ns window. Zero elapsed reports 0, not inf.
static inline double bench_ops_per_sec(uint64_t ops, uint64_t elapsedNs)
{
    return elapsedNs ? (double)ops * 1e9 / (double)elapsedNs : 0.0;
}

#endif // ANOPTIC_TEST_TEMPLATES_BENCH_H
