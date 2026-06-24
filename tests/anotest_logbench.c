/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Optional logger benchmark: the lock-free MPSC ring (anoptic_core) vs the preserved mutex baseline
// (logging_old.c, namespaced mtxlog_*). Built so it cannot rot, but DISABLED in CTest like
// anotest_chariots -- run ./anotest_logbench by hand. It is a benchmark, not a pass/fail test: it
// always exits 0 and prints a table.
//
// Two measurements, each run for both implementations:
//   1. Single-thread enqueue latency -- bursts sized to fit the buffer (no drain in the timed
//      region), so it isolates the producer fast path (format + reserve + copy + publish vs
//      format + lock + memcpy + unlock). Uncontended, so the two look similar by design.
//   2. Multi-thread throughput -- P producers hammer enqueue while ONE flusher thread drains
//      concurrently (the real single-consumer deployment). This is where the lock-free ring should
//      pull ahead: producers never serialize on a shared mutex.

#include <anoptic_logger.h>          // ring logger (ano_log_*)
#include "logging/logging_old.h"     // mutex baseline (mtxlog_*)
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
static void make_dir(const char *p) { _mkdir(p); }
#else
#include <sys/stat.h>
static void make_dir(const char *p) { mkdir(p, 0777); }
#endif

#define BENCH_DIR   "anolog_bench"

#define LAT_BURST   512      // enqueues per timed round; must fit both buffers with no drain
#define LAT_ROUNDS  4000     // timed rounds -> ~2M enqueues per implementation
#define TP_MSGS     200000   // messages per producer thread
#define MAXP        16       // largest producer-thread count measured (@16 oversubscribes an 8-core box: the stress point)
static const int TP_THREADS[] = {1, 2, 4, 8, 16};
#define TP_POINTS   (int)(sizeof TP_THREADS / sizeof TP_THREADS[0])

#define VAR_MSGS    100000   // variable-length messages per producer thread
#define VAR_MIN     8        // smallest random message body (bytes)
#define VAR_MAX     1024     // largest random message body (bytes)
#define VAR_SEED    0xA0B1C2u // fixed srand seed for reproducible variable-length runs
#define MIX_MSGS    100000   // mixed small+large messages per producer thread
#define MIX_LARGE   2048     // large-message body for the mixed battery (bytes)
#define MIX_SMALL   16       // small-message body for the mixed battery (bytes)
#define MIX_SEED    0x5E3D17u // fixed srand seed for the mixed battery

// The two implementations behind one vtable. ano_log_* and mtxlog_* share these signatures, so the
// benchmark drives both through identical call sites.
typedef struct {
    const char *name;
    int  (*init)(void);
    int  (*enqueue)(log_types_t, const char *, int, const char *, ...);
    void (*flush)(void);
    int  (*cleanup)(void);
    int  (*output_dir)(const char *);
} logger_api;

static const logger_api RING  = {
    "ring  (lock-free MPSC)", ano_log_init, ano_log_enqueue, ano_log_flush, ano_log_cleanup, ano_log_output_dir
};
static const logger_api MUTEX = {
    "mutex (baseline)",       mtxlog_init,  mtxlog_enqueue,  mtxlog_flush,  mtxlog_cleanup,  mtxlog_output_dir
};


/* Single-thread enqueue latency (ns per enqueue, fast path only) */

static double run_latency(const logger_api *api)
{
    for (int i = 0; i < 256; i++)                       // warm caches + branch predictors
        api->enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "warm %d", i);
    api->flush();

    uint64_t total = 0;
    long     ops   = 0;
    for (int r = 0; r < LAT_ROUNDS; r++) {
        uint64_t start = ano_timestamp_raw();
        for (int i = 0; i < LAT_BURST; i++)            // burst fits the buffer: pure enqueue, no drain
            api->enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "bench message number %d payload", i);
        total += ano_timestamp_raw() - start;
        ops   += LAT_BURST;
        api->flush();                                  // untimed: empty the buffer for the next round
    }
    return (double)total / (double)ops;                // ns / enqueue
}


/* Multi-thread throughput (messages/sec, with one concurrent flusher) */

static _Atomic bool g_flusher_stop;

typedef struct { const logger_api *api; int count; int id; } prod_arg;
typedef struct { const logger_api *api; } flush_arg;

static void *producer(void *p)
{
    prod_arg *a = p;
    for (int i = 0; i < a->count; i++)
        a->api->enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "thread %d message %d with payload", a->id, i);
    return NULL;
}

// Single consumer: exactly one flusher thread drains on a tight-ish timer while producers run, so
// neither buffer wedges at full and the measurement reflects the enqueue path, not disk speed.
static void *flusher(void *p)
{
    const logger_api *api = ((flush_arg *)p)->api;
    while (!atomic_load(&g_flusher_stop)) {
        api->flush();
        ano_sleep(200);   // 0.2 ms between drain passes
    }
    return NULL;
}

static double run_throughput(const logger_api *api, int producers)
{
    atomic_store(&g_flusher_stop, false);
    anothread_t fl;
    flush_arg fa = { api };
    ano_thread_create(&fl, NULL, flusher, &fa);

    anothread_t prod[MAXP];
    prod_arg    args[MAXP];

    uint64_t t0 = ano_timestamp_raw();
    for (int i = 0; i < producers; i++) {
        args[i] = (prod_arg){ api, TP_MSGS, i };
        ano_thread_create(&prod[i], NULL, producer, &args[i]);
    }
    for (int i = 0; i < producers; i++)
        ano_thread_join(prod[i], NULL);
    uint64_t t1 = ano_timestamp_raw();

    atomic_store(&g_flusher_stop, true);
    ano_thread_join(fl, NULL);
    api->flush();   // drain the tail

    double secs = (double)(t1 - t0) / 1e9;
    return (double)producers * (double)TP_MSGS / secs;   // messages / sec
}


/* Variable-length throughput (random length 8..1024B, random ASCII content)
   Stresses ring spanning / wrapping / full. Content is built as a NUL-terminated string and logged
   as ("%s", buf) -- the format is a literal, so no varargs/format mismatch is ever possible.
   rand() is seeded once per battery (VAR_SEED) for reproducibility. */

static int g_var_msgs;   // per-thread message count for the active variable-length battery

static char rand_ascii(void)                              // printable ASCII, no embedded NUL
{
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,-_";
    return alphabet[rand() % (int)(sizeof alphabet - 1)];
}

static void *var_producer(void *p)
{
    prod_arg *a = p;
    char buf[VAR_MAX + 1];
    for (int i = 0; i < a->count; i++) {
        int len = VAR_MIN + rand() % (VAR_MAX - VAR_MIN + 1);
        for (int j = 0; j < len; j++)
            buf[j] = rand_ascii();
        buf[len] = '\0';
        a->api->enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "%s", buf);   // literal fmt, random content
    }
    return NULL;
}

// Mixed small+large: each message is randomly tiny (MIX_SMALL) or large (MIX_LARGE), 50/50. Forces
// the ring to interleave records that span very different fractions of the buffer.
static void *mix_producer(void *p)
{
    prod_arg *a = p;
    char buf[MIX_LARGE + 1];
    for (int i = 0; i < a->count; i++) {
        int len = (rand() & 1) ? MIX_LARGE : MIX_SMALL;
        for (int j = 0; j < len; j++)
            buf[j] = rand_ascii();
        buf[len] = '\0';
        a->api->enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "%s", buf);   // literal fmt, random content
    }
    return NULL;
}

// Shared runner for the random-content batteries: same one-flusher topology as run_throughput, but
// drives a caller-supplied producer fn over g_var_msgs messages.
static double run_var_throughput(const logger_api *api, int producers,
                                 void *(*prod_fn)(void *), unsigned seed)
{
    srand(seed);                       // reproducible content stream (producers spawned serially below)

    atomic_store(&g_flusher_stop, false);
    anothread_t fl;
    flush_arg fa = { api };
    ano_thread_create(&fl, NULL, flusher, &fa);

    anothread_t prod[MAXP];
    prod_arg    args[MAXP];

    uint64_t t0 = ano_timestamp_raw();
    for (int i = 0; i < producers; i++) {
        args[i] = (prod_arg){ api, g_var_msgs, i };
        ano_thread_create(&prod[i], NULL, prod_fn, &args[i]);
    }
    for (int i = 0; i < producers; i++)
        ano_thread_join(prod[i], NULL);
    uint64_t t1 = ano_timestamp_raw();

    atomic_store(&g_flusher_stop, true);
    ano_thread_join(fl, NULL);
    api->flush();

    double secs = (double)(t1 - t0) / 1e9;
    return (double)producers * (double)g_var_msgs / secs;   // messages / sec
}


/* Driver */

typedef struct {
    double latency_ns;
    double throughput[TP_POINTS];
    double var_throughput[TP_POINTS];   // variable-length 8..1024B battery
    double mix_throughput[TP_POINTS];   // mixed small+large battery
} result;

static result measure(const logger_api *api)
{
    api->init();
    api->output_dir(BENCH_DIR);     // redirect off the default game-dir file

    result r;
    r.latency_ns = run_latency(api);
    for (int i = 0; i < TP_POINTS; i++)
        r.throughput[i] = run_throughput(api, TP_THREADS[i]);

    g_var_msgs = VAR_MSGS;
    for (int i = 0; i < TP_POINTS; i++)
        r.var_throughput[i] = run_var_throughput(api, TP_THREADS[i], var_producer, VAR_SEED);

    g_var_msgs = MIX_MSGS;
    for (int i = 0; i < TP_POINTS; i++)
        r.mix_throughput[i] = run_var_throughput(api, TP_THREADS[i], mix_producer, MIX_SEED);

    api->cleanup();
    return r;
}

int main(void)
{
    make_dir(BENCH_DIR);

    printf("Anoptic logger benchmark -- ring (lock-free MPSC) vs mutex baseline\n");
    printf("  latency:    %d enqueues x %d rounds, single thread, fast path\n", LAT_BURST, LAT_ROUNDS);
    printf("  throughput: %d msgs/producer, one concurrent flusher\n\n", TP_MSGS);

    result ring  = measure(&RING);
    result mutex = measure(&MUTEX);

    printf("%-26s %14s %14s %10s\n", "metric", "ring", "mutex", "ring/mutex");
    printf("-------------------------------------------------------------------------\n");
    printf("%-26s %12.1f ns %12.1f ns %9.2fx\n", "enqueue latency (1 thread)",
           ring.latency_ns, mutex.latency_ns,
           mutex.latency_ns / ring.latency_ns);                 // >1 = ring faster per call
    for (int i = 0; i < TP_POINTS; i++) {
        char label[40];
        snprintf(label, sizeof label, "throughput @ %d producer%s", TP_THREADS[i],
                 TP_THREADS[i] == 1 ? "" : "s");
        printf("%-26s %9.2f M/s %11.2f M/s %9.2fx\n", label,
               ring.throughput[i] / 1e6, mutex.throughput[i] / 1e6,
               ring.throughput[i] / mutex.throughput[i]);       // >1 = ring faster
    }

    // Variable-length battery: random length VAR_MIN..VAR_MAX, random ASCII, logged as ("%s", buf).
    printf("\nvariable-length messages -- %d msgs/producer, random %d..%d B, seed 0x%X\n",
           VAR_MSGS, VAR_MIN, VAR_MAX, VAR_SEED);
    printf("-------------------------------------------------------------------------\n");
    for (int i = 0; i < TP_POINTS; i++) {
        char label[40];
        snprintf(label, sizeof label, "varlen @ %d producer%s", TP_THREADS[i],
                 TP_THREADS[i] == 1 ? "" : "s");
        printf("%-26s %9.2f M/s %11.2f M/s %9.2fx\n", label,
               ring.var_throughput[i] / 1e6, mutex.var_throughput[i] / 1e6,
               ring.var_throughput[i] / mutex.var_throughput[i]);
    }

    // Mixed battery: 50/50 tiny vs large records interleaved in the same ring.
    printf("\nmixed small+large messages -- %d msgs/producer, %dB / %dB 50:50, seed 0x%X\n",
           MIX_MSGS, MIX_SMALL, MIX_LARGE, MIX_SEED);
    printf("-------------------------------------------------------------------------\n");
    for (int i = 0; i < TP_POINTS; i++) {
        char label[40];
        snprintf(label, sizeof label, "mixed @ %d producer%s", TP_THREADS[i],
                 TP_THREADS[i] == 1 ? "" : "s");
        printf("%-26s %9.2f M/s %11.2f M/s %9.2fx\n", label,
               ring.mix_throughput[i] / 1e6, mutex.mix_throughput[i] / 1e6,
               ring.mix_throughput[i] / mutex.mix_throughput[i]);
    }

    printf("\n(ring/mutex > 1.0 means the ring won. Latency column inverts the ratio so >1 is\n");
    printf(" always \"ring better\". Numbers vary run to run; take the trend, not the digits.)\n");

    // Tidy the throwaway files.
    remove(BENCH_DIR "/anoptic.log");
    remove(BENCH_DIR "/anoptic_mtx.log");
    return 0;
}
