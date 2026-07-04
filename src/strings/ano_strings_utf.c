/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UTF-8 iteration, encoding, and Unicode case/classification for anostr_t.
// Decoding is strict and total: malformed input yields U+FFFD advancing one byte.
// Case and classification are two-stage table lookups (ano_unicode_tables.h).

#include "anoptic_strings_utf.h"

#include "strings/ano_unicode_tables.h"

// ---------------------------------------------------------------------------------------------
// Decode core.

static inline bool rune_is_surrogate(anorune_t r)
{
    return r >= 0xD800u && r <= 0xDFFFu;
}

// Strict decode at p[0], n >= 1 bytes available.
// Returns bytes consumed (1..4) with the rune in *out, or 0 on malformed input.
static int utf8_decode(const uint8_t *p, size_t n, anorune_t *out)
{
    uint8_t b0 = p[0];
    if (b0 < 0x80u) {
        *out = b0;
        return 1;
    }

    int      need;  // continuation bytes after the lead
    anorune_t r, min;
    if ((b0 & 0xE0u) == 0xC0u) { need = 1; r = b0 & 0x1Fu; min = 0x80u; }
    else if ((b0 & 0xF0u) == 0xE0u) { need = 2; r = b0 & 0x0Fu; min = 0x800u; }
    else if ((b0 & 0xF8u) == 0xF0u) { need = 3; r = b0 & 0x07u; min = 0x10000u; }
    else return 0;  // continuation byte or 0xF8..0xFF lead

    if (n - 1 < (size_t)need)
        return 0;   // truncated at end
    for (int k = 1; k <= need; k++) {
        if ((p[k] & 0xC0u) != 0x80u)
            return 0;
        r = (r << 6) | (p[k] & 0x3Fu);
    }
    if (r < min || r > ANORUNE_MAX || rune_is_surrogate(r))
        return 0;   // overlong, out of range, or encoded surrogate

    *out = r;
    return 1 + need;
}

anorune_t anostr_rune_next(anostr_t s, size_t *i)
{
    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);
    size_t at = *i;
    if (at >= s.len) {
        *i = s.len;
        return ANORUNE_REPLACEMENT;
    }
    anorune_t r;
    int consumed = utf8_decode(p + at, s.len - at, &r);
    if (consumed == 0) {
        *i = at + 1;
        return ANORUNE_REPLACEMENT;
    }
    *i = at + (size_t)consumed;
    return r;
}

anorune_t anostr_rune_prev(anostr_t s, size_t *i)
{
    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);
    size_t at = *i;
    if (at > s.len)
        at = s.len;
    if (at == 0) {
        *i = 0;
        return ANORUNE_REPLACEMENT;
    }

    // Back over at most 3 continuation bytes to a candidate lead, then decode forward.
    // The candidate counts only if its sequence ends exactly at `at`.
    // Anything else means the byte at `at - 1` is malformed on its own.
    size_t start = at - 1;
    while (start > 0 && at - start < 4 && (p[start] & 0xC0u) == 0x80u)
        start--;

    anorune_t r;
    int consumed = utf8_decode(p + start, s.len - start, &r);
    if (consumed > 0 && start + (size_t)consumed == at) {
        *i = start;
        return r;
    }
    *i = at - 1;
    return ANORUNE_REPLACEMENT;
}

size_t anostr_rune_count(anostr_t s)
{
    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);
    size_t count = 0;
    for (uint32_t i = 0; i < s.len; i++)
        count += (p[i] & 0xC0u) != 0x80u;
    return count;
}

bool anostr_utf8_valid(anostr_t s)
{
    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);
    size_t i = 0;
    while (i < s.len) {
        anorune_t r;
        int consumed = utf8_decode(p + i, s.len - i, &r);
        if (consumed == 0)
            return false;
        i += (size_t)consumed;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Encode.

int anorune_encode(char buf[4], anorune_t r)
{
    if (r > ANORUNE_MAX || rune_is_surrogate(r))
        r = ANORUNE_REPLACEMENT;
    if (r < 0x80u) {
        buf[0] = (char)r;
        return 1;
    }
    if (r < 0x800u) {
        buf[0] = (char)(0xC0u | (r >> 6));
        buf[1] = (char)(0x80u | (r & 0x3Fu));
        return 2;
    }
    if (r < 0x10000u) {
        buf[0] = (char)(0xE0u | (r >> 12));
        buf[1] = (char)(0x80u | ((r >> 6) & 0x3Fu));
        buf[2] = (char)(0x80u | (r & 0x3Fu));
        return 3;
    }
    buf[0] = (char)(0xF0u | (r >> 18));
    buf[1] = (char)(0x80u | ((r >> 12) & 0x3Fu));
    buf[2] = (char)(0x80u | ((r >> 6) & 0x3Fu));
    buf[3] = (char)(0x80u | (r & 0x3Fu));
    return 4;
}

int anostr_builder_append_rune(anostr_builder_t *b, anorune_t r)
{
    char buf[4];
    int n = anorune_encode(buf, r);
    return anostr_builder_append(b, buf, (size_t)n);
}

// ---------------------------------------------------------------------------------------------
// Case and classification. Record 0 is the identity record.
// Unassigned, unlisted, and beyond-BMP runes fall through to it with no special casing here.

static inline const ano_uc_record_t *uc_record(anorune_t r)
{
    if (r >= ANO_UC_TABLE_MAX)
        return &ano_uc_records[0];
    size_t block = ano_uc_stage1[r >> 8];
    return &ano_uc_records[ano_uc_stage2[block * 256 + (r & 0xFFu)]];
}

anorune_t anorune_to_upper(anorune_t r)
{
    return (anorune_t)((int64_t)r + uc_record(r)->upper_delta);
}

anorune_t anorune_to_lower(anorune_t r)
{
    return (anorune_t)((int64_t)r + uc_record(r)->lower_delta);
}

bool anorune_is_letter(anorune_t r)
{
    return (uc_record(r)->flags & ANO_UC_LETTER) != 0;
}

bool anorune_is_digit(anorune_t r)
{
    return (uc_record(r)->flags & ANO_UC_DIGIT) != 0;
}

bool anorune_is_whitespace(anorune_t r)
{
    return (uc_record(r)->flags & ANO_UC_WHITESPACE) != 0;
}

bool anorune_is_mark(anorune_t r)
{
    return (uc_record(r)->flags & ANO_UC_MARK) != 0;
}

// ---------------------------------------------------------------------------------------------
// Encoding conversion. In: decode foreign units to runes, append through the builder
// (which sanitizes to U+FFFD and canonicalizes inline/long on freeze).
// Out: worst-case allocation, one pass, shrink to exact.

// Decode the code unit(s) at src[*i], advancing *i. Unpaired surrogates -> U+FFFD.
static anorune_t utf16_next(const char16_t *src, size_t count, size_t *i)
{
    uint32_t hi = src[(*i)++];
    if (hi < 0xD800u || hi > 0xDFFFu)
        return hi;
    if (hi <= 0xDBFFu && *i < count) {
        uint32_t lo = src[*i];
        if (lo >= 0xDC00u && lo <= 0xDFFFu) {
            (*i)++;
            return 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u);
        }
    }
    return ANORUNE_REPLACEMENT;
}

anostr_t anostr_from_utf16(mi_heap_t *heap, const char16_t *src, size_t count)
{
    if (src == NULL)
        return anostr_empty();
    // Worst case 3 UTF-8 bytes per unit (a pair is 2 units -> 4 bytes, still under).
    uint64_t worst = (uint64_t)count * 3;
    anostr_builder_t b = anostr_builder_make(heap, worst > UINT32_MAX ? 0 : (uint32_t)worst);
    for (size_t i = 0; i < count; ) {
        if (anostr_builder_append_rune(&b, utf16_next(src, count, &i)) != 0) {
            anostr_builder_discard(&b);
            return anostr_empty();
        }
    }
    return anostr_freeze(&b);
}

anostr_t anostr_from_utf16_cstr(mi_heap_t *heap, const char16_t *src)
{
    if (src == NULL)
        return anostr_empty();
    size_t count = 0;
    while (src[count] != 0)
        count++;
    return anostr_from_utf16(heap, src, count);
}

char16_t *anostr_to_utf16(mi_heap_t *heap, anostr_t s, size_t *count)
{
    if (count != NULL)
        *count = 0;
    if (heap == NULL)
        return NULL;
    // Worst case one unit per byte (4-byte runes are 2 units, still under), plus NUL.
    char16_t *out = mi_heap_malloc(heap, ((size_t)s.len + 1) * sizeof(char16_t));
    if (out == NULL)
        return NULL;
    size_t n = 0;
    for (size_t i = 0; i < s.len; ) {
        anorune_t r = anostr_rune_next(s, &i);
        if (r >= 0x10000u) {
            r -= 0x10000u;
            out[n++] = (char16_t)(0xD800u + (r >> 10));
            out[n++] = (char16_t)(0xDC00u + (r & 0x3FFu));
        } else {
            out[n++] = (char16_t)r;
        }
    }
    out[n] = 0;
    if (count != NULL)
        *count = n;
    // A failed shrink keeps the original block: correct, just slack until heap teardown.
    char16_t *exact = mi_heap_realloc(heap, out, (n + 1) * sizeof(char16_t));
    return exact != NULL ? exact : out;
}

anostr_t anostr_from_utf32(mi_heap_t *heap, const anorune_t *src, size_t count)
{
    if (src == NULL)
        return anostr_empty();
    uint64_t worst = (uint64_t)count * 4;
    anostr_builder_t b = anostr_builder_make(heap, worst > UINT32_MAX ? 0 : (uint32_t)worst);
    for (size_t k = 0; k < count; k++) {
        if (anostr_builder_append_rune(&b, src[k]) != 0) {
            anostr_builder_discard(&b);
            return anostr_empty();
        }
    }
    return anostr_freeze(&b);
}

anorune_t *anostr_to_utf32(mi_heap_t *heap, anostr_t s, size_t *count)
{
    if (count != NULL)
        *count = 0;
    if (heap == NULL)
        return NULL;
    // Worst case one rune per byte, plus NUL.
    anorune_t *out = mi_heap_malloc(heap, ((size_t)s.len + 1) * sizeof(anorune_t));
    if (out == NULL)
        return NULL;
    size_t n = 0;
    for (size_t i = 0; i < s.len; )
        out[n++] = anostr_rune_next(s, &i);
    out[n] = 0;
    if (count != NULL)
        *count = n;
    anorune_t *exact = mi_heap_realloc(heap, out, (n + 1) * sizeof(anorune_t));
    return exact != NULL ? exact : out;
}
