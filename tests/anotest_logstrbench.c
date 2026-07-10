/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Logger x strings stress benchmark: a fuckton of anostr_t values through
 * ano_log("... %.*s ...", anostr_fmt(s)) -- the deferred-capture path copying UTF-8
 * item names (Latin, accents, Cyrillic, Greek, kana, Han, Runic) into the ring under
 * multi-producer fire. Per-call enqueue latency percentiles like anotest_logtail, plus
 * two oracles that make it a correctness test as well as a benchmark:
 *   - no loss: output lines == records enqueued (the ring's contract);
 *   - byte transparency: a sentinel line's exact UTF-8 bytes (あの Bjørn's Agda Gun)
 *     appear in the file untouched -- capture honors %.*s precision on bytes that are
 *     NOT NUL-terminated (inline anostr_t) and the drain renders them verbatim.
 * Deterministic (fixed seed). argv[1] overrides messages-per-producer.
 * Built so it cannot rot, DISABLED in ctest -- run ./anotest_logstrbench by hand from
 * a -O3 build (build.bat 7). Prints a table; exits nonzero only if an oracle breaks. */

#include <anoptic_logging.h>
#include <anoptic_memory.h>
#include <anoptic_strings_utf.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include "templates/bench.h"
#include "templates/rng.h"
#include "templates/scratch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_DIR ANO_TEST_OUTDIR "/anolog_str"

#define DEFAULT_MSGS 50000      // per producer per point
#define MAXP         8
static const int POINTS[] = {1, 4, 8};
#define NPOINTS      (int)(sizeof POINTS / sizeof POINTS[0])

#define POOL_N   1024           // distinct names, mixed scripts
#define SENTINEL "\xE3\x81\x82\xE3\x81\xAE Bj\xC3\xB8rn's Agda Gun"

static int g_msgs = DEFAULT_MSGS;
static anostr_t g_pool[POOL_N];

/* Tick source: calibrated rdtsc on x86-64 (QPC's 100ns grain would swallow the ~40ns
   fast path), ano_timestamp_ticks elsewhere. Same scheme as anotest_logtail. */
#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
static inline uint64_t tick_now(void) { return __rdtsc(); }
static double g_nsPerTick = 1.0;
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

// The same inventory-name shapes the sort benchmark uses, heavy on non-ASCII.
static const char *base_ascii[] = {
    "Sword", "Shield", "Potion", "Scroll", "Amulet", "Ring", "Helm", "Bow",
};
static const char *base_other[] = {
    "\xC3\x89p\xC3\xA9""e", "M\xC3\xBCller", "\xC3\x85sgard",
    "\xD0\x9C\xD0\xB5\xD1\x87", "\xCE\x9E\xCE\xAF\xCF\x86\xCE\xBF\xCF\x82",
    "\xE3\x81\x8B\xE3\x81\x9F\xE3\x81\xAA", "\xE5\x89\xA3",
    "\xE1\x9A\xA6" "Runeblade\xE1\x9A\xA6",
};
static const char *mods[] = {
    "Healing", "Might", "Frost", "Flame", "Shadows", "the Bear", "Storms", "Glory",
};

static void pool_init(mi_heap_t *heap)
{
    test_rng rng = rng_make(0x106571Fu);
    char buf[96];
    for (size_t k = 0; k < POOL_N; k++) {
        if (rng_below(&rng, 2) == 0)
            snprintf(buf, sizeof buf, "%s of %s",
                     base_ascii[rng_below(&rng, sizeof base_ascii / sizeof base_ascii[0])],
                     mods[rng_below(&rng, sizeof mods / sizeof mods[0])]);
        else
            snprintf(buf, sizeof buf, "%s %s",
                     base_other[rng_below(&rng, sizeof base_other / sizeof base_other[0])],
                     mods[rng_below(&rng, sizeof mods / sizeof mods[0])]);
        g_pool[k] = anostr_from_cstr(heap, buf);
    }
}

typedef struct {
    int       id, count;
    bench_lat lat;
} prod_arg;

static void *producer(void *p)
{
    prod_arg *a = p;
    test_rng rng = rng_make(0xB10CE500u ^ (uint32_t)a->id);
    for (int i = 0; i < a->count; i++) {
        anostr_t name = g_pool[rng_below(&rng, POOL_N)];
        uint64_t t0 = tick_now();
        ano_log(ANO_INFO, "loot: %.*s x%d", anostr_fmt(name), i & 63);
        bench_lat_add(&a->lat, tick_now() - t0);
    }
    return NULL;
}

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
    ano_log_flush();

    bench_lat merged;
    bench_lat_init(&merged, buf, (size_t)producers * (size_t)g_msgs);
    merged.n = (size_t)producers * (size_t)g_msgs;
    for (int i = 0; i < producers; i++)
        merged.lost += arg[i].lat.lost;
    return bench_lat_stats_with(&merged, tick_to_ns);
}

// Whole-file scan for the sentinel's exact UTF-8 bytes. Byte transparency oracle.
static bool file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *all = malloc((size_t)sz + 1);
    bool found = false;
    if (all != NULL && fread(all, 1, (size_t)sz, f) == (size_t)sz) {
        size_t nl = strlen(needle);
        for (long i = 0; i + (long)nl <= sz && !found; i++)
            found = memcmp(all + i, needle, nl) == 0;
    }
    free(all);
    fclose(f);
    return found;
}

int main(int argc, char **argv)
{
    scratch_anchor_to_exe();
    if (argc > 1) {
        int v = atoi(argv[1]);
        if (v > 0) g_msgs = v;
    }
    tick_calibrate();

    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap == NULL) { printf("FAIL: mi_heap_new\n"); return 1; }
    pool_init(heap);

    uint64_t *buf = malloc((size_t)MAXP * (size_t)g_msgs * sizeof *buf);
    if (buf == NULL) { fprintf(stderr, "sample buffer allocation failed\n"); return 0; }

    printf("Logger x strings: %%.*s-captured UTF-8 item names, %d msgs/producer\n\n", g_msgs);

    scratch_make_dir(OUT_DIR);
    ano_log_init();
    ano_log_output_dir(OUT_DIR);

    uint64_t expected = 0;
    for (int i = 0; i < 256; i++) {   // warm caches, branch predictors, the ring
        ano_log(ANO_INFO, "warm %.*s", anostr_fmt(g_pool[i % POOL_N]));
        expected++;
    }
    ano_log_flush();

    bench_lat_header();
    for (int p = 0; p < NPOINTS; p++) {
        bench_stats s = run_point(POINTS[p], buf);
        expected += (uint64_t)POINTS[p] * (uint64_t)g_msgs;
        char label[48];
        snprintf(label, sizeof label, "log anostr @ %d producer%s", POINTS[p],
                 POINTS[p] == 1 ? "" : "s");
        bench_lat_row(label, s);
    }

    // The sentinel: an inline-unfriendly mixed-script name (29 bytes, heap-backed) and
    // a 12-byte inline value whose bytes are NOT NUL-terminated -- both must land
    // byte-exact.
    anostr_t sentinel = anostr_lit(SENTINEL);
    anostr_t inl = anostr_lit("inline-12b!!");
    ano_log(ANO_INFO, "sentinel: %.*s / %.*s", anostr_fmt(sentinel), anostr_fmt(inl));
    expected++;

    ano_log_cleanup();  // final drain + close, so the file below is complete

    int failures = 0;
    char outLog[96];    // the logger's file is session-stamped: <dir>/<stamp>_ano.log
    snprintf(outLog, sizeof outLog, "%s/%s_ano.log", OUT_DIR, ano_fs_session_stamp());
    uint64_t lines = scratch_count_lines(outLog);
    if (lines != expected) {
        printf("ORACLE BROKEN: %llu lines in file, %llu records enqueued\n",
               (unsigned long long)lines, (unsigned long long)expected);
        failures++;
    } else {
        printf("\nno-loss oracle: %llu lines == %llu records\n",
               (unsigned long long)lines, (unsigned long long)expected);
    }
    if (!file_contains(outLog, "sentinel: " SENTINEL " / inline-12b!!")) {
        printf("ORACLE BROKEN: sentinel UTF-8 bytes corrupted or missing\n");
        failures++;
    } else {
        printf("byte-transparency oracle: sentinel UTF-8 intact in the file\n");
    }

    remove(outLog);
    scratch_remove_dir(OUT_DIR);
    free(buf);
    return failures == 0 ? 0 : 1;
}
