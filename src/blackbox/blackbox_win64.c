/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Win64: crashes arrive as SEH exceptions, not signals -- SetUnhandledExceptionFilter is the hook.
// The CRT's signal machinery bypasses SEH entirely: abort() and any raise(SIGSEGV/SIGILL/SIGFPE)
// (ours or a library's) dispatch straight to signal() handlers, so CRT hooks for those four ride
// alongside. One catch, measured on MinGW/UCRT: once those hooks exist, the CRT's own exception
// translator hands some REAL hardware faults (AV, illegal instruction, FLT_*) to them ahead of the
// unhandled filter, parking the faulting EXCEPTION_POINTERS in the per-thread _pxcptinfoptrs slot.
// The slot is the distinguisher: non-NULL = translated hardware fault (full SEH decode, die with the
// true NTSTATUS), NULL = genuine raise() (record the signal, CRT default exit). The deadman is a
// watchdog thread parked on an event since init: SetEvent is one kernel call a dying thread can
// afford, while CreateThread at crash time (it allocates) is not.

#include <anoptic_logging.h>
#include "blackbox/blackbox_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

// Once the event signals, the process has BB_DEADMAN_MS to finish dying: a wedged record write or
// hail-mary flush becomes an exit, never a hang.
#define BB_DEADMAN_MS 5000u

// Stack the kernel guarantees an EXCEPTION_STACK_OVERFLOW filter on an armed thread: enough for
// the record write (file open, 64 frames, module names) and the hail mary's locks. Without it the
// filter runs on whatever sliver survived the guard page -- a record by luck, not by contract.
#define BB_STACK_GUARANTEE (64u * 1024u)

static atomic_int bb_entered;
static HANDLE     bb_deadmanEvent;

static DWORD WINAPI bb_watchdog(LPVOID arg)
{
    (void)arg;
    WaitForSingleObject(bb_deadmanEvent, INFINITE);
    Sleep(BB_DEADMAN_MS);
    TerminateProcess(GetCurrentProcess(), 3);   // 3: abort()'s exit code -- died crashing, not hanging
    return 0;
}

// WriteFile until done or dead. Ignores failure -- there is no one left to tell.
static void bb_write_all(HANDLE h, const char *buf, size_t len)
{
    while (len > 0) {
        DWORD wrote = 0;
        if (!WriteFile(h, buf, (DWORD)len, &wrote, NULL) || wrote == 0)
            return;
        buf += wrote;
        len -= wrote;
    }
}

static void bb_puts(HANDLE h, const char *s) { bb_write_all(h, s, strlen(s)); }
static void bb_dec(HANDLE h, unsigned long long v) { char b[20]; bb_write_all(h, b, bb_fmt_dec(b, v)); }
static void bb_hex(HANDLE h, unsigned long long v) { char b[18]; bb_write_all(h, b, bb_fmt_hex(b, v)); }

// Exception code -> name, the usual suspects only.
static const char *bb_code_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:        return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION:      return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:          return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_IN_PAGE_ERROR:         return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    default:                              return "unrecognized exception";
    }
}

// One frame as "module+0xoffset [0xaddress]". ASLR makes bare addresses useless; module+offset is
// what WinDbg and addr2line want. GetModuleHandleExA/GetModuleFileNameA allocate nothing.
static void bb_frame(HANDLE h, void *addr)
{
    bb_puts(h, "  ");
    HMODULE mod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod)
        && mod != NULL) {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(mod, path, sizeof path);
        const char *base = path;    // basename only: the directory noise helps no one
        for (DWORD i = 0; i < n; i++)
            if (path[i] == '\\' || path[i] == '/') base = path + i + 1;
        bb_puts(h, n > 0 ? base : "?");
        bb_puts(h, "+");
        bb_hex(h, (uintptr_t)addr - (uintptr_t)mod);
    } else {
        bb_puts(h, "?");
    }
    bb_puts(h, " [");
    bb_hex(h, (uintptr_t)addr);
    bb_puts(h, "]\n");
}

// Stage 2 then Stage 3, shared by the SEH filter and the CRT signal hooks. xp == NULL means a CRT
// signal; signame says which. The record is on disk before the hail mary, so a deadlocked flush
// costs nothing the deadman can't collect.
static void bb_record_and_flush(const EXCEPTION_POINTERS *xp, const char *signame)
{
    SetEvent(bb_deadmanEvent);

    // Stage 2: the flight record. FILE_APPEND_DATA: never destroys a previous record.
    HANDLE h = CreateFileA(bb_crashPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool haveFile = h != INVALID_HANDLE_VALUE;
    if (!haveFile)
        h = GetStdHandle(STD_ERROR_HANDLE);
    bb_puts(h, "==== ANOPTIC BLACKBOX: we are going down ====\nunix time: ");
    bb_dec(h, (unsigned long long)time(NULL));
    bb_puts(h, "\n");
    if (xp != NULL) {
        const EXCEPTION_RECORD *xr = xp->ExceptionRecord;
        bb_puts(h, "exception: ");
        bb_hex(h, xr->ExceptionCode);
        bb_puts(h, " (");
        bb_puts(h, bb_code_name(xr->ExceptionCode));
        bb_puts(h, ")\nfault address: ");
        bb_hex(h, (uintptr_t)xr->ExceptionAddress);
        bb_puts(h, "\n");
        if ((xr->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
             || xr->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && xr->NumberParameters >= 2) {
            bb_puts(h, xr->ExceptionInformation[0] == 1 ? "access: write at "
                     : xr->ExceptionInformation[0] == 8 ? "access: execute at "
                                                        : "access: read at ");
            bb_hex(h, xr->ExceptionInformation[1]);
            bb_puts(h, "\n");
        }
    } else {
        bb_puts(h, "signal: ");
        bb_puts(h, signame);
        bb_puts(h, "\n");
    }
    // Innermost frames are the blackbox + the exception dispatcher; the crash site sits a few below.
    void *frames[64];
    USHORT n = CaptureStackBackTrace(0, 64, frames, NULL);
    bb_puts(h, "backtrace:\n");
    for (USHORT i = 0; i < n; i++)
        bb_frame(h, frames[i]);
    bb_puts(h, "==== end of record ====\n");
    if (haveFile) {
        FlushFileBuffers(h);
        CloseHandle(h);
        bb_puts(GetStdHandle(STD_ERROR_HANDLE),
                "anoptic: fatal exception, blackbox record written to CRASH.log\n");
    }

    // Stage 3: the hail mary.
    ano_log_flush();
}

// The last stop before WER. CONTINUE_SEARCH after the record: the process still dies its normal
// death -- debugger, WER, exit code all intact.
static LONG WINAPI bb_filter(EXCEPTION_POINTERS *xp)
{
    if (atomic_exchange(&bb_entered, 1) != 0)
        for (;;) Sleep(INFINITE);   // a second crashing thread parks; the first owns the record
    bb_record_and_flush(xp, NULL);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The CRT-signal set: abort() and CRT-raised faults. A real fault takes SEH, never this table.
static const struct { int sig; const char *name; } bb_crtHooked[] = {
    { SIGABRT, "SIGABRT (abort)" },
    { SIGSEGV, "SIGSEGV (CRT raise)" },
    { SIGILL,  "SIGILL (CRT raise)" },
    { SIGFPE,  "SIGFPE (CRT raise)" },
};
#define BB_NCRT (sizeof bb_crtHooked / sizeof bb_crtHooked[0])

// UCRT's per-thread exception-pointers slot, set by the CRT translator before it dispatches a
// hardware fault to a signal() handler. MinGW's <signal.h> does not surface it.
#ifndef _pxcptinfoptrs
void **__cdecl __pxcptinfoptrs(void);
#define _pxcptinfoptrs (*__pxcptinfoptrs())
#endif

// Everything the CRT signal machinery delivers: genuine raise()/abort(), and the hardware faults the
// CRT translator claims ahead of the SEH filter (see the file banner).
static void bb_on_signal(int sig)
{
    if (atomic_exchange(&bb_entered, 1) != 0)
        for (;;) Sleep(INFINITE);
    const EXCEPTION_POINTERS *xp = (const EXCEPTION_POINTERS *)_pxcptinfoptrs;
    if (xp != NULL && xp->ExceptionRecord != NULL) {
        // A translated hardware fault: the full SEH record is available, use it, and exit with the
        // true NTSTATUS (returning would refault into the consumed reentry gate; WER's terminate is
        // what this replaces).
        bb_record_and_flush(xp, NULL);
        TerminateProcess(GetCurrentProcess(), xp->ExceptionRecord->ExceptionCode);
    }
    const char *name = "CRT signal";
    for (size_t i = 0; i < BB_NCRT; i++)
        if (bb_crtHooked[i].sig == sig) { name = bb_crtHooked[i].name; break; }
    bb_record_and_flush(NULL, name);
    signal(sig, SIG_DFL);
    raise(sig);     // default disposition: the CRT's own exit for this signal
}

// Inputs: none. Output: 0 when the deadman and every hook armed, -1 otherwise.
int bb_install(void)
{
    bb_deadmanEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (bb_deadmanEvent == NULL)
        return -1;
    HANDLE wd = CreateThread(NULL, 64 * 1024, bb_watchdog, NULL, 0, NULL);
    if (wd == NULL)
        return -1;
    CloseHandle(wd);
    (void)bb_thread_arm();  // main's stack-overflow guarantee; refusal is not worth failing install
    SetUnhandledExceptionFilter(bb_filter);
    int rc = 0;
    for (size_t i = 0; i < BB_NCRT; i++)
        if (signal(bb_crtHooked[i].sig, bb_on_signal) == SIG_ERR)
            rc = -1;
    return rc;
}

// The guarantee is per-thread kernel state: set at spawn (ano_thread_create routes here), nothing
// to release -- it dies with the thread. Idempotent: re-arming asks for the same reservation.
int bb_thread_arm(void)
{
    ULONG g = BB_STACK_GUARANTEE;
    return SetThreadStackGuarantee(&g) ? 0 : -1;
}

void bb_thread_disarm(void)
{
}
