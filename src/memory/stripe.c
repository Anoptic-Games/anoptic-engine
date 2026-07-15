/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Stripe + counting parent.
// Stripe: per-lane chunk chains; chunks grain-aligned, header/size multiples of grain -> lanes never share a grain region.
// Planes: N SoA arrays from one parent acquisition; born full, side-listed until reset splices into lane 0.
// Counting parent: interposer ledger at the parent seam. Ctx + size headers from the inner parent.

#include <anoptic_memory_pools.h>

#include <stddef.h>
#include <string.h>

#define STRIPE_CHUNK_DEFAULT (64u * 1024u)
#define STRIPE_MAX_ALIGN     4096u

static inline size_t st_align_up(size_t v, size_t a)   { return (v + a - 1) & ~(a - 1); }
static inline bool   st_is_pow2(size_t v)              { return v != 0 && (v & (v - 1)) == 0; }

static inline void *st_pacquire(const ano_mem_parent *p, size_t size, size_t align)
{
    return p->acquire ? p->acquire(p->ctx, size, align) : NULL;
}

static inline void st_prelease(const ano_mem_parent *p, void *block)
{
    if (p->release && block != NULL)
        p->release(p->ctx, block);
}

/* Parent ledger */

typedef struct cnt_ctx {
    ano_mem_parent inner;
    ano_mem_parent_ledger *ledger;
} cnt_ctx;

// Per-block: [pad ... hdr][total][payload]. Lead-in multiple of align (floor 16); total for bytes_back; hdr recovers base.
static inline size_t cnt_hdr(size_t align)
{
    return align < 16 ? 16 : align;
}

static void *cnt_acquire(void *ctx, size_t size, size_t align)
{
    cnt_ctx *c = ctx;
    if (!st_is_pow2(align))
        return NULL;
    size_t hdr = cnt_hdr(align);
    if (size > SIZE_MAX - hdr)
        return NULL;
    size_t total = hdr + size;
    // Acquire at hdr (>= 16): size_t words before payload stay aligned; payload keeps child's align.
    char *base = st_pacquire(&c->inner, total, hdr);
    if (base == NULL)
        return NULL;
    char *payload = base + hdr;
    ((size_t *)payload)[-1] = total;
    ((size_t *)payload)[-2] = hdr;
    ano_mem_parent_ledger *l = c->ledger;
    l->acquires   += 1;
    l->bytes_out  += total;
    l->live_bytes += total;
    if (l->live_bytes > l->peak_bytes)
        l->peak_bytes = l->live_bytes;
    return payload;
}

static void cnt_release(void *ctx, void *block)
{
    cnt_ctx *c = ctx;
    size_t total = ((size_t *)block)[-1];
    size_t hdr   = ((size_t *)block)[-2];
    ano_mem_parent_ledger *l = c->ledger;
    l->releases   += 1;
    l->bytes_back += total;
    l->live_bytes -= total;
    st_prelease(&c->inner, (char *)block - hdr);
}

ano_mem_parent ano_mem_parent_counting(ano_mem_parent inner, ano_mem_parent_ledger *ledger)
{
    ano_mem_parent dead = { NULL, NULL, NULL };
    if (inner.acquire == NULL || ledger == NULL)
        return dead;
    cnt_ctx *c = st_pacquire(&inner, sizeof *c, 16);
    if (c == NULL)
        return dead;
    c->inner  = inner;
    c->ledger = ledger;
    *ledger   = (ano_mem_parent_ledger){0};     // birth zero; caller owns storage
    ano_mem_parent p = { c, cnt_acquire, inner.release ? cnt_release : NULL };
    return p;
}

/* Stripe */

typedef struct stripe_chunk {
    struct stripe_chunk *next;
    size_t cap;                 // total bytes incl. header; multiple of grain
} stripe_chunk;

typedef struct stripe_lane {
    char *at;                   // next free byte in current chunk
    char *end;                  // one past current chunk
    stripe_chunk *head, *tail;
    stripe_chunk *cur;          // chunk [at, end) points into
} stripe_lane;

struct ano_mem_stripe {
    ano_mem_parent parent;
    size_t nlanes;
    size_t grain;
    size_t hdr;                 // per-chunk header, rounded to whole granules
    size_t chunk_hint;          // acquisition granularity, multiple of grain
    stripe_chunk *planes;       // this epoch's plane-set chunks (born full)
    ano_mem_stats st;           // peaks folded at reset/stats
    stripe_lane lane[];
};

static inline void stripe_fold_peaks(ano_mem_stats *st)
{
    if (st->live_bytes  > st->peak_bytes)  st->peak_bytes  = st->live_bytes;
    if (st->live_blocks > st->peak_blocks) st->peak_blocks = st->live_blocks;
}

// One grain-aligned chunk from parent. NULL on exhaustion.
static stripe_chunk *stripe_chunk_acquire(ano_mem_stripe *s, size_t bytes)
{
    size_t align = s->grain < 16 ? 16 : s->grain;
    stripe_chunk *c = st_pacquire(&s->parent, bytes, align);
    if (c == NULL)
        return NULL;
    c->next = NULL;
    c->cap  = bytes;
    s->st.chunk_bytes    += bytes;
    s->st.chunk_count    += 1;
    s->st.parent_acquires += 1;
    s->st.parent_bytes    += bytes;
    return c;
}

ano_mem_stripe *ano_mem_stripe_make(ano_mem_parent parent, const ano_mem_stripe_cfg *cfg)
{
    if (parent.acquire == NULL)
        return NULL;
    size_t lanes = cfg && cfg->lanes      ? cfg->lanes      : 1;
    size_t grain = cfg && cfg->grain      ? cfg->grain      : ANO_THREAD_LINE;
    size_t hint  = cfg && cfg->chunk_hint ? cfg->chunk_hint : STRIPE_CHUNK_DEFAULT;
    if (!st_is_pow2(grain) || grain > STRIPE_MAX_ALIGN)
        return NULL;
    if (lanes > (SIZE_MAX - sizeof(ano_mem_stripe)) / sizeof(stripe_lane))
        return NULL;
    size_t hdr = st_align_up(sizeof(stripe_chunk), grain < 16 ? 16 : grain);
    if (hint > SIZE_MAX / 2)
        return NULL;
    hint = st_align_up(hint < hdr + grain ? hdr + grain : hint, grain);

    size_t ctrl = sizeof(ano_mem_stripe) + lanes * sizeof(stripe_lane);
    ano_mem_stripe *s = st_pacquire(&parent, ctrl, 64);
    if (s == NULL)
        return NULL;
    memset(s, 0, ctrl);
    s->parent     = parent;
    s->nlanes     = lanes;
    s->grain      = grain;
    s->hdr        = hdr;
    s->chunk_hint = hint;
    s->st.chunk_bytes     = ctrl;
    s->st.chunk_count     = 1;
    s->st.parent_acquires = 1;
    s->st.parent_bytes    = ctrl;
    return s;
}

static inline void stripe_point_at(stripe_lane *ln, stripe_chunk *c, size_t hdr)
{
    ln->cur = c;
    ln->at  = (char *)c + hdr;
    ln->end = (char *)c + c->cap;
}

// Slow path: walk unused chunks, else grow the lane.
static void *stripe_alloc_slow(ano_mem_stripe *s, stripe_lane *ln, size_t size, size_t align)
{
    if (ln->cur != NULL) {
        for (stripe_chunk *c = ln->cur->next; c != NULL; c = c->next) {
            uintptr_t data = (uintptr_t)c + s->hdr;
            uintptr_t p    = (data + (align - 1)) & ~(uintptr_t)(align - 1);
            if (p + size <= (uintptr_t)c + c->cap) {
                stripe_point_at(ln, c, s->hdr);
                ln->at = (char *)(p + size);
                s->st.live_bytes      += size;
                s->st.requested_bytes += size;
                s->st.live_blocks     += 1;
                return (void *)p;
            }
        }
    }
    size_t want = s->hdr + size + align;
    if (want < size)
        return NULL;
    size_t bytes = st_align_up(want, s->grain);
    if (bytes < s->chunk_hint)
        bytes = s->chunk_hint;
    stripe_chunk *c = stripe_chunk_acquire(s, bytes);
    if (c == NULL)
        return NULL;
    if (ln->tail) ln->tail->next = c;
    else          ln->head = c;
    ln->tail = c;
    stripe_point_at(ln, c, s->hdr);
    uintptr_t p = ((uintptr_t)ln->at + (align - 1)) & ~(uintptr_t)(align - 1);
    ln->at = (char *)(p + size);            // sized to fit
    s->st.live_bytes      += size;
    s->st.requested_bytes += size;
    s->st.live_blocks     += 1;
    return (void *)p;
}

void *ano_mem_stripe_alloc(ano_mem_stripe *s, size_t lane, size_t size, size_t align)
{
    if (s == NULL || lane >= s->nlanes || size == 0 || size > SIZE_MAX / 2)
        return NULL;
    if (align == 0)
        align = s->grain;
    else if (!st_is_pow2(align) || align > STRIPE_MAX_ALIGN)
        return NULL;
    stripe_lane *ln = &s->lane[lane];
    // Fast path: align, compare, bump.
    uintptr_t p = ((uintptr_t)ln->at + (align - 1)) & ~(uintptr_t)(align - 1);
    if (p + size <= (uintptr_t)ln->end) {
        ln->at = (char *)(p + size);
        s->st.live_bytes      += size;
        s->st.requested_bytes += size;
        s->st.live_blocks     += 1;
        return (void *)p;
    }
    return stripe_alloc_slow(s, ln, size, align);
}

int ano_mem_stripe_planes(ano_mem_stripe *s, const size_t *count, const size_t *elem_size,
                          size_t n_planes, void **out_planes)
{
    if (s == NULL || count == NULL || elem_size == NULL || out_planes == NULL || n_planes == 0)
        return -1;
    // Pass 1: offsets. Every base on a granule; park offsets in out_planes until base exists.
    size_t total = s->hdr;
    size_t sum   = 0;
    for (size_t i = 0; i < n_planes; i++) {
        if (elem_size[i] != 0 && count[i] > SIZE_MAX / elem_size[i])
            return -1;
        size_t bytes = count[i] * elem_size[i];
        out_planes[i] = (void *)(uintptr_t)total;
        if (bytes > SIZE_MAX - total)
            return -1;
        size_t end = total + bytes;
        size_t up  = st_align_up(end, s->grain);
        if (up < end)
            return -1;
        total = up;
        sum  += bytes;
    }
    stripe_chunk *c = stripe_chunk_acquire(s, total);
    if (c == NULL)
        return -1;
    c->next   = s->planes;
    s->planes = c;
    for (size_t i = 0; i < n_planes; i++)
        out_planes[i] = (char *)c + (uintptr_t)out_planes[i];
    s->st.live_bytes      += sum;
    s->st.requested_bytes += sum;
    s->st.live_blocks     += n_planes;
    return 0;
}

void ano_mem_stripe_reset(ano_mem_stripe *s)
{
    if (s == NULL)
        return;
    // Plane chunks -> plain lane-0 capacity for next epoch.
    stripe_lane *l0 = &s->lane[0];
    while (s->planes != NULL) {
        stripe_chunk *c = s->planes;
        s->planes = c->next;
        c->next = NULL;
        if (l0->tail) l0->tail->next = c;
        else          l0->head = c;
        l0->tail = c;
    }
    for (size_t i = 0; i < s->nlanes; i++) {
        stripe_lane *ln = &s->lane[i];
        if (ln->head != NULL)
            stripe_point_at(ln, ln->head, s->hdr);
    }
    stripe_fold_peaks(&s->st);
    s->st.live_bytes      = 0;
    s->st.requested_bytes = 0;
    s->st.live_blocks     = 0;
}

void ano_mem_stripe_destroy(ano_mem_stripe *s)
{
    if (s == NULL)
        return;
    ano_mem_parent parent = s->parent;
    for (size_t i = 0; i < s->nlanes; i++) {
        stripe_chunk *c = s->lane[i].head;
        while (c != NULL) {
            stripe_chunk *next = c->next;
            st_prelease(&parent, c);
            c = next;
        }
    }
    stripe_chunk *c = s->planes;
    while (c != NULL) {
        stripe_chunk *next = c->next;
        st_prelease(&parent, c);
        c = next;
    }
    st_prelease(&parent, s);
}

ano_mem_stats ano_mem_stripe_stats(const ano_mem_stripe *s)
{
    ano_mem_stats out = {0};
    if (s == NULL)
        return out;
    out = s->st;
    stripe_fold_peaks(&out);
    return out;
}
