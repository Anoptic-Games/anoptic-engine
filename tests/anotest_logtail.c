/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Tail-latency benchmark: per-call ano_log() percentiles (p50/p90/p99/p99.9).
// Calibrated rdtsc on x86-64, else ano_timestamp_ticks. P producers + owned drain thread.
// DISABLED in CTest. Always exits 0. argv[1] = messages-per-producer.

#include <anoptic_log.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include "templates/bench.h"    // percentile instrumentation
#include "templates/scratch.h"  // scratch dir for the output file

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAIL_DIR    ANO_TEST_OUTDIR "/anolog_tail"

#define DEFAULT_MSGS 100000     // per producer per point
#define MAXP         16
static const int POINTS[] = {1, 2, 4, 8, 16};
#define NPOINTS      (int)(sizeof POINTS / sizeof POINTS[0])

static int g_msgs = DEFAULT_MSGS;

/* Tick Source */

// rdtsc calibrated against platform clock (below QPC grain). Invariant TSC assumed.

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
static inline uint64_t tick_now(void) { return __rdtsc(); }
static double g_nsPerTick = 1.0;

// Ratio from ~50ms window vs ano_timestamp_raw.
static void tick_calibrate(void)
{
    uint64_t ns0 = ano_timestamp_raw(), t0 = tick_now();
    while (ano_timestamp_raw() - ns0 < 50u * 1000u * 1000u) { }
    uint64_t ns1 = ano_timestamp_raw(), t1 = tick_now();
    g_nsPerTick = (double)(ns1 - ns0) / (double)(t1 - t0);
}
static uint64_t tick_to_ns(uint64_t t) { return (uint64_t)((double)t * g_nsPerTick); }
#else
static inline uint64_t tick_now(void) { return ano_timestamp_ticks(); }
static void tick_calibrate(void) { }
static uint64_t tick_to_ns(uint64_t t) { return ano_ticks_to_ns(t); }
#endif

/* Producers */

// Every call timed; samples into a caller-owned slice of one big buffer.

typedef struct {
    int       id, count;
    bench_lat lat;
} prod_arg;

static void *producer(void *p)
{
    prod_arg *a = p;
    for (int i = 0; i < a->count; i++) {
        uint64_t t0 = tick_now();
        ano_log(ANO_INFO, "tail bench thread %d message %d with payload", a->id, i);
        bench_lat_add(&a->lat, tick_now() - t0);
    }
    return NULL;
}

// One point: P producers time every enqueue into adjacent slices, merged to one percentile row.
static bench_stats run_point(int producers, uint64_t *buf)
{
    anothread_t th[MAXP];
    prod_arg    arg[MAXP];
    for (int i = 0; i < producers; i++) {
        arg[i] = (prod_arg){ .id = i, .count = g_msgs };
        bench_lat_init(&arg[i].lat, buf + (size_t)i * (size_t)g_msgs, (size_t)g_msgs);
        ano_thread_create(&th[i], NULL, producer, &arg[i]);
    }
    for (int i = 0; i < producers; i++)
        ano_thread_join(th[i], NULL);
    ano_log_flush();    // drain the tail so the next point starts empty

    bench_lat merged;   // adjacent slices, all full: one contiguous sample set
    bench_lat_init(&merged, buf, (size_t)producers * (size_t)g_msgs);
    merged.n = (size_t)producers * (size_t)g_msgs;
    for (int i = 0; i < producers; i++)
        merged.lost += arg[i].lat.lost;
    return bench_lat_stats_with(&merged, tick_to_ns);
}

int main(int argc, char **argv)
{
    scratch_anchor_to_exe();   // scratch relative to this exe's dir, cross-platform

    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0) g_msgs = v;
    }

    tick_calibrate();

    uint64_t *buf = malloc((size_t)MAXP * (size_t)g_msgs * sizeof *buf);
    if (buf == NULL) {
        fprintf(stderr, "logtail: sample buffer allocation failed\n");
        return 0;   // benchmark, not a test: never fails the suite
    }

    printf("Anoptic logger tail benchmark -- per-call enqueue latency percentiles\n");
    printf("  %d msgs/producer, logger's own drain thread consuming\n\n", g_msgs);

    // Timer cost (subtract from series below).
    {
        bench_lat ovh;
        bench_lat_init(&ovh, buf, 4096);
        for (int i = 0; i < 4096; i++) {
            uint64_t t0 = tick_now();
            bench_lat_add(&ovh, tick_now() - t0);
        }
        bench_stats s = bench_lat_stats_with(&ovh, tick_to_ns);
        bench_lat_header();
        bench_lat_row("timer overhead", s);
    }

    scratch_make_dir(TAIL_DIR);
    ano_log_init();
    ano_log_output_dir(TAIL_DIR);

    for (int i = 0; i < 256; i++)   // warm caches + branch predictors
        ano_log(ANO_INFO, "warm %d", i);
    ano_log_flush();

    for (int p = 0; p < NPOINTS; p++) {
        bench_stats s = run_point(POINTS[p], buf);
        char label[40];
        snprintf(label, sizeof label, "enqueue @ %d producer%s", POINTS[p],
                 POINTS[p] == 1 ? "" : "s");
        bench_lat_row(label, s);
    }

    ano_log_cleanup();
    free(buf);
    char tailLog[96];
    snprintf(tailLog, sizeof tailLog, "%s/%s_ano.log", TAIL_DIR, ano_fs_session_stamp());
    remove(tailLog);
    scratch_remove_dir(TAIL_DIR);

    printf("\n(Full-ring waits are part of the tail by design: the producer self-throttles to the\n"
           " drain rate rather than dropping. Numbers vary run to run; take the trend.)\n");
    return 0;
}
