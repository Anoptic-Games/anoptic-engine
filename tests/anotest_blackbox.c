/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_log_crash.h, end to end, on real crashes:
 *   - the bb_fmt_dec/bb_fmt_hex handler formatters against the printf oracle
 *   - ano_log_crash_init: returns 0, resolves <exe>/logs/<session-stamp>_CRASH.log, creates nothing
 *   - one child process per scenario dies its scripted death (AV read/write/execute, abort,
 *     illegal instruction, integer divide by zero, CRT raise, a crash on a non-main thread, a
 *     crash inside ano_log_write, a crash under a 6-producer storm, a crash with the ring pinned
 *     full, a stack overflow on main and on a spawned thread, two simultaneous crashers on win64)
 *   - the logger mesh: records committed before the crash survive into the scratch session log
 *     through the Stage 3 hail mary
 *   - the deadman: a poisoned deferred format crashes the drainer under the drain mutex, the hail
 *     mary self-deadlocks, and the watchdog must kill the process at ~5 s
 *   - Stage 4: a leftover *_CRASH.log is announced on the next boot ("n crash logs detected"),
 *     each crash session gets its own record file, a stamp collision appends, and boot prunes
 *     both log types to the newest 4 (the live session excepted)
 * The parent discovers each child's stamped files by suffix scan (bb_scan_suffix).
 * Oracles: exact death (exit code on win64, termination signal on POSIX), exactly ONE complete
 * record per crash (begin/end markers + backtrace), required/forbidden record substrings, and
 * exact sentinel counts in the scratch session log.
 * No args = parent mode, argv[1] = crash-child scenario. Exit 0 == pass. */

#include <anoptic_log_crash.h>
#include <anoptic_filesystem.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include "log/log_crash_internal.h"
#include "templates/scratch.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/utime.h>
#define set_mtime(p, t) do { struct _utimbuf ub_ = { .actime = (t), .modtime = (t) }; _utime((p), &ub_); } while (0)
#else
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>
#define set_mtime(p, t) do { struct utimbuf ub_ = { .actime = (t), .modtime = (t) }; utime((p), &ub_); } while (0)
#endif

#define LOG_DIR      "anotest_blackbox_scratch"
#define CRASH_DIR    "logs"                 // ano_fs_logpath under the exe-anchored CWD
#define LOG_SUFFIX   "_ano.log"             // session log files: <stamp>_ano.log
#define CRASH_SUFFIX "_CRASH.log"           // session crash records: <stamp>_CRASH.log
#define REC_BEGIN "==== ANOPTIC BLACKBOX: we are going down ===="
#define REC_END   "==== end of record ===="

// One death per platform pair: win64 judges the exact exit code, POSIX the termination signal.
// 0 = must exit(0), -1 = any fatal signal (Darwin reports some faults as SIGBUS).
#if defined(_WIN32)
typedef unsigned long death_t;
#define DIE(win, posix) ((death_t)(win))
#define SUB(win, posix) (win)
#else
typedef int death_t;
#define DIE(win, posix) ((death_t)(posix))
#define SUB(win, posix) (posix)
// Forbidden substring in the record for a raised fault signal.
// NULL on Darwin, where XNU fakes a hardware si_code and si_addr for raise().
#if defined(__APPLE__)
#define SIG_SEGVISH (-1)
#define RAISED_ADDR NULL
#else
#define SIG_SEGVISH SIGSEGV
#define RAISED_ADDR "fault address"
#endif
#endif

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL (%d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)


/* ---------------- child scenarios: one scripted death each ---------------- */

static void sc_segv_write(void) { *(volatile int *)0 = 42; }
static void sc_segv_read(void)  { volatile int x = *(volatile int *)8; (void)x; }

// DEP/NX: jump into a readable-writable, never-executable page.
static void sc_segv_exec(void)
{
#if defined(_WIN32)
    void *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (page == NULL) exit(41);
#else
    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) exit(41);
#endif
    memset(page, 0xC3, 16);
    ((void (*)(void))page)();
}

static void sc_abort(void) { abort(); }

static void sc_illegal(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile ("ud2");
#elif defined(__aarch64__)
    __asm__ volatile (".inst 0x00000000");  // udf #0
#else
    __builtin_trap();
#endif
}

#if defined(__x86_64__) || defined(_M_X64)
// Hardware integer divide fault (x86-only, aarch64 does not trap). Inline asm like
// sc_illegal, not C division: InstCombine folds constant-dividend 1/z into icmp+select
// (no idiv, no trap, "wrong death: exit 0" at -O2+), and 1/0 in C is UB besides.
static void sc_intdiv(void)
{
    __asm__ volatile ("xorl %%ecx, %%ecx\n\t"
                      "movl $1, %%eax\n\t"
                      "cltd\n\t"
                      "idivl %%ecx"
                      ::: "eax", "ecx", "edx", "cc");
}
#endif

static void sc_raise_segv(void) { raise(SIGSEGV); }
static void sc_raise_ill(void)  { raise(SIGILL); }
static void sc_raise_fpe(void)  { raise(SIGFPE); }

// Crash off the main thread.
static void *segv_thread(void *arg) { (void)arg; *(volatile int *)0 = 1; return NULL; }
static void sc_thread_segv(void)
{
    anothread_t t;
    if (ano_thread_create(&t, NULL, segv_thread, NULL) != 0) exit(41);
    ano_thread_join(t, NULL);   // never returns: the process dies under the join
}

// 32 buffered lines then an immediate crash: every one must survive the hail mary.
static void sc_hailmary(void)
{
    for (int i = 0; i < 32; i++)
        ano_log(ANO_INFO, "hailmary-sentinel-%d", i);
    *(volatile int *)0 = 1;
}

static void sc_empty_ring(void)
{
    ano_log(ANO_INFO, "empty-ring-sentinel");
    ano_log_flush();                    // ring drained before the crash
    *(volatile int *)0 = 1;
}

static void sc_now_path(void)
{
    ano_rlog(ANO_FATAL, ANO_FILE | ANO_NOW, "fatal-now-sentinel");  // synchronous write + fsync
    *(volatile int *)0 = 1;
}

// Crash inside ano_log_write: capture's strlen on a wild %s pointer.
static void sc_midwrite(void)
{
    ano_log(ANO_INFO, "midwrite-sentinel-before");
    ano_log(ANO_INFO, "%s", (const char *)16);
}

// Producer storm shared by contention/ring_full. Runs until the process dies.
static atomic_bool g_spamHuge;
static void *spammer(void *arg)
{
    uint32_t s = 0x1234u + (uint32_t)(uintptr_t)arg * 2654435761u;
    bool huge = atomic_load(&g_spamHuge);
    static char big[8][4001];
    char *bigbuf = big[(uintptr_t)arg & 7];
    if (huge) { memset(bigbuf, 'A' + ((int)(uintptr_t)arg & 7), 4000); bigbuf[4000] = 0; }
    char buf[128];
    uint32_t it = 0;
    for (;;) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        if (huge) {
            ano_log_write(ANO_INFO, 0, NULL, 0, "%s", bigbuf);  // 4000 B entries: the ring pins full
        } else {
            int n = (int)(s % 100u) + 8;
            for (int i = 0; i < n; i++) buf[i] = (char)('!' + (int)((s + (uint32_t)i) % 90u));
            buf[n] = 0;
            ano_log_write(ANO_INFO, 0, "anotest", 1, "%s", buf);
            ano_log_write(ANO_WARN, 0, NULL, 0, "mix=%d/%x/%s", (int)s, s, "alpha");
            if ((++it & 63u) == 0)
                ano_log_write(ANO_ERROR, ANO_NOW | ANO_FILE, NULL, 0, "now-%u", it);  // NOW under storm
        }
    }
    return NULL;
}
static void storm_then_crash(bool huge)
{
    atomic_store(&g_spamHuge, huge);
    anothread_t t[6];
    for (intptr_t i = 0; i < 6; i++)
        if (ano_thread_create(&t[i], NULL, spammer, (void *)i) != 0) exit(41);
    ano_sleep(100000);      // 100 ms of storm
    *(volatile int *)0 = 1;
}
static void sc_contention(void) { storm_then_crash(false); }
static void sc_ring_full(void)  { storm_then_crash(true); }

// Poison a deferred capture: the fmt pointer, freed between enqueue and drain, crashes the
// drainer under the drain mutex. The hail mary self-deadlocks and the deadman must fire.
static void sc_deadman(void)
{
#if defined(_WIN32)
    char *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (page == NULL) exit(41);
#else
    char *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) exit(41);
#endif
    memcpy(page, "poison %d", 10);
    ano_sleep(5000);        // 5 ms idle: let the drainer park
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    ano_log_write(ANO_INFO, 0, NULL, 0, (const char *)page, 7);
#pragma GCC diagnostic pop
#if defined(_WIN32)
    VirtualFree(page, 0, MEM_RELEASE);  // freed while the drainer is still waking
#else
    munmap(page, 4096);
#endif
    ano_sleep(7u * 1000u * 1000u);      // the deadman (5 s) fires first
    exit(42);               // race lost: the drainer rendered before the free. Parent retries.
}

// Main-thread stack overflow: the record comes off the crash stack bb_install armed
// (sigaltstack / SetThreadStackGuarantee).
static uint64_t overflow_rec(uint64_t d)
{
    volatile char pad[4096];
    pad[0] = (char)d;
    if (d == UINT64_MAX) return 0;      // never true: silences -Winfinite-recursion
    return overflow_rec(d + 1) + (uint64_t)pad[0];
}
static void sc_stack_overflow(void) { (void)overflow_rec(1); }

// The same overflow on a spawned thread: ano_thread_create's spawn shim arms its crash stack.
static void *overflow_thread(void *arg) { (void)arg; (void)overflow_rec(1); return NULL; }
static void sc_thread_overflow(void)
{
    anothread_t t;
    if (ano_thread_create(&t, NULL, overflow_thread, NULL) != 0) exit(41);
    ano_thread_join(t, NULL);   // never returns: the process dies under the join
}

#if defined(_WIN32)
// Two threads crash in the same instant: one owns the record, the other parks.
// win64-only (on POSIX the winner's disarm hands the loser SIG_DFL and truncates the record).
static atomic_int g_crashGate;
static void *race_thread(void *arg)
{
    (void)arg;
    atomic_fetch_add(&g_crashGate, 1);
    while (atomic_load(&g_crashGate) < 2) { }   // spin-align both threads
    *(volatile int *)0 = 1;
    return NULL;
}
static void sc_double(void)
{
    anothread_t a, b;
    if (ano_thread_create(&a, NULL, race_thread, NULL) != 0) exit(41);
    if (ano_thread_create(&b, NULL, race_thread, NULL) != 0) exit(41);
    ano_thread_join(a, NULL);
    ano_thread_join(b, NULL);
}
#endif

static void sc_clean(void)
{
    ano_log(ANO_INFO, "clean-run-sentinel");
    ano_log_flush();
    ano_log_cleanup();
}

typedef struct { const char *name; void (*fn)(void); } child_t;
static const child_t CHILDREN[] = {
    { "clean",          sc_clean },
    { "segv_write",     sc_segv_write },
    { "segv_read",      sc_segv_read },
    { "segv_exec",      sc_segv_exec },
    { "abort",          sc_abort },
    { "illegal",        sc_illegal },
#if defined(__x86_64__) || defined(_M_X64)
    { "intdiv",         sc_intdiv },
#endif
    { "raise_segv",     sc_raise_segv },
    { "raise_ill",      sc_raise_ill },
    { "raise_fpe",      sc_raise_fpe },
    { "thread_segv",    sc_thread_segv },
    { "hailmary",       sc_hailmary },
    { "empty_ring",     sc_empty_ring },
    { "now_path",       sc_now_path },
    { "midwrite",       sc_midwrite },
    { "contention",     sc_contention },
    { "ring_full",      sc_ring_full },
    { "deadman",        sc_deadman },
    { "stack_overflow", sc_stack_overflow },
    { "thread_overflow", sc_thread_overflow },
#if defined(_WIN32)
    { "double",         sc_double },
#endif
};
#define NCHILDREN (sizeof CHILDREN / sizeof CHILDREN[0])

// Child entry: arm logger (into the scratch dir) + blackbox, then die the named death.
// "nolog": blackbox alone, no logger.
static int child_main(const char *name)
{
    if (strcmp(name, "nolog") == 0) {
        if (ano_log_crash_init() != 0) return 41;
        *(volatile int *)0 = 1;
        return 0;
    }
    if (ano_log_init() != 0) return 40;
    scratch_make_dir(LOG_DIR);
    if (ano_log_output_dir(LOG_DIR) != 0) return 43;
    if (ano_log_crash_init() != 0) return 41;
    for (size_t i = 0; i < NCHILDREN; i++) {
        if (strcmp(CHILDREN[i].name, name) == 0) {
            CHILDREN[i].fn();
            return 0;       // only 'clean' (and a lost deadman race via exit) comes back
        }
    }
    fprintf(stderr, "unknown scenario: %s\n", name);
    return 44;
}


/* ---------------- parent: spawn, judge, report ---------------- */

typedef struct {
    const char *name;
    death_t die;            // DIE(win64 exit code, POSIX signal); POSIX 0 = exit(0), -1 = any signal
    int  want_record;       // 1 = exactly one complete record, 0 = no crash record may exist
    const char *rec_sub;    // required substring in the crash record (NULL: none)
    const char *rec_absent; // forbidden substring in the crash record (NULL: none)
    const char *log_sub;    // required substring in the scratch session log (NULL: none)
    int  log_n;             // exact occurrence count of log_sub, -1 = presence is enough
    bool no_logfile;        // the scratch session log must not exist at all
    unsigned min_ms, max_ms;// wall bounds on the child, 0 = unchecked
    int  retries;           // extra attempts when the child exits race_exit
    int  race_exit;
} scen_t;

static const scen_t SCENARIOS[] = {
    { "clean",       DIE(0, 0),                    0, NULL, NULL, "clean-run-sentinel", 1, false, 0, 0, 0, 0 },
    { "segv_write",  DIE(0xC0000005, SIG_SEGVISH), 1, SUB("access: write",   "fault address:"), NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "segv_read",   DIE(0xC0000005, SIG_SEGVISH), 1, SUB("access: read",    "fault address:"), NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "segv_exec",   DIE(0xC0000005, -1),          1, SUB("access: execute", "fault address:"), NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "abort",       DIE(3, SIGABRT),              1, "SIGABRT", NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "illegal",     DIE(0xC000001D, SIGILL),      1, SUB("EXCEPTION_ILLEGAL_INSTRUCTION", "SIGILL"), NULL, NULL, -1, false, 0, 0, 0, 0 },
#if defined(__x86_64__) || defined(_M_X64)
    { "intdiv",      DIE(0xC0000094, SIGFPE),      1, SUB("EXCEPTION_INT_DIVIDE_BY_ZERO",  "SIGFPE"), NULL, NULL, -1, false, 0, 0, 0, 0 },
#endif
    // User raise: the CRT hooks catch what SEH never sees. POSIX si_code <= 0 forbids si_addr.
    { "raise_segv",  DIE(3, SIGSEGV),              1, SUB("SIGSEGV (CRT raise)", "SIGSEGV"), SUB(NULL, RAISED_ADDR), NULL, -1, false, 0, 0, 0, 0 },
    { "raise_ill",   DIE(3, SIGILL),               1, SUB("SIGILL (CRT raise)",  "SIGILL"),  SUB(NULL, RAISED_ADDR), NULL, -1, false, 0, 0, 0, 0 },
    { "raise_fpe",   DIE(3, SIGFPE),               1, SUB("SIGFPE (CRT raise)",  "SIGFPE"),  SUB(NULL, RAISED_ADDR), NULL, -1, false, 0, 0, 0, 0 },
    { "thread_segv", DIE(0xC0000005, SIG_SEGVISH), 1, SUB("EXCEPTION_ACCESS_VIOLATION", "fault address:"), NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "hailmary",    DIE(0xC0000005, SIG_SEGVISH), 1, NULL, NULL, "hailmary-sentinel-",       32, false, 0, 0, 0, 0 },
    { "empty_ring",  DIE(0xC0000005, SIG_SEGVISH), 1, NULL, NULL, "empty-ring-sentinel",       1, false, 0, 0, 0, 0 },
    { "now_path",    DIE(0xC0000005, SIG_SEGVISH), 1, NULL, NULL, "fatal-now-sentinel",        1, false, 0, 0, 0, 0 },
    { "midwrite",    DIE(0xC0000005, SIG_SEGVISH), 1, SUB("access: read", "fault address:"), NULL, "midwrite-sentinel-before", 1, false, 0, 0, 0, 0 },
    { "contention",  DIE(0xC0000005, SIG_SEGVISH), 1, NULL, NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "ring_full",   DIE(0xC0000005, SIG_SEGVISH), 1, NULL, NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "nolog",       DIE(0xC0000005, SIG_SEGVISH), 1, NULL, NULL, NULL, -1, true,  0, 0, 0, 0 },
    { "deadman",     DIE(3, SIGALRM),              1, SUB("EXCEPTION_ACCESS_VIOLATION", "SIGSEGV"), NULL, NULL, -1, false, 4200, 25000, 3, 42 },
    { "stack_overflow",  DIE(0xC00000FD, SIG_SEGVISH), 1, SUB("EXCEPTION_STACK_OVERFLOW", "fault address:"), NULL, NULL, -1, false, 0, 0, 0, 0 },
    { "thread_overflow", DIE(0xC00000FD, SIG_SEGVISH), 1, SUB("EXCEPTION_STACK_OVERFLOW", "fault address:"), NULL, NULL, -1, false, 0, 0, 0, 0 },
#if defined(_WIN32)
    { "double",      DIE(0xC0000005, 0),           1, "EXCEPTION_ACCESS_VIOLATION", NULL, NULL, -1, false, 0, 0, 0, 0 },
#endif
};
#define NSCEN (sizeof SCENARIOS / sizeof SCENARIOS[0])

static char g_exe[MAXPATH + 32];    // absolute path to this binary

static char *read_all(const char *path, size_t *out_len)
{
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    *out_len = rd;
    return buf;
}

static int count_sub(const char *buf, size_t len, const char *needle)
{
    size_t nl = strlen(needle);
    if (buf == NULL || nl == 0 || len < nl) return 0;
    int n = 0;
    for (size_t i = 0; i + nl <= len; i++)
        if (buf[i] == needle[0] && memcmp(buf + i, needle, nl) == 0) { n++; i += nl - 1; }
    return n;
}

// Newest `suffix` file in `dir` composed into out, false when none.
static bool find_suffix(const char *dir, const char *suffix, char *out, size_t cap)
{
    char name[MAXPATH];
    if (bb_scan_suffix(dir, suffix, name) == 0)
        return false;
    snprintf(out, cap, "%s/%s", dir, name);
    return true;
}

// Remove every `suffix` file in `dir`, one scan per pass.
static void remove_all_suffix(const char *dir, const char *suffix)
{
    char path[MAXPATH + 40];
    while (find_suffix(dir, suffix, path, sizeof path))
        if (remove(path) != 0)
            break;  // undeletable: bail rather than spin
}

#if defined(_WIN32)

// Spawn this exe with `scenario`, wait (30 s cap), return the raw exit code. 0xDEAD = cap hit.
static unsigned long spawn_child(const char *scenario, unsigned *ms)
{
    char cmd[MAXPATH + 96];
    snprintf(cmd, sizeof cmd, "\"%s\" %s", g_exe, scenario);
    STARTUPINFOA si = { .cb = sizeof si };
    PROCESS_INFORMATION pi;
    uint64_t t0 = ano_timestamp_us();
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        *ms = 0;
        return 0xFFFFFFFEul;
    }
    unsigned long code = 0xFFFFFFFDul;
    if (WaitForSingleObject(pi.hProcess, 30000) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0xDEADu);
        WaitForSingleObject(pi.hProcess, 5000);
        code = 0xDEADul;
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    *ms = (unsigned)((ano_timestamp_us() - t0) / 1000u);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}
typedef unsigned long child_status_t;
static bool death_matches(death_t want, child_status_t code) { return code == want; }
static bool death_is_race(child_status_t code, int race_exit)
{ return race_exit != 0 && code == (child_status_t)race_exit; }
static void death_print(child_status_t code, char *out, size_t cap)
{ snprintf(out, cap, "exit 0x%08lX", code); }

#else

// Spawn this exe with `scenario`, return the raw waitpid status (-1 on spawn failure).
// A hung child is left to the CTest timeout.
static int spawn_child(const char *scenario, unsigned *ms)
{
    uint64_t t0 = ano_timestamp_us();
    pid_t pid = fork();
    if (pid == 0) {
        execl(g_exe, g_exe, scenario, (char *)NULL);
        _exit(39);
    }
    if (pid < 0) { *ms = 0; return -1; }
    int status = 0;
    waitpid(pid, &status, 0);
    *ms = (unsigned)((ano_timestamp_us() - t0) / 1000u);
    return status;
}
typedef int child_status_t;
static bool death_matches(death_t want, child_status_t status)
{
    if (status == -1) return false;
    if (want == 0)  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (want == -1) return WIFSIGNALED(status);
    return WIFSIGNALED(status) && WTERMSIG(status) == (int)want;
}
static bool death_is_race(child_status_t status, int race_exit)
{ return race_exit != 0 && WIFEXITED(status) && WEXITSTATUS(status) == race_exit; }
static void death_print(child_status_t status, char *out, size_t cap)
{
    if (WIFSIGNALED(status))    snprintf(out, cap, "signal %d", WTERMSIG(status));
    else if (WIFEXITED(status)) snprintf(out, cap, "exit %d", WEXITSTATUS(status));
    else                        snprintf(out, cap, "status 0x%08X", (unsigned)status);
}

#endif

// One scenario: fresh crash-record + scratch-log slate, spawn, judge death + record + sentinels.
static void run_scenario(const scen_t *sc)
{
    child_status_t st = 0;
    unsigned ms = 0;
    for (int a = 0; a <= sc->retries; a++) {
        remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
        remove_all_suffix(LOG_DIR, LOG_SUFFIX);
        st = spawn_child(sc->name, &ms);
        if (!death_is_race(st, sc->race_exit))
            break;
        printf("%s: race lost, retrying...\n", sc->name);
    }

    char seen[48];
    death_print(st, seen, sizeof seen);
    CHECK(death_matches(sc->die, st), "%s: wrong death: %s", sc->name, seen);

    // Discover the child's stamped files by suffix.
    char recPath[MAXPATH + 40], logPath[MAXPATH + 40];
    size_t recLen = 0, logLen = 0;
    char *rec = find_suffix(CRASH_DIR, CRASH_SUFFIX, recPath, sizeof recPath)
              ? read_all(recPath, &recLen) : NULL;
    char *log = find_suffix(LOG_DIR, LOG_SUFFIX, logPath, sizeof logPath)
              ? read_all(logPath, &logLen) : NULL;

    if (sc->want_record == 1) {
        CHECK(rec != NULL, "%s: no crash record", sc->name);
        if (rec != NULL) {
            // Exactly one complete record: begin + end markers paired, backtrace present.
            CHECK(count_sub(rec, recLen, REC_BEGIN) == 1, "%s: want exactly 1 record, have %d",
                  sc->name, count_sub(rec, recLen, REC_BEGIN));
            CHECK(count_sub(rec, recLen, REC_END) == 1, "%s: record not terminated", sc->name);
            CHECK(count_sub(rec, recLen, "backtrace:") == 1, "%s: record has no backtrace", sc->name);
        }
    } else if (sc->want_record == 0) {
        CHECK(rec == NULL, "%s: unexpected crash record", sc->name);
    }
    if (sc->rec_sub != NULL)
        CHECK(count_sub(rec, recLen, sc->rec_sub) > 0, "%s: crash record missing \"%s\"",
              sc->name, sc->rec_sub);
    if (sc->rec_absent != NULL)
        CHECK(count_sub(rec, recLen, sc->rec_absent) == 0, "%s: crash record must not contain \"%s\"",
              sc->name, sc->rec_absent);
    if (sc->log_sub != NULL) {
        int n = count_sub(log, logLen, sc->log_sub);
        if (sc->log_n >= 0)
            CHECK(n == sc->log_n, "%s: session log has %d x \"%s\", want %d",
                  sc->name, n, sc->log_sub, sc->log_n);
        else
            CHECK(n > 0, "%s: session log missing \"%s\"", sc->name, sc->log_sub);
    }
    if (sc->no_logfile)
        CHECK(log == NULL, "%s: session log written without a logger", sc->name);
    if (sc->max_ms > 0)
        CHECK(ms >= sc->min_ms && ms <= sc->max_ms,
              "%s: duration %u ms outside [%u, %u]", sc->name, ms, sc->min_ms, sc->max_ms);

    free(rec);
    free(log);
}

// The handler formatters against the printf oracle. bb_fmt_hex is fixed-width by contract.
static void test_fmt_helpers(void)
{
    static const unsigned long long V[] = {
        0, 1, 7, 9, 10, 11, 99, 100, 101, 4096, 65535,
        4294967295ull, 4294967296ull, 999999999999999999ull,
        0xdeadbeefull, 0x8000000000000000ull, 18446744073709551615ull,
    };
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        char got[24], want[24];
        size_t n = bb_fmt_dec(got, V[i]);
        got[n] = 0;
        snprintf(want, sizeof want, "%llu", V[i]);
        CHECK(strcmp(got, want) == 0, "bb_fmt_dec(%llu) = \"%s\", want \"%s\"", V[i], got, want);

        char gotx[24], wantx[24];
        size_t nx = bb_fmt_hex(gotx, V[i]);
        gotx[nx] = 0;
        snprintf(wantx, sizeof wantx, "0x%016llx", V[i]);
        CHECK(nx == 18, "bb_fmt_hex(%llu) length %zu, want 18", V[i], nx);
        CHECK(strcmp(gotx, wantx) == 0, "bb_fmt_hex(%llu) = \"%s\", want \"%s\"", V[i], gotx, wantx);
    }
}

// Init in THIS process: returns 0, resolves <gamepath>/logs/<stamp>_CRASH.log, creates no file
// on a clean boot.
static void test_init_shape(void)
{
    remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
    CHECK(ano_log_crash_init() == 0, "ano_log_crash_init failed");
    char name[MAXPATH];
    CHECK(bb_scan_suffix(CRASH_DIR, CRASH_SUFFIX, name) == 0,
          "init created a crash record on a clean boot");
    size_t n = strlen(bb_crashPath), sl = strlen(CRASH_SUFFIX);
    CHECK(n >= sl && strcmp(bb_crashPath + n - sl, CRASH_SUFFIX) == 0,
          "bb_crashPath \"%s\" does not end in " CRASH_SUFFIX, bb_crashPath);
    CHECK(strstr(bb_crashPath, ano_fs_session_stamp()) != NULL,
          "bb_crashPath \"%s\" does not carry the session stamp %s",
          bb_crashPath, ano_fs_session_stamp());
    ano_fspath gp = ano_fs_gamepath();
    if (gp.length > 0)
        CHECK(strncmp(bb_crashPath, gp.str, gp.length) == 0,
              "bb_crashPath \"%s\" not rooted in the exe dir \"%s\"", bb_crashPath, gp.str);
}

// A second crash must never destroy the first record: per-session stamped files, collisions
// append. The total across files is the oracle.
static void test_append(void)
{
    remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
    remove_all_suffix(LOG_DIR, LOG_SUFFIX);
    unsigned ms;
    child_status_t s1 = spawn_child("segv_write", &ms);
    child_status_t s2 = spawn_child("segv_write", &ms);
    CHECK(death_matches(DIE(0xC0000005, SIG_SEGVISH), s1), "append: first crash died wrong");
    CHECK(death_matches(DIE(0xC0000005, SIG_SEGVISH), s2), "append: second crash died wrong");
    int begins = 0, ends = 0;
    char path[MAXPATH + 40];
    while (find_suffix(CRASH_DIR, CRASH_SUFFIX, path, sizeof path)) {   // destructive sum, cleans as it goes
        size_t len = 0;
        char *rec = read_all(path, &len);
        begins += count_sub(rec, len, REC_BEGIN);
        ends   += count_sub(rec, len, REC_END);
        free(rec);
        if (remove(path) != 0)
            break;
    }
    CHECK(begins == 2 && ends == 2,
          "append: want 2 complete records across sessions, have %d/%d begin/end", begins, ends);
}

// Stage 4: no crash record -> silence, a crashed flight -> the next boot announces and leaves it.
static void test_stage4(void)
{
    unsigned ms;
    char name[MAXPATH], logPath[MAXPATH + 40];
    size_t len = 0;

    remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
    remove_all_suffix(LOG_DIR, LOG_SUFFIX);
    child_status_t c = spawn_child("clean", &ms);
    CHECK(death_matches(DIE(0, 0), c), "stage4: control clean run died");
    char *log = find_suffix(LOG_DIR, LOG_SUFFIX, logPath, sizeof logPath)
              ? read_all(logPath, &len) : NULL;
    CHECK(count_sub(log, len, "crash log") == 0, "stage4: announcement without a crash record");
    free(log);

    child_status_t s = spawn_child("segv_write", &ms);
    CHECK(death_matches(DIE(0xC0000005, SIG_SEGVISH), s), "stage4: crash child died wrong");
    CHECK(bb_scan_suffix(CRASH_DIR, CRASH_SUFFIX, name) == 1, "stage4: crash left no record");

    remove_all_suffix(LOG_DIR, LOG_SUFFIX);
    c = spawn_child("clean", &ms);
    CHECK(death_matches(DIE(0, 0), c), "stage4: post-crash clean run died");
    log = find_suffix(LOG_DIR, LOG_SUFFIX, logPath, sizeof logPath)
        ? read_all(logPath, &len) : NULL;
    CHECK(count_sub(log, len, "crash log detected") > 0, "stage4: previous crash not announced");
    free(log);
    CHECK(bb_scan_suffix(CRASH_DIR, CRASH_SUFFIX, name) == 1,
          "stage4: record not preserved for the investigation");
}

// Retention: boot prunes each log type to the newest 4, the live session's files excepted.
// Six crashes leave 5 records on disk (each boot prunes to 4 before its own crash adds one);
// the closing clean boot prunes back down to exactly 4.
static void test_retention(void)
{
    remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
    remove_all_suffix(CRASH_DIR, LOG_SUFFIX);
    remove_all_suffix(LOG_DIR, LOG_SUFFIX);
    unsigned ms;
    for (int i = 0; i < 6; i++) {
        child_status_t s = spawn_child("segv_write", &ms);
        CHECK(death_matches(DIE(0xC0000005, SIG_SEGVISH), s), "retention: crash child %d died wrong", i);
    }
    child_status_t c = spawn_child("clean", &ms);
    CHECK(death_matches(DIE(0, 0), c), "retention: clean run died");
    char name[MAXPATH];
    CHECK(bb_scan_suffix(CRASH_DIR, CRASH_SUFFIX, name) == 4,
          "retention: want 4 crash records after pruning, have %d",
          bb_scan_suffix(CRASH_DIR, CRASH_SUFFIX, name));
    CHECK(bb_scan_suffix(CRASH_DIR, LOG_SUFFIX, name) <= 5,
          "retention: session logs not pruned, have %d",
          bb_scan_suffix(CRASH_DIR, LOG_SUFFIX, name));
}

// Prune order: the OLDEST records by last-write time die first, never the newest. Six fabricated
// records get mtimes in REVERSE name order, so a name-ordered prune would keep the wrong four.
static void test_prune_order(void)
{
    static const char *names[] = {
        "2001-01-01_000001_CRASH.log", "2002-01-01_000001_CRASH.log",
        "2003-01-01_000001_CRASH.log", "2004-01-01_000001_CRASH.log",
        "2005-01-01_000001_CRASH.log", "2006-01-01_000001_CRASH.log",
    };
    remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
    time_t base = time(NULL) - 7200;
    char path[MAXPATH + 40];
    for (int i = 0; i < 6; i++) {
        snprintf(path, sizeof path, "%s/%s", CRASH_DIR, names[i]);
        FILE *f = fopen(path, "w");
        CHECK(f != NULL, "prune_order: cannot fabricate %s", names[i]);
        if (f != NULL) { fputs("fabricated record\n", f); fclose(f); }
        set_mtime(path, base + (time_t)(5 - i) * 600);  // names[0] newest ... names[5] oldest
    }

    unsigned ms;
    child_status_t c = spawn_child("clean", &ms);
    CHECK(death_matches(DIE(0, 0), c), "prune_order: clean run died");

    // The four newest by mtime survive (names[0..3]), the two oldest are gone (names[4..5]).
    for (int i = 0; i < 6; i++) {
        snprintf(path, sizeof path, "%s/%s", CRASH_DIR, names[i]);
        FILE *f = fopen(path, "rb");
        if (i < 4)
            CHECK(f != NULL, "prune_order: newest-by-mtime %s was deleted", names[i]);
        else
            CHECK(f == NULL, "prune_order: oldest-by-mtime %s survived", names[i]);
        if (f != NULL) fclose(f);
    }
    remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
}

int main(int argc, char **argv)
{
    if (!scratch_anchor_to_exe()) {
        printf("FAIL: cannot anchor to the exe directory\n");
        return 1;
    }
    if (argc > 1)
        return child_main(argv[1]);

    test_fmt_helpers();
    test_init_shape();

    ano_fspath gp = ano_fs_gamepath();
    if (gp.length == 0) {
        printf("FAIL: cannot resolve the exe path for self-spawning\n");
        return 1;
    }
#if defined(_WIN32)
    snprintf(g_exe, sizeof g_exe, "%s\\anotest_blackbox.exe", gp.str);
#else
    snprintf(g_exe, sizeof g_exe, "%s/anotest_blackbox", gp.str);
#endif

    scratch_make_dir(LOG_DIR);
    for (size_t i = 0; i < NSCEN; i++)
        run_scenario(&SCENARIOS[i]);
    test_append();
    test_stage4();
    test_retention();
    test_prune_order();

    // Full sweep: crash records, scratch session logs, and the children's default-init session
    // logs under logs/.
    remove_all_suffix(CRASH_DIR, CRASH_SUFFIX);
    remove_all_suffix(CRASH_DIR, LOG_SUFFIX);
    remove_all_suffix(LOG_DIR, LOG_SUFFIX);
    scratch_remove_dir(LOG_DIR);

    if (failures == 0) { printf("anotest_blackbox: all checks passed\n"); return 0; }
    printf("anotest_blackbox: %d check(s) failed\n", failures);
    return 1;
}
