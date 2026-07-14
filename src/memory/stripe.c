/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The fourth allocator, plus the two measurement seams the other three were missing. STUB.
//
// TODO(W3, M4): ano_mem_stripe -- lane-isolated allocation where two different lanes NEVER
// share a grain-sized region, and ano_mem_stripe_planes: N parallel SoA arrays from ONE
// parent acquisition, every base on the grain. Models D and E are INEXPRESSIBLE without it,
// and its first production consumer is a real bug fix: g_readers[64] is 64 x 16-byte lanes,
// four to a cache line, raced by 1.2M reader observations.
//
// TODO(W3, M4): ano_mem_multipool_class_stats -- a class HISTOGRAM, which is what B.4 asks
// for and what a {min_block, max_block} window cannot answer.
//
// TODO(W3, M4): ano_mem_parent_counting -- the ONLY way a per-domain heap footprint becomes
// measurable (D19). Summing each allocator's chunk_bytes misses every chunk mimalloc still
// holds; counting at the parent seam does not.

#include <anoptic_memory_pools.h>

#include <stddef.h>

ano_mem_parent ano_mem_parent_counting(ano_mem_parent inner, ano_mem_parent_ledger *ledger)
{
    (void)ledger;
    return inner;                               // TODO(W3, M4): interpose and count
}

size_t ano_mem_multipool_class_stats(const ano_mem_multipool *mp, ano_mem_class_stats *out,
                                     size_t cap)
{
    (void)mp; (void)out; (void)cap;
    return 0;                                   // TODO(W3, M4)
}

ano_mem_stripe *ano_mem_stripe_make(ano_mem_parent parent, const ano_mem_stripe_cfg *cfg)
{
    (void)parent; (void)cfg;
    return NULL;                                // TODO(W3, M4)
}

void *ano_mem_stripe_alloc(ano_mem_stripe *s, size_t lane, size_t size, size_t align)
{
    (void)s; (void)lane; (void)size; (void)align;
    return NULL;                                // TODO(W3, M4)
}

int ano_mem_stripe_planes(ano_mem_stripe *s, const size_t *count, const size_t *elem_size,
                          size_t n_planes, void **out_planes)
{
    (void)s; (void)count; (void)elem_size; (void)n_planes; (void)out_planes;
    return -1;                                  // TODO(W3, M4)
}

void ano_mem_stripe_reset(ano_mem_stripe *s)
{
    (void)s;                                    // TODO(W3, M4)
}

void ano_mem_stripe_destroy(ano_mem_stripe *s)
{
    (void)s;                                    // TODO(W3, M4)
}

ano_mem_stats ano_mem_stripe_stats(const ano_mem_stripe *s)
{
    (void)s;
    return (ano_mem_stats){0};                  // TODO(W3, M4)
}
