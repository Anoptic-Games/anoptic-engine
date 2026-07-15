/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anostr_t value type, hash, slicing, keep, builder. Long bytes live in backing (caller heap or external borrow).

#include "strings/ano_strings_internal.h"

#include <stdarg.h>
#include <stdio.h>

anostr_t anostr_from(mi_heap_t *heap, const void *bytes, size_t len)
{
    if (bytes == NULL || len > UINT32_MAX)
        return anostr_empty();
    if (len <= ANOSTR_INLINE_CAP)
        return anostr_make_inline_(bytes, len);
    if (heap == NULL)
        return anostr_empty();
    char *copy = mi_heap_malloc(heap, len);
    if (copy == NULL)
        return anostr_empty();
    memcpy(copy, bytes, len);
    return anostr_make_long_(copy, len);
}

anostr_t anostr_from_cstr(mi_heap_t *heap, const char *cstr)
{
    if (cstr == NULL)
        return anostr_empty();
    return anostr_from(heap, cstr, strlen(cstr));
}

anostr_t anostr_view(const char *bytes, size_t len)
{
    if (bytes == NULL || len > UINT32_MAX)
        return anostr_empty();
    if (len <= ANOSTR_INLINE_CAP)
        return anostr_make_inline_(bytes, len);
    return anostr_make_long_(bytes, len);
}

/* Hash */

// FNV-1a, both widths. Runtime twins of ANOSTR_SID/ANOSTR_SID32.

uint64_t anostr_hash(anostr_t s)
{
    const char *p = anostr_bytes(&s);
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < s.len; i++) {
        h ^= (uint8_t)p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

uint32_t anostr_hash32(anostr_t s)
{
    const char *p = anostr_bytes(&s);
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < s.len; i++) {
        h ^= (uint8_t)p[i];
        h *= 0x01000193u;
    }
    return h;
}

/* Slicing and Promotion */

anostr_t anostr_slice(anostr_t s, size_t start, size_t end)
{
    if (end > s.len)
        end = s.len;
    if (start > end)
        start = end;
    size_t n = end - start;
    if (n <= ANOSTR_INLINE_CAP)
        return anostr_make_inline_(anostr_bytes(&s) + start, n);
    // n > 12: s was long, borrow its backing (I4).
    return anostr_make_long_(s.ptr + start, n);
}

anostr_t anostr_keep(mi_heap_t *heap, anostr_t s)
{
    if (s.len <= ANOSTR_INLINE_CAP)
        return s;   // inline: identity
    return anostr_from(heap, s.ptr, s.len);
}

char *anostr_to_cstr(mi_heap_t *heap, anostr_t s)
{
    if (heap == NULL)
        return NULL;
    char *out = mi_heap_malloc(heap, (size_t)s.len + 1);
    if (out == NULL)
        return NULL;
    memcpy(out, anostr_bytes(&s), s.len);
    out[s.len] = '\0';
    return out;
}

/* Builder */

anostr_builder_t anostr_builder_make(mi_heap_t *heap, uint32_t reserve)
{
    anostr_builder_t b = { .ptr = NULL, .len = 0, .cap = 0, .heap = heap };
    if (heap != NULL && reserve > 0) {
        b.ptr = mi_heap_malloc(heap, reserve);
        if (b.ptr != NULL)
            b.cap = reserve;
    }
    return b;
}

// Grow so cap >= need. Geometric doubling from 16, clamped to UINT32_MAX. Untouched on fail.
static int builder_reserve(anostr_builder_t *b, uint64_t need)
{
    if (need <= b->cap)
        return 0;
    uint64_t cap = b->cap ? (uint64_t)b->cap * 2 : 16;
    while (cap < need)
        cap *= 2;
    if (cap > UINT32_MAX)
        cap = UINT32_MAX;
    char *grown = mi_heap_realloc(b->heap, b->ptr, cap);
    if (grown == NULL)
        return -1;
    b->ptr = grown;
    b->cap = (uint32_t)cap;
    return 0;
}

int anostr_builder_append(anostr_builder_t *b, const void *bytes, size_t n)
{
    if (b->heap == NULL || bytes == NULL)
        return -1;
    if (n == 0)
        return 0;
    uint64_t need = (uint64_t)b->len + n;
    if (need > UINT32_MAX || builder_reserve(b, need) != 0)
        return -1;
    memcpy(b->ptr + b->len, bytes, n);
    b->len = (uint32_t)need;
    return 0;
}

int anostr_builder_append_str(anostr_builder_t *b, anostr_t s)
{
    return anostr_builder_append(b, anostr_bytes(&s), s.len);
}

int anostr_builder_append_cstr(anostr_builder_t *b, const char *cstr)
{
    if (cstr == NULL)
        return -1;
    return anostr_builder_append(b, cstr, strlen(cstr));
}

int anostr_builder_appendf(anostr_builder_t *b, const char *fmt, ...)
{
    if (b->heap == NULL || fmt == NULL)
        return -1;

    va_list args, measure;
    va_start(args, fmt);
    va_copy(measure, args);
    int need = vsnprintf(NULL, 0, fmt, measure);
    va_end(measure);
    if (need < 0) {
        va_end(args);
        return -1;
    }

    // +1: vsnprintf writes a NUL into spare capacity, not counted in len.
    uint64_t total = (uint64_t)b->len + (uint64_t)need + 1;
    if (total > UINT32_MAX || builder_reserve(b, total) != 0) {
        va_end(args);
        return -1;
    }
    vsnprintf(b->ptr + b->len, (size_t)need + 1, fmt, args);
    va_end(args);
    b->len += (uint32_t)need;
    return 0;
}

anostr_t anostr_freeze(anostr_builder_t *b)
{
    if (b->heap == NULL)
        return anostr_empty();

    anostr_t s;
    if (b->len <= ANOSTR_INLINE_CAP) {
        s = anostr_make_inline_(b->ptr != NULL ? b->ptr : "", b->len);
        mi_free(b->ptr);    // inline owes nothing to the buffer
    } else {
        // Shrink to len and hand buffer to the value. Failed shrink keeps the original block.
        char *exact = mi_heap_realloc(b->heap, b->ptr, b->len);
        s = anostr_make_long_(exact != NULL ? exact : b->ptr, b->len);
    }
    *b = (anostr_builder_t){0};     // consumed: heap == NULL fails further appends
    return s;
}

void anostr_builder_discard(anostr_builder_t *b)
{
    mi_free(b->ptr);
    *b = (anostr_builder_t){0};
}
