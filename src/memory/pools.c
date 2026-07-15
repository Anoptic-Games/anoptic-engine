/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Monotonic, multipool, pool over a parent.
// Layout: chunks acquired at >= block align; headers/strides multiples of it (positional align).
// Free lists thread the first word of each free block. Control state in parent memory (wink-out reclaim).

#include <anoptic_memory_pools.h>

#include <string.h>

#define MONO_SLAB_DEFAULT   (64u * 1024u)
#define MONO_SLAB_MIN       4096u
#define MONO_SLAB_MAX       (8u * 1024u * 1024u)
#define CHUNK_BYTES_MIN     4096u
#define CHUNK_BYTES_MAX     (512u * 1024u)
#define MAX_ALIGN           4096u
#define OVERSIZE_HDR        4096u   // payload 4096-aligned behind tracking header

static inline size_t align_up(size_t v, size_t a)     { return (v + a - 1) & ~(a - 1); }
static inline bool   is_pow2(size_t v)                { return v != 0 && (v & (v - 1)) == 0; }
static inline size_t pow2_ceil(size_t v)              // v >= 1; saturates, no wrap
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

/* Parents */

static void *parent_heap_acquire(void *ctx, size_t size, size_t align)
{
    return mi_heap_malloc_aligned((mi_heap_t *)ctx, size, align);
}

static void parent_heap_release(void *ctx, void *block)
{
    (void)ctx;
    mi_free(block);     // routes home to owning heap
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

/* Monotonic */

typedef struct mono_slab {
    struct mono_slab *next;
    size_t cap;     // total slab bytes, header included
} mono_slab;

#define MONO_HDR 64u    // slab base 4096-aligned -> data starts 64-aligned

// Bump cursor {at, end}. Slabs before cur abandoned; after cur = unused capacity from prior epoch.
// Dedicated oversize slabs live on a side list, splice back as capacity at reset.
struct ano_mem_monotonic {
    char *at;               // next free byte in current slab
    char *end;              // one past current slab
    ano_mem_parent parent;
    mono_slab *head;
    mono_slab *tail;
    mono_slab *cur;         // slab [at, end) points into
    mono_slab *dedicated;   // this epoch's oversize slabs (born full)
    size_t     next_slab;   // next growth slab request size
    ano_mem_stats st;       // peaks folded at reset/stats (live only grows in-epoch)
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
    m->st.chunk_bytes     = sizeof *m;
    m->st.chunk_count     = 1;
    m->st.parent_acquires = 1;
    m->st.parent_bytes    = sizeof *m;
    return m;
}

// Fresh growth slab linked at tail. NULL if parent cannot serve.
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
    m->st.chunk_bytes     += bytes;
    m->st.chunk_count     += 1;
    m->st.parent_acquires += 1;
    m->st.parent_bytes    += bytes;
    return s;
}

static inline void mono_point_at(ano_mem_monotonic *m, mono_slab *s)
{
    m->cur = s;
    m->at  = (char *)s + MONO_HDR;
    m->end = (char *)s + s->cap;
}

// Slow path: walk unused capacity, else grow or dedicate.
static void *mono_alloc_slow(ano_mem_monotonic *m, size_t size, size_t align)
{
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
        // Dedicated slab: born full, side-listed; bump cursor and current tail stay put.
        mono_slab *s = pacquire(&m->parent, want, MAX_ALIGN);
        if (s == NULL)
            return NULL;
        s->cap  = want;
        s->next = m->dedicated;
        m->dedicated = s;
        m->st.chunk_bytes     += want;
        m->st.chunk_count     += 1;
        m->st.parent_acquires += 1;
        m->st.parent_bytes    += want;
        m->st.oversize_hits   += 1;
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
    m->at = (char *)(p + size);             // sized to fit
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
    // Fast path: align, compare, bump.
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
    // Dedicated slabs -> plain capacity for next epoch.
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
    mono_fold_peaks(m);
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
    s = m->st;      // fold peaks into the copy
    if (s.live_bytes  > s.peak_bytes)  s.peak_bytes  = s.live_bytes;
    if (s.live_blocks > s.peak_blocks) s.peak_blocks = s.live_blocks;
    s.requested_bytes = s.live_bytes;  // bump charges at request size
    return s;
}

/* Free-list core */

// Shared by multipool classes and the fixed pool.
typedef struct pool_chunk {
    struct pool_chunk *next;
    size_t bytes;
} pool_chunk;

typedef struct pool_core {
    void  *free_head;       // LIFO through first word of each free block
    size_t stride;          // serving size; multiple of align
    size_t align;
    size_t next_blocks;     // geometric refill, in blocks
    size_t total_blocks;    // ever carved (cap checks this)
    size_t hits;            // cumulative allocs from this class
    size_t live;            // blocks currently out
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
    c->hits         = 0;
    c->live         = 0;
}

// Carve up to next_blocks onto the free list (clamped by max_blocks). Returns blocks carved, 0 on exhaustion.
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
    st->chunk_bytes     += bytes;
    st->chunk_count     += 1;
    st->parent_acquires += 1;
    st->parent_bytes    += bytes;

    char *base = (char *)ck + hdr;
    for (size_t i = n; i-- > 0; ) {         // push descending -> pops walk ascending
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

/* Multipool */

typedef struct ov_hdr {                     // oversize passthrough; payload at +4096
    struct ov_hdr *prev, *next;
    size_t bytes;
} ov_hdr;

struct ano_mem_multipool {
    ano_mem_parent parent;
    size_t   min_block, max_block;
    uint32_t log2min, nclasses;
    bool     explicit_cls;      // cfg.classes, not geometric
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

// Class for size (<= max_block): clz on geometric, smallest fitting stride on explicit list.
static inline pool_core *mp_class_for(ano_mem_multipool *mp, size_t size)
{
    if (!mp->explicit_cls)
        return &mp->cls[mp_class_of(mp, size)];
    uint32_t i = 0;
    while (mp->cls[i].stride < size)
        i++;
    return &mp->cls[i];
}

// Explicit list: each class multiple of 16, strictly ascending; last is max_block.
static bool mp_classes_valid(const size_t *classes, size_t n)
{
    if (n == 0 || n > UINT32_MAX)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (classes[i] < 16 || classes[i] % 16 != 0)
            return false;
        if (i > 0 && classes[i] <= classes[i - 1])
            return false;
    }
    return true;
}

ano_mem_multipool *ano_mem_multipool_make(ano_mem_parent parent,
                                          const ano_mem_multipool_cfg *cfg)
{
    if (parent.acquire == NULL)
        return NULL;
    bool explicit_cls = cfg && cfg->classes != NULL && cfg->class_count != 0;
    size_t minb, maxb;
    uint32_t nclasses;
    if (explicit_cls) {
        if (!mp_classes_valid(cfg->classes, cfg->class_count))
            return NULL;
        if (cfg->class_count > (SIZE_MAX - sizeof(ano_mem_multipool)) / sizeof(pool_core))
            return NULL;
        nclasses = (uint32_t)cfg->class_count;
        minb = cfg->classes[0];
        maxb = cfg->classes[nclasses - 1];
    } else {
        minb = cfg && cfg->min_block ? cfg->min_block : 16;
        maxb = cfg && cfg->max_block ? cfg->max_block : (size_t)1 << 20;
        if (!is_pow2(minb) || !is_pow2(maxb) || minb < 16 || maxb < minb)
            return NULL;
        nclasses = (uint32_t)__builtin_ctzll((unsigned long long)maxb)
                 - (uint32_t)__builtin_ctzll((unsigned long long)minb) + 1;
    }

    ano_mem_multipool *mp = pacquire(&parent, sizeof *mp + nclasses * sizeof(pool_core), 64);
    if (mp == NULL)
        return NULL;
    memset(mp, 0, sizeof *mp + nclasses * sizeof(pool_core));
    mp->parent       = parent;
    mp->min_block    = minb;
    mp->max_block    = maxb;
    mp->log2min      = explicit_cls ? 0 : (uint32_t)__builtin_ctzll((unsigned long long)minb);
    mp->nclasses     = nclasses;
    mp->explicit_cls = explicit_cls;
    for (uint32_t i = 0; i < nclasses; i++) {
        // Positional align: largest pow2 dividing stride, capped at 4096. Explicit classes >= 16-aligned.
        size_t stride = explicit_cls ? cfg->classes[i] : minb << i;
        size_t align  = stride & (~stride + 1);
        if (align > MAX_ALIGN)
            align = MAX_ALIGN;
        pool_core_init(&mp->cls[i], stride, align);
    }
    mp->st.chunk_bytes     = sizeof *mp + nclasses * sizeof(pool_core);
    mp->st.chunk_count     = 1;
    mp->st.parent_acquires = 1;
    mp->st.parent_bytes    = mp->st.chunk_bytes;
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
    mp->st.chunk_bytes     += h->bytes;
    mp->st.chunk_count     += 1;
    mp->st.parent_acquires += 1;
    mp->st.parent_bytes    += h->bytes;
    mp->st.oversize_hits   += 1;
    mp->st.requested_bytes += size;
    stats_on_alloc(&mp->st, size);
    return (char *)h + OVERSIZE_HDR;
}

static void mp_oversize_free(ano_mem_multipool *mp, void *p, size_t size)
{
    (void)size;                                 // header records the truth
    ov_hdr *h = (ov_hdr *)((char *)p - OVERSIZE_HDR);
    size_t rec = h->bytes - OVERSIZE_HDR;
    if (h->prev) h->prev->next = h->next;
    else         mp->oversize  = h->next;
    if (h->next) h->next->prev = h->prev;
    mp->st.chunk_bytes     -= h->bytes;
    mp->st.chunk_count     -= 1;
    if (mp->parent.release != NULL)
        mp->st.parent_releases += 1;            // releaseless parent keeps the block
    mp->st.requested_bytes -= rec;
    stats_on_free(&mp->st, rec);
    prelease(&mp->parent, h);
}

void *ano_mem_multipool_alloc(ano_mem_multipool *mp, size_t size)
{
    if (mp == NULL || size == 0)
        return NULL;
    if (size > mp->max_block)
        return mp_oversize_alloc(mp, size);
    pool_core *c = mp_class_for(mp, size);
    void *p = pool_core_pop(c);
    if (p == NULL) {
        if (pool_core_refill(c, &mp->parent, &mp->chunks, &mp->st, 0) == 0)
            return NULL;
        p = pool_core_pop(c);
    }
    c->hits += 1;
    c->live += 1;
    mp->st.requested_bytes += size;
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
    pool_core *c = mp_class_for(mp, size);
    pool_core_push(c, p);
    c->live -= 1;
    // requested is LIVE; same-class free at a different size saturates (resyncs at quiescence).
    mp->st.requested_bytes -= size < mp->st.requested_bytes ? size : mp->st.requested_bytes;
    stats_on_free(&mp->st, c->stride);
    if (mp->st.live_blocks == 0)
        mp->st.requested_bytes = 0;
}

size_t ano_mem_multipool_class_stats(const ano_mem_multipool *mp, ano_mem_class_stats *out,
                                     size_t cap)
{
    if (mp == NULL)
        return 0;
    size_t n = mp->nclasses;
    if (out != NULL) {
        size_t m = n < cap ? n : cap;
        for (size_t i = 0; i < m; i++) {
            const pool_core *c = &mp->cls[i];
            out[i] = (ano_mem_class_stats){
                .stride      = c->stride,
                .hits        = c->hits,
                .live_blocks = c->live,
                .free_blocks = c->total_blocks - c->live,
            };
        }
    }
    return n;
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

/* Fixed pool */

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
    if (block_align < alignof(void *))
        block_align = alignof(void *);          // free-list links live in the block

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
    p->st.chunk_bytes     = sizeof *p;
    p->st.chunk_count     = 1;
    p->st.parent_acquires = 1;
    p->st.parent_bytes    = sizeof *p;
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
    p->core.hits += 1;
    p->core.live += 1;
    stats_on_alloc(&p->st, p->core.stride);
    return b;
}

void ano_mem_pool_free(ano_mem_pool *p, void *block)
{
    if (p == NULL || block == NULL)
        return;
    pool_core_push(&p->core, block);
    p->core.live -= 1;
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
    if (p == NULL)
        return z;
    z = p->st;
    z.requested_bytes = z.live_blocks * p->usable;  // every request is block_size
    return z;
}
