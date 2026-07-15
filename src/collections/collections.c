/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Plane lifecycle for anoptic_collections.h. Hot paths are inline in the header.

#include <anoptic_collections.h>

// Round up to pow2, floor 2, ceiling 2^31 (0 = out of range). Cap <= 2^31 for u32 SPSC diffs and i64 seq arithmetic.
static uint32_t pow2_cap(uint32_t capacity)
{
    if (capacity > (1u << 31))
        return 0;
    uint32_t c = 2;
    while (c < capacity)
        c <<= 1;
    return c;
}

static void *plane_acquire(ano_mem_parent p, size_t size)
{
    return p.acquire == NULL ? NULL : p.acquire(p.ctx, size, ANO_CACHE_LINE);
}

static void plane_release(ano_mem_parent p, void *block)
{
    if (p.release != NULL && block != NULL)
        p.release(p.ctx, block);                // NULL release: wink-out
}

/* SPSC */

bool ano_ring_spsc_init(anoring_spsc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride)
{
    if (r == NULL || stride == 0)
        return false;
    uint32_t cap = pow2_cap(capacity);
    if (cap == 0 || (uint64_t)cap * stride > SIZE_MAX / 2)
        return false;
    uint8_t *buf = plane_acquire(parent, (size_t)cap * stride);
    if (buf == NULL)
        return false;
    memset(r, 0, sizeof *r);
    atomic_init(&r->tail, 0u);
    atomic_init(&r->head, 0u);
    r->mask   = cap - 1;
    r->stride = stride;
    r->buf    = buf;
    r->parent = parent;
    return true;
}

void ano_ring_spsc_destroy(anoring_spsc *r)
{
    if (r == NULL)
        return;
    plane_release(r->parent, r->buf);
    memset(r, 0, sizeof *r);
}

/* Vyukov Planes */

// Shared Vyukov plane construction.

static bool vy_planes(ano_mem_parent parent, uint32_t capacity, uint32_t stride,
                      uint64_t *out_mask, _Atomic uint64_t **out_seq, uint8_t **out_data)
{
    if (stride == 0)
        return false;
    uint32_t cap = pow2_cap(capacity);
    if (cap == 0 || (uint64_t)cap * stride > SIZE_MAX / 2)
        return false;
    _Atomic uint64_t *seq = plane_acquire(parent, (size_t)cap * sizeof *seq);
    uint8_t *data         = plane_acquire(parent, (size_t)cap * stride);
    if (seq == NULL || data == NULL) {
        plane_release(parent, seq);
        plane_release(parent, data);
        return false;
    }
    for (uint32_t i = 0; i < cap; i++)          // lap 0: free at index
        atomic_init(&seq[i], i);
    *out_mask = cap - 1;
    *out_seq  = seq;
    *out_data = data;
    return true;
}

bool ano_ring_mpsc_init(anoring_mpsc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride)
{
    if (r == NULL)
        return false;
    uint64_t mask;
    _Atomic uint64_t *seq;
    uint8_t *data;
    if (!vy_planes(parent, capacity, stride, &mask, &seq, &data))
        return false;
    memset(r, 0, sizeof *r);
    atomic_init(&r->tail, 0u);
    atomic_init(&r->head, 0u);
    r->mask   = mask;
    r->stride = stride;
    r->seq    = seq;
    r->data   = data;
    r->parent = parent;
    return true;
}

bool ano_ring_spmc_init(anoring_spmc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride)
{
    if (r == NULL)
        return false;
    uint64_t mask;
    _Atomic uint64_t *seq;
    uint8_t *data;
    if (!vy_planes(parent, capacity, stride, &mask, &seq, &data))
        return false;
    memset(r, 0, sizeof *r);
    atomic_init(&r->tail, 0u);
    atomic_init(&r->head, 0u);
    r->mask   = mask;
    r->stride = stride;
    r->seq    = seq;
    r->data   = data;
    r->parent = parent;
    return true;
}

bool ano_ring_mpmc_init(anoring_mpmc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride)
{
    if (r == NULL)
        return false;
    uint64_t mask;
    _Atomic uint64_t *seq;
    uint8_t *data;
    if (!vy_planes(parent, capacity, stride, &mask, &seq, &data))
        return false;
    memset(r, 0, sizeof *r);
    atomic_init(&r->tail, 0u);
    atomic_init(&r->head, 0u);
    r->mask   = mask;
    r->stride = stride;
    r->seq    = seq;
    r->data   = data;
    r->parent = parent;
    return true;
}

void ano_ring_mpsc_destroy(anoring_mpsc *r)
{
    if (r == NULL)
        return;
    plane_release(r->parent, (void *)r->seq);
    plane_release(r->parent, r->data);
    memset(r, 0, sizeof *r);
}

void ano_ring_spmc_destroy(anoring_spmc *r)
{
    if (r == NULL)
        return;
    plane_release(r->parent, (void *)r->seq);
    plane_release(r->parent, r->data);
    memset(r, 0, sizeof *r);
}

void ano_ring_mpmc_destroy(anoring_mpmc *r)
{
    if (r == NULL)
        return;
    plane_release(r->parent, (void *)r->seq);
    plane_release(r->parent, r->data);
    memset(r, 0, sizeof *r);
}

/* Seqpub */

bool ano_seqpub_init(anoseqpub *p, ano_mem_parent parent, uint32_t stride)
{
    if (p == NULL || stride == 0)
        return false;
    void *value = plane_acquire(parent, stride);
    if (value == NULL)
        return false;
    memset(p, 0, sizeof *p);
    atomic_init(&p->version, 0u);               // 0 = never published
    p->stride = stride;
    p->value  = value;
    p->parent = parent;
    return true;
}

void ano_seqpub_destroy(anoseqpub *p)
{
    if (p == NULL)
        return;
    plane_release(p->parent, p->value);
    memset(p, 0, sizeof *p);
}
