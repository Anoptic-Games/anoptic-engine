/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The shared POSIX implementation, included by exactly one TU per platform (blackbox_linux.c /
// blackbox_macos.c). sigaction + sigaltstack + the frame-pointer walk are identical on both; the
// including TU supplies the two genuinely native pieces, forward-declared below: bb_modmap_build
// (snapshot the loaded modules, calm time only) and bb_crash_regs (dig pc/fp/lr out of the
// interrupted mcontext). No <execinfo.h>: glibc's backtrace() lazily dlopens libgcc_s and resolves
// exported symbols only, musl doesn't ship it at all. We walk the fp chain ourselves.

#ifndef BLACKBOX_POSIX_H
#define BLACKBOX_POSIX_H

#include <anoptic_logging.h>
#include <anoptic_memory.h>
#include "blackbox/blackbox_internal.h"

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

// One crash stack's worth: room for the handler, the frame walk, and the hail mary's locks.
#define BB_ALTSTACK_SZ (64u * 1024u)

// The handler runs here so a blown stack can still be reported. Static: nothing allocates at crash time.
static char bb_altStack[BB_ALTSTACK_SZ];

/* ---- the module map: name every pc without touching a loader at crash time ---- */

// One executable range of one loaded module (pcs land nowhere else). base is the load bias, so
// pc - base is the link-time address addr2line -e / atos -o consume directly. Filled once by the
// platform's bb_modmap_build at init and only binary-searched in the handler: both native loaders
// (dl_iterate_phdr, dyld) lock, so the map is NEVER rebuilt at crash time. Modules dlopen'd after
// init (GPU driver ICDs) print as "?".
typedef struct {
    uintptr_t lo, hi;       // [lo, hi) of the mapped executable segment
    uintptr_t base;         // load bias of the owning module
    char      name[40];     // basename, truncated: the directory noise helps no one
} bb_mod_t;

#define BB_MAXMODS 256
static bb_mod_t bb_mods[BB_MAXMODS];
static int      bb_nmods;

// The two native pieces, defined by the including platform TU.
static void bb_modmap_build(void);
static void bb_crash_regs(void *uctx, uintptr_t *pc, uintptr_t *fp, uintptr_t *lr);

// Insert one executable range, kept sorted by lo. Calm time only. A full table drops the rest:
// "?" in a record is honest, a table resize at crash time is not.
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

// Ranges are disjoint and sorted: lower-bound on hi, then confirm lo. NULL = no module owns pc.
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

/* ---- the walker: a backtrace with nothing under it but write(2) ---- */

// aarch64 PAC (arm64e system dylibs, -mbranch-protection distros) signs return addresses in the
// high bits; strip to the 47-bit user VA before resolving. x86_64 passes through untouched.
#if defined(__aarch64__) || defined(__arm64__)
#define BB_PC_STRIP(a) ((a) & (((uintptr_t)1 << 47) - 1))
#else
#define BB_PC_STRIP(a) (a)
#endif

// write() to /dev/null as the memory probe: EFAULT on a wild frame pointer instead of a second
// fault inside the handler. Opened at init; if it never opened, the walk stops at the pc.
static int bb_probeFd = -1;

static bool bb_readable(uintptr_t addr, size_t len)
{
    return bb_probeFd >= 0 && write(bb_probeFd, (const void *)addr, len) == (ssize_t)len;
}

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

// One frame as "module+0xoffset [0xaddress]", matching the win64 record: ASLR makes bare addresses
// useless, and the offset is link-time relative -- addr2line -e (Linux) / atos -o (macOS) eat it raw.
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

// The crash backtrace, innermost first: the interrupted pc (the fault site itself), lr where the
// architecture keeps it in a register (an unspilled leaf's caller exists ONLY there; occasionally a
// duplicate frame, never a lie), then the fp chain -- fp[0] the caller's fp, fp[1] its return
// address, identical on x86_64 and aarch64. Every dereference is probed and every step must
// strictly ascend: a corrupt chain ends the walk, never the handler. Needs frame pointers, which
// the root CMakeLists pins on every UNIX build.
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
        if (next <= fp)     // strictly ascending or the chain is lying
            return;
        fp = next;
    }
}

// Stage 2 then Stage 3. Async-signal-safe calls only, up until the hail mary -- which is why the
// hail mary goes last: by then the record is already on disk and a deadlock costs nothing the
// deadman can't collect.
static void bb_handler(int sig, siginfo_t *info, void *uctx)
{
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
    // Straight from the interrupted context: frame zero IS the crash site, no handler noise above it.
    bb_puts(fd, "backtrace:\n");
    bb_backtrace(fd, uctx);
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
    // Calm-time prep the handler must never do itself: snapshot the loaded modules (the native
    // loaders lock) and open the probe fd (the walker's license to dereference).
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

// sigaltstack state is thread-local: bb_install only covered the installing (main) thread, with a
// static stack immune to heap state. Every other thread arms here at spawn -- calm time, so a heap
// stack is fine -- and releases at exit. Idempotent: a second arm on the same thread is a no-op.
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
    sigaltstack(&off, NULL);    // handlers fall back to the thread stack for the exit window
    ano_aligned_free(bb_threadAltStack);
    bb_threadAltStack = NULL;
}

#endif // BLACKBOX_POSIX_H
