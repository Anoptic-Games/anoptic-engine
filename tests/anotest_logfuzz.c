/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Fuzzer for lock-free MPSC logger. Producers hammer well-typed ano_log_write; flusher drains randomly.
//   1) CONTENT: "%s" + randstr (span/wrap/full-ring). Random level, occasional NOW, output-dir swap.
//      Routes FILE-bound for line-count oracle.
//   2) FORMATTER: fixed (literal-fmt, typed-arg) templates only. Never random fmt + random args.
// Invariant: drop nothing. One record == one line. Final line count == enqueued.
// Deterministic. argv[1] = per-thread iters. TSan suppressions: mimalloc abandon/teardown only.

#include <anoptic_log.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include "templates/rng.h"      // per-thread deterministic xorshift
#include "templates/scratch.h"  // scratch dirs + line-count oracle

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Suppress mimalloc teardown frames only (logger races stay visible).
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
const char *__tsan_default_suppressions(void);
const char *__tsan_default_suppressions(void)
{
    return "race:_mi_page_abandon\n"
           "race:_mi_heap_collect_abandon\n"
           "race:mi_heap_collect_ex\n"
           "race:_mi_thread_done\n"
           "race:mi_pthread_done\n";
}
#  endif
#endif

// ANO_TEST_OUTDIR = test build tree (fallback "." in scratch.h).
#define DIR_A      ANO_TEST_OUTDIR "/anolog_fuzz"
#define DIR_B      ANO_TEST_OUTDIR "/anolog_fuzz_alt"
// Session-stamped paths, resolved in main().
static char PATH_A[96], PATH_B[96];

#define PRODUCERS      6
#define DEFAULT_ITERS  4000     // per producer
#define MAX_CONTENT    600      // > one ring entry, < message cap

// Drop-nothing oracle: total enqueued.
static _Atomic uint64_t g_enqueued;
static _Atomic int      g_worker_fail;
static _Atomic bool     g_stop;
static int              g_iters = DEFAULT_ITERS;

// One deferred-formatter enqueue via fixed (literal-fmt, typed-arg) template.
static void formatter_case(test_rng *s)
{
    int      i  = (int)rng_next(s) - (int)0x40000000; // full signed range
    unsigned u  = rng_next(s);
    long long ll = ((long long)rng_next(s) << 31) ^ rng_next(s);
    unsigned long long ull = ((unsigned long long)rng_next(s) << 32) | rng_next(s);
    int      w  = (int)rng_below(s, 12);              // width 0..11
    int      pr = (int)rng_below(s, 8);               // precision 0..7
    double   d  = (double)(int)rng_next(s) / (double)(1 + rng_below(s, 1000));
    int      ch = 0x21 + (int)rng_below(s, 0x5d);     // printable char arg
    static const char *strs[] = { "alpha", "", "x", "spanning-sample", "0xZZ" };
    const char *sv = strs[rng_below(s, sizeof strs / sizeof strs[0])];

    switch (rng_below(s, 18)) {
    case 0:  ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt d=%d", i); break;
    case 1:  ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt u=%u", u); break;
    case 2:  ano_log_write(ANO_WARN,  0, __FILE_NAME__, __LINE__, "fmt x=%x", u); break;
    case 3:  ano_log_write(ANO_WARN,  0, __FILE_NAME__, __LINE__, "fmt X=%X", u); break;
    case 4:  ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt o=%o", u); break;
    case 5:  ano_log_write(ANO_ERROR, 0, __FILE_NAME__, __LINE__, "fmt lld=%lld", ll); break;
    case 6:  ano_log_write(ANO_ERROR, 0, __FILE_NAME__, __LINE__, "fmt llu=%llu", ull); break;
    case 7:  ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt f=%.3f", d); break;
    case 8:  ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt e=%e", d); break;
    case 9:  ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt g=%g", d); break;
    case 10: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt c=%c", ch); break;
    case 11: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt s=[%s]", sv); break;
    case 12: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt wd=[%*d]", w, i); break;
    case 13: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt pf=[%.*f]", pr, d); break;
    case 14: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt wpf=[%*.*f]", w, pr, d); break;
    case 15: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt mix=%d/%s/%x", i, sv, u); break;
    case 16: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt zpd=[%05d]", i); break;
    case 17: ano_log_write(ANO_INFO,  0, __FILE_NAME__, __LINE__, "fmt ws=[%-8.3s]", sv); break;
    }
}

static const ano_loglevel_t LEVELS[] = { ANO_INFO, ANO_WARN, ANO_ERROR };

static void *producer(void *arg)
{
    // Deterministic per-thread seed, no shared RNG state.
    test_rng s = rng_make(0x1000 + (uint32_t)(intptr_t)arg * 2654435761u);
    char content[MAX_CONTENT + 1];
    uint64_t local = 0;

    for (int it = 0; it < g_iters && !atomic_load_explicit(&g_stop, memory_order_relaxed); it++) {
        uint32_t pick = rng_below(&s, 100);
        ano_loglevel_t lvl = LEVELS[rng_below(&s, sizeof LEVELS / sizeof LEVELS[0])];

        if (pick < 2) {
            // Occasional output-dir swap between two valid dirs. Records still all land in one of the
            // two files we sum, so the oracle holds. This call itself enqueues nothing.
            ano_log_output_dir((rng_next(&s) & 1) ? DIR_A : DIR_B);
        } else if (pick < 5) {
            // Occasional NOW route. FILE named explicitly, so one file line and no echo.
            rng_fill_printable(&s, content, 1, MAX_CONTENT);
            ano_log_write(lvl, ANO_NOW | ANO_FILE, __FILE_NAME__, __LINE__, "%s", content);
            local++;
        } else if (pick < 45) {
            // Deferred formatter via fixed literal + typed args.
            formatter_case(&s);
            local++;
        } else {
            // Variable-length random CONTENT, logged safely as "%s".
            rng_fill_printable(&s, content, 1, MAX_CONTENT);
            if (ano_log_write(lvl, 0, __FILE_NAME__, __LINE__, "%s", content) < 0)
                atomic_fetch_add(&g_worker_fail, 1);   // never expected: 0 or 1 only
            local++;
        }
    }
    atomic_fetch_add_explicit(&g_enqueued, local, memory_order_relaxed);
    return NULL;
}

static void *flusher(void *arg)
{
    test_rng s = rng_make(0xBEEF ^ (uint32_t)(intptr_t)arg);
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        ano_log_flush();
        ano_sleep(50 + rng_below(&s, 400));   // short random interval, 50..449 us
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0) g_iters = v;
    }
    scratch_anchor_to_exe();   // scratch relative to this exe's dir, cross-platform
    scratch_make_dir(DIR_A);
    scratch_make_dir(DIR_B);
    snprintf(PATH_A, sizeof PATH_A, "%s/%s_ano.log", DIR_A, ano_fs_session_stamp());
    snprintf(PATH_B, sizeof PATH_B, "%s/%s_ano.log", DIR_B, ano_fs_session_stamp());
    remove(PATH_A);
    remove(PATH_B);

    if (ano_log_init() != 0) {
        fprintf(stderr, "logfuzz: ano_log_init failed\n");
        return 1;
    }
    ano_log_set_level(ANO_INFO);    // gate open: nothing dropped by severity, counts stay exact
    ano_log_output_dir(DIR_A);

    atomic_store(&g_enqueued, 0);
    atomic_store(&g_worker_fail, 0);
    atomic_store(&g_stop, false);

    anothread_t prod[PRODUCERS], flush;
    ano_thread_create(&flush, NULL, flusher, NULL);
    for (intptr_t i = 0; i < PRODUCERS; i++) {
        if (ano_thread_create(&prod[i], NULL, producer, (void *)i) != 0) {
            fprintf(stderr, "logfuzz: thread create failed\n");
            return 1;
        }
    }

    for (int i = 0; i < PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);

    // Producers stopped: stop flusher, final drain, then read back.
    atomic_store(&g_stop, true);
    ano_thread_join(flush, NULL);
    ano_log_flush();

    // Point output away so cleanup's final drain touches neither file.
    uint64_t enq = atomic_load(&g_enqueued);
    int wfail = atomic_load(&g_worker_fail);

    ano_log_cleanup();

    uint64_t lines = scratch_count_lines(PATH_A) + scratch_count_lines(PATH_B);

    printf("logfuzz: producers=%d iters=%d enqueued=%llu lines=%llu\n",
           PRODUCERS, g_iters, (unsigned long long)enq, (unsigned long long)lines);

    // Drop the test files and directories.
    remove(PATH_A);
    remove(PATH_B);
    scratch_remove_dir(DIR_A);
    scratch_remove_dir(DIR_B);

    if (wfail != 0) {
        fprintf(stderr, "logfuzz: FAIL: %d enqueue call(s) returned an unexpected error code\n", wfail);
        return 1;
    }
    if (lines != enq) {
        fprintf(stderr,
                "logfuzz: FAIL: line count %llu != enqueued %llu (logger dropped or duplicated %lld record(s))\n",
                (unsigned long long)lines, (unsigned long long)enq, (long long)lines - (long long)enq);
        return 1;
    }

    printf("logfuzz: PASS: every enqueued record survived (no loss, no duplication)\n");
    return 0;
}
