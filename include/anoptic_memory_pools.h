/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Memory Pools
//
// Specialty local allocators over mimalloc, a strict superset of anoptic_memory.h.
// Three tools (Lakos, CppCon 2017; production shape: Bloomberg bdlma):
//
//   ano_mem_monotonic  bump allocation over growing slabs; no per-object free; reset or
//                      destroy. For batch-shaped lifetimes: parse staging, level load.
//   ano_mem_multipool  power-of-two size classes, each an O(1) free list carved from the
//                      parent in chunks. For long-running subsystems with size variation
//                      and churn. Fragmentation-immune by construction.
//   ano_mem_pool       one fixed block size, O(1) free list, optional hard cap with
//                      false-on-empty. The streaming chunk pool is an instance of this.
//
// One convention: every allocator takes a PARENT (a mi_heap_t or another pools allocator),
// so composition -- Lakos's Multipool<Monotonic>, his overall benchmark winner -- falls out
// of the constructor. The parent indirection sits on the chunk-refill path only, never on
// the per-allocation hot path.
//
// Wink-out is not magic, it is mi_heap_destroy: every allocator, its control struct
// included, lives in memory acquired from its parent chain, whose root is exactly one
// mi_heap. Destroying that heap reclaims every chunk of every allocator built over it in
// one call. After the backing heap dies, the allocator pointers are dead too -- do not
// call destroy on them afterwards. destroy() is the retail teardown for one allocator
// while its parent lives on.
//
// Threading: single-owner, like mi_heap_t and anostr_intern_t. Sharing is structural:
// ownership of filled memory transfers between threads through messages, never through a
// shared allocator.
//
// Totality: every function tolerates hostile-but-typed input. Failure is NULL (alloc/make)
// or a no-op (free/destroy of NULL), never UB. Freeing a pointer an allocator did not
// hand out, or double-freeing, IS undefined -- same contract as free(3).

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_POOLS_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_POOLS_H

#include <stddef.h>
#include <stdint.h>

#include "anoptic_memory.h"

typedef struct ano_mem_monotonic ano_mem_monotonic;
typedef struct ano_mem_multipool ano_mem_multipool;
typedef struct ano_mem_pool      ano_mem_pool;

// ---------------------------------------------------------------------------------------------
// The parent: where an allocator gets its chunks. A 3-word value, passed by copy.
// release == NULL means chunks are never returned retail (a monotonic parent): the
// allocator's destroy() becomes a bookkeeping no-op and the memory flows back when the
// PARENT resets, is destroyed, or winks out. That is the Multipool<Monotonic> semantic.

typedef struct ano_mem_parent {
    void *ctx;
    void *(*acquire)(void *ctx, size_t size, size_t align);  // NULL on exhaustion, never UB
    void  (*release)(void *ctx, void *block);                // NULL: no retail return path
} ano_mem_parent;

// A parent over a mimalloc heap. Chunks come from mi_heap_malloc_aligned, return via mi_free.
ano_mem_parent ano_mem_parent_heap(mi_heap_t *heap);

// A parent over the DEFAULT heap of whichever thread acquires (mi_malloc_aligned/mi_free).
// The one parent that is safe when the owning allocator is mutex-shared across threads
// (mi_heap_t parents are single-thread-owner; frees route home from any thread). No
// wink-out exists here: destroy() is the only teardown.
ano_mem_parent ano_mem_parent_default(void);

// A parent over a monotonic arena: chunks bump-allocate and never return retail.
ano_mem_parent ano_mem_parent_monotonic(ano_mem_monotonic *mono);

// ---------------------------------------------------------------------------------------------
// Stats. Queryable per allocator, updated with plain stores (single-owner, no atomics).
// live/peak count what the allocator has handed out, measured in SERVING size (the block's
// class size, not the request); chunk_* counts what it holds from its parent, control
// struct and chunk headers included. peak_* are high-water marks since make().

typedef struct ano_mem_stats {
    size_t live_bytes;
    size_t live_blocks;
    size_t peak_bytes;
    size_t peak_blocks;
    size_t chunk_bytes;
    size_t chunk_count;
} ano_mem_stats;

// ---------------------------------------------------------------------------------------------
// Monotonic: bump over parent-owned slabs. Slabs double geometrically from first_slab
// (0 = 64 KiB default) up to an internal cap; a request larger than the cap gets a
// dedicated slab. No per-object free exists on purpose.

// A new arena whose slabs (and control struct) come from parent.
// first_slab is a hint in bytes; 0 picks the default. NULL if the parent cannot serve.
ano_mem_monotonic *ano_mem_monotonic_make(ano_mem_parent parent, size_t first_slab);

// size bytes at an align boundary (power of 2, <= 4096; 0 = alignof(max_align_t)).
// NULL if size == 0, align is invalid, or the parent is exhausted. Never UB.
void *ano_mem_monotonic_alloc(ano_mem_monotonic *m, size_t size, size_t align);

// Rewind to empty, KEEPING every slab for reuse (the per-ingest staging idiom).
// live stats rezero; chunk stats persist. Every pointer ever returned is dead.
void ano_mem_monotonic_reset(ano_mem_monotonic *m);

// Return every slab and the control struct to the parent (no-op per block if the parent
// has no release). m is dead afterwards. NULL is a no-op.
void ano_mem_monotonic_destroy(ano_mem_monotonic *m);

ano_mem_stats ano_mem_monotonic_stats(const ano_mem_monotonic *m);

// ---------------------------------------------------------------------------------------------
// Multipool: geometric size classes min_block, 2*min_block, ..., max_block; a request maps
// to the smallest class that fits (one clz, no search). Frees push onto the class's free
// list; chunk memory returns to the parent only at destroy or wink-out, so steady-state
// churn never touches the parent. Requests beyond max_block pass through to the parent
// individually (tracked, so destroy still reclaims them).
//
// Alignment: a block is aligned to min(its class size, 4096). Any class >= ANO_CACHE_LINE
// hands out cache-line-aligned blocks; there is no separate aligned-alloc on purpose.

typedef struct ano_mem_multipool_cfg {
    size_t min_block;   // power of 2 >= 16;        0 = 16
    size_t max_block;   // power of 2 >= min_block; 0 = 1 MiB. Beyond it: parent passthrough.
} ano_mem_multipool_cfg;

// A new multipool over parent. cfg == NULL takes every default; invalid cfg yields NULL.
ano_mem_multipool *ano_mem_multipool_make(ano_mem_parent parent,
                                          const ano_mem_multipool_cfg *cfg);

// size bytes from the smallest fitting class (or the parent, past max_block).
// NULL if size == 0 or the parent is exhausted. Never UB.
void *ano_mem_multipool_alloc(ano_mem_multipool *mp, size_t size);

// Return a block. size must be the size passed to alloc (any size mapping to the same
// class also works; a different class is UB, like a wrong free(3)). p == NULL is a no-op.
// O(1): sized free is what lets blocks carry zero per-block metadata.
void ano_mem_multipool_free(ano_mem_multipool *mp, void *p, size_t size);

// Return every chunk, every passthrough block, and the control struct to the parent.
// mp is dead afterwards. NULL is a no-op.
void ano_mem_multipool_destroy(ano_mem_multipool *mp);

ano_mem_stats ano_mem_multipool_stats(const ano_mem_multipool *mp);

// ---------------------------------------------------------------------------------------------
// Pool: one fixed block size. max_blocks > 0 caps the population: alloc returns NULL when
// the cap is reached and the free list is empty -- false-on-empty, it never grows past the
// cap and never blocks. max_blocks == 0 grows on demand.

// A new pool over parent. block_size >= 1 (rounded up internally to hold a free-list
// link); block_align is a power of 2 <= 4096, 0 = min(pow2ceil(block_size), 4096).
// NULL on invalid input or parent exhaustion.
ano_mem_pool *ano_mem_pool_make(ano_mem_parent parent, size_t block_size,
                                size_t block_align, size_t max_blocks);

// Prewarm: grow capacity to at least blocks free-or-live blocks (clamped to max_blocks).
// 0 on success, -1 if the parent cannot serve that many. Partial growth is kept.
int ano_mem_pool_reserve(ano_mem_pool *p, size_t blocks);

// One block, exactly block_size usable bytes. NULL when capped-and-empty or the parent
// is exhausted. Never UB, never blocking.
void *ano_mem_pool_alloc(ano_mem_pool *p);

// Return a block to the free list. block == NULL is a no-op.
void ano_mem_pool_free(ano_mem_pool *p, void *block);

// Return every chunk and the control struct to the parent. p is dead afterwards.
// NULL is a no-op.
void ano_mem_pool_destroy(ano_mem_pool *p);

ano_mem_stats ano_mem_pool_stats(const ano_mem_pool *p);

#endif // ANOPTICENGINE_ANOPTIC_MEMORY_POOLS_H