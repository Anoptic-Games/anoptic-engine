/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Win64: SEH via SetUnhandledExceptionFilter + CRT signal() for abort/raise.
// CRT translator may hand hardware faults to signal hooks first: _pxcptinfoptrs non-NULL = SEH decode + NTSTATUS exit, NULL = genuine raise().
// Deadman: watchdog thread parked on an event since init.

#include <anoptic_log.h>
#include "log/log_crash_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

// After event signal, BB_DEADMAN_MS to finish dying.
#define BB_DEADMAN_MS 5000u

// Stack guarantee for EXCEPTION_STACK_OVERFLOW filter: record write + hail-mary locks.
#define BB_STACK_GUARANTEE (64u * 1024u)

static atomic_int bb_entered;
static HANDLE     bb_deadmanEvent;

static DWORD WINAPI bb_watchdog(LPVOID arg)
{
    (void)arg;
    WaitForSingleObject(bb_deadmanEvent, INFINITE);
    Sleep(BB_DEADMAN_MS);
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

// WriteFile until done or dead.
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

// Exception code -> name.
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

// One frame "module+0xoffset [0xaddress]". GetModuleHandleExA/GetModuleFileNameA allocate nothing.
static void bb_frame(HANDLE h, void *addr)
{
    bb_puts(h, "  ");
    HMODULE mod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod)
        && mod != NULL) {
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(mod, path, sizeof path);
        const char *base = path;
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

// Stage 2 then 3. Shared by SEH filter and CRT signal hooks. xp == NULL => CRT signal (signame).
static void bb_record_and_flush(const EXCEPTION_POINTERS *xp, const char *signame)
{
    SetEvent(bb_deadmanEvent);

    // Stage 2: flight record. FILE_APPEND_DATA (never truncates).
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
    // Innermost frames are blackbox + dispatcher; crash site a few below.
    void *frames[64];
    USHORT n = CaptureStackBackTrace(0, 64, frames, NULL);
    bb_puts(h, "backtrace:\n");
    for (USHORT i = 0; i < n; i++)
        bb_frame(h, frames[i]);
    bb_puts(h, "==== end of record ====\n");
    if (haveFile) {
        FlushFileBuffers(h);
        CloseHandle(h);
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        bb_puts(err, "anoptic: fatal exception, blackbox record written to ");
        bb_puts(err, bb_crashPath);
        bb_puts(err, "\n");
    }

    // Stage 3: hail mary.
    ano_log_flush();
}

// Last stop before WER. CONTINUE_SEARCH keeps debugger/WER/exit intact.
static LONG WINAPI bb_filter(EXCEPTION_POINTERS *xp)
{
    if (atomic_exchange(&bb_entered, 1) != 0)
        for (;;) Sleep(INFINITE);   // second crasher parks
    bb_record_and_flush(xp, NULL);
    return EXCEPTION_CONTINUE_SEARCH;
}

// CRT-signal set: abort() and CRT-raised faults. Real faults take SEH.
static const struct { int sig; const char *name; } bb_crtHooked[] = {
    { SIGABRT, "SIGABRT (abort)" },
    { SIGSEGV, "SIGSEGV (CRT raise)" },
    { SIGILL,  "SIGILL (CRT raise)" },
    { SIGFPE,  "SIGFPE (CRT raise)" },
};
#define BB_NCRT (sizeof bb_crtHooked / sizeof bb_crtHooked[0])

// UCRT per-thread exception-pointers slot (CRT translator). MinGW <signal.h> omits it.
#ifndef _pxcptinfoptrs
void **__cdecl __pxcptinfoptrs(void);
#define _pxcptinfoptrs (*__pxcptinfoptrs())
#endif

// CRT signal delivery: raise()/abort(), and hardware faults claimed ahead of SEH (see banner).
static void bb_on_signal(int sig)
{
    if (atomic_exchange(&bb_entered, 1) != 0)
        for (;;) Sleep(INFINITE);
    const EXCEPTION_POINTERS *xp = (const EXCEPTION_POINTERS *)_pxcptinfoptrs;
    if (xp != NULL && xp->ExceptionRecord != NULL) {
        // Translated hardware fault: full SEH + NTSTATUS exit.
        bb_record_and_flush(xp, NULL);
        TerminateProcess(GetCurrentProcess(), xp->ExceptionRecord->ExceptionCode);
    }
    const char *name = "CRT signal";
    for (size_t i = 0; i < BB_NCRT; i++)
        if (bb_crtHooked[i].sig == sig) { name = bb_crtHooked[i].name; break; }
    bb_record_and_flush(NULL, name);
    signal(sig, SIG_DFL);
    raise(sig);
}

// Stage 4 scan, calm time. Suffix recheck guards 8.3 short-name hits.
int bb_scan_suffix(const char *dir, const char *suffix, char *newest)
{
    char pat[MAXPATH + 8];
    int n = snprintf(pat, sizeof pat, "%s\\*%s", dir, suffix);
    if (n <= 0 || n >= (int)sizeof pat)
        return 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    size_t sl = strlen(suffix);
    int count = 0;
    newest[0] = '\0';
    do {
        size_t nl = strlen(fd.cFileName);
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            || nl < sl || nl >= MAXPATH || strcmp(fd.cFileName + nl - sl, suffix) != 0)
            continue;
        count++;
        if (strcmp(fd.cFileName, newest) > 0)
            strcpy(newest, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}

// Pass 1: top-keep by mtime. Pass 2: remove the rest.
int bb_prune_suffix(const char *dir, const char *suffix, int keep, const char *skip)
{
    if (keep > 8) keep = 8;
    bb_prune_t top[8];
    int  nTop = 0;
    size_t sl = strlen(suffix), kl = skip != NULL ? strlen(skip) : 0;

    char pat[MAXPATH + 8];
    int n = snprintf(pat, sizeof pat, "%s\\*%s", dir, suffix);
    if (n <= 0 || n >= (int)sizeof pat)
        return 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        size_t nl = strlen(fd.cFileName);
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            || nl < sl || nl >= MAXPATH || strcmp(fd.cFileName + nl - sl, suffix) != 0)
            continue;
        unsigned long long t = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32)
                             | fd.ftLastWriteTime.dwLowDateTime;
        bb_top_insert(top, keep, &nTop, t, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    int removed = 0;
    do {
        size_t nl = strlen(fd.cFileName);
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            || nl < sl || nl >= MAXPATH || strcmp(fd.cFileName + nl - sl, suffix) != 0)
            continue;
        if (kl > 0 && strncmp(fd.cFileName, skip, kl) == 0)
            continue;   // live session
        bool kept = false;
        for (int i = 0; i < nTop && !kept; i++)
            kept = strcmp(fd.cFileName, top[i].name) == 0;
        if (kept)
            continue;
        char path[MAXPATH * 2 + 8];
        snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
        if (remove(path) == 0)
            removed++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return removed;
}

// 0 when deadman + every hook armed, -1 otherwise.
int bb_install(void)
{
    bb_deadmanEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (bb_deadmanEvent == NULL)
        return -1;
    HANDLE wd = CreateThread(NULL, 64 * 1024, bb_watchdog, NULL, 0, NULL);
    if (wd == NULL)
        return -1;
    CloseHandle(wd);
    (void)bb_thread_arm();  // main stack-overflow guarantee
    SetUnhandledExceptionFilter(bb_filter);
    int rc = 0;
    for (size_t i = 0; i < BB_NCRT; i++)
        if (signal(bb_crtHooked[i].sig, bb_on_signal) == SIG_ERR)
            rc = -1;
    return rc;
}

// Per-thread stack guarantee at spawn. Idempotent.
int bb_thread_arm(void)
{
    ULONG g = BB_STACK_GUARANTEE;
    return SetThreadStackGuarantee(&g) ? 0 : -1;
}

void bb_thread_disarm(void)
{
}
