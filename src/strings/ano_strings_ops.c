/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Byte-level operations over anostr_t values: find, concat, join, split. 
// All total (bad or oversized input yields the empty string / ANOSTR_NPOS, never UB), all following the module's
#include "strings/ano_strings_internal.h"

size_t anostr_find(anostr_t s, anostr_t needle, size_t from)
{
    if (from > s.len)
        from = s.len;
    if (needle.len == 0)
        return from;
    if (needle.len > s.len || from > s.len - needle.len)
        return ANOSTR_NPOS;

    const char *hay = anostr_bytes(&s);
    const char *nd  = anostr_bytes(&needle);
    size_t last = s.len - needle.len;       // last viable start index
    for (size_t i = from; i <= last; i++) {
        // memchr skips to the next candidate first byte; the window above bounds it.
        const char *hit = memchr(hay + i, nd[0], last - i + 1);
        if (hit == NULL)
            return ANOSTR_NPOS;
        i = (size_t)(hit - hay);
        if (memcmp(hay + i, nd, needle.len) == 0)
            return i;
    }
    return ANOSTR_NPOS;
}

anostr_t anostr_join(mi_heap_t *heap, anostr_t sep, const anostr_t *parts, size_t count)
{
    if (count == 0 || parts == NULL)
        return anostr_empty();

    uint64_t total = (uint64_t)sep.len * (count - 1);
    for (size_t i = 0; i < count; i++)
        total += parts[i].len;
    if (total > UINT32_MAX)
        return anostr_empty();

    // Inline result: assemble on the stack, no allocator involved.
    if (total <= ANOSTR_INLINE_CAP) {
        char buf[ANOSTR_INLINE_CAP];
        size_t off = 0;
        for (size_t i = 0; i < count; i++) {
            memcpy(buf + off, anostr_bytes(&parts[i]), parts[i].len);
            off += parts[i].len;
            if (i + 1 < count) {
                memcpy(buf + off, anostr_bytes(&sep), sep.len);
                off += sep.len;
            }
        }
        return anostr_make_inline_(buf, (size_t)total);
    }

    if (heap == NULL)
        return anostr_empty();
    char *dst = mi_heap_malloc(heap, (size_t)total);
    if (dst == NULL)
        return anostr_empty();

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(dst + off, anostr_bytes(&parts[i]), parts[i].len);
        off += parts[i].len;
        if (i + 1 < count) {
            memcpy(dst + off, anostr_bytes(&sep), sep.len);
            off += sep.len;
        }
    }
    return anostr_make_long_(dst, (size_t)total);
}

anostr_t anostr_concat(mi_heap_t *heap, anostr_t a, anostr_t b)
{
    anostr_t parts[2] = { a, b };
    return anostr_join(heap, anostr_empty(), parts, 2);
}

bool anostr_split_next(anostr_split_t *it, anostr_t *piece)
{
    if (it->done)
        return false;

    if (it->sep.len == 0) {     // no separator to cut on: the whole string, once
        *piece = it->src;
        it->done = true;
        return true;
    }

    size_t idx = anostr_find(it->src, it->sep, it->pos);
    if (idx == ANOSTR_NPOS) {
        *piece = anostr_slice(it->src, it->pos, it->src.len);
        it->done = true;        // the final piece (possibly empty, e.g. a trailing sep)
        return true;
    }
    *piece = anostr_slice(it->src, it->pos, idx);
    it->pos = idx + it->sep.len;
    return true;
}
