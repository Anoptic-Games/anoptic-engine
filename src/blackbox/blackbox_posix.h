/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The shared POSIX implementation, included by exactly one TU per platform (blackbox_linux.c /
// blackbox_macos.c). sigaction + sigaltstack + <execinfo.h> are identical on both today; per-platform
// divergence (mcontext register dumps) gets its own home the day someone wants it.

#ifndef BLACKBOX_POSIX_H
#define BLACKBOX_POSIX_H

#include <anoptic_logging.h>
#include "blackbox/blackbox_internal.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

// From handler entry the process has BB_DEADMAN_S seconds to finish dying: SIGALRM keeps its default
// disposition (terminate), so a wedged record write or hail-mary flush becomes an exit, never a hang.
#define BB_DEADMAN_S 5u

// The hooked set. "SIGSEGV, SIGABRT, SIGFPE, etc." -- etc. being SIGILL and SIGBUS.
static const struct { int sig; const char *name; } bb_hooked[] = {
    { SIGSEGV, "SIGSEGV" },
    { SIGABRT, "SIGABRT" },
    { SIGFPE,  "SIGFPE"  },
    { SIGILL,  "SIGILL"  },
    { SIGBUS,  "SIGBUS"  },
};
#define BB_NHOOKED (sizeof bb_hooked / sizeof bb_hooked[0])

// One crashing thread owns the record; latecomers park forever (the deadman or the re-raise kills
// the process out from under them).
static atomic_int bb_entered;

// The handler runs here so a blown stack can still be reported. Static: nothing allocates at crash time.
static char bb_altStack[64 * 1024];

// write() until done or dead. Async-signal-safe, ignores failure -- there is no one left to tell.
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

// Stage 2 then Stage 3. Async-signal-safe calls only, up until the hail mary -- which is why the
// hail mary goes last: by then the record is already on disk and a deadlock costs nothing the
// deadman can't collect.
static void bb_handler(int sig, siginfo_t *info, void *uctx)
{
    (void)uctx;
    if (atomic_exchange(&bb_entered, 1) != 0)
        for (;;) pause();

    alarm(BB_DEADMAN_S);

    // Disarm every hook first: a second fault below terminates instead of recursing.
    for (size_t i = 0; i < BB_NHOOKED; i++) {
        struct sigaction dfl = { .sa_handler = SIG_DFL };
        sigaction(bb_hooked[i].sig, &dfl, NULL);
    }

    const char *name = "unhooked signal";
    for (size_t i = 0; i < BB_NHOOKED; i++)
        if (bb_hooked[i].sig == sig) { name = bb_hooked[i].name; break; }

    // Stage 2: the flight record, raw syscalls only. O_APPEND: never destroys a previous record.
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
    // si_code <= 0 is user-generated (raise/kill): si_addr holds garbage there, not a fault address.
    if (info->si_code > 0 && (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE)) {
        bb_puts(fd, "fault address: ");
        bb_hex(fd, (uintptr_t)info->si_addr);
        bb_puts(fd, "\n");
    }
    // Innermost frames are this handler + the kernel trampoline; the crash site sits right below.
    // Static symbols print as module+offset -- addr2line/atos turns those into names offline.
    void *frames[64];
    int n = backtrace(frames, 64);
    bb_puts(fd, "backtrace:\n");
    backtrace_symbols_fd(frames, n, fd);
    bb_puts(fd, "==== end of record ====\n");
    fsync(fd);
    if (fd != STDERR_FILENO) {
        close(fd);
        bb_puts(STDERR_FILENO, "anoptic: fatal ");
        bb_puts(STDERR_FILENO, name);
        bb_puts(STDERR_FILENO, ", blackbox record written to CRASH.log\n");
    }

    // Stage 3: the hail mary.
    ano_log_flush();

    // Re-raise with default disposition: the true exit status, the core dump, the works.
    sigset_t un;
    sigemptyset(&un);
    sigaddset(&un, sig);
    pthread_sigmask(SIG_UNBLOCK, &un, NULL);
    raise(sig);
    _exit(128 + sig);   // unreachable unless the default action was somehow suppressed
}

// Inputs: none. Output: 0 when every hook installed, -1 if any refused (the rest stay armed).
int bb_install(void)
{
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

#endif // BLACKBOX_POSIX_H
