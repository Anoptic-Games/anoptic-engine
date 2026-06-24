/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Baseline + adversarial suite for the mutex logger. The first block round-trips normal usage
// through the file sink. The rest abuses the public interface on the assumption it WILL be called
// wrong: pathological inputs, bogus config, rejected sink switches, lifecycle misuse (calls before
// init / after cleanup), and three contention tests (flush-vs-write, an ABA tripwire, config/sink
// thrash). The logger runs no background thread, so every case drains explicitly via ano_log_flush()
// and its file contents are deterministic. The final case leaves a human-readable log for inspection.

#include <anoptic_logger.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#if defined(_WIN32)
#include <direct.h>
static void  make_dir(const char *p) { _mkdir(p); }
static char *cwd_str(char *b, size_t n) { return _getcwd(b, (int)n); }
#else
#include <sys/stat.h>
#include <unistd.h>
static void  make_dir(const char *p) { mkdir(p, 0777); }
static char *cwd_str(char *b, size_t n) { return getcwd(b, n); }
#endif

#define LOG_DIR      "anolog_test"
#define LOG_PATH     "anolog_test/anoptic.log"
#define LOG_DIR_ALT  "anolog_test_alt"
#define LOG_PATH_ALT "anolog_test_alt/anoptic.log"
#define VIS_DIR      "anolog_visible"
#define VIS_PATH     "anolog_visible/anoptic.log"

#define THREAD_COUNT    4
#define MSGS_PER_THREAD 50

// Per-case failure flag set by CHECK; each test returns it (0 = pass).
static int g_fail;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "  FAIL: %s\n", (msg)); g_fail = 1; } } while (0)

static _Atomic int  g_worker_fail;
static _Atomic bool g_stop;


/* Helpers */

// Read the whole file into a NUL-terminated heap buffer. Caller frees. NULL if unreadable.
static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) { if (out_len) *out_len = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); if (out_len) *out_len = 0; return NULL; }
    char *buf = malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

static int count_lines(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (*s == '\n') n++;
    return n;
}

// Longest line length (bytes between newlines). Used to verify truncation clamping.
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

// Fresh sink file for the next case: drop the old one, then (re)point the logger at LOG_DIR.
static void reset_sink(void)
{
    remove(LOG_PATH);
    ano_log_output_dir(LOG_DIR);
}


/* Baseline cases */

static int test_roundtrip(void)
{
    g_fail = 0;
    reset_sink();
    ano_log_info("hello %d", 7);
    ano_log_warn("warn line");
    ano_log_error("err %s", "x");
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "roundtrip: file readable");
    if (c) {
        CHECK(count_lines(c) == 3, "roundtrip: three lines");
        CHECK(strstr(c, "INFO") && strstr(c, "hello 7"), "roundtrip: info line");
        CHECK(strstr(c, "WARN") && strstr(c, "warn line"), "roundtrip: warn line");
        CHECK(strstr(c, "ERROR") && strstr(c, "err x"), "roundtrip: error line");
        CHECK(strstr(c, "anotest_logging.c:") != NULL, "roundtrip: file:line prefix");
        free(c);
    }
    return g_fail;
}

static int test_formatting(void)
{
    g_fail = 0;
    reset_sink();
    ano_log_info("int=%d str=%s hex=%x width=%5d", 42, "abc", 255, 3);
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

static int test_accumulation_order(void)
{
    g_fail = 0;
    reset_sink();
    for (int i = 0; i < 50; i++)
        ano_log_info("seq %d", i);
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
    reset_sink();
    ano_log_set_level(LOG_ERROR);
    ano_log_info("gated info message");
    ano_log_error("passing error message");
    ano_log_flush();
    ano_log_set_level(LOG_DEBUG);

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "gate: file readable");
    if (c) {
        CHECK(strstr(c, "gated info message") == NULL, "gate: sub-threshold line suppressed");
        CHECK(strstr(c, "passing error message") != NULL, "gate: at-threshold line kept");
        free(c);
    }
    return g_fail;
}

static int test_drop_newest(void)
{
    g_fail = 0;
    reset_sink();
    ano_log_set_full_policy(ANO_LOG_DROP_NEWEST);

    uint64_t before = ano_log_dropped();
    int rejected = 0;
    for (int i = 0; i < 5000; i++)
        if (ano_log_enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "flood %d", i) == -1)
            rejected++;
    uint64_t after = ano_log_dropped();

    ano_log_set_full_policy(ANO_LOG_FULL_IMMEDIATE);
    ano_log_flush();

    CHECK(rejected > 0, "drop: some records rejected");
    CHECK(after - before == (uint64_t)rejected, "drop: counter matches -1 returns");
    return g_fail;
}

static int test_immediate_order(void)
{
    g_fail = 0;
    reset_sink();
    ano_log_info("buffered before immediate");
    ano_log_immediate(LOG_ERROR, __FILE_NAME__, __LINE__, "immediate %d", 99);
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

static int test_truncation(void)
{
    g_fail = 0;
    reset_sink();
    char big[6000];
    memset(big, 'A', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    ano_log_info("%s", big);
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
    reset_sink();
    ano_log_info("%s", "");
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
        if (ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, "worker %d msg %d", id, i) != 0)
            atomic_fetch_add(&g_worker_fail, 1);
    return NULL;
}

static int test_concurrent(void)
{
    g_fail = 0;
    reset_sink();
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


/* Contention 1 — flush while writing, write while flushing */

#define C1_PRODUCERS 4
#define C1_PER       250
#define C1_FLUSHERS  2

static void *c1_producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < C1_PER; i++)
        ano_log_enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "c1 p%d %d", id, i);
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
    reset_sink();
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

    // IMMEDIATE policy never drops, so every record must survive regardless of flush interleaving.
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL, "contention1: file readable");
    if (c) {
        CHECK(count_lines(c) == C1_PRODUCERS * C1_PER, "contention1: every record survives flush-vs-write");
        free(c);
    }
    return g_fail;
}


/* Contention 2 — ABA tripwire */

// Each worker cycles the buffer empty -> partial -> empty repeatedly (enqueue a batch, then flush
// to drain), so the internal length keeps returning to the same value while a peer concurrently
// does the same. The mutex logger is ABA-immune by construction; this case exists so the future
// lock-free ring -- which recycles head/tail counters and CAN suffer ABA -- must also pass it.
#define C2_THREADS 4
#define C2_CYCLES  40
#define C2_BATCH   10

static void *c2_worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int cyc = 0; cyc < C2_CYCLES; cyc++) {
        for (int i = 0; i < C2_BATCH; i++)
            ano_log_enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "c2 t%d c%d i%d", id, cyc, i);
        ano_log_flush();   // drain to empty: buffer state recycles back to 0
    }
    return NULL;
}

static int test_contention_2_aba_bait(void)
{
    g_fail = 0;
    reset_sink();

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


/* Contention 3 — config and sink thrash under load */

#define C3_PRODUCERS 3
#define C3_OPS       600

static void *c3_producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < C3_OPS; i++)
        ano_log_enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "c3 p%d %d", id, i);
    return NULL;
}

// Cycles every config knob and flips the sink between two directories while producers hammer the
// log. Counting is impossible (gated levels, drops, two files), so the assertion is survival: no
// crash, no deadlock, TSan-clean, and the logger is still functional afterward.
static void *c3_thrasher(void *arg)
{
    (void)arg;
    for (int n = 0; n < C3_OPS; n++) {
        ano_log_set_level((n & 1) ? LOG_DEBUG : LOG_INFO);
        ano_log_set_full_policy((ano_log_full_policy_t)(n % 3));   // IMMEDIATE / DROP_NEWEST / BLOCK
        if (n % 50 == 0)
            ano_log_output_dir((n % 100 == 0) ? LOG_DIR : LOG_DIR_ALT);   // swap the sink mid-write
    }
    return NULL;
}

static int test_contention_3_config_thrash(void)
{
    g_fail = 0;
    reset_sink();
    make_dir(LOG_DIR_ALT);

    anothread_t prod[C3_PRODUCERS], thr;
    ano_thread_create(&thr, NULL, c3_thrasher, NULL);
    for (intptr_t i = 0; i < C3_PRODUCERS; i++)
        ano_thread_create(&prod[i], NULL, c3_producer, (void *)i);
    for (int i = 0; i < C3_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    ano_thread_join(thr, NULL);

    // Restore sane config and confirm the logger still works end to end.
    ano_log_set_level(LOG_DEBUG);
    ano_log_set_full_policy(ANO_LOG_FULL_IMMEDIATE);
    ano_log_output_dir(LOG_DIR);
    ano_log_info("c3 survived: %s", "yes");
    ano_log_flush();

    char *c = slurp(LOG_PATH, NULL);
    CHECK(c != NULL && strstr(c, "c3 survived: yes"),
          "contention3: logger functional after config/sink thrash");
    free(c);
    return g_fail;
}


/* Abuse — pathological-but-legal inputs */

static int test_abuse_inputs(void)
{
    g_fail = 0;
    reset_sink();

    // C-string-scannable content first (no embedded NUL yet to stop strstr/longest_line).
    ano_log_info("multi\nline\nmessage");              // newlines inside one record
    ano_log_info("%9000d", 7);                         // width far past the cap: must clamp, not overflow
    ano_log_info("%d %s %x %o %c %u %ld 100%%",        // every common conversion at once
                 1, "two", 0xab, 64, 'Z', 5u, 6L);
    ano_log_info("");                                  // empty format
    ano_log_info("%s%s%s%s%s", "", "", "", "", "");    // five empty %s
    ano_log_flush();

    size_t len1 = 0;
    char *c = slurp(LOG_PATH, &len1);
    CHECK(c != NULL && len1 > 0, "abuse: survived pathological inputs, file non-empty");
    if (c) {
        CHECK(longest_line(c) <= 4096, "abuse: oversize width still clamped");
        CHECK(strstr(c, "two") != NULL, "abuse: multi-conversion line present");
        free(c);
    }

    // Embedded NUL last: stored byte-for-byte (length-based, that's the point), but it blocks
    // C-string scans, so only assert the file grew by the record.
    ano_log_info("a%cb", 0);
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
    reset_sink();

    // Absurd-high level gates everything.
    ano_log_set_level((log_types_t)999);
    ano_log_info("gated by absurd level");
    ano_log_flush();
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c == NULL || strstr(c, "absurd level") == NULL, "abuse-config: absurd-high level gates all");
    free(c);

    // Absurd-low level passes everything.
    ano_log_set_level((log_types_t)-1000);
    ano_log_info("passes with absurd-low level");
    ano_log_flush();
    c = slurp(LOG_PATH, NULL);
    CHECK(c && strstr(c, "absurd-low"), "abuse-config: absurd-low level passes all");
    free(c);
    ano_log_set_level(LOG_DEBUG);

    // Out-of-range policy: must not crash; it falls through to a drain-and-append.
    ano_log_set_full_policy((ano_log_full_policy_t)42);
    for (int i = 0; i < 5000; i++)
        ano_log_enqueue(LOG_INFO, __FILE_NAME__, __LINE__, "bogus policy flood %d", i);
    ano_log_set_full_policy(ANO_LOG_FULL_IMMEDIATE);
    ano_log_flush();
    c = slurp(LOG_PATH, NULL);
    CHECK(c && strstr(c, "bogus policy flood 4999"), "abuse-config: bogus full policy survived flood");
    free(c);
    return g_fail;
}

static int test_abuse_output_dir(void)
{
    g_fail = 0;
    reset_sink();
    ano_log_info("before bad output_dir");
    ano_log_flush();

    CHECK(ano_log_output_dir(NULL) == -1, "abuse-dir: NULL rejected");
    CHECK(ano_log_output_dir("") == -1, "abuse-dir: empty rejected");
    CHECK(ano_log_output_dir("no_such_dir_zzz/deeper/deepest") == -1, "abuse-dir: nonexistent rejected");

    char longp[512];
    memset(longp, 'a', sizeof longp - 1);
    longp[sizeof longp - 1] = '\0';
    CHECK(ano_log_output_dir(longp) == -1, "abuse-dir: overlong path rejected");

    // Every rejected switch must have left the working sink intact.
    ano_log_info("after bad output_dir");
    ano_log_flush();
    char *c = slurp(LOG_PATH, NULL);
    CHECK(c && strstr(c, "before bad output_dir") && strstr(c, "after bad output_dir"),
          "abuse-dir: working sink survived rejected switches");
    free(c);
    return g_fail;
}

// Every entry point must be a safe no-op when the logger is not live (before init / after cleanup).
static int test_lifecycle_guard(const char *when)
{
    g_fail = 0;
    int r = ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, "%s enqueue", when);
    ano_log_immediate(LOG_WARN, __FILE_NAME__, __LINE__, "%s immediate (expected on stderr)", when);
    ano_log_set_level(LOG_WARN);
    ano_log_set_full_policy(ANO_LOG_DROP_NEWEST);
    ano_log_flush();
    int dr = ano_log_output_dir("anywhere");
    uint64_t d = ano_log_dropped();
    CHECK(r == 0, "lifecycle: enqueue is a no-op (returns 0) when not live");
    CHECK(dr == -1, "lifecycle: output_dir refused when not live");
    CHECK(d == 0, "lifecycle: dropped reads 0 when not live");
    return g_fail;
}


/* Visible — a human-readable log left on disk for inspection (NOT removed at the end) */

static int test_visible_output(void)
{
    g_fail = 0;
    make_dir(VIS_DIR);
    remove(VIS_PATH);
    ano_log_output_dir(VIS_DIR);

    ano_log_info("=== Anoptic logger showcase: this file is left on disk for you to read ===");
    ano_log_debug("a debug line (present only in a DEBUG build)");
    ano_log_info("formatted: int=%d str=%s hex=0x%x float=%.3f", 42, "hello", 255, 3.14159);
    ano_log_warn("a warning about something");
    ano_log_error("an error with %d codes", 3);
    ano_log_info("a multi-line\nmessage that spans\nthree physical lines");
    ano_log_info("byte-transparent UTF-8: %s", "cafe \xE2\x98\x95 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    ano_log_immediate(LOG_FATAL, __FILE_NAME__, __LINE__, "a FATAL routed through the immediate path");
    for (int i = 1; i <= 5; i++)
        ano_log_info("counted line %d of 5", i);
    ano_log_flush();

    ano_log_output_dir(LOG_DIR);   // move the sink off the showcase file so nothing else touches it

    char *c = slurp(VIS_PATH, NULL);
    CHECK(c != NULL && strstr(c, "showcase"), "visible: showcase file written");
    free(c);
    return g_fail;
}


int main(void)
{
    int failures = 0;

    // Pre-init abuse: every entry point must be safe before ano_log_init.
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
        { "accumulation_order",         test_accumulation_order },
        { "level_gate",                 test_level_gate },
        { "drop_newest",                test_drop_newest },
        { "immediate_order",            test_immediate_order },
        { "truncation",                 test_truncation },
        { "empty_message",              test_empty_message },
        { "concurrent",                 test_concurrent },
        { "contention_1_flush_vs_write", test_contention_1_flush_vs_write },
        { "contention_2_aba_bait",       test_contention_2_aba_bait },
        { "contention_3_config_thrash",  test_contention_3_config_thrash },
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

    // Post-cleanup abuse: every entry point must be safe after ano_log_cleanup.
    {
        int rc = test_lifecycle_guard("post-cleanup");
        printf("  [%s] %s\n", rc == 0 ? "PASS" : "FAIL", "lifecycle_postcleanup");
        failures += rc;
    }

    // Ephemeral files are removed; the showcase file is left on disk on purpose.
    remove(LOG_PATH);
    remove(LOG_PATH_ALT);

    char cwd[1024];
    if (cwd_str(cwd, sizeof cwd))
        printf("  Showcase log left for inspection: %s/%s\n", cwd, VIS_PATH);
    else
        printf("  Showcase log left for inspection: ./%s\n", VIS_PATH);

    if (failures != 0) {
        fprintf(stderr, "anoptic_logging: %d case(s) failed\n", failures);
        return 1;
    }
    printf("anoptic_logging: all cases passed.\n");
    return 0;
}
