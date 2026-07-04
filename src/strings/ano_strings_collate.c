/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Human lexicographic order: UCA over the DUCET default table (ano_collate_tables.h),
// trimmed to the shipped scripts (Latin, Greek, Cyrillic, Runic, kana, punctuation).
// Runes decompose to NFD, then map to collation elements. Compare runs one level at a
// time (primary, secondary, tertiary), streaming, no allocation.
// Unlisted code points get UCA implicit weights, which preserve code point order.

#include <stdlib.h>

#include "anoptic_strings_utf.h"

#include "strings/ano_collate_tables.h"

#define CE_PRIMARY(ce)   ((ce) >> 16)
#define CE_SECONDARY(ce) (((ce) >> 5) & 0x7FFu)
#define CE_TERTIARY(ce)  ((ce) & 0x1Fu)

// One source rune's worth of CEs. The generator asserts no rune can exceed this.
enum { CE_QUEUE_CAP = 64 };

static const uint16_t *decomp_lookup(anorune_t cp, uint32_t *len)
{
    if (cp >= 0x10000u)     // the tables are BMP-bound by construction
        return NULL;
    size_t lo = 0, hi = sizeof ano_decomp_cp / sizeof ano_decomp_cp[0];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (ano_decomp_cp[mid] < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= sizeof ano_decomp_cp / sizeof ano_decomp_cp[0] || ano_decomp_cp[lo] != cp)
        return NULL;
    uint16_t span = ano_decomp_span[lo];
    *len = span & 0x7u;
    return &ano_decomp_pool[span >> 3];
}

// Streams the collation elements of s. Refilled one source rune at a time.
typedef struct ce_iter_t {
    anostr_t s;
    size_t   i;
    int      qn, qk;
    uint32_t q[CE_QUEUE_CAP];
} ce_iter_t;

static void ce_push_cp(ce_iter_t *it, anorune_t cp)
{
    if (cp < 0x10000u) {
        size_t block = ano_ce_stage1[cp >> 8];
        uint32_t span_idx = ano_ce_stage2[block * 256 + (cp & 0xFFu)];
        if (span_idx != 0) {
            uint16_t span = ano_ce_spans[span_idx];
            const uint32_t *ce = &ano_ce_pool[span >> 4];
            for (uint32_t k = 0; k < (span & 0xFu); k++)
                it->q[it->qn++] = ce[k];
            return;
        }
    }
    // UCA implicit weights: everything unlisted (Han included) keeps code point order,
    // sorting after every listed primary.
    uint32_t hi16 = 0xFBC0u + (cp >> 15);
    uint32_t lo16 = (cp & 0x7FFFu) | 0x8000u;
    it->q[it->qn++] = hi16 << 16 | 0x20u << 5 | 0x2u;
    it->q[it->qn++] = lo16 << 16;
}

static bool ce_next(ce_iter_t *it, uint32_t *ce)
{
    while (it->qk == it->qn) {
        if (it->i >= anostr_len(it->s))
            return false;
        it->qk = it->qn = 0;
        anorune_t r = anostr_rune_next(it->s, &it->i);
        uint32_t dlen;
        const uint16_t *d = decomp_lookup(r, &dlen);
        if (d != NULL) {
            for (uint32_t k = 0; k < dlen; k++)
                ce_push_cp(it, d[k]);
        } else {
            ce_push_cp(it, r);
        }
    }
    *ce = it->q[it->qk++];
    return true;
}

// The next nonzero weight of the given level. False once the string is exhausted.
static bool next_weight(ce_iter_t *it, int level, uint32_t *w)
{
    uint32_t ce;
    while (ce_next(it, &ce)) {
        uint32_t v = level == 0 ? CE_PRIMARY(ce)
                   : level == 1 ? CE_SECONDARY(ce) : CE_TERTIARY(ce);
        if (v != 0) {
            *w = v;
            return true;
        }
    }
    return false;
}

static int collate_level(anostr_t a, anostr_t b, int level)
{
    ce_iter_t A = { .s = a }, B = { .s = b };
    for (;;) {
        uint32_t wa, wb;
        bool ha = next_weight(&A, level, &wa);
        bool hb = next_weight(&B, level, &wb);
        if (!ha || !hb)
            return ha == hb ? 0 : (ha ? 1 : -1);
        if (wa != wb)
            return wa < wb ? -1 : 1;
    }
}

int anostr_collate(anostr_t a, anostr_t b)
{
    for (int level = 0; level < 3; level++) {
        int c = collate_level(a, b, level);
        if (c != 0)
            return c;
    }
    return anostr_compare(a, b);    // byte order makes the order total
}

static int collate_qsort(const void *a, const void *b)
{
    return anostr_collate(*(const anostr_t *)a, *(const anostr_t *)b);
}

void anostr_sort(anostr_t *items, size_t count)
{
    if (items != NULL && count > 1)
        qsort(items, count, sizeof items[0], collate_qsort);
}

bool anostr_eq_base(anostr_t a, anostr_t b)
{
    return collate_level(a, b, 0) == 0;
}

bool anostr_starts_base(anostr_t s, anostr_t prefix)
{
    ce_iter_t S = { .s = s }, P = { .s = prefix };
    for (;;) {
        uint32_t wp, ws;
        if (!next_weight(&P, 0, &wp))
            return true;
        if (!next_weight(&S, 0, &ws) || ws != wp)
            return false;
    }
}

size_t anostr_find_base(anostr_t s, anostr_t needle, size_t from)
{
    size_t len = anostr_len(s);
    size_t i = from < len ? from : len;
    for (;;) {
        if (anostr_starts_base(anostr_slice(s, i, len), needle))
            return i;
        if (i >= len)
            return ANOSTR_NPOS;
        anostr_rune_next(s, &i);    // next candidate start
    }
}
