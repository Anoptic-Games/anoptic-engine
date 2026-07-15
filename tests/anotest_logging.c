/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Baseline + adversarial suite. Round-trip normal usage, then abuse the public API.
// Every case drains via ano_log_flush before readback. Final case leaves a human-readable log.

#include <anoptic_log.h>
#include <anoptic_strings.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>
#include <anoptic_filesystem.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#if defined(_WIN32)
#include <direct.h>
static void  make_dir(const char *p) { _mkdir(p); }
static void  remove_dir(const char *p) { _rmdir(p); }
static char *cwd_str(char *b, size_t n) { return _getcwd(b, (int)n); }
#else
#include <sys/stat.h>
#include <unistd.h>
static void  make_dir(const char *p) { mkdir(p, 0777); }
static void  remove_dir(const char *p) { rmdir(p); }
static char *cwd_str(char *b, size_t n) { return getcwd(b, n); }
#endif

// Scratch paths relative to CWD (main anchors via ano_fs_chdir_gamepath).
#ifndef ANO_TEST_OUTDIR
#define ANO_TEST_OUTDIR "."
#endif
#define LOG_DIR      ANO_TEST_OUTDIR "/anolog_test"
#define LOG_DIR_ALT  ANO_TEST_OUTDIR "/anolog_test_alt"
#define VIS_DIR      ANO_TEST_OUTDIR "/anolog_visible"

// Session-stamped paths (<dir>/<stamp>_ano.log), resolved in main().
static char LOG_PATH[96], LOG_PATH_ALT[96], VIS_PATH[96];

static void resolve_log_paths(void)
{
    snprintf(LOG_PATH,     sizeof LOG_PATH,     "%s/%s_ano.log", LOG_DIR,     ano_fs_session_stamp());
    snprintf(LOG_PATH_ALT, sizeof LOG_PATH_ALT, "%s/%s_ano.log", LOG_DIR_ALT, ano_fs_session_stamp());
    snprintf(VIS_PATH,     sizeof VIS_PATH,     "%s/%s_ano.log", VIS_DIR,     ano_fs_session_stamp());
}

#define THREAD_COUNT    4
#define MSGS_PER_THREAD 50

// Per-case fail flag (CHECK). 0 = pass.
static int g_fail;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "  FAIL: %s\n", (msg)); g_fail = 1; } } while (0)

static _Atomic int  g_worker_fail;
static _Atomic bool g_stop;


/* Helpers */

// Slurp whole file into NUL-terminated heap buffer. Caller frees. NULL if unreadable.
// Grow-until-EOF (not size-then-read): avoids stale short size on attribute-caching FS.
static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) { if (out_len) *out_len = 0; return NULL; }

    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) { fclose(f); if (out_len) *out_len = 0; return NULL; }

    for (;;) {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nbuf = realloc(buf, ncap);
            if (nbuf == NULL) { free(buf); fclose(f); if (out_len) *out_len = 0; return NULL; }
            buf = nbuf;
            cap = ncap;
        }
        size_t got = fread(buf + len, 1, cap - len - 1, f);
        len += got;
        if (got == 0) break;
    }

    if (ferror(f)) { free(buf); fclose(f); if (out_len) *out_len = 0; return NULL; }
    fclose(f);
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static int count_lines(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (*s == '\n') n++;
    return n;
}

// Longest line length (bytes between newlines).
static size_t longest_line(const char *s)
{
    size_t best = 0, cur = 0;
    for (; *s; s++) {
        if (*s == '\n') { if (cur > best) best = cur; cur = 0; }
        else cur++;
    }
    if (cur > best) best = cur;
    return best;
}

// Fresh output file: drop old, (re)point logger at LOG_DIR.
static void reset_output(void)
{
    remove(LOG_PATH);
    ano_log_output_dir(LOG_DIR);
}


/* Baseline cases */

static int test_roundtrip(void)
{
    g_fail = 0;
    reset_output();
    ano_log(ANO_INFO, "hello %d", 7);
    ano_log(ANO_WARN, "warn line");
    ano_log(ANO_ERROR, "err %s", "x");
    ano_olog(ANO_INFO, "origin line");
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "roundtrip: file readable");
    if (c) {
        CHECK(count_lines(c) == 4, "roundtrip: four lines");
        CHECK(strstr(c, "INFO") && strstr(c, "hello 7"), "roundtrip: info line");
        CHECK(strstr(c, "WARN") && strstr(c, "warn line"), "roundtrip: warn line");
        CHECK(strstr(c, "ERROR") && strstr(c, "err x"), "roundtrip: error line");
        int origins = 0;
        for (char *s = c; (s = strstr(s, "anotest_logging.c:")) != NULL; s++)
            origins++;
        CHECK(origins == 1, "roundtrip: only the olog line carries the file:line prefix");
        free(c);
    }
    return g_fail;
}

static int test_formatting(void)
{
    g_fail = 0;
    reset_output();
    ano_log(ANO_INFO, "int=%d str=%s hex=%x width=%5d", 42, "abc", 255, 3);
    ano_log_flush();

    char expect[256];
    snprintf(expect, sizeof expect, "int=%d str=%s hex=%x width=%5d", 42, "abc", 255, 3);

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "formatting: file readable");
    if (c) {
        CHECK(strstr(c, expect) != NULL, "formatting: body matches snprintf byte-for-byte");
        free(c);
    }
    return g_fail;
}

// Deferred path vs snprintf across the conversion matrix. Message must match snprintf byte-identical.
// Unique "Dnn:" prefix bounds each expected substring.
static int test_deferred_formatting(void)
{
    g_fail = 0;
    reset_output();

    char exp[64][256];
    int  ne = 0;
#define DCHK(...) do { \
        snprintf(exp[ne], sizeof exp[ne], __VA_ARGS__); \
        ano_log(ANO_INFO, __VA_ARGS__); \
        ne++; \
    } while (0)

    DCHK("D00:[%d]", -2147483647 - 1);     // INT_MIN
    DCHK("D01:[%5d]", 42);
    DCHK("D02:[%-5d]", 42);
    DCHK("D03:[%05d]", 42);
    DCHK("D04:[%+d]", 42);
    DCHK("D05:[% d]", 42);
    DCHK("D06:[%x]", 0xdeadbeefu);
    DCHK("D07:[%#x]", 255);
    DCHK("D08:[%X]", 0xabc);
    DCHK("D09:[%o]", 64);
    DCHK("D10:[%u]", 4000000000u);
    DCHK("D11:[%ld]", 9000000000L);
    DCHK("D12:[%lld]", -9000000000000LL);
    DCHK("D13:[%lu]", 18000000000UL);
    DCHK("D14:[%zu]", (size_t)123456);
    DCHK("D15:[%.3f]", 3.14159);
    DCHK("D16:[%10.3f]", 3.14159);
    DCHK("D17:[%-10.3f]", 3.14159);
    DCHK("D18:[%+.2f]", 2.5);
    DCHK("D19:[%e]", 12345.678);
    DCHK("D20:[%g]", 0.0001);
    DCHK("D21:[%c]", 'Q');
    DCHK("D22:[%5c]", 'Q');
    DCHK("D23:[%s]", "hello");
    DCHK("D24:[%-8s]", "hi");
    DCHK("D25:[%.3s]", "truncateme");
    DCHK("D26:[%*d]", 6, 42);              // width from arg
    DCHK("D27:[%.*f]", 2, 3.14159);        // precision from arg
    DCHK("D28:[%-*.*f]", 8, 2, 3.5);       // width and precision from args
    DCHK("D29:x=%%=%d", 7);                // literal percent plus an arg
    DCHK("D30:[%hhd]", 200);               // narrows to signed char in printf
    DCHK("D31:[%hu]", 70000);              // narrows to unsigned short
    DCHK("D32:multi %d and %s and %5.2f", 3, "mix", 1.5);
    DCHK("D33:[%d]", 0);                                   // zero, hand-rolled
    DCHK("D34:[%x]", 0);                                   // zero hex
    DCHK("D35:[%lld]", -9223372036854775807LL - 1);       // LLONG_MIN magnitude edge
    DCHK("D36:[%llu]", 18446744073709551615ULL);          // ULLONG_MAX
    DCHK("D37:[%u]", 4294967295u);                        // UINT_MAX
    DCHK("D38:[%lx]", 0xfeedfacecafeUL);                  // long hex, hand-rolled
    DCHK("D39:[%*d]", -5, 42);                            // negative '*' width = left justify
    DCHK("D40:[%#x]", 0);                                  // alt-form zero
    DCHK("D41:[%#o]", 0);
    DCHK("D42:[%.0d]", 0);                                 // precision 0 of zero = empty
    DCHK("D43:[%5.0d]", 0);                                // width with empty precision-0
    DCHK("D44:[%s]", "");                                  // empty string
    DCHK("D45:[%+.1f]", -0.0);                             // signed negative zero
    DCHK("D46:[%08.2f]", -3.5);                            // zero-pad, sign, width, precision
    DCHK("D47:[%jd]", (intmax_t)(-9223372036854775807LL - 1));  // INTMAX_MIN, j length, hand-rolled
    DCHK("D48:[%td]", (ptrdiff_t)-1);                      // t length, signed
    DCHK("D49:[%zu]", (size_t)0);                          // z length, zero
    DCHK("D50:[%llX]", 0xDEADBEEFCAFEULL);                 // uppercase 64-bit hex, hand-rolled
    DCHK("D51:[%o]", 8);                                   // octal, no '#' prefix
    DCHK("D52:[%c]", 0xFF);                                // high byte, raw
    DCHK("D53:[%.*d]", 5, 42);                             // '*' precision = zero-pad to 00042
    DCHK("D54:[%#.0o]", 0);                                // alt-form octal zero = "0"
    DCHK("D55:[%#.0x]", 0);                                // alt-form hex zero = "" (prefix suppressed)
    DCHK("D56:[%-*.*s]", 10, 3, "abcdef");                 // width + precision on string
    DCHK("D57:[%*d]", 0, 42);                              // '*' width 0 = no width

    // anostr_t via %.*s + anostr_fmt: capture reads exactly len bytes (inline not NUL-terminated).
    anostr_t sInl = anostr_lit("inline-val!!");                 // 12 B, inline, no NUL after
    anostr_t sLng = anostr_lit("a-string-longer-than-twelve");  // 27 B, long variant
    DCHK("D58:[%.*s]", anostr_fmt(sInl));
    DCHK("D59:[%.*s]", anostr_fmt(sLng));
    DCHK("D60:[%32.*s]", anostr_fmt(sLng));                     // width + '*' precision combined
    DCHK("D61:[%.*s]", anostr_fmt(anostr_empty()));
    DCHK("D62:[%.*s]", anostr_fmt(anostr_slice(sLng, 2, 8)));   // a sliced value logs too

#undef DCHK
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "deferred: file readable");
    if (c) {
        for (int i = 0; i < ne; i++) {
            if (strstr(c, exp[i]) == NULL) {
                fprintf(stderr, "  MISMATCH case %d: expected \"%s\"\n", i, exp[i]);
                g_fail = 1;
            }
        }
        CHECK(g_fail == 0, "deferred: every conversion matches snprintf byte-for-byte");
        free(c);
    }
    return g_fail;
}

static int test_accumulation_order(void)
{
    g_fail = 0;
    reset_output();
    for (int i = 0; i < 50; i++)
        ano_log(ANO_INFO, "seq %d", i);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "order: file readable");
    if (c) {
        CHECK(count_lines(c) == 50, "order: fifty lines");
        char *first = strstr(c, "seq 0");
        char *last = strstr(c, "seq 49");
        CHECK(first && last && first < last, "order: seq 0 precedes seq 49");
        free(c);
    }
    return g_fail;
}

static int test_level_gate(void)
{
    g_fail = 0;
    reset_output();
    ano_log_set_level(ANO_ERROR);
    ano_log(ANO_INFO, "gated info message");
    ano_log(ANO_ERROR, "passing error message");
    ano_log_flush();
    ano_log_set_level(ANO_INFO);

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "gate: file readable");
    if (c) {
        CHECK(strstr(c, "gated info message") == NULL, "gate: sub-threshold line suppressed");
        CHECK(strstr(c, "passing error message") != NULL, "gate: at-threshold line kept");
        free(c);
    }
    return g_fail;
}

// Concurrent flood past ring capacity. Producers take full path (return 1). Nothing lost.
// One producer cannot overflow a continuously-drained ring.
#define FULL_THREADS 6
#define FULL_PER     12000
#define FULL_FLOOD   (FULL_THREADS * FULL_PER)

static _Atomic int g_full_flushed;

static void *full_flooder(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < FULL_PER; i++)
        if (ano_log_write(ANO_INFO, 0, __FILE_NAME__, __LINE__, "flood t%d %d", id, i) == 1)
            atomic_fetch_add(&g_full_flushed, 1);
    return NULL;
}

static int test_full_ring(void)
{
    g_fail = 0;
    reset_output();
    atomic_store(&g_full_flushed, 0);

    anothread_t t[FULL_THREADS];
    for (intptr_t i = 0; i < FULL_THREADS; i++)
        ano_thread_create(&t[i], NULL, full_flooder, (void *)i);
    for (int i = 0; i < FULL_THREADS; i++)
        ano_thread_join(t[i], NULL);
    ano_log_flush();

    // Saturation is observation (TSan may keep up). Invariant: no loss.
    if (atomic_load(&g_full_flushed) == 0)
        printf("  NOTE: flood did not saturate the ring this run (consumer kept pace)\n");
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "full: file readable");
    if (c) {
        CHECK(count_lines(c) == FULL_FLOOD, "full: every record survives heavy concurrent flood");
        free(c);
    }
    return g_fail;
}

static int test_immediate_order(void)
{
    g_fail = 0;
    reset_output();
    ano_log(ANO_INFO, "buffered before immediate");
    ano_log_write(ANO_ERROR, ANO_NOW, __FILE_NAME__, __LINE__, "immediate %d", 99);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "immediate: file readable");
    if (c) {
        char *buffered = strstr(c, "buffered before immediate");
        char *immediate = strstr(c, "immediate 99");
        CHECK(buffered != NULL, "immediate: buffered record flushed");
        CHECK(immediate != NULL, "immediate: immediate record written");
        CHECK(buffered && immediate && buffered < immediate, "immediate: buffered precedes immediate");
        free(c);
    }
    return g_fail;
}

// Per-call routing. TERM-only stays out of file. File is the observable. Also flips INFO default.
static int test_routing(void)
{
    g_fail = 0;
    reset_output();
    ano_rlog(ANO_INFO, ANO_TERM, "route term only %d", 1);
    ano_rlog(ANO_INFO, ANO_BOTH, "route both %d", 2);
    ano_rlog(ANO_INFO, ANO_FILE, "route file only %d", 3);
    ano_rlog(ANO_WARN, ANO_NOW, "route now default sink %d", 4);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "routing: file readable");
    if (c) {
        CHECK(strstr(c, "route term only 1") == NULL, "routing: TERM-only absent from file");
        CHECK(strstr(c, "route both 2") != NULL, "routing: BOTH lands in file");
        CHECK(strstr(c, "route file only 3") != NULL, "routing: FILE lands in file");
        CHECK(strstr(c, "route now default sink 4") != NULL, "routing: NOW inherits FILE and lands");
        CHECK(count_lines(c) == 3, "routing: exactly three file records");
        free(c);
    }

    // Rebind INFO to TERM-only, log, restore. File must not grow.
    ano_log_set_route(ANO_INFO, ANO_TERM);
    ano_log(ANO_INFO, "rerouted info line");
    ano_log_flush();
    ano_log_set_route(ANO_INFO, ANO_FILE);
    c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL && strstr(c, "rerouted info line") == NULL,
          "routing: set_route override keeps INFO out of the file");
    free(c);
    return g_fail;
}

static int test_truncation(void)
{
    g_fail = 0;
    reset_output();
    char big[6000];
    memset(big, 'A', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    ano_log(ANO_INFO, "%s", big);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "truncation: file readable");
    if (c) {
        CHECK(longest_line(c) <= 4096, "truncation: oversize line clamped to message cap");
        free(c);
    }
    return g_fail;
}

static int test_empty_message(void)
{
    g_fail = 0;
    reset_output();
    ano_log(ANO_INFO, "%s", "");
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "empty: file readable");
    if (c) {
        CHECK(count_lines(c) == 1, "empty: one line emitted");
        CHECK(strstr(c, "INFO") != NULL, "empty: prefix present despite empty body");
        free(c);
    }
    return g_fail;
}

static void *worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < MSGS_PER_THREAD; i++)
        if (ano_log_write(ANO_ERROR, 0, __FILE_NAME__, __LINE__, "worker %d msg %d", id, i) != 0)
            atomic_fetch_add(&g_worker_fail, 1);
    return NULL;
}

static int test_concurrent(void)
{
    g_fail = 0;
    reset_output();
    atomic_store(&g_worker_fail, 0);

    anothread_t workers[THREAD_COUNT];
    for (intptr_t i = 0; i < THREAD_COUNT; i++)
        CHECK(ano_thread_create(&workers[i], NULL, worker, (void *)i) == 0, "concurrent: thread create");
    for (int i = 0; i < THREAD_COUNT; i++)
        ano_thread_join(workers[i], NULL);
    ano_log_flush();

    CHECK(atomic_load(&g_worker_fail) == 0, "concurrent: every enqueue accepted");
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "concurrent: file readable");
    if (c) {
        CHECK(count_lines(c) == THREAD_COUNT * MSGS_PER_THREAD, "concurrent: every record flushed");
        free(c);
    }
    return g_fail;
}


/* Contention 1 */

// Flush while writing, write while flushing.

#define C1_PRODUCERS 4
#define C1_PER       250
#define C1_FLUSHERS  2

static void *c1_producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < C1_PER; i++)
        ano_log_write(ANO_INFO, 0, __FILE_NAME__, __LINE__, "c1 p%d %d", id, i);
    return NULL;
}

static void *c1_flusher(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_stop)) {
        ano_log_flush();
        ano_sleep(100);   // 0.1 ms: hammer the drain path without a pathological tight spin
    }
    return NULL;
}

static int test_contention_1_flush_vs_write(void)
{
    g_fail = 0;
    reset_output();
    atomic_store(&g_stop, false);

    anothread_t prod[C1_PRODUCERS], flush[C1_FLUSHERS];
    for (int i = 0; i < C1_FLUSHERS; i++)
        ano_thread_create(&flush[i], NULL, c1_flusher, NULL);
    for (intptr_t i = 0; i < C1_PRODUCERS; i++)
        ano_thread_create(&prod[i], NULL, c1_producer, (void *)i);

    for (int i = 0; i < C1_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    atomic_store(&g_stop, true);
    for (int i = 0; i < C1_FLUSHERS; i++)
        ano_thread_join(flush[i], NULL);
    ano_log_flush();

    // IMMEDIATE never drops: every record survives flush interleaving.
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "contention1: file readable");
    if (c) {
        CHECK(count_lines(c) == C1_PRODUCERS * C1_PER, "contention1: every record survives flush-vs-write");
        free(c);
    }
    return g_fail;
}


/* Contention 2 */

// ABA tripwire.

// Workers cycle empty -> partial -> empty concurrently. ABA tripwire for the lock-free ring.
#define C2_THREADS 4
#define C2_CYCLES  40
#define C2_BATCH   10

static void *c2_worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int cyc = 0; cyc < C2_CYCLES; cyc++) {
        for (int i = 0; i < C2_BATCH; i++)
            ano_log_write(ANO_INFO, 0, __FILE_NAME__, __LINE__, "c2 t%d c%d i%d", id, cyc, i);
        ano_log_flush();   // drain to empty: buffer state recycles back to 0
    }
    return NULL;
}

static int test_contention_2_aba_bait(void)
{
    g_fail = 0;
    reset_output();

    anothread_t t[C2_THREADS];
    for (intptr_t i = 0; i < C2_THREADS; i++)
        ano_thread_create(&t[i], NULL, c2_worker, (void *)i);
    for (int i = 0; i < C2_THREADS; i++)
        ano_thread_join(t[i], NULL);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "contention2: file readable");
    if (c) {
        CHECK(count_lines(c) == C2_THREADS * C2_CYCLES * C2_BATCH,
              "contention2: no record lost or duplicated across recycled buffer states");
        free(c);
    }
    return g_fail;
}


/* Contention 3 */

// Config and output-file thrash under load.

#define C3_PRODUCERS 3
#define C3_OPS       600

static void *c3_producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < C3_OPS; i++)
        ano_log_write(ANO_INFO, 0, __FILE_NAME__, __LINE__, "c3 p%d %d", id, i);
    return NULL;
}

// Thrash every config knob + output dir under load. Assert survival (no crash/deadlock), then function.
static void *c3_thrasher(void *arg)
{
    (void)arg;
    for (int n = 0; n < C3_OPS; n++) {
        ano_log_set_level((n & 1) ? ANO_INFO : ANO_WARN);
        if (n % 50 == 0)
            ano_log_output_dir((n % 100 == 0) ? LOG_DIR : LOG_DIR_ALT);   // swap the output file mid-write
    }
    return NULL;
}

static int test_contention_3_config_thrash(void)
{
    g_fail = 0;
    reset_output();
    make_dir(LOG_DIR_ALT);

    anothread_t prod[C3_PRODUCERS], thr;
    ano_thread_create(&thr, NULL, c3_thrasher, NULL);
    for (intptr_t i = 0; i < C3_PRODUCERS; i++)
        ano_thread_create(&prod[i], NULL, c3_producer, (void *)i);
    for (int i = 0; i < C3_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    ano_thread_join(thr, NULL);

    // Restore config, confirm end-to-end.
    ano_log_set_level(ANO_INFO);
    ano_log_output_dir(LOG_DIR);
    ano_log(ANO_INFO, "c3 survived: %s", "yes");
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL && strstr(c, "c3 survived: yes"),
          "contention3: logger functional after config/output-file thrash");
    free(c);
    return g_fail;
}


/* Abuse */

// Pathological-but-legal inputs.

static int test_abuse_inputs(void)
{
    g_fail = 0;
    reset_output();

    // C-string-scannable first (no embedded NUL).
    ano_log(ANO_INFO, "multi\nline\nmessage");              // newlines inside one record
    ano_log(ANO_INFO, "%9000d", 7);                         // width far past the cap: must clamp, not overflow
    ano_log(ANO_INFO, "%d %s %x %o %c %u %ld 100%%",        // every common conversion at once
                 1, "two", 0xab, 64, 'Z', 5u, 6L);
    ano_log(ANO_INFO, "");                                  // empty format
    ano_log(ANO_INFO, "%s%s%s%s%s", "", "", "", "", "");    // five empty %s
    ano_log_flush();

    size_t len1 = 0;
    char *c = slurp(LOG_PATH, &len1);
    CHECK(c != NULL && len1 > 0, "abuse: survived pathological inputs, file non-empty");
    if (c) {
        CHECK(longest_line(c) <= 4096, "abuse: oversize width still clamped");
        CHECK(strstr(c, "two") != NULL, "abuse: multi-conversion line present");
        free(c);
    }

    // Embedded NUL last: stored byte-for-byte; only assert file grew.
    ano_log(ANO_INFO, "a%cb", 0);
    ano_log_flush();
    size_t len2 = 0;
    char *c2 = slurp(LOG_PATH, &len2);
    CHECK(c2 != NULL && len2 > len1, "abuse: embedded-NUL record stored (file grew)");
    free(c2);
    return g_fail;
}

static int test_abuse_config(void)
{
    g_fail = 0;
    reset_output();

    // Absurd-high level gates all.
    ano_log_set_level((ano_loglevel_t)999);
    ano_log(ANO_INFO, "gated by absurd level");
    ano_log_flush();
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c == NULL || strstr(c, "absurd level") == NULL, "abuse-config: absurd-high level gates all");
    free(c);

    // Absurd-low level passes all.
    ano_log_set_level((ano_loglevel_t)-1000);
    ano_log(ANO_INFO, "passes with absurd-low level");
    ano_log_flush();
    c = slurp(LOG_PATH, NULL);
    CHECK(c && strstr(c, "absurd-low"), "abuse-config: absurd-low level passes all");
    free(c);
    ano_log_set_level(ANO_INFO);
    return g_fail;
}

static int test_abuse_output_dir(void)
{
    g_fail = 0;
    reset_output();
    ano_log(ANO_INFO, "before bad output_dir");
    ano_log_flush();

    CHECK(ano_log_output_dir(NULL) == -1, "abuse-dir: NULL rejected");
    CHECK(ano_log_output_dir("") == -1, "abuse-dir: empty rejected");
    CHECK(ano_log_output_dir("no_such_dir_zzz/deeper/deepest") == -1, "abuse-dir: nonexistent rejected");

    char longp[512];
    memset(longp, 'a', sizeof longp - 1);
    longp[sizeof longp - 1] = '\0';
    CHECK(ano_log_output_dir(longp) == -1, "abuse-dir: overlong path rejected");

    // Rejected switches leave working output intact.
    ano_log(ANO_INFO, "after bad output_dir");
    ano_log_flush();
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c && strstr(c, "before bad output_dir") && strstr(c, "after bad output_dir"),
          "abuse-dir: working output file survived rejected switches");
    free(c);
    return g_fail;
}

// Entry points are safe no-ops when not live.
static int test_lifecycle_guard(const char *when)
{
    g_fail = 0;
    int r = ano_log_write(ANO_ERROR, 0, __FILE_NAME__, __LINE__, "%s enqueue", when);
    ano_log_write(ANO_WARN, ANO_NOW, __FILE_NAME__, __LINE__, "%s immediate (expected on stderr)", when);
    ano_log_set_level(ANO_WARN);
    ano_log_flush();
    int dr = ano_log_output_dir("anywhere");
    CHECK(r == 0, "lifecycle: enqueue is a no-op (returns 0) when not live");
    CHECK(dr == -1, "lifecycle: output_dir refused when not live");
    return g_fail;
}


/* Visible */

// Human-readable log left on disk (not removed).

static int test_visible_output(void)
{
    g_fail = 0;
    make_dir(VIS_DIR);
    remove(VIS_PATH);
    ano_log_output_dir(VIS_DIR);

    ano_log(ANO_INFO, "=== Anoptic logger showcase: this file is left on disk for you to read ===");
    ano_debug_log(ANO_INFO, "a debug line (present only in a DEBUG build)");
    ano_log(ANO_INFO, "formatted: int=%d str=%s hex=0x%x float=%.3f", 42, "hello", 255, 3.14159);
    ano_olog(ANO_INFO, "an origin line carrying this call site's file:line");
    ano_log(ANO_WARN, "a warning about something");
    ano_log(ANO_ERROR, "an error with %d codes", 3);
    ano_log(ANO_INFO, "a multi-line\nmessage that spans\nthree physical lines");
    ano_log(ANO_INFO, "byte-transparent UTF-8: %s", "cafe \xE2\x98\x95 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    ano_log(ANO_FATAL, "a FATAL routed through the default BOTH|NOW route");
    for (int i = 1; i <= 5; i++)
        ano_log(ANO_INFO, "counted line %d of 5", i);
    ano_log_flush();

    ano_log_output_dir(LOG_DIR);   // move the output off the showcase file so nothing else touches it

    char *c = slurp(VIS_PATH, NULL);
    CHECK(c != NULL && strstr(c, "showcase"), "visible: showcase file written");
    free(c);
    return g_fail;
}


/* Edge Cases */

// Boundaries, seams, alternation, churn.

// Body exactly at 4096-byte cap: one line clamped to cap.
static int test_edge_cap_boundary(void)
{
    g_fail = 0;
    reset_output();
    char body[4096];
    memset(body, 'B', sizeof body - 1);
    body[sizeof body - 1] = '\0';
    ano_log(ANO_INFO, "%s", body);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "edge-cap: file readable");
    if (c) {
        CHECK(count_lines(c) == 1, "edge-cap: one line emitted");
        CHECK(longest_line(c) <= 4096, "edge-cap: line clamped to message cap");
        free(c);
    }
    return g_fail;
}

// Many 1-byte-body records. Exact count, no loss.
#define TINY_COUNT 2000
static int test_edge_tiny_records(void)
{
    g_fail = 0;
    reset_output();
    for (int i = 0; i < TINY_COUNT; i++)
        ano_log(ANO_INFO, "%c", 'a' + (i % 26));
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "edge-tiny: file readable");
    if (c) {
        CHECK(count_lines(c) == TINY_COUNT, "edge-tiny: every tiny record survives");
        free(c);
    }
    return g_fail;
}

// Several wraps with mid-stream flushes. Unique indices, exact total, first/last ordered.
#define SEAM_BATCHES 8
#define SEAM_PER     900
static int test_edge_ring_seam(void)
{
    g_fail = 0;
    reset_output();
    int n = 0;
    for (int b = 0; b < SEAM_BATCHES; b++) {
        for (int i = 0; i < SEAM_PER; i++)
            ano_log(ANO_INFO, "seam %d", n++);
        ano_log_flush();   // drain mid-stream so the write cursor laps the buffer across batches
    }

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "edge-seam: file readable");
    if (c) {
        CHECK(count_lines(c) == SEAM_BATCHES * SEAM_PER, "edge-seam: every record survives ring wrap");
        char *first = strstr(c, "seam 0");
        char buf[32];
        snprintf(buf, sizeof buf, "seam %d", SEAM_BATCHES * SEAM_PER - 1);
        char *last = strstr(c, buf);
        CHECK(first && last && first < last, "edge-seam: order preserved across seams");
        free(c);
    }
    return g_fail;
}

// Alternate buffered + immediate. Exact total, buffered before following immediate.
#define ALT_PAIRS 200
static int test_edge_alternating_immediate(void)
{
    g_fail = 0;
    reset_output();
    for (int i = 0; i < ALT_PAIRS; i++) {
        ano_log(ANO_INFO, "alt buffered %d", i);
        ano_log_write(ANO_ERROR, ANO_NOW, __FILE_NAME__, __LINE__, "alt immediate %d", i);
    }
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "edge-alt: file readable");
    if (c) {
        CHECK(count_lines(c) == ALT_PAIRS * 2, "edge-alt: every buffered and immediate record present");
        char *b0 = strstr(c, "alt buffered 0");
        char *i0 = strstr(c, "alt immediate 0");
        CHECK(b0 && i0 && b0 < i0, "edge-alt: buffered precedes its paired immediate");
        free(c);
    }
    return g_fail;
}

// Switch output between two dirs. Pre-switch content survives in its file.
static int test_edge_output_dir_switch(void)
{
    g_fail = 0;
    reset_output();
    make_dir(LOG_DIR_ALT);
    remove(LOG_PATH_ALT);   // start both targets empty

    for (int round = 0; round < 4; round++) {
        ano_log_output_dir(LOG_DIR);
        ano_log(ANO_INFO, "switch primary r%d", round);
        ano_log_flush();
        ano_log_output_dir(LOG_DIR_ALT);
        ano_log(ANO_INFO, "switch alt r%d", round);
        ano_log_flush();
    }
    ano_log_output_dir(LOG_DIR);

    char *p = slurp(LOG_PATH, NULL);
    char *a = slurp(LOG_PATH_ALT, NULL);
    CHECK(p != NULL && a != NULL, "edge-dirswitch: both files readable");
    if (p && a) {
        CHECK(count_lines(p) == 4 && count_lines(a) == 4, "edge-dirswitch: four lines per target");
        CHECK(strstr(p, "switch primary r3") && strstr(a, "switch alt r3"),
              "edge-dirswitch: content survives in each target file");
    }
    free(p);
    free(a);
    return g_fail;
}

// Churn level between records. INFO survives iff level <= ANO_INFO. Exact survivor count.
static int test_edge_level_churn(void)
{
    g_fail = 0;
    reset_output();
    int expect = 0;
    for (int i = 0; i < 100; i++) {
        ano_loglevel_t lvl = (i & 1) ? ANO_ERROR : ANO_INFO;
        ano_log_set_level(lvl);
        ano_log(ANO_INFO, "churn info %d", i);    // survives only when lvl == ANO_INFO (even i)
        if (lvl <= ANO_INFO) expect++;
        ano_log(ANO_ERROR, "churn error %d", i);  // ERROR >= every level set here, always survives
        expect++;
    }
    ano_log_set_level(ANO_INFO);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "edge-levelchurn: file readable");
    if (c) {
        CHECK(count_lines(c) == expect, "edge-levelchurn: exact survivor count under level churn");
        free(c);
    }
    return g_fail;
}


/* Contention */

// Heavy producers vs flushers, sustained soak.

#define HEAVY_PRODUCERS 12
#define HEAVY_PER       1500
#define HEAVY_FLUSHERS  2

static void *heavy_producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    ano_loglevel_t lvls[3] = { ANO_INFO, ANO_WARN, ANO_ERROR };
    for (int i = 0; i < HEAVY_PER; i++)
        ano_log_write(lvls[i % 3], 0, __FILE_NAME__, __LINE__, "heavy p%d %d", id, i);
    return NULL;
}

static void *heavy_flusher(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_stop)) {
        ano_log_flush();
        ano_sleep(50);
    }
    return NULL;
}

// 12 producers + 2 flushers. Level DEBUG, no-loss exact total.
static int test_contention_heavy_mixed(void)
{
    g_fail = 0;
    reset_output();
    ano_log_set_level(ANO_INFO);
    atomic_store(&g_stop, false);

    anothread_t prod[HEAVY_PRODUCERS], flush[HEAVY_FLUSHERS];
    for (int i = 0; i < HEAVY_FLUSHERS; i++)
        ano_thread_create(&flush[i], NULL, heavy_flusher, NULL);
    for (intptr_t i = 0; i < HEAVY_PRODUCERS; i++)
        ano_thread_create(&prod[i], NULL, heavy_producer, (void *)i);
    for (int i = 0; i < HEAVY_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    atomic_store(&g_stop, true);
    for (int i = 0; i < HEAVY_FLUSHERS; i++)
        ano_thread_join(flush[i], NULL);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "heavy-mixed: file readable");
    if (c) {
        CHECK(count_lines(c) == HEAVY_PRODUCERS * HEAVY_PER, "heavy-mixed: every record survives");
        free(c);
    }
    return g_fail;
}

// Soak: 16 producers + 1 flusher. Exact line count = zero loss.
#define SOAK_PRODUCERS 16
#define SOAK_PER       2000

static void *soak_producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < SOAK_PER; i++)
        ano_log_write(ANO_INFO, 0, __FILE_NAME__, __LINE__, "soak p%d %d", id, i);
    return NULL;
}

static int test_contention_soak(void)
{
    g_fail = 0;
    reset_output();
    atomic_store(&g_stop, false);

    anothread_t prod[SOAK_PRODUCERS], flush;
    ano_thread_create(&flush, NULL, heavy_flusher, NULL);
    for (intptr_t i = 0; i < SOAK_PRODUCERS; i++)
        ano_thread_create(&prod[i], NULL, soak_producer, (void *)i);
    for (int i = 0; i < SOAK_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    atomic_store(&g_stop, true);
    ano_thread_join(flush, NULL);
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "soak: file readable");
    if (c) {
        CHECK(count_lines(c) == SOAK_PRODUCERS * SOAK_PER, "soak: exact line count, no loss over soak");
        free(c);
    }
    return g_fail;
}


/* Premature Join */

// Producers finish/exit before any flush.

#define PJ_PRODUCERS 8
#define PJ_PER       300

static void *pj_producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < PJ_PER; i++)
        ano_log_write(ANO_INFO, 0, __FILE_NAME__, __LINE__, "pj p%d %d", id, i);
    return NULL;
}

// Join all producers before any flush, then flush on main. Shared ring keeps records. Exact total.
static int test_premature_join_all(void)
{
    g_fail = 0;
    reset_output();

    anothread_t prod[PJ_PRODUCERS];
    for (intptr_t i = 0; i < PJ_PRODUCERS; i++)
        ano_thread_create(&prod[i], NULL, pj_producer, (void *)i);
    for (int i = 0; i < PJ_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);   // all producers dead before the first flush

    ano_log_flush();   // first and only flush, on main, after every producer exited

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "pj-all: file readable");
    if (c) {
        CHECK(count_lines(c) == PJ_PRODUCERS * PJ_PER,
              "pj-all: shared ring loses nothing when producers exit before draining");
        // Spot-check first and last producer records.
        char b0[32], bN[32];
        snprintf(b0, sizeof b0, "pj p0 %d", PJ_PER - 1);
        snprintf(bN, sizeof bN, "pj p%d %d", PJ_PRODUCERS - 1, PJ_PER - 1);
        CHECK(strstr(c, b0) && strstr(c, bN), "pj-all: first and last producer's records present");
        free(c);
    }
    return g_fail;
}

// Join half early, rest later, one flush. Exact total across both waves.
static int test_premature_join_half(void)
{
    g_fail = 0;
    reset_output();

    anothread_t prod[PJ_PRODUCERS];
    for (intptr_t i = 0; i < PJ_PRODUCERS; i++)
        ano_thread_create(&prod[i], NULL, pj_producer, (void *)i);

    int half = PJ_PRODUCERS / 2;
    for (int i = 0; i < half; i++)
        ano_thread_join(prod[i], NULL);   // first wave exits while the rest still produce
    for (int i = half; i < PJ_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);   // second wave joined

    ano_log_flush();   // single drain after all producers exited

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "pj-half: file readable");
    if (c) {
        CHECK(count_lines(c) == PJ_PRODUCERS * PJ_PER,
              "pj-half: exact total, early-exited producers lose nothing");
        free(c);
    }
    return g_fail;
}


int main(void)
{
    int failures = 0;

    // Anchor scratch to exe dir before I/O.
    if (!ano_fs_chdir_gamepath()) {
        fprintf(stderr, "chdir to gamepath failed\n");
        return 1;
    }
    resolve_log_paths();    // latch the session stamp

    // Pre-init abuse.
    {
        int rc = test_lifecycle_guard("pre-init");
        printf("  [%s] %s\n", rc == 0 ? "PASS" : "FAIL", "lifecycle_preinit");
        failures += rc;
    }

    if (ano_log_init() != 0) {
        fprintf(stderr, "ano_log_init failed\n");
        return 1;
    }
    make_dir(LOG_DIR);

    struct { const char *name; int (*fn)(void); } cases[] = {
        { "roundtrip",                  test_roundtrip },
        { "formatting",                 test_formatting },
        { "deferred_formatting",        test_deferred_formatting },
        { "accumulation_order",         test_accumulation_order },
        { "level_gate",                 test_level_gate },
        { "full_ring",                  test_full_ring },
        { "immediate_order",            test_immediate_order },
        { "routing",                    test_routing },
        { "truncation",                 test_truncation },
        { "empty_message",              test_empty_message },
        { "concurrent",                 test_concurrent },
        { "contention_1_flush_vs_write", test_contention_1_flush_vs_write },
        { "contention_2_aba_bait",       test_contention_2_aba_bait },
        { "contention_3_config_thrash",  test_contention_3_config_thrash },
        { "edge_cap_boundary",          test_edge_cap_boundary },
        { "edge_tiny_records",          test_edge_tiny_records },
        { "edge_ring_seam",             test_edge_ring_seam },
        { "edge_alternating_immediate", test_edge_alternating_immediate },
        { "edge_output_dir_switch",     test_edge_output_dir_switch },
        { "edge_level_churn",           test_edge_level_churn },
        { "contention_heavy_mixed",     test_contention_heavy_mixed },
        { "contention_soak",            test_contention_soak },
        { "premature_join_all",         test_premature_join_all },
        { "premature_join_half",        test_premature_join_half },
        { "abuse_inputs",               test_abuse_inputs },
        { "abuse_config",               test_abuse_config },
        { "abuse_output_dir",           test_abuse_output_dir },
        { "visible_output",             test_visible_output },
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int rc = cases[i].fn();
        printf("  [%s] %s\n", rc == 0 ? "PASS" : "FAIL", cases[i].name);
        failures += rc;
    }

    ano_log_cleanup();

    // Post-cleanup abuse.
    {
        int rc = test_lifecycle_guard("post-cleanup");
        printf("  [%s] %s\n", rc == 0 ? "PASS" : "FAIL", "lifecycle_postcleanup");
        failures += rc;
    }

    char cwd[1024];
    if (cwd_str(cwd, sizeof cwd))
        printf("  Showcase log written and verified: %s/%s\n", cwd, VIS_PATH);
    else
        printf("  Showcase log written and verified: ./%s\n", VIS_PATH);

    // Remove everything this test created.
    remove(LOG_PATH);
    remove(LOG_PATH_ALT);
    remove(VIS_PATH);
    remove_dir(LOG_DIR);
    remove_dir(LOG_DIR_ALT);
    remove_dir(VIS_DIR);

    if (failures != 0) {
        fprintf(stderr, "anoptic_logging: %d case(s) failed\n", failures);
        return 1;
    }
    printf("anoptic_logging: all cases passed.\n");
    return 0;
}
