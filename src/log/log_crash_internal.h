/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private: state shared by common TU and platform hooks, plus async-signal-safe formatters.

#ifndef LOG_CRASH_INTERNAL_H
#define LOG_CRASH_INTERNAL_H

#include <anoptic_filesystem.h>   // MAXPATH

#include <stddef.h>
#include <string.h>

// "<gamedir>/logs/<session-stamp>_CRASH.log". Resolved once by ano_log_crash_init, never in a handler.
extern char bb_crashPath[];

// Stage 1, per-platform: install fatal hooks. 0 ok, -1 if any failed.
int bb_install(void);

// Stage 4 helper: count `dir` entries ending in `suffix`, copy lex-last into newest[MAXPATH]. Calm time. Count or 0.
int bb_scan_suffix(const char *dir, const char *suffix, char *newest);

// Retention cap at boot: newest BB_KEEP_LOGS of each log type survive.
#define BB_KEEP_LOGS 4

// Boot prune: delete `suffix` entries keeping `keep` newest (cap 8) by mtime, skip names starting with `skip`. Calm time. Files removed.
int bb_prune_suffix(const char *dir, const char *suffix, int keep, const char *skip);

// One prune candidate: mtime (bigger = newer) + name.
typedef struct { unsigned long long mtime; char name[MAXPATH]; } bb_prune_t;

// Insert into descending top-`cap` for bb_prune_suffix. *n = live count.
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

// Per-thread Stage 1: arm/release calling thread's crash stack. 0 ok, -1 if OS refused.
int  bb_thread_arm(void);
void bb_thread_disarm(void);

// Decimal of v into out, returns length. Async-signal-safe.
static inline size_t bb_fmt_dec(char *out, unsigned long long v)
{
    char tmp[20];
    size_t i = 0;
    do { tmp[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (size_t k = 0; k < i; k++) out[k] = tmp[i - 1 - k];
    return i;
}

// "0x" + 16 hex digits of v into out, returns 18. Fixed width.
static inline size_t bb_fmt_hex(char *out, unsigned long long v)
{
    static const char dig[] = "0123456789abcdef";
    out[0] = '0'; out[1] = 'x';
    for (int k = 15; k >= 0; k--) { out[2 + k] = dig[v & 0xf]; v >>= 4; }
    return 18;
}

#endif // LOG_CRASH_INTERNAL_H
