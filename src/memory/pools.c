/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anoptic_memory_pools.h: monotonic, multipool, pool -- local allocators over a parent.
// Layout rules that make the math below sound:
//   - every chunk/slab is acquired at >= its blocks' alignment, headers are rounded up to
//     that alignment, and strides are multiples of it, so block alignment is positional;
//   - free lists thread through the first word of each free block (no per-block metadata);
//   - all control state lives in parent memory, so wink-out (destroying the root mi_heap)
//     reclaims allocator + chunks + payloads with nothing left to touch.

#include <anoptic_memory_pools.h>

#include <string.h>

#define MONO_SLAB_DEFAULT   (64u * 1024u)
#define MONO_SLAB_MIN       4096u
#define MONO_SLAB_MAX       (8u * 1024u * 1024u)
#define CHUNK_BYTES_MIN     4096u
#define CHUNK_BYTES_MAX     (512u * 1024u)
#define MAX_ALIGN           4096u
#define OVERSIZE_HDR        4096u   // keeps the payload 4096-aligned behind the tracking header

static inline size_t align_up(size_t v, size_t a)     { return (v + a - 1) & ~(a - 1); }
static inline bool   is_pow2(size_t v)                { return v != 0 && (v & (v - 1)) == 0; }
static inline size_t pow2_ceil(size_t v)              // v >= 1; saturates rather than wraps
{
    if (v <= 1) return 1;
    if (v > (SIZE_MAX >> 1) + 1) return 0;
    return (size_t)1 << (64 - __builtin_clzll((unsigned long long)(v - 1)));
}

static inline void stats_on_alloc(ano_mem_stats *st, size_t bytes)
{
    st->live_bytes += bytes;
    st->live_blocks += 1;
    if (st->live_bytes  > st->peak_bytes)  st->peak_bytes  = st->live_bytes;
    if (st->live_blocks > st->peak_blocks) st->peak_blocks = st->live_blocks;
}

static inline void stats_on_free(ano_mem_stats *st, size_t bytes)
{
    st->live_bytes  -= bytes;
    st->live_blocks -= 1;
}

// ---------------------------------------------------------------------------------------------
// Parents.

static void *parent_heap_acquire(void *ctx, size_t size, size_t align)
{
    return mi_heap_malloc_aligned((mi_heap_t *)ctx, size, align);
}

static void parent_heap_release(void *ctx, void *block)
{
    (void)ctx;
    mi_free(block);     // mimalloc routes any block back to its owning heap
}

ano_mem_parent ano_mem_parent_heap(mi_heap_t *heap)
{
    ano_mem_parent p = { heap, heap ? parent_heap_acquire : NULL,
                               heap ? parent_heap_release : NULL };
    return p;
}

static void *parent_default_acquire(void *ctx, size_t size, size_t align)
{
    (void)ctx;
    return mi_malloc_aligned(size, align);
}

ano_mem_parent ano_mem_parent_default(void)
{
    ano_mem_parent p = { NULL, parent_default_acquire, parent_heap_release };
    return p;
}

static void *parent_mono_acquire(void *ctx, size_t size, size_t align)
{
    return ano_mem_monotonic_alloc((ano_mem_monotonic *)ctx, size, align);
}

ano_mem_parent ano_mem_parent_monotonic(ano_mem_monotonic *mono)
{
    ano_mem_parent p = { mono, mono ? parent_mono_acquire : NULL, NULL };
    return p;
}

static inline void *pacquire(const ano_mem_parent *p, size_t size, size_t align)
{
    return p->acquire ? p->acquire(p->ctx, size, align) : NULL;
}

static inline void prelease(const ano_mem_parent *p, void *block)
{
    if (p->release && block != NULL)
        p->release(p->ctx, block);
}

// ---------------------------------------------------------------------------------------------
// Monotonic.

typedef struct mono_slab {
    struct mono_slab *next;
    size_t cap;     // total slab bytes, header included
} mono_slab;

#define MONO_HDR 64u    // slab base is 4096-aligned, so data starts 64-aligned

// The bump cursor is a bare {at, end} pair (the two-pointer arena fast path); slabs keep
// no used counter. Within an epoch, slabs before cur are abandoned, slabs after cur are
// untouched capacity from a previous epoch. Dedicated oversize slabs are born full, so
// they live on a side list for the epoch and splice back in as plain capacity at reset.
struct ano_mem_monotonic {
    char *at;               // next free byte in the current slab
    char *end;              // one past the current slab
    ano_mem_parent parent;
    mono_slab *head;
    mono_slab *tail;
    mono_slab *cur;         // the slab [at, end) points into
    mono_slab *dedicated;   // this epoch's oversize slabs (full from birth)
    size_t     next_slab;   // bytes to request for the next growth slab
    ano_mem_stats st;       // peaks are folded at reset/destroy/stats: within an epoch
                            // live only grows, so the hot path skips the max() dance
};

static inline void mono_fold_peaks(ano_mem_monotonic *m)
{
    if (m->st.live_bytes  > m->st.peak_bytes)  m->st.peak_bytes  = m->st.live_bytes;
    if (m->st.live_blocks > m->st.peak_blocks) m->st.peak_blocks = m->st.live_blocks;
}

ano_mem_monotonic *ano_mem_monotonic_make(ano_mem_parent parent, size_t first_slab)
{
    if (parent.acquire == NULL)
        return NULL;
    ano_mem_monotonic *m = pacquire(&parent, sizeof *m, 64);
    if (m == NULL)
        return NULL;
    memset(m, 0, sizeof *m);
    m->parent = parent;
    if (first_slab == 0)
        first_slab = MONO_SLAB_DEFAULT;
    if (first_slab < MONO_SLAB_MIN) first_slab = MONO_SLAB_MIN;
    if (first_slab > MONO_SLAB_MAX) first_slab = MONO_SLAB_MAX;
    m->next_slab = first_slab;
    m->st.chunk_bytes = sizeof *m;
    m->st.chunk_count = 1;
    return m;
}

// A fresh growth slab of total bytes, linked at the tail. NULL if the parent cannot serve.
static mono_slab *mono_grow(ano_mem_monotonic *m, size_t bytes)
{
    mono_slab *s = pacquire(&m->parent, bytes, MAX_ALIGN);
    if (s == NULL)
        return NULL;
    s->next = NULL;
    s->cap  = bytes;
    if (m->tail) m->tail->next = s;
    else         m->head = s;
    m->tail = s;
    m->st.chunk_bytes += bytes;
    m->st.chunk_count += 1;
    return s;
}

static inline void mono_point_at(ano_mem_monotonic *m, mono_slab *s)
{
    m->cur = s;
    m->at  = (char *)s + MONO_HDR;
    m->end = (char *)s + s->cap;
}

// The out-of-line half: advance through untouched capacity, else grow or dedicate.
static void *mono_alloc_slow(ano_mem_monotonic *m, size_t size, size_t align)
{
    // Untouched slabs from a previous epoch may still serve.
    if (m->cur != NULL) {
        for (mono_slab *s = m->cur->next; s != NULL; s = s->next) {
            uintptr_t data = (uintptr_t)s + MONO_HDR;
            uintptr_t p    = (data + (align - 1)) & ~(uintptr_t)(align - 1);
            if (p + size <= (uintptr_t)s + s->cap) {
                mono_point_at(m, s);
                m->at = (char *)(p + size);
                m->st.live_bytes  += size;
                m->st.live_blocks += 1;
                return (void *)p;
            }
        }
    }
    size_t want = MONO_HDR + size + align;
    if (want > m->next_slab) {
        // Dedicated slab: born full, parked on the side list so the bump cursor and the
        // current slab's remaining tail stay exactly where they are.
        mono_slab *s = pacquire(&m->parent, want, MAX_ALIGN);
        if (s == NULL)
            return NULL;
        s->cap  = want;
        s->next = m->dedicated;
        m->dedicated = s;
        m->st.chunk_bytes += want;
        m->st.chunk_count += 1;
        uintptr_t p = ((uintptr_t)s + MONO_HDR + (align - 1)) & ~(uintptr_t)(align - 1);
        m->st.live_bytes  += size;
        m->st.live_blocks += 1;
        return (void *)p;
    }
    size_t bytes   = m->next_slab;
    size_t doubled = m->next_slab * 2;
    m->next_slab = doubled > MONO_SLAB_MAX ? MONO_SLAB_MAX : doubled;
    mono_slab *s = mono_grow(m, bytes);
    if (s == NULL)
        return NULL;
    mono_point_at(m, s);
    uintptr_t p = ((uintptr_t)m->at + (align - 1)) & ~(uintptr_t)(align - 1);
    m->at = (char *)(p + size);             // cannot overflow: sized to fit
    m->st.live_bytes  += size;
    m->st.live_blocks += 1;
    return (void *)p;
}

void *ano_mem_monotonic_alloc(ano_mem_monotonic *m, size_t size, size_t align)
{
    if (m == NULL || size == 0 || size > SIZE_MAX / 2)
        return NULL;
    if (align == 0)
        align = _Alignof(max_align_t);
    if (!is_pow2(align) || align > MAX_ALIGN)
        return NULL;
    // The two-pointer fast path: one align, one compare, one bump.
    uintptr_t p = ((uintptr_t)m->at + (align - 1)) & ~(uintptr_t)(align - 1);
    if (p + size <= (uintptr_t)m->end) {
        m->at = (char *)(p + size);
        m->st.live_bytes  += size;
        m->st.live_blocks += 1;
        return (void *)p;
    }
    return mono_alloc_slow(m, size, align);
}

void ano_mem_monotonic_reset(ano_mem_monotonic *m)
{
    if (m == NULL)
        return;
    // This epoch's dedicated slabs become plain capacity for the next one.
    while (m->dedicated != NULL) {
        mono_slab *s = m->dedicated;
        m->dedicated = s->next;
        s->next = NULL;
        if (m->tail) m->tail->next = s;
        else         m->head = s;
        m->tail = s;
    }
    if (m->head != NULL)
        mono_point_at(m, m->head);
    mono_fold_peaks(m);                 // live only grew since the last fold
    m->st.live_bytes  = 0;
    m->st.live_blocks = 0;
}

void ano_mem_monotonic_destroy(ano_mem_monotonic *m)
{
    if (m == NULL)
        return;
    ano_mem_parent parent = m->parent;
    mono_slab *s = m->head;
    while (s != NULL) {
        mono_slab *next = s->next;
        prelease(&parent, s);
        s = next;
    }
    s = m->dedicated;
    while (s != NULL) {
        mono_slab *next = s->next;
        prelease(&parent, s);
        s = next;
    }
    prelease(&parent, m);
}

ano_mem_stats ano_mem_monotonic_stats(const ano_mem_monotonic *m)
{
    ano_mem_stats s = {0};
    if (m == NULL)
        return s;
    s = m->st;      // fold peaks into the copy: live only grows between resets
    if (s.live_bytes  > s.peak_bytes)  s.peak_bytes  = s.live_bytes;
    if (s.live_blocks > s.peak_blocks) s.peak_blocks = s.live_blocks;
    return s;
}

// ---------------------------------------------------------------------------------------------
// Free-list core, shared by multipool classes and the fixed pool.

typedef struct pool_chunk {
    struct pool_chunk *next;
    size_t bytes;
} pool_chunk;

typedef struct pool_core {
    void  *free_head;       // LIFO through the first word of each free block
    size_t stride;          // serving size; a multiple of align
    size_t align;
    size_t next_blocks;     // geometric refill, in blocks
    size_t total_blocks;    // ever carved (the pool cap checks against this)
} pool_core;

static void pool_core_init(pool_core *c, size_t stride, size_t align)
{
    c->free_head    = NULL;
    c->stride       = stride;
    c->align        = align;
    c->next_blocks  = CHUNK_BYTES_MIN / stride;
    if (c->next_blocks == 0)
        c->next_blocks = 1;
    c->total_blocks = 0;
}

// Carve one chunk of up to c->next_blocks blocks (at least min_blocks, at most max_blocks
// when max_blocks != 0) and push them onto the free list. Returns blocks carved, 0 on
// parent exhaustion. Doubles next_blocks up to the byte target.
static size_t pool_core_refill(pool_core *c, const ano_mem_parent *parent,
                               pool_chunk **chunks, ano_mem_stats *st, size_t max_blocks)
{
    size_t n = c->next_blocks;
    if (max_blocks != 0) {
        size_t left = max_blocks - c->total_blocks;
        if (left == 0)
            return 0;
        if (n > left)
            n = left;
    }
    size_t hdr = align_up(sizeof(pool_chunk), c->align);
    if (n > (SIZE_MAX - hdr) / c->stride)
        return 0;
    size_t bytes = hdr + n * c->stride;
    pool_chunk *ck = pacquire(parent, bytes, c->align > 16 ? c->align : 16);
    if (ck == NULL)
        return 0;
    ck->next  = *chunks;
    ck->bytes = bytes;
    *chunks   = ck;
    st->chunk_bytes += bytes;
    st->chunk_count += 1;

    char *base = (char *)ck + hdr;
    for (size_t i = n; i-- > 0; ) {         // push descending so pops walk ascending
        void *b = base + i * c->stride;
        *(void **)b = c->free_head;
        c->free_head = b;
    }
    c->total_blocks += n;

    size_t cap = CHUNK_BYTES_MAX / c->stride;
    if (cap == 0) cap = 1;
    size_t doubled = c->next_blocks * 2;
    c->next_blocks = doubled > cap ? cap : doubled;
    if (c->next_blocks == 0)
        c->next_blocks = 1;
    return n;
}

static inline void *pool_core_pop(pool_core *c)
{
    void *p = c->free_head;
    if (p)
        c->free_head = *(void **)p;
    return p;
}

static inline void pool_core_push(pool_core *c, void *p)
{
    *(void **)p = c->free_head;
    c->free_head = p;
}

// ---------------------------------------------------------------------------------------------
// Multipool.

typedef struct ov_hdr {                     // oversize passthrough tracking, payload at +4096
    struct ov_hdr *prev, *next;
    size_t bytes;
} ov_hdr;

struct ano_mem_multipool {
    ano_mem_parent parent;
    size_t   min_block, max_block;
    uint32_t log2min, nclasses;
    pool_chunk *chunks;
    ov_hdr     *oversize;
    ano_mem_stats st;
    pool_core cls[];
};

static inline uint32_t mp_class_of(const ano_mem_multipool *mp, size_t size)
{
    if (size <= mp->min_block)
        return 0;
    return (uint32_t)(64 - __builtin_clzll((unsigned long long)(size - 1))) - mp->log2min;
}

ano_mem_multipool *ano_mem_multipool_make(ano_mem_parent parent,
                                          const ano_mem_multipool_cfg *cfg)
{
    if (parent.acquire == NULL)
        return NULL;
    size_t minb = cfg && cfg->min_block ? cfg->min_block : 16;
    size_t maxb = cfg && cfg->max_block ? cfg->max_block : (size_t)1 << 20;
    if (!is_pow2(minb) || !is_pow2(maxb) || minb < 16 || maxb < minb)
        return NULL;

    uint32_t log2min  = (uint32_t)__builtin_ctzll((unsigned long long)minb);
    uint32_t nclasses = (uint32_t)__builtin_ctzll((unsigned long long)maxb) - log2min + 1;

    ano_mem_multipool *mp = pacquire(&parent, sizeof *mp + nclasses * sizeof(pool_core), 64);
    if (mp == NULL)
        return NULL;
    memset(mp, 0, sizeof *mp + nclasses * sizeof(pool_core));
    mp->parent    = parent;
    mp->min_block = minb;
    mp->max_block = maxb;
    mp->log2min   = log2min;
    mp->nclasses  = nclasses;
    for (uint32_t i = 0; i < nclasses; i++) {
        size_t stride = minb << i;
        size_t align  = stride < MAX_ALIGN ? stride : MAX_ALIGN;
        pool_core_init(&mp->cls[i], stride, align);
    }
    mp->st.chunk_bytes = sizeof *mp + nclasses * sizeof(pool_core);
    mp->st.chunk_count = 1;
    return mp;
}

static void *mp_oversize_alloc(ano_mem_multipool *mp, size_t size)
{
    if (size > SIZE_MAX - OVERSIZE_HDR)
        return NULL;
    ov_hdr *h = pacquire(&mp->parent, OVERSIZE_HDR + size, MAX_ALIGN);
    if (h == NULL)
        return NULL;
    h->prev = NULL;
    h->next = mp->oversize;
    h->bytes = OVERSIZE_HDR + size;
    if (mp->oversize)
        mp->oversize->prev = h;
    mp->oversize = h;
    mp->st.chunk_bytes += h->bytes;
    mp->st.chunk_count += 1;
    stats_on_alloc(&mp->st, size);
    return (char *)h + OVERSIZE_HDR;
}

static void mp_oversize_free(ano_mem_multipool *mp, void *p, size_t size)
{
    ov_hdr *h = (ov_hdr *)((char *)p - OVERSIZE_HDR);
    if (h->prev) h->prev->next = h->next;
    else         mp->oversize  = h->next;
    if (h->next) h->next->prev = h->prev;
    mp->st.chunk_bytes -= h->bytes;
    mp->st.chunk_count -= 1;
    stats_on_free(&mp->st, size);
    prelease(&mp->parent, h);
}

void *ano_mem_multipool_alloc(ano_mem_multipool *mp, size_t size)
{
    if (mp == NULL || size == 0)
        return NULL;
    if (size > mp->max_block)
        return mp_oversize_alloc(mp, size);
    pool_core *c = &mp->cls[mp_class_of(mp, size)];
    void *p = pool_core_pop(c);
    if (p == NULL) {
        if (pool_core_refill(c, &mp->parent, &mp->chunks, &mp->st, 0) == 0)
            return NULL;
        p = pool_core_pop(c);
    }
    stats_on_alloc(&mp->st, c->stride);
    return p;
}

void ano_mem_multipool_free(ano_mem_multipool *mp, void *p, size_t size)
{
    if (mp == NULL || p == NULL || size == 0)
        return;
    if (size > mp->max_block) {
        mp_oversize_free(mp, p, size);
        return;
    }
    pool_core *c = &mp->cls[mp_class_of(mp, size)];
    pool_core_push(c, p);
    stats_on_free(&mp->st, c->stride);
}

void ano_mem_multipool_destroy(ano_mem_multipool *mp)
{
    if (mp == NULL)
        return;
    ano_mem_parent parent = mp->parent;
    ov_hdr *h = mp->oversize;
    while (h != NULL) {
        ov_hdr *next = h->next;
        prelease(&parent, h);
        h = next;
    }
    pool_chunk *ck = mp->chunks;
    while (ck != NULL) {
        pool_chunk *next = ck->next;
        prelease(&parent, ck);
        ck = next;
    }
    prelease(&parent, mp);
}

ano_mem_stats ano_mem_multipool_stats(const ano_mem_multipool *mp)
{
    ano_mem_stats z = {0};
    return mp ? mp->st : z;
}

// ---------------------------------------------------------------------------------------------
// Fixed pool.

struct ano_mem_pool {
    ano_mem_parent parent;
    pool_core  core;
    size_t     max_blocks;
    size_t     usable;      // caller-visible block_size
    pool_chunk *chunks;
    ano_mem_stats st;
};

ano_mem_pool *ano_mem_pool_make(ano_mem_parent parent, size_t block_size,
                                size_t block_align, size_t max_blocks)
{
    if (parent.acquire == NULL || block_size == 0 || block_size > SIZE_MAX / 4)
        return NULL;
    if (block_align == 0) {
        block_align = pow2_ceil(block_size);
        if (block_align == 0 || block_align > MAX_ALIGN)
            block_align = MAX_ALIGN;
    }
    if (!is_pow2(block_align) || block_align > MAX_ALIGN)
        return NULL;

    size_t stride = block_size < sizeof(void *) ? sizeof(void *) : block_size;
    stride = align_up(stride, block_align);

    ano_mem_pool *p = pacquire(&parent, sizeof *p, 64);
    if (p == NULL)
        return NULL;
    memset(p, 0, sizeof *p);
    p->parent     = parent;
    p->max_blocks = max_blocks;
    p->usable     = block_size;
    pool_core_init(&p->core, stride, block_align);
    p->st.chunk_bytes = sizeof *p;
    p->st.chunk_count = 1;
    return p;
}

int ano_mem_pool_reserve(ano_mem_pool *p, size_t blocks)
{
    if (p == NULL)
        return -1;
    if (p->max_blocks != 0 && blocks > p->max_blocks)
        blocks = p->max_blocks;
    while (p->core.total_blocks < blocks) {
        if (pool_core_refill(&p->core, &p->parent, &p->chunks, &p->st, p->max_blocks) == 0)
            return -1;
    }
    return 0;
}

void *ano_mem_pool_alloc(ano_mem_pool *p)
{
    if (p == NULL)
        return NULL;
    void *b = pool_core_pop(&p->core);
    if (b == NULL) {
        if (pool_core_refill(&p->core, &p->parent, &p->chunks, &p->st, p->max_blocks) == 0)
            return NULL;                    // capped-and-empty or parent exhausted
        b = pool_core_pop(&p->core);
    }
    stats_on_alloc(&p->st, p->core.stride);
    return b;
}

void ano_mem_pool_free(ano_mem_pool *p, void *block)
{
    if (p == NULL || block == NULL)
        return;
    pool_core_push(&p->core, block);
    stats_on_free(&p->st, p->core.stride);
}

void ano_mem_pool_destroy(ano_mem_pool *p)
{
    if (p == NULL)
        return;
    ano_mem_parent parent = p->parent;
    pool_chunk *ck = p->chunks;
    while (ck != NULL) {
        pool_chunk *next = ck->next;
        prelease(&parent, ck);
        ck = next;
    }
    prelease(&parent, p);
}

ano_mem_stats ano_mem_pool_stats(const ano_mem_pool *p)
{
    ano_mem_stats z = {0};
    return p ? p->st : z;
}
