/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Constructors shared by the strings module. Internal: callers uphold I1-I4.

#ifndef ANOPTIC_SRC_STRINGS_INTERNAL_H
#define ANOPTIC_SRC_STRINGS_INTERNAL_H

#include "anoptic_strings.h"

// Inline value from len <= 12 bytes. Starts all-zero so I3 holds.
static inline anostr_t anostr_make_inline_(const void *bytes, size_t len)
{
    anostr_t s = {0};
    s.len = (uint32_t)len;
    memcpy(s.prefix, bytes, len < 4 ? len : 4);
    if (len > 4)
        memcpy(s.suffix, (const char *)bytes + 4, len - 4);
    return s;
}

// Long value over len > 12 bytes already living somewhere durable.
static inline anostr_t anostr_make_long_(const char *bytes, size_t len)
{
    anostr_t s = {0};
    s.len = (uint32_t)len;
    memcpy(s.prefix, bytes, 4);
    s.ptr = bytes;
    return s;
}

// Interning table (shared with anostr_sym_sort's key cache). Single mutator.
struct anostr_intern_t {
    mi_heap_t *heap;
    uint32_t   count;       // interned strings, dense syms 0..count-1
    uint32_t   slotMask;    // slot capacity minus 1
    uint32_t  *slots;       // sym + 1, 0 marks empty
    uint64_t  *hashes;      // per-symbol cached hash
    anostr_t  *strs;        // canonical value per symbol
    uint32_t   arrCap;      // hashes/strs capacity
    // Collation-key cache, filled lazily by anostr_sym_sort. Watermark, no per-entry flag.
    uint64_t  *collateKeys; // per-symbol prefix key, [0 .. collateKeyed)
    uint32_t   collateKeyed;
    uint32_t   collateKeyCap;
};

#endif // ANOPTIC_SRC_STRINGS_INTERNAL_H
