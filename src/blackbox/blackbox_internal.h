/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private module header: state shared between the common TU and the per-platform hooks, plus the async-signal-safe formatters.

#ifndef BLACKBOX_INTERNAL_H
#define BLACKBOX_INTERNAL_H

#include <anoptic_filesystem.h>   // MAXPATH

#include <stddef.h>
#include <string.h>

// "<gamedir>/logs/<session-stamp>_CRASH.log", NUL-terminated. Resolved once by ano_blackbox_init, never inside a handler. Fallbacks: <gamedir>, then a CWD-relative name.
extern char bb_crashPath[];

// Stage 1, per-platform: install the fatal hooks. Output: 0 on success, -1 if any failed.
int bb_install(void);

// Stage 4 helper, per-platform: count `dir` entries ending in `suffix`, copying the lexicographically last into newest[MAXPATH]. Calm time only (init, tests). Output: the count, 0 when none or no dir.
int bb_scan_suffix(const char *dir, const char *suffix, char *newest);

// Per-thread Stage 1, per-platform: arm/release the calling thread's crash stack (see ano_blackbox_thread_arm). Output: 0 on success, -1 if the OS refused.
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

#endif // BLACKBOX_INTERNAL_H
