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

anostr_t anostr_replace_all(mi_heap_t *heap, anostr_t s, anostr_t needle, anostr_t repl)
{
    if (needle.len == 0 || needle.len > s.len)
        return s;

    // Pass one: count non-overlapping matches.
    size_t matches = 0;
    for (size_t at = 0; (at = anostr_find(s, needle, at)) != ANOSTR_NPOS; at += needle.len)
        matches++;
    if (matches == 0)
        return s;   // untouched, same backing, no alloc

    // Exact in u64: matches <= len/needle.len keeps both products < 2^64.
    uint64_t total = repl.len >= needle.len
        ? (uint64_t)s.len + (uint64_t)(repl.len - needle.len) * matches
        : (uint64_t)s.len - (uint64_t)(needle.len - repl.len) * matches;
    if (total > UINT32_MAX)
        return anostr_empty();

    // Pass two: gap and replacement memcpys into the destination.
    char inlineBuf[ANOSTR_INLINE_CAP];
    char *dst = inlineBuf;
    if (total > ANOSTR_INLINE_CAP) {
        if (heap == NULL)
            return anostr_empty();
        dst = mi_heap_malloc(heap, (size_t)total);
        if (dst == NULL)
            return anostr_empty();
    }

    const char *src = anostr_bytes(&s);
    const char *rep = anostr_bytes(&repl);
    size_t out = 0, from = 0;
    for (size_t at = 0; (at = anostr_find(s, needle, at)) != ANOSTR_NPOS; ) {
        memcpy(dst + out, src + from, at - from);
        out += at - from;
        memcpy(dst + out, rep, repl.len);
        out += repl.len;
        at += needle.len;
        from = at;
    }
    memcpy(dst + out, src + from, s.len - from);

    return total <= ANOSTR_INLINE_CAP ? anostr_make_inline_(dst, (size_t)total)
                                      : anostr_make_long_(dst, (size_t)total);
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
