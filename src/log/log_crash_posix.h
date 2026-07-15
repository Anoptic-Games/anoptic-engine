/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Shared POSIX impl, included by one TU per platform (linux/macos).
// Including TU supplies bb_modmap_build (calm) and bb_crash_regs (pc/fp/lr from mcontext).
// No <execinfo.h>: fp chain walked by hand.

#ifndef LOG_CRASH_POSIX_H
#define LOG_CRASH_POSIX_H

#include <anoptic_log.h>
#include <anoptic_memory.h>
#include "log/log_crash_internal.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// From handler entry, BB_DEADMAN_S to finish dying (SIGALRM default = terminate).
#define BB_DEADMAN_S 5u

// Hooked signals.
static const struct { int sig; const char *name; } bb_hooked[] = {
    { SIGSEGV, "SIGSEGV" },
    { SIGABRT, "SIGABRT" },
    { SIGFPE,  "SIGFPE"  },
    { SIGILL,  "SIGILL"  },
    { SIGBUS,  "SIGBUS"  },
};
#define BB_NHOOKED (sizeof bb_hooked / sizeof bb_hooked[0])

// One crasher owns the record. Latecomers park until deadman / re-raise.
static atomic_int bb_entered;

// Alt stack: handler + frame walk + hail-mary locks.
#define BB_ALTSTACK_SZ (64u * 1024u)

// Static alt stack: no alloc at crash, blown stack still reports.
static char bb_altStack[BB_ALTSTACK_SZ];

/* Module Map */

// Name every pc without touching a loader at crash time.
// One executable range. base = load bias (pc - base for addr2line/atos). Filled at init, binary-searched in handler. Post-init dlopen -> "?".
typedef struct {
    uintptr_t lo, hi;       // [lo, hi)
    uintptr_t base;         // load bias
    char      name[40];     // basename
} bb_mod_t;

#define BB_MAXMODS 256
static bb_mod_t bb_mods[BB_MAXMODS];
static int      bb_nmods;

// Native pieces from including platform TU.
static void bb_modmap_build(void);
static void bb_crash_regs(void *uctx, uintptr_t *pc, uintptr_t *fp, uintptr_t *lr);

// Insert executable range sorted by lo. Calm time. Full table drops rest.
static void bb_mod_add(uintptr_t lo, uintptr_t hi, uintptr_t base, const char *name)
{
    if (bb_nmods >= BB_MAXMODS || hi <= lo)
        return;
    int i = bb_nmods++;
    for (; i > 0 && bb_mods[i - 1].lo > lo; i--)
        bb_mods[i] = bb_mods[i - 1];
    bb_mods[i].lo = lo;
    bb_mods[i].hi = hi;
    bb_mods[i].base = base;
    const char *b = name;
    for (const char *p = name; *p; p++)
        if (*p == '/') b = p + 1;
    size_t n = strlen(b);
    if (n >= sizeof bb_mods[i].name)
        n = sizeof bb_mods[i].name - 1;
    memcpy(bb_mods[i].name, b, n);
    bb_mods[i].name[n] = 0;
}

// Disjoint+sorted: lower-bound on hi, confirm lo. NULL if none owns pc.
static const bb_mod_t *bb_mod_find(uintptr_t pc)
{
    int lo = 0, hi = bb_nmods;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (pc >= bb_mods[mid].hi) lo = mid + 1;
        else                       hi = mid;
    }
    return (lo < bb_nmods && pc >= bb_mods[lo].lo) ? &bb_mods[lo] : NULL;
}

/* Walker */

// Backtrace with nothing under it but write(2).
// aarch64 PAC: strip to 47-bit user VA. x86_64 passes through.

#if defined(__aarch64__) || defined(__arm64__)
#define BB_PC_STRIP(a) ((a) & (((uintptr_t)1 << 47) - 1))
#else
#define BB_PC_STRIP(a) (a)
#endif

// Probe: write() to /dev/null (EFAULT on wild fp). Opened at init. Unopened -> stop at pc.
static int bb_probeFd = -1;

static bool bb_readable(uintptr_t addr, size_t len)
{
    return bb_probeFd >= 0 && write(bb_probeFd, (const void *)addr, len) == (ssize_t)len;
}

// write() until done or dead. Async-signal-safe.
static void bb_write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0)
            return;
        buf += (size_t)n;
        len -= (size_t)n;
    }
}

static void bb_puts(int fd, const char *s) { bb_write_all(fd, s, strlen(s)); }
static void bb_dec(int fd, unsigned long long v) { char b[20]; bb_write_all(fd, b, bb_fmt_dec(b, v)); }
static void bb_hex(int fd, unsigned long long v) { char b[18]; bb_write_all(fd, b, bb_fmt_hex(b, v)); }

// One frame "module+0xoffset [0xaddress]" (matches win64). Offset for addr2line/atos.
static void bb_frame(int fd, uintptr_t pc)
{
    bb_puts(fd, "  ");
    const bb_mod_t *m = bb_mod_find(pc);
    if (m != NULL) {
        bb_puts(fd, m->name);
        bb_puts(fd, "+");
        bb_hex(fd, pc - m->base);
    } else {
        bb_puts(fd, "?");
    }
    bb_puts(fd, " [");
    bb_hex(fd, pc);
    bb_puts(fd, "]\n");
}

// Backtrace innermost-first: pc, lr (if register), then fp chain (fp[0]=caller fp, fp[1]=ret).
// Every deref probed, steps must strictly ascend. Corrupt chain ends walk, not handler. Needs frame pointers.
#define BB_MAXFRAMES 64
static void bb_backtrace(int fd, void *uctx)
{
    uintptr_t pc = 0, fp = 0, lr = 0;
    bb_crash_regs(uctx, &pc, &fp, &lr);
    pc = BB_PC_STRIP(pc);
    lr = BB_PC_STRIP(lr);
    bb_frame(fd, pc);
    if (lr != 0 && lr != pc)
        bb_frame(fd, lr);
    for (int i = 0; i < BB_MAXFRAMES; i++) {
        if (fp == 0 || (fp & (sizeof(uintptr_t) - 1)) != 0
            || !bb_readable(fp, 2 * sizeof(uintptr_t)))
            return;
        uintptr_t next = ((const uintptr_t *)fp)[0];
        uintptr_t ret  = BB_PC_STRIP(((const uintptr_t *)fp)[1]);
        if (ret == 0)
            return;
        bb_frame(fd, ret);
        if (next <= fp)     // strictly ascending
            return;
        fp = next;
    }
}

// Stage 2 then 3. Async-signal-safe until hail mary.
static void bb_handler(int sig, siginfo_t *info, void *uctx)
{
    if (atomic_exchange(&bb_entered, 1) != 0)
        for (;;) pause();

    alarm(BB_DEADMAN_S);

    // Disarm hooks first: second fault terminates, no recurse.
    for (size_t i = 0; i < BB_NHOOKED; i++) {
        struct sigaction dfl = { .sa_handler = SIG_DFL };
        sigaction(bb_hooked[i].sig, &dfl, NULL);
    }

    const char *name = "unhooked signal";
    for (size_t i = 0; i < BB_NHOOKED; i++)
        if (bb_hooked[i].sig == sig) { name = bb_hooked[i].name; break; }

    // Stage 2: flight record, raw syscalls. O_APPEND (never truncates).
    int fd = open(bb_crashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        fd = STDERR_FILENO;
    bb_puts(fd, "==== ANOPTIC BLACKBOX: we are going down ====\nunix time: ");
    bb_dec(fd, (unsigned long long)time(NULL));
    bb_puts(fd, "\nsignal: ");
    bb_dec(fd, (unsigned long long)sig);
    bb_puts(fd, " (");
    bb_puts(fd, name);
    bb_puts(fd, ")\n");
    // si_code <= 0: user-generated, si_addr garbage.
    if (info->si_code > 0 && (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE)) {
        bb_puts(fd, "fault address: ");
        bb_hex(fd, (uintptr_t)info->si_addr);
        bb_puts(fd, "\n");
    }
    // Interrupted context: frame zero = crash site.
    bb_puts(fd, "backtrace:\n");
    bb_backtrace(fd, uctx);
    bb_puts(fd, "==== end of record ====\n");
    fsync(fd);
    if (fd != STDERR_FILENO) {
        close(fd);
        bb_puts(STDERR_FILENO, "anoptic: fatal ");
        bb_puts(STDERR_FILENO, name);
        bb_puts(STDERR_FILENO, ", blackbox record written to ");
        bb_puts(STDERR_FILENO, bb_crashPath);
        bb_puts(STDERR_FILENO, "\n");
    }

    // Stage 3: hail mary.
    ano_log_flush();

    // Re-raise with default disposition.
    sigset_t un;
    sigemptyset(&un);
    sigaddset(&un, sig);
    pthread_sigmask(SIG_UNBLOCK, &un, NULL);
    raise(sig);
    _exit(128 + sig);
}

// Stage 4 scan, calm time.
int bb_scan_suffix(const char *dir, const char *suffix, char *newest)
{
    DIR *d = opendir(dir);
    if (d == NULL)
        return 0;
    size_t sl = strlen(suffix);
    int count = 0;
    newest[0] = '\0';
    for (struct dirent *e; (e = readdir(d)) != NULL; ) {
        size_t nl = strlen(e->d_name);
        if (nl < sl || nl >= MAXPATH || strcmp(e->d_name + nl - sl, suffix) != 0)
            continue;
        count++;
        if (strcmp(e->d_name, newest) > 0)
            strcpy(newest, e->d_name);
    }
    closedir(d);
    return count;
}

// Pass 1: top-keep by mtime. Pass 2: remove the rest.
int bb_prune_suffix(const char *dir, const char *suffix, int keep, const char *skip)
{
    if (keep > 8) keep = 8;
    bb_prune_t top[8];
    int  nTop = 0;
    size_t sl = strlen(suffix), kl = skip != NULL ? strlen(skip) : 0;

    DIR *d = opendir(dir);
    if (d == NULL)
        return 0;
    for (struct dirent *e; (e = readdir(d)) != NULL; ) {
        size_t nl = strlen(e->d_name);
        if (nl < sl || nl >= MAXPATH || strcmp(e->d_name + nl - sl, suffix) != 0)
            continue;
        char path[MAXPATH * 2 + 8];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        unsigned long long t = stat(path, &st) == 0 ? (unsigned long long)st.st_mtime : 0;
        bb_top_insert(top, keep, &nTop, t, e->d_name);
    }
    closedir(d);

    d = opendir(dir);
    if (d == NULL)
        return 0;
    int removed = 0;
    for (struct dirent *e; (e = readdir(d)) != NULL; ) {
        size_t nl = strlen(e->d_name);
        if (nl < sl || nl >= MAXPATH || strcmp(e->d_name + nl - sl, suffix) != 0)
            continue;
        if (kl > 0 && strncmp(e->d_name, skip, kl) == 0)
            continue;   // live session
        bool kept = false;
        for (int i = 0; i < nTop && !kept; i++)
            kept = strcmp(e->d_name, top[i].name) == 0;
        if (kept)
            continue;
        char path[MAXPATH * 2 + 8];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (remove(path) == 0)
            removed++;
    }
    closedir(d);
    return removed;
}

// 0 when every hook installed, -1 if any refused (rest stay armed).
int bb_install(void)
{
    // Calm prep: module snapshot + probe fd.
    bb_modmap_build();
    bb_probeFd = open("/dev/null", O_WRONLY | O_CLOEXEC);

    stack_t ss = { .ss_sp = bb_altStack, .ss_size = sizeof bb_altStack };
    if (sigaltstack(&ss, NULL) != 0)
        return -1;
    struct sigaction sa = { .sa_sigaction = bb_handler, .sa_flags = SA_SIGINFO | SA_ONSTACK };
    sigemptyset(&sa.sa_mask);
    int rc = 0;
    for (size_t i = 0; i < BB_NHOOKED; i++)
        if (sigaction(bb_hooked[i].sig, &sa, NULL) != 0)
            rc = -1;
    return rc;
}

// sigaltstack is thread-local: bb_install arms main only. Others arm at spawn / disarm at exit. Idempotent.
static _Thread_local char *bb_threadAltStack;

int bb_thread_arm(void)
{
    if (bb_threadAltStack != NULL)
        return 0;
    char *mem = ano_aligned_malloc(BB_ALTSTACK_SZ, 16);
    if (mem == NULL)
        return -1;
    stack_t ss = { .ss_sp = mem, .ss_size = BB_ALTSTACK_SZ };
    if (sigaltstack(&ss, NULL) != 0) {
        ano_aligned_free(mem);
        return -1;
    }
    bb_threadAltStack = mem;
    return 0;
}

void bb_thread_disarm(void)
{
    if (bb_threadAltStack == NULL)
        return;
    stack_t off = { .ss_flags = SS_DISABLE };
    sigaltstack(&off, NULL);
    ano_aligned_free(bb_threadAltStack);
    bb_threadAltStack = NULL;
}

#endif // LOG_CRASH_POSIX_H
