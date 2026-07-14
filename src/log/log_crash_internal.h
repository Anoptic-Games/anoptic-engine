/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private module header: state shared between the common TU and the per-platform hooks, plus the async-signal-safe formatters.

#ifndef LOG_CRASH_INTERNAL_H
#define LOG_CRASH_INTERNAL_H

#include <anoptic_filesystem.h>   // MAXPATH

#include <stddef.h>
#include <string.h>

// "<gamedir>/logs/<session-stamp>_CRASH.log", NUL-terminated. Resolved once by ano_log_crash_init, never inside a handler. Fallbacks: <gamedir>, then a CWD-relative name.
extern char bb_crashPath[];

// Stage 1, per-platform: install the fatal hooks. Output: 0 on success, -1 if any failed.
int bb_install(void);

// Stage 4 helper, per-platform: count `dir` entries ending in `suffix`, copying the lexicographically last into newest[MAXPATH]. Calm time only (init, tests). Output: the count, 0 when none or no dir.
int bb_scan_suffix(const char *dir, const char *suffix, char *newest);

// Retention cap applied at boot: newest BB_KEEP_LOGS files of each log type survive.
#define BB_KEEP_LOGS 4

// Boot housekeeping, per-platform: delete `dir` entries ending in `suffix`, keeping the `keep` newest (capped at 8) by last-write time, name as tiebreaker, plus any name starting with `skip` (the live session stamp, NULL to skip none). Oldest die first. Calm time only. Output: files removed.
int bb_prune_suffix(const char *dir, const char *suffix, int keep, const char *skip);

// One prune candidate: last-write time (platform units, bigger = newer) + name.
typedef struct { unsigned long long mtime; char name[MAXPATH]; } bb_prune_t;

// Insert a candidate into the descending top-`cap` array for bb_prune_suffix. *n is the live count.
static inline void bb_top_insert(bb_prune_t top[], int cap, int *n, unsigned long long mtime, const char *name)
{
    int i = *n;
    while (i > 0 && (mtime > top[i - 1].mtime
                     || (mtime == top[i - 1].mtime && strcmp(name, top[i - 1].name) > 0))) i--;
    if (i >= cap) return;
    int last = (*n < cap) ? *n : cap - 1;
    for (int k = last; k > i; k--) top[k] = top[k - 1];
    top[i].mtime = mtime;
    strcpy(top[i].name, name);
    if (*n < cap) (*n)++;
}

// Per-thread Stage 1, per-platform: arm/release the calling thread's crash stack (see ano_log_crash_thread_arm). Output: 0 on success, -1 if the OS refused.
int  bb_thread_arm(void);
void bb_thread_disarm(void);

// Decimal of v into out, returns length. Async-signal-safe, no printf machinery.
static inline size_t bb_fmt_dec(char *out, unsigned long long v)
{
    char tmp[20];
    size_t i = 0;
    do { tmp[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (size_t k = 0; k < i; k++) out[k] = tmp[i - 1 - k];
    return i;
}

// "0x" + 16 hex digits of v into out, returns length (18). Fixed width.
static inline size_t bb_fmt_hex(char *out, unsigned long long v)
{
    static const char dig[] = "0123456789abcdef";
    out[0] = '0'; out[1] = 'x';
    for (int k = 15; k >= 0; k--) { out[2 + k] = dig[v & 0xf]; v >>= 4; }
    return 18;
}

#endif // LOG_CRASH_INTERNAL_H
