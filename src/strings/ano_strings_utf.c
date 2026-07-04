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

bool anorune_is_punct(anorune_t r)
{
    return (uc_record(r)->flags & ANO_UC_PUNCT) != 0;
}

// ---------------------------------------------------------------------------------------------
// Rune-class culling. One pass, survivors copied in runs so a lightly-culled string is
// mostly memcpy. ASCII rides an 8-byte high-bit test and a bitset, only non-ASCII decodes.

static uint8_t cull_uc_mask(uint32_t classes)
{
    uint8_t m = 0;
    if (classes & ANOSTR_CULL_WHITESPACE) m |= ANO_UC_WHITESPACE;
    if (classes & ANOSTR_CULL_PUNCT)      m |= ANO_UC_PUNCT;
    if (classes & ANOSTR_CULL_MARK)       m |= ANO_UC_MARK;
    return m;
}

// Byte offset of the first rune to cull, or len if none. Same walk as the copy loop.
static size_t cull_find_first(anostr_t s, uint8_t ucMask, const uint64_t asciiSet[2])
{
    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);
    size_t i = 0;
    while (i < s.len) {
        // 8-byte chunks with no high bit and no member byte skip in one test.
        while (i + 8 <= s.len) {
            uint64_t chunk;
            memcpy(&chunk, p + i, 8);
            if (chunk & 0x8080808080808080ull)
                break;
            bool hit = false;
            for (int k = 0; k < 8 && !hit; k++) {
                uint8_t b = p[i + k];
                hit = (asciiSet[b >> 6] >> (b & 63)) & 1;
            }
            if (hit)
                break;
            i += 8;
        }
        if (i >= s.len)
            break;
        uint8_t b = p[i];
        if (b < 0x80u) {
            if ((asciiSet[b >> 6] >> (b & 63)) & 1)
                return i;
            i++;
            continue;
        }
        size_t at = i;
        anorune_t r = anostr_rune_next(s, &i);
        if (r < ANO_UC_TABLE_MAX && (uc_record(r)->flags & ucMask) != 0)
            return at;
    }
    return s.len;
}

anostr_t anostr_cull(mi_heap_t *heap, anostr_t s, uint32_t classes)
{
    uint8_t ucMask = cull_uc_mask(classes);
    if (ucMask == 0 || s.len == 0)
        return s;

    // ASCII membership bitset for these classes, from the tables (one source of truth).
    uint64_t asciiSet[2] = {0};
    for (uint32_t c = 0; c < 128; c++)
        if ((uc_record(c)->flags & ucMask) != 0)
            asciiSet[c >> 6] |= 1ull << (c & 63);

    size_t first = cull_find_first(s, ucMask, asciiSet);
    if (first == s.len)
        return s;   // nothing to cull, same backing, no alloc

    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);
    anostr_builder_t b = anostr_builder_make(heap, s.len);
    if (anostr_builder_append(&b, p, first) != 0) {
        anostr_builder_discard(&b);
        return anostr_empty();
    }

    size_t i = first, runStart = first;
    while (i < s.len) {
        size_t at = i;
        bool   culled;
        uint8_t byte = p[i];
        if (byte < 0x80u) {
            culled = (asciiSet[byte >> 6] >> (byte & 63)) & 1;
            i++;
        } else {
            anorune_t r = anostr_rune_next(s, &i);
            culled = r < ANO_UC_TABLE_MAX && (uc_record(r)->flags & ucMask) != 0;
        }
        if (culled) {
            if (at > runStart && anostr_builder_append(&b, p + runStart, at - runStart) != 0) {
                anostr_builder_discard(&b);
                return anostr_empty();
            }
            runStart = i;
        }
    }
    if (s.len > runStart && anostr_builder_append(&b, p + runStart, s.len - runStart) != 0) {
        anostr_builder_discard(&b);
        return anostr_empty();
    }
    return anostr_freeze(&b);
}

// ---------------------------------------------------------------------------------------------
// Sort a string's runes ascending by code point (UTF-8 byte order IS code point order).
// Pure ASCII: counting sort, no decode. Otherwise decode to runes, sort, re-encode.

static int rune_cmp_(const void *a, const void *b)
{
    anorune_t x = *(const anorune_t *)a, y = *(const anorune_t *)b;
    return x < y ? -1 : (x > y);
}

anostr_t anostr_rune_sort(mi_heap_t *heap, anostr_t s)
{
    if (s.len < 2)
        return s;
    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);

    bool ascii = true;
    for (size_t i = 0; i < s.len && ascii; i++)
        ascii = p[i] < 0x80u;
    if (ascii) {
        uint32_t counts[128] = {0};
        for (size_t i = 0; i < s.len; i++)
            counts[p[i]]++;
        anostr_builder_t b = anostr_builder_make(heap, s.len);
        char fill[64];
        for (uint32_t c = 0; c < 128; c++) {
            size_t remaining = counts[c];
            if (remaining == 0)
                continue;
            memset(fill, (int)c, remaining < sizeof fill ? remaining : sizeof fill);
            while (remaining > 0) {
                size_t chunk = remaining < sizeof fill ? remaining : sizeof fill;
                if (anostr_builder_append(&b, fill, chunk) != 0) {
                    anostr_builder_discard(&b);
                    return anostr_empty();
                }
                remaining -= chunk;
            }
        }
        return anostr_freeze(&b);
    }

    // At most len runes. Malformed bytes decode to U+FFFD.
    anorune_t *runes = mi_malloc((size_t)s.len * sizeof *runes);
    if (runes == NULL)
        return anostr_empty();
    size_t n = 0;
    for (size_t i = 0; i < s.len; )
        runes[n++] = anostr_rune_next(s, &i);
    qsort(runes, n, sizeof runes[0], rune_cmp_);

    anostr_t out = anostr_from_utf32(heap, runes, n);
    mi_free(runes);
    return out;
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
