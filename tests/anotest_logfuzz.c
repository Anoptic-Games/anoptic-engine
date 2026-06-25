/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Standalone fuzzer for the lock-free MPSC logger. Many producer threads hammer ano_log_* with
// randomized but ALWAYS well-typed input while a flusher thread drains on short random intervals.
// Two streams of input share one harness:
//   1) variable-length CONTENT logged safely as ano_log_enqueue(level, file, line, "%s", randstr),
//      lengths spanning a few bytes up to past one ring entry (exercises spanning, wrap, full-ring
//      wait). Random level, occasional ano_log_immediate, occasional ano_log_output_dir between two
//      valid dirs.
//   2) the deferred FORMATTER, fuzzed by a fixed table of literal format strings each paired with
//      correctly-typed randomized args. A random fmt is NEVER paired with random args -- only fixed
//      (literal-fmt, typed-arg) templates, chosen at random per call.
//
// The one invariant: the logger drops nothing. Every record each producer enqueues becomes exactly
// one output line (random content forbids '\n' and '\0', so one record == one line). After stopping
// all producers and a final flush, the summed line count across both output files must equal the
// total enqueued. Mismatch -> non-zero exit with a clear message.
//
// Deterministic by default: fixed srand seed, fixed thread count, a few thousand iterations each, a
// few seconds wall. argv[1] overrides the per-thread iteration count for a longer soak.
// The harness's own cross-thread state is all atomic. The output-dir swap closes the old file handle
// (mi_free) on a producer thread; TSan sees that cross-thread free race with mimalloc's thread-local
// heap teardown when another producer exits. That race lives inside mimalloc's abandoned-page protocol,
// safe by its design but opaque to TSan, not in the logger or this harness, so the teardown frames are
// suppressed below.

#include <anoptic_logging.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
static void make_dir(const char *p) { _mkdir(p); }
static void remove_dir(const char *p) { _rmdir(p); }
#else
#include <sys/stat.h>
#include <unistd.h>
static void make_dir(const char *p) { mkdir(p, 0777); }
static void remove_dir(const char *p) { rmdir(p); }
#endif

// Suppress only mimalloc's thread-teardown frames (see the file banner). Matching either side of a race
// suppresses the report, and the logger's own logic never goes through these, so its races stay visible.
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

// CMake points ANO_TEST_OUTDIR at this test's build tree, so scratch dirs never land in the caller's CWD.
#ifndef ANO_TEST_OUTDIR
#define ANO_TEST_OUTDIR "."
#endif
#define DIR_A      ANO_TEST_OUTDIR "/anolog_fuzz"
#define PATH_A     ANO_TEST_OUTDIR "/anolog_fuzz/anoptic.log"
#define DIR_B      ANO_TEST_OUTDIR "/anolog_fuzz_alt"
#define PATH_B     ANO_TEST_OUTDIR "/anolog_fuzz_alt/anoptic.log"

#define PRODUCERS      6
#define DEFAULT_ITERS  4000     // per producer; ~few seconds, overflows the ring repeatedly
#define MAX_CONTENT    600      // > one ring entry (64/128B line) to force spanning/wrap, < message cap

// Total records actually enqueued, summed across all producers. The drop-nothing oracle.
static _Atomic uint64_t g_enqueued;
static _Atomic int      g_worker_fail;
static _Atomic bool     g_stop;
static int              g_iters = DEFAULT_ITERS;

// Per-thread xorshift, no shared RNG state (rand() is not thread-safe). Seeded deterministically.
static inline uint32_t xs(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (*s = x);
}

// Build a NUL-terminated random string of length [1,MAX_CONTENT] from printable bytes only, never
// '\n' or '\0' (so one record stays one line and strlen/line-count stay honest).
static size_t make_content(char *buf, uint32_t *s)
{
    size_t len = 1 + xs(s) % MAX_CONTENT;
    for (size_t i = 0; i < len; i++) {
        // printable ASCII 0x20..0x7e, excluding nothing dangerous (no '\n'/'\0' in that range).
        buf[i] = (char)(0x20 + xs(s) % (0x7e - 0x20 + 1));
    }
    buf[len] = '\0';
    return len;
}

// One enqueue through the deferred formatter, via a FIXED literal fmt + correctly-typed randomized
// args. Each case pairs a literal with args of exactly the right type -- never a random fmt. Returns
// nothing; the caller counts it. Pick is chosen at random per call.
static void formatter_case(uint32_t *s)
{
    int      i  = (int)xs(s) - (int)0x40000000; // full signed range
    unsigned u  = xs(s);
    long long ll = ((long long)xs(s) << 31) ^ xs(s);
    unsigned long long ull = ((unsigned long long)xs(s) << 32) | xs(s);
    int      w  = (int)(xs(s) % 12);            // width 0..11
    int      pr = (int)(xs(s) % 8);             // precision 0..7
    double   d  = (double)(int)xs(s) / (double)(1 + xs(s) % 1000);
    int      ch = 0x21 + (int)(xs(s) % 0x5d);         // printable char arg
    static const char *strs[] = { "alpha", "", "x", "spanning-sample", "0xZZ" };
    const char *sv = strs[xs(s) % (sizeof strs / sizeof strs[0])];

    switch (xs(s) % 18) {
    case 0:  ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt d=%d", i); break;
    case 1:  ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt u=%u", u); break;
    case 2:  ano_log_enqueue(LOG_WARN,  __FILE_NAME__, __LINE__, "fmt x=%x", u); break;
    case 3:  ano_log_enqueue(LOG_WARN,  __FILE_NAME__, __LINE__, "fmt X=%X", u); break;
    case 4:  ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt o=%o", u); break;
    case 5:  ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, "fmt lld=%lld", ll); break;
    case 6:  ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, "fmt llu=%llu", ull); break;
    case 7:  ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt f=%.3f", d); break;
    case 8:  ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt e=%e", d); break;
    case 9:  ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt g=%g", d); break;
    case 10: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt c=%c", ch); break;
    case 11: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt s=[%s]", sv); break;
    case 12: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt wd=[%*d]", w, i); break;
    case 13: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt pf=[%.*f]", pr, d); break;
    case 14: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt wpf=[%*.*f]", w, pr, d); break;
    case 15: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt mix=%d/%s/%x", i, sv, u); break;
    case 16: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt zpd=[%05d]", i); break;
    case 17: ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, "fmt ws=[%-8.3s]", sv); break;
    }
}

static const log_types_t LEVELS[] = { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

static void *producer(void *arg)
{
    uint32_t s = 0x1000 + (uint32_t)(intptr_t)arg * 2654435761u; // deterministic per-thread seed
    if (s == 0) s = 1;
    char content[MAX_CONTENT + 1];
    uint64_t local = 0;

    for (int it = 0; it < g_iters && !atomic_load_explicit(&g_stop, memory_order_relaxed); it++) {
        uint32_t pick = xs(&s) % 100;
        log_types_t lvl = LEVELS[xs(&s) % (sizeof LEVELS / sizeof LEVELS[0])];

        if (pick < 2) {
            // Occasional output-dir swap between two valid dirs. Records still all land in one of the
            // two files we sum, so the oracle holds. This call itself enqueues nothing.
            ano_log_output_dir((xs(&s) & 1) ? DIR_A : DIR_B);
        } else if (pick < 5) {
            // Occasional immediate (FATAL/sync path). emit_one writes exactly one file line.
            make_content(content, &s);
            ano_log_immediate(lvl, __FILE_NAME__, __LINE__, "%s", content);
            local++;
        } else if (pick < 45) {
            // Deferred formatter via fixed literal + typed args.
            formatter_case(&s);
            local++;
        } else {
            // Variable-length random CONTENT, logged safely as "%s".
            make_content(content, &s);
            if (ano_log_enqueue(lvl, __FILE_NAME__, __LINE__, "%s", content) < 0)
                atomic_fetch_add(&g_worker_fail, 1);   // never expected: 0 or 1 only
            local++;
        }
    }
    atomic_fetch_add_explicit(&g_enqueued, local, memory_order_relaxed);
    return NULL;
}

static void *flusher(void *arg)
{
    uint32_t s = 0xBEEF ^ (uint32_t)(intptr_t)arg;
    if (s == 0) s = 1;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        ano_log_flush();
        ano_sleep(50 + xs(&s) % 400);   // short random interval, 50..449 us
    }
    return NULL;
}

// Count '\n' in a file, 0 if absent/unreadable. The output appends exactly one '\n' per record.
static uint64_t count_lines(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    uint64_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n') n++;
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0) g_iters = v;
    }
    srand(1234567u);   // fixed seed: deterministic by default (unused beyond determinism intent)

    make_dir(DIR_A);
    make_dir(DIR_B);
    remove(PATH_A);
    remove(PATH_B);

    if (ano_log_init() != 0) {
        fprintf(stderr, "logfuzz: ano_log_init failed\n");
        return 1;
    }
    ano_log_set_level(LOG_DEBUG);   // gate open: nothing dropped by severity, counts stay exact
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

    // All producers stopped: stop the flusher, then a final synchronous drain before reading back.
    atomic_store(&g_stop, true);
    ano_thread_join(flush, NULL);
    ano_log_flush();

    // Point the output away from both files so cleanup's final drain (empty here) touches neither.
    uint64_t enq = atomic_load(&g_enqueued);
    int wfail = atomic_load(&g_worker_fail);

    ano_log_cleanup();

    uint64_t lines = count_lines(PATH_A) + count_lines(PATH_B);

    printf("logfuzz: producers=%d iters=%d enqueued=%llu lines=%llu\n",
           PRODUCERS, g_iters, (unsigned long long)enq, (unsigned long long)lines);

    // Counted above; drop the files and directories so a manual run leaves nothing behind.
    remove(PATH_A);
    remove(PATH_B);
    remove_dir(DIR_A);
    remove_dir(DIR_B);

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
