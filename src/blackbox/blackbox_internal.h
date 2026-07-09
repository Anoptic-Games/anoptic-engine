/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private module header: state shared between the common TU and the per-platform hooks, plus the
// async-signal-safe formatters. Everything a crashing thread touches lives here or in its platform TU.

#ifndef BLACKBOX_INTERNAL_H
#define BLACKBOX_INTERNAL_H

#include <stddef.h>
#include <string.h>

// "<gamedir>/CRASH.log", NUL-terminated. Resolved once by ano_blackbox_init, never inside a handler
// (path resolution allocates and formats). Falls back to a CWD-relative "CRASH.log".
extern char bb_crashPath[];

// Stage 1, per-platform: install the fatal hooks. Output: 0 on success, -1 if any failed.
int bb_install(void);

// Decimal of v into out, returns length. No printf machinery: a handler may not malloc or lock.
static inline size_t bb_fmt_dec(char *out, unsigned long long v)
{
    char tmp[20];
    size_t i = 0;
    do { tmp[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (size_t k = 0; k < i; k++) out[k] = tmp[i - 1 - k];
    return i;
}

// "0x" + 16 hex digits of v into out, returns length (18). Fixed width: alignment over brevity.
static inline size_t bb_fmt_hex(char *out, unsigned long long v)
{
    static const char dig[] = "0123456789abcdef";
    out[0] = '0'; out[1] = 'x';
    for (int k = 15; k >= 0; k--) { out[2 + k] = dig[v & 0xf]; v >>= 4; }
    return 18;
}

#endif // BLACKBOX_INTERNAL_H
