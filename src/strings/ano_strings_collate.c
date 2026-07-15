/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Human lexicographic order: UCA/DUCET (ano_collate_tables.h), shipped scripts only.
// Decompose to NFD, map to CEs, compare one level at a time (primary/secondary/tertiary).
// Unlisted code points get UCA implicit weights.
// Sort: u64 collate-prefix keys, stable LSD radix, key-equal runs restream or full-key memcmp.

#include <stdlib.h>

#include "anoptic_strings_utf.h"

#include "strings/ano_collate_tables.h"
#include "strings/ano_strings_internal.h"

#define CE_PRIMARY(ce)   ((ce) >> 16)
#define CE_SECONDARY(ce) (((ce) >> 5) & 0x7FFu)
#define CE_TERTIARY(ce)  ((ce) & 0x1Fu)

// One source rune's worth of CEs.
enum { CE_QUEUE_CAP = 64 };

static const uint16_t *decomp_lookup(anorune_t cp, uint32_t *len)
{
    if (cp >= 0x10000u)     // tables are BMP-only
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

static int ce_push_cp(uint32_t *q, int qn, anorune_t cp)
{
    if (cp < 0x10000u) {
        size_t block = ano_ce_stage1[cp >> 8];
        uint32_t span_idx = ano_ce_stage2[block * 256 + (cp & 0xFFu)];
        if (span_idx != 0) {
            uint16_t span = ano_ce_spans[span_idx];
            const uint32_t *ce = &ano_ce_pool[span >> 4];
            for (uint32_t k = 0; k < (span & 0xFu); k++)
                q[qn++] = ce[k];
            return qn;
        }
    }
    // UCA implicit weights: unlisted (Han included) keep code point order after listed primaries.
    uint32_t hi16 = 0xFBC0u + (cp >> 15);
    uint32_t lo16 = (cp & 0x7FFFu) | 0x8000u;
    q[qn++] = hi16 << 16 | 0x20u << 5 | 0x2u;
    q[qn++] = lo16 << 16;
    return qn;
}

// One rune's CEs (decomposition included) appended to q.
static int ce_push_rune(uint32_t *q, int qn, anorune_t r)
{
    uint32_t dlen;
    const uint16_t *d = decomp_lookup(r, &dlen);
    if (d != NULL) {
        for (uint32_t k = 0; k < dlen; k++)
            qn = ce_push_cp(q, qn, d[k]);
        return qn;
    }
    return ce_push_cp(q, qn, r);
}

static bool ce_next(ce_iter_t *it, uint32_t *ce)
{
    while (it->qk == it->qn) {
        if (it->i >= anostr_len(it->s))
            return false;
        it->qk = 0;
        it->qn = ce_push_rune(it->q, 0, anostr_rune_next(it->s, &it->i));
    }
    *ce = it->q[it->qk++];
    return true;
}

// Next nonzero weight of the given level. False once exhausted.
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
    if (anostr_eq(a, b))
        return 0;   // byte-equal is collate-equal
    for (int level = 0; level < 3; level++) {
        int c = collate_level(a, b, level);
        if (c != 0)
            return c;
    }
    return anostr_compare(a, b);    // byte order makes the order total
}

/* Collation Prefix Keys */

// First four nonzero primaries, big-endian.

// Next four nonzero primaries after skipping `skip`. skip = 0 is the public prefix key.
static uint64_t collate_prefix_skip(anostr_t s, uint32_t skip)
{
    uint64_t key = 0;
    int      got = 0;

    // ASCII fast path: one CE per byte. Primary 0 (ignorables) skipped.
    const uint8_t *p = (const uint8_t *)anostr_bytes(&s);
    size_t i = 0;
    while (i < s.len) {
        uint8_t b = p[i];
        if (b >= 0x80u)
            break;
        uint32_t pri = ano_ce_ascii[b] >> 16;
        if (pri != 0) {
            if (skip > 0) {
                skip--;
            } else {
                key |= (uint64_t)pri << (48 - 16 * got);
                if (++got == 4)
                    return key;
            }
        }
        i++;
    }
    if (i >= s.len)
        return key;

    // Non-ASCII from byte i: full streaming pipeline.
    ce_iter_t it = { .s = s, .i = i };
    uint32_t w;
    while (got < 4 && next_weight(&it, 0, &w)) {
        if (skip > 0) {
            skip--;
            continue;
        }
        key |= (uint64_t)w << (48 - 16 * got);
        got++;
    }
    return key;
}

uint64_t anostr_collate_prefix(anostr_t s)
{
    return collate_prefix_skip(s, 0);
}

/* Full Sort Keys */

// Per level: nonzero weights as u16 BE, then 0x0000 terminator. Then raw bytes.

typedef struct key_buf_t {
    uint8_t *p;
    size_t   n, cap;
    bool     oom;
} key_buf_t;

static void kb_bytes(key_buf_t *kb, const void *src, size_t n)
{
    if (kb->n + n > kb->cap) {
        size_t cap = kb->cap ? kb->cap : 256;
        while (cap < kb->n + n)
            cap *= 2;
        uint8_t *fresh = mi_realloc(kb->p, cap);
        if (fresh == NULL) {
            kb->oom = true;
            return;
        }
        kb->p = fresh;
        kb->cap = cap;
    }
    memcpy(kb->p + kb->n, src, n);
    kb->n += n;
}

static void kb_w16(key_buf_t *kb, uint32_t w)
{
    uint8_t be[2] = { (uint8_t)(w >> 8), (uint8_t)w };
    kb_bytes(kb, be, 2);
}

// One CE stream feeds all three levels: primaries to kb, L2/L3 park in side buffers.
static void collate_key_emit(key_buf_t *kb, key_buf_t *l2, key_buf_t *l3, anostr_t s)
{
    l2->n = l3->n = 0;
    l2->oom = l3->oom = false;
    ce_iter_t it = { .s = s };
    uint32_t ce;
    while (ce_next(&it, &ce)) {
        uint32_t w1 = CE_PRIMARY(ce), w2 = CE_SECONDARY(ce), w3 = CE_TERTIARY(ce);
        if (w1) kb_w16(kb, w1);
        if (w2) kb_w16(l2, w2);
        if (w3) kb_w16(l3, w3);
    }
    kb_w16(kb, 0);
    if (l2->n > 0) kb_bytes(kb, l2->p, l2->n);
    kb_w16(kb, 0);
    if (l3->n > 0) kb_bytes(kb, l3->p, l3->n);
    kb_w16(kb, 0);
    if (s.len > 0) kb_bytes(kb, anostr_bytes(&s), s.len);
    kb->oom = kb->oom || l2->oom || l3->oom;
}

anostr_t anostr_collate_key(mi_heap_t *heap, anostr_t s)
{
    key_buf_t kb = {0}, l2 = {0}, l3 = {0};
    collate_key_emit(&kb, &l2, &l3, s);
    anostr_t out = kb.oom ? anostr_empty() : anostr_from(heap, kb.p, kb.n);
    mi_free(kb.p);
    mi_free(l2.p);
    mi_free(l3.p);
    return out;
}

/* Sort */

// (key, index) records, stable LSD radix, key-equal runs settled by tie handlers.

typedef struct sort_rec_t {
    uint64_t key;
    uint32_t idx;
    uint32_t pad_;
} sort_rec_t;

static_assert(sizeof(sort_rec_t) == sizeof(anostr_t),
              "the radix ping-pong buffer doubles as the gather buffer");

// Maps a record index back to its string.
typedef anostr_t (*rec_str_fn_t)(const void *ctx, uint32_t idx);

static anostr_t rec_str_items_(const void *ctx, uint32_t idx)
{
    return ((const anostr_t *)ctx)[idx];
}

typedef struct sym_ctx_t {
    const anostr_intern_t *t;
    const anostr_sym      *syms;
} sym_ctx_t;

static anostr_t rec_str_syms_(const void *ctx, uint32_t idx)
{
    const sym_ctx_t *c = ctx;
    return anostr_sym_str(c->t, c->syms[idx]);
}

// Stable: strict > only.
static void sort_recs_insertion(sort_rec_t *a, size_t n)
{
    for (size_t i = 1; i < n; i++) {
        sort_rec_t cur = a[i];
        size_t j = i;
        while (j > 0 && a[j - 1].key > cur.key) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = cur;
    }
}

// LSD radix, 8 passes of 8 bits. Shared digit skips its pass. Scatter is stable.
static void sort_recs(sort_rec_t *a, sort_rec_t *tmp, size_t n)
{
    if (n <= 48) {
        sort_recs_insertion(a, n);
        return;
    }

    uint32_t hist[8][256];  // counters fit: count capped at UINT32_MAX
    memset(hist, 0, sizeof hist);
    for (size_t i = 0; i < n; i++) {
        uint64_t k = a[i].key;
        for (int d = 0; d < 8; d++)
            hist[d][(k >> (d * 8)) & 0xFFu]++;
    }

    sort_rec_t *src = a, *dst = tmp;
    for (int d = 0; d < 8; d++) {
        if (hist[d][(src[0].key >> (d * 8)) & 0xFFu] == n)
            continue;   // every key shares this digit
        uint32_t pos[256], sum = 0;
        for (int b = 0; b < 256; b++) {
            pos[b] = sum;
            sum += hist[d][b];
        }
        for (size_t i = 0; i < n; i++)
            dst[pos[(src[i].key >> (d * 8)) & 0xFFu]++] = src[i];
        sort_rec_t *swap = src;
        src = dst;
        dst = swap;
    }
    if (src != a)
        memcpy(a, src, n * sizeof *a);
}

// Small key-equal runs: stable insertion on streaming collate.
static void tie_insertion(sort_rec_t *r, size_t n, rec_str_fn_t str_of, const void *ctx)
{
    for (size_t i = 1; i < n; i++) {
        sort_rec_t cur = r[i];
        anostr_t   cs = str_of(ctx, cur.idx);
        size_t j = i;
        while (j > 0 && anostr_collate(str_of(ctx, r[j - 1].idx), cs) > 0) {
            r[j] = r[j - 1];
            j--;
        }
        r[j] = cur;
    }
}

enum { TIE_BULK_MIN = 17 };     // below this, restreaming beats full keys

typedef struct tie_view_t {
    const uint8_t *key;
    size_t         klen;
    uint32_t       idx;
} tie_view_t;

static int tie_view_cmp_(const void *a, const void *b)
{
    const tie_view_t *x = a, *y = b;
    size_t n = x->klen < y->klen ? x->klen : y->klen;
    int c = memcmp(x->key, y->key, n);
    if (c != 0)
        return c < 0 ? -1 : 1;
    if (x->klen != y->klen)
        return x->klen < y->klen ? -1 : 1;
    return x->idx < y->idx ? -1 : (x->idx > y->idx);    // identical: input order
}

// Large key-equal runs: full sort keys into one buffer, memcmp settles. False on alloc fail.
static bool tie_bulk(sort_rec_t *r, size_t n, rec_str_fn_t str_of, const void *ctx)
{
    tie_view_t *views = mi_malloc(n * sizeof *views);
    if (views == NULL)
        return false;

    key_buf_t kb = {0}, l2 = {0}, l3 = {0};
    for (size_t i = 0; i < n; i++) {
        collate_key_emit(&kb, &l2, &l3, str_of(ctx, r[i].idx));
        views[i].klen = kb.n;   // running end -> length below
        views[i].idx = r[i].idx;
    }
    mi_free(l2.p);
    mi_free(l3.p);
    if (kb.oom) {
        mi_free(kb.p);
        mi_free(views);
        return false;
    }
    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        size_t end = views[i].klen;
        views[i].key = kb.p + off;
        views[i].klen = end - off;
        off = end;
    }

    qsort(views, n, sizeof *views, tie_view_cmp_);
    for (size_t i = 0; i < n; i++)
        r[i].idx = views[i].idx;    // keys equal across the run, only idx moves

    mi_free(kb.p);
    mi_free(views);
    return true;
}

// Settle a run whose primaries agree through `skip`, MSD-style: next four primaries, sort, recurse.
// Exhausted/deep runs fall back to full keys or streaming.
enum { TIE_MSD_MAX_SKIP = 256 };    // this deep: build full keys instead

static void tie_leaf(sort_rec_t *r, size_t n, rec_str_fn_t str_of, const void *ctx)
{
    if (n < TIE_BULK_MIN || !tie_bulk(r, n, str_of, ctx))
        tie_insertion(r, n, str_of, ctx);
}

static void tie_msd(sort_rec_t *r, size_t n, uint32_t skip, rec_str_fn_t str_of, const void *ctx)
{
    if (n < 2)
        return;

    anostr_t first = str_of(ctx, r[0].idx);
    bool allSame = true;
    for (size_t i = 1; i < n && allSame; i++)
        allSame = anostr_eq(first, str_of(ctx, r[i].idx));
    if (allSame)
        return;     // duplicates, input order stands

    if (skip >= TIE_MSD_MAX_SKIP) {
        tie_leaf(r, n, str_of, ctx);
        return;
    }

    bool anyWeight = false;
    for (size_t i = 0; i < n; i++) {
        r[i].key = collate_prefix_skip(str_of(ctx, r[i].idx), skip);
        anyWeight = anyWeight || r[i].key != 0;
    }
    if (!anyWeight) {   // primaries exhausted: levels 2/3/bytes decide
        tie_leaf(r, n, str_of, ctx);
        return;
    }

    if (n <= 48) {
        sort_recs_insertion(r, n);
    } else {
        sort_rec_t *tmp = mi_malloc(n * sizeof *tmp);
        if (tmp == NULL) {
            tie_leaf(r, n, str_of, ctx);
            return;
        }
        sort_recs(r, tmp, n);
        mi_free(tmp);
    }

    for (size_t lo = 0; lo < n; ) {
        size_t hi = lo + 1;
        while (hi < n && r[hi].key == r[lo].key)
            hi++;
        tie_msd(r + lo, hi - lo, skip + 4, str_of, ctx);
        lo = hi;
    }
}

static void resolve_ties(sort_rec_t *recs, size_t n, rec_str_fn_t str_of, const void *ctx)
{
    for (size_t lo = 0; lo < n; ) {
        size_t hi = lo + 1;
        while (hi < n && recs[hi].key == recs[lo].key)
            hi++;
        tie_msd(recs + lo, hi - lo, 4, str_of, ctx);
        lo = hi;
    }
}

// Already sorted? Keys nondecreasing; equal-key neighbors pay a streaming collate.
static bool recs_presorted(const sort_rec_t *recs, size_t n, rec_str_fn_t str_of, const void *ctx)
{
    for (size_t i = 1; i < n; i++) {
        if (recs[i - 1].key > recs[i].key)
            return false;
        if (recs[i - 1].key == recs[i].key &&
            anostr_collate(str_of(ctx, recs[i - 1].idx), str_of(ctx, recs[i].idx)) > 0)
            return false;
    }
    return true;
}

// Fills recs into sorted order. True if already sorted.
static bool collate_sort_core(sort_rec_t *recs, sort_rec_t *tmp, size_t n,
                              rec_str_fn_t str_of, const void *ctx)
{
    if (recs_presorted(recs, n, str_of, ctx))
        return true;
    sort_recs(recs, tmp, n);
    resolve_ties(recs, n, str_of, ctx);
    return false;
}

// Comparator fallbacks for allocation failure. Thread-local ctx.
static _Thread_local const anostr_t *fb_items_;

static int collate_qsort(const void *a, const void *b)
{
    return anostr_collate(*(const anostr_t *)a, *(const anostr_t *)b);
}

static int fb_order_cmp_(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    int c = anostr_collate(fb_items_[x], fb_items_[y]);
    if (c != 0)
        return c;
    return x < y ? -1 : (x > y);
}

static _Thread_local const anostr_intern_t *fb_tbl_;

static int fb_sym_cmp_(const void *a, const void *b)
{
    anostr_sym x = *(const anostr_sym *)a, y = *(const anostr_sym *)b;
    int c = anostr_collate(anostr_sym_str(fb_tbl_, x), anostr_sym_str(fb_tbl_, y));
    if (c != 0)
        return c;
    return x < y ? -1 : (x > y);
}

void anostr_sort(anostr_t *items, size_t count)
{
    if (items == NULL || count < 2)
        return;
    sort_rec_t *recs = count <= UINT32_MAX ? mi_malloc(2 * count * sizeof *recs) : NULL;
    if (recs == NULL) {     // no scratch: correct, slower
        qsort(items, count, sizeof items[0], collate_qsort);
        return;
    }
    sort_rec_t *tmp = recs + count;

    for (size_t i = 0; i < count; i++)
        recs[i] = (sort_rec_t){ anostr_collate_prefix(items[i]), (uint32_t)i, 0 };

    if (!collate_sort_core(recs, tmp, count, rec_str_items_, items)) {
        anostr_t *gather = (anostr_t *)tmp;     // radix scratch, now free
        for (size_t i = 0; i < count; i++)
            gather[i] = items[recs[i].idx];
        memcpy(items, gather, count * sizeof items[0]);
    }
    mi_free(recs);
}

void anostr_sort_idx(const anostr_t *items, size_t count, uint32_t *order)
{
    if (order == NULL || count == 0)
        return;
    for (size_t i = 0; i < count; i++)
        order[i] = (uint32_t)i;
    if (items == NULL || count < 2 || count > UINT32_MAX)
        return;

    sort_rec_t *recs = mi_malloc(2 * count * sizeof *recs);
    if (recs == NULL) {
        fb_items_ = items;
        qsort(order, count, sizeof order[0], fb_order_cmp_);
        return;
    }
    for (size_t i = 0; i < count; i++)
        recs[i] = (sort_rec_t){ anostr_collate_prefix(items[i]), (uint32_t)i, 0 };

    if (!collate_sort_core(recs, recs + count, count, rec_str_items_, items))
        for (size_t i = 0; i < count; i++)
            order[i] = recs[i].idx;
    mi_free(recs);
}

// Extends the per-symbol key cache to cover every symbol. NULL if it cannot grow.
static const uint64_t *sym_key_cache(anostr_intern_t *t)
{
    if (t->collateKeyed >= t->count)
        return t->collateKeys;
    if (t->collateKeyCap < t->count) {
        uint64_t *fresh = mi_heap_realloc(t->heap, t->collateKeys,
                                          (size_t)t->arrCap * sizeof *fresh);
        if (fresh == NULL)
            return NULL;
        t->collateKeys = fresh;
        t->collateKeyCap = t->arrCap;   // strs cap >= count
    }
    for (uint32_t sym = t->collateKeyed; sym < t->count; sym++)
        t->collateKeys[sym] = anostr_collate_prefix(t->strs[sym]);
    t->collateKeyed = t->count;
    return t->collateKeys;
}

void anostr_sym_sort(anostr_intern_t *t, anostr_sym *syms, size_t count)
{
    if (t == NULL || syms == NULL || count < 2)
        return;
    sort_rec_t *recs = count <= UINT32_MAX ? mi_malloc(2 * count * sizeof *recs) : NULL;
    if (recs == NULL) {
        fb_tbl_ = t;
        qsort(syms, count, sizeof syms[0], fb_sym_cmp_);
        return;
    }
    sort_rec_t *tmp = recs + count;

    const uint64_t *cache = sym_key_cache(t);
    for (size_t i = 0; i < count; i++) {
        anostr_sym sym = syms[i];
        uint64_t key = sym >= t->count ? 0      // out of range = empty string
                     : cache != NULL   ? cache[sym]
                                       : anostr_collate_prefix(t->strs[sym]);
        recs[i] = (sort_rec_t){ key, (uint32_t)i, 0 };
    }

    sym_ctx_t ctx = { t, syms };
    if (!collate_sort_core(recs, tmp, count, rec_str_syms_, &ctx)) {
        anostr_sym *gather = (anostr_sym *)tmp;
        for (size_t i = 0; i < count; i++)
            gather[i] = syms[recs[i].idx];
        memcpy(syms, gather, count * sizeof syms[0]);
    }
    mi_free(recs);
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
