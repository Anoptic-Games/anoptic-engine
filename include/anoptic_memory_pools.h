/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Memory Pools
// Specialty local allocators over mimalloc (superset of anoptic_memory.h).
//   ano_mem_monotonic  bump over growing slabs; no per-object free; reset or destroy.
//   ano_mem_multipool  power-of-two size classes; O(1) free lists carved from parent chunks.
//   ano_mem_pool       fixed block size; O(1) free list; optional hard cap (false-on-empty).
// Parent: every allocator takes a parent (mi_heap_t or another pools allocator). Hot path never hits the parent.
// Wink-out: mi_heap_destroy on the root reclaims the chain; allocator pointers die with it. destroy() is retail teardown while the parent lives.
// Threading: single-owner. Totality: hostile-but-typed input -> NULL / no-op, never UB. Wrong free or double-free is UB.

#ifndef ANOPTICENGINE_ANOPTIC_MEMORY_POOLS_H
#define ANOPTICENGINE_ANOPTIC_MEMORY_POOLS_H

#include <stddef.h>
#include <stdint.h>

#include "anoptic_memory.h"

typedef struct ano_mem_monotonic ano_mem_monotonic;
typedef struct ano_mem_multipool ano_mem_multipool;
typedef struct ano_mem_pool      ano_mem_pool;
typedef struct ano_mem_stripe    ano_mem_stripe;


/* Parents */

// Where an allocator gets its chunks. 3-word value, passed by copy.
// release == NULL: no retail return (monotonic parent); destroy() is bookkeeping only.
typedef struct ano_mem_parent {
    void *ctx;
    void *(*acquire)(void *ctx, size_t size, size_t align);  // NULL on exhaustion, never UB
    void  (*release)(void *ctx, void *block);                // NULL: no retail return path
} ano_mem_parent;

// Parent over a mimalloc heap.
ano_mem_parent ano_mem_parent_heap(mi_heap_t *heap);

// Parent over the thread-default heap. Mutex-shareable; no wink-out (destroy only).
ano_mem_parent ano_mem_parent_default(void);

// Parent over a monotonic arena: bump-alloc, no retail return.
ano_mem_parent ano_mem_parent_monotonic(ano_mem_monotonic *mono);

<<<<<<< HEAD
// Counting interposer at the parent seam. Ledger must outlive every allocator over the returned parent.
=======
// The parent LEDGER: an interposer that counts bytes crossing the parent seam. This is the
// only way a per-domain heap footprint becomes MEASURABLE -- it captures every chunk any
// arena takes from mimalloc, which a per-allocator chunk_bytes sum cannot (D19). The ledger
// must outlive every allocator built over the returned parent.
>>>>>>> block-b1-base
typedef struct ano_mem_parent_ledger {
    size_t acquires, releases, bytes_out, bytes_back, live_bytes, peak_bytes;
} ano_mem_parent_ledger;
ano_mem_parent ano_mem_parent_counting(ano_mem_parent inner, ano_mem_parent_ledger *ledger);
<<<<<<< HEAD
=======

// ---------------------------------------------------------------------------------------------
// Stats. Queryable per allocator, updated with plain stores (single-owner, no atomics).
// live/peak count what the allocator has handed out, measured in SERVING size (the block's
// class size, not the request); chunk_* counts what it holds from its parent, control
// struct and chunk headers included. peak_* are high-water marks since make().
//
// requested_bytes is LIVE, not cumulative, so live internal fragmentation is exactly
// live_bytes - requested_bytes. parent_* count the chunk-refill seam; oversize_hits counts
// requests past max_block that passed through to the parent individually.
>>>>>>> block-b1-base


/* Stats */

// Plain stores (single-owner). live/peak = serving size handed out; chunk_* = held from parent.
// requested_bytes is LIVE: internal fragmentation = live_bytes - requested_bytes.
typedef struct ano_mem_stats {
    size_t live_bytes;
    size_t live_blocks;
    size_t peak_bytes;
    size_t peak_blocks;
    size_t chunk_bytes;
    size_t chunk_count;
    size_t requested_bytes;
    size_t parent_acquires;
    size_t parent_releases;
    size_t parent_bytes;
    size_t oversize_hits;
} ano_mem_stats;


/* Monotonic */

// Bump over parent slabs. Slabs double from first_slab (0 = 64 KiB; clamped to [4 KiB, 8 MiB]) up to 8 MiB.
// Request that will not fit the next growth slab gets a dedicated side-listed slab. No per-object free.

// first_slab hint in bytes; 0 = default. NULL if parent cannot serve.
ano_mem_monotonic *ano_mem_monotonic_make(ano_mem_parent parent, size_t first_slab);

// size at align (pow2, <= 4096; 0 = alignof(max_align_t)). NULL if size 0, bad align, or exhausted.
void *ano_mem_monotonic_alloc(ano_mem_monotonic *m, size_t size, size_t align);

// Rewind empty, keep slabs. live stats rezero; chunk stats persist. Prior pointers die.
void ano_mem_monotonic_reset(ano_mem_monotonic *m);

// Return slabs + control to parent. m dies. NULL is a no-op.
void ano_mem_monotonic_destroy(ano_mem_monotonic *m);

ano_mem_stats ano_mem_monotonic_stats(const ano_mem_monotonic *m);


<<<<<<< HEAD
/* Multipool */

// Classes min_block .. max_block (geometric via clz), or cfg.classes (smallest fitting stride).
// Frees push the class free list; class chunks return only at destroy/wink-out.
// Past max_block -> parent-backed oversize (4096-byte header + payload); free returns to parent.
// Alignment: largest pow2 dividing class stride, capped at 4096 (positional).

// classes/class_count optionally replace the geometric ladder (ascending, multiples of 16; last = max_block).
// NULL/0 keeps geometric.
=======
// classes/class_count OPTIONALLY replace the geometric ladder with an explicit, ascending,
// histogram-tuned class list (each a multiple of 16; the last is max_block). A kind whose
// size distribution is known -- audio PCM, shader blobs -- fights on its own ground with it.
// NULL/0 keeps the geometric classes.
>>>>>>> block-b1-base
typedef struct ano_mem_multipool_cfg {
    size_t min_block;   // power of 2 >= 16;        0 = 16
    size_t max_block;   // power of 2 >= min_block; 0 = 1 MiB. Beyond it: parent passthrough.
    const size_t *classes;
    size_t class_count;
} ano_mem_multipool_cfg;

// cfg == NULL takes defaults; invalid cfg -> NULL.
ano_mem_multipool *ano_mem_multipool_make(ano_mem_parent parent,
                                          const ano_mem_multipool_cfg *cfg);

// Smallest fitting class, or parent past max_block. NULL if size 0 or exhausted.
void *ano_mem_multipool_alloc(ano_mem_multipool *mp, size_t size);

// size must match alloc's class (wrong class = UB). p == NULL is a no-op. O(1) sized free: no per-block metadata.
void ano_mem_multipool_free(ano_mem_multipool *mp, void *p, size_t size);

// Return chunks, passthroughs, control to parent. mp dies. NULL is a no-op.
void ano_mem_multipool_destroy(ano_mem_multipool *mp);

ano_mem_stats ano_mem_multipool_stats(const ano_mem_multipool *mp);

<<<<<<< HEAD
// Per-class occupancy. Returns class count; fills out[0..min(count,cap)).
=======
// Per-class occupancy: what a size histogram actually hit. Output: classes reported (the
// pool's class count, even when cap is smaller); fills out[0..min(count,cap)).
>>>>>>> block-b1-base
typedef struct ano_mem_class_stats {
    size_t stride, hits, live_blocks, free_blocks;
} ano_mem_class_stats;
size_t ano_mem_multipool_class_stats(const ano_mem_multipool *mp, ano_mem_class_stats *out,
                                     size_t cap);
<<<<<<< HEAD
=======

// ---------------------------------------------------------------------------------------------
// Stripe: the fourth allocator, and the one an SoA engine cannot do without. It hands out
// LANE-ISOLATED memory -- two different lanes NEVER share a grain-sized region -- so a table
// of per-thread or per-lane rows stops false-sharing by construction rather than by a
// hand-placed alignas that the next field addition silently breaks.
//
// ano_mem_stripe_planes is the SoA primitive: N parallel arrays carved from ONE parent
// acquisition, every base on the grain. That is exactly what a plane-set block wants.

typedef struct ano_mem_stripe_cfg {
    size_t lanes;       // independently-written lanes; 0 = 1
    size_t grain;       // isolation distance; 0 = ANO_THREAD_LINE
    size_t chunk_hint;  // parent acquisition granularity; 0 = 64 KiB
} ano_mem_stripe_cfg;

ano_mem_stripe *ano_mem_stripe_make(ano_mem_parent parent, const ano_mem_stripe_cfg *cfg);

// size bytes for lane `lane`, at `align` (0 = the grain). Two different lanes never share a
// grain-sized region. NULL on a bad lane, size 0, or parent exhaustion. Never UB.
void *ano_mem_stripe_alloc(ano_mem_stripe *s, size_t lane, size_t size, size_t align);

// N parallel SoA arrays from ONE parent acquisition, every base on the grain.
// 0 on success (out_planes[0..n_planes) filled), -1 on overflow or exhaustion.
int   ano_mem_stripe_planes(ano_mem_stripe *s, const size_t *count, const size_t *elem_size,
                            size_t n_planes, void **out_planes);

// Rewind to empty, KEEPING every chunk for reuse. Every pointer ever returned is dead.
void  ano_mem_stripe_reset(ano_mem_stripe *s);

// Return every chunk and the control struct to the parent. s is dead afterwards.
void  ano_mem_stripe_destroy(ano_mem_stripe *s);

ano_mem_stats ano_mem_stripe_stats(const ano_mem_stripe *s);

// ---------------------------------------------------------------------------------------------
// Pool: one fixed block size. max_blocks > 0 caps the population: alloc returns NULL when
// the cap is reached and the free list is empty -- false-on-empty, it never grows past the
// cap and never blocks. max_blocks == 0 grows on demand.
>>>>>>> block-b1-base


/* Stripe */

// Lane-isolated memory. Different lanes never share a grain-sized region.
// ano_mem_stripe_planes: N parallel SoA arrays from one parent acquisition, every base on the grain.
typedef struct ano_mem_stripe_cfg {
    size_t lanes;       // independently-written lanes; 0 = 1
    size_t grain;       // isolation distance; 0 = ANO_THREAD_LINE
    size_t chunk_hint;  // parent acquisition granularity; 0 = 64 KiB
} ano_mem_stripe_cfg;

ano_mem_stripe *ano_mem_stripe_make(ano_mem_parent parent, const ano_mem_stripe_cfg *cfg);

// size for lane, at align (0 = grain). NULL on bad lane, size 0, or exhaustion.
void *ano_mem_stripe_alloc(ano_mem_stripe *s, size_t lane, size_t size, size_t align);

// N SoA planes from one parent acquisition. 0 ok (fills out_planes), -1 on overflow/exhaustion.
int   ano_mem_stripe_planes(ano_mem_stripe *s, const size_t *count, const size_t *elem_size,
                            size_t n_planes, void **out_planes);

// Rewind empty, keep chunks. Prior pointers die.
void  ano_mem_stripe_reset(ano_mem_stripe *s);

// Return chunks + control to parent. s dies.
void  ano_mem_stripe_destroy(ano_mem_stripe *s);

ano_mem_stats ano_mem_stripe_stats(const ano_mem_stripe *s);


/* Pool */

// Fixed block size. max_blocks > 0: NULL when capped and empty. max_blocks == 0: grow on demand.

// block_size >= 1 (stride rounded up for free-list link + align); block_align pow2 <= 4096,
// 0 = min(pow2ceil(block_size), 4096), then floored to alignof(void*).
ano_mem_pool *ano_mem_pool_make(ano_mem_parent parent, size_t block_size,
                                size_t block_align, size_t max_blocks);

// Grow capacity to at least `blocks` (clamped to max_blocks). 0 ok, -1 if parent cannot; partial kept.
int ano_mem_pool_reserve(ano_mem_pool *p, size_t blocks);

// One block of block_size. NULL when capped-and-empty or exhausted. Never blocking.
void *ano_mem_pool_alloc(ano_mem_pool *p);

// Return block to free list. NULL is a no-op.
void ano_mem_pool_free(ano_mem_pool *p, void *block);

// Return chunks + control to parent. p dies. NULL is a no-op.
void ano_mem_pool_destroy(ano_mem_pool *p);

ano_mem_stats ano_mem_pool_stats(const ano_mem_pool *p);

#endif // ANOPTICENGINE_ANOPTIC_MEMORY_POOLS_H
