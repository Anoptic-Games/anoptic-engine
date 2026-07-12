/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_memory_pools.h: monotonic, multipool, fixed pool, parent
 * composition, stats, wink-out.
 *   - hostile-but-typed input: every call is total, failure is NULL/no-op, never UB;
 *   - correctness fuzz: random alloc/free churn with a shadow oracle -- every live block
 *     holds its fill pattern until the moment it is freed (no overlap, no corruption);
 *   - alignment: blocks land on min(class/block size, 4096) boundaries as documented;
 *   - monotonic reset reuses the same slab bytes; multipool free lists recycle LIFO;
 *   - pool false-on-empty at the max_blocks cap, recovery after a free;
 *   - composition: multipool over monotonic (release-less parent) churns correctly and
 *     tears down as a no-op;
 *   - wink-out: allocators + payloads die with their backing heap, nothing to free.
 * Exit 0 == pass. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "anoptic_memory_pools.h"
#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// ---------------------------------------------------------------------------------------------

static void test_hostile_inputs(void)
{
    ano_mem_parent dead = ano_mem_parent_heap(NULL);
    CHECK(dead.acquire == NULL, "NULL heap yields a dead parent");
    CHECK(ano_mem_monotonic_make(dead, 0) == NULL, "monotonic over dead parent refuses");
    CHECK(ano_mem_multipool_make(dead, NULL) == NULL, "multipool over dead parent refuses");
    CHECK(ano_mem_pool_make(dead, 64, 0, 0) == NULL, "pool over dead parent refuses");

    ano_mem_parent deadMono = ano_mem_parent_monotonic(NULL);
    CHECK(deadMono.acquire == NULL, "NULL monotonic yields a dead parent");

    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    CHECK(heap != NULL, "mi_heap_new");
    if (!heap) return;
    ano_mem_parent par = ano_mem_parent_heap(heap);

    // Invalid multipool configs refuse.
    ano_mem_multipool_cfg bad1 = { .min_block = 24, .max_block = 0 };          // non-pow2
    ano_mem_multipool_cfg bad2 = { .min_block = 8,  .max_block = 0 };          // < 16
    ano_mem_multipool_cfg bad3 = { .min_block = 64, .max_block = 32 };         // max < min
    CHECK(ano_mem_multipool_make(par, &bad1) == NULL, "non-pow2 min_block refuses");
    CHECK(ano_mem_multipool_make(par, &bad2) == NULL, "min_block < 16 refuses");
    CHECK(ano_mem_multipool_make(par, &bad3) == NULL, "max_block < min_block refuses");

    // Invalid pool shapes refuse.
    CHECK(ano_mem_pool_make(par, 0, 0, 0) == NULL, "block_size 0 refuses");
    CHECK(ano_mem_pool_make(par, 64, 24, 0) == NULL, "non-pow2 align refuses");
    CHECK(ano_mem_pool_make(par, 64, 8192, 0) == NULL, "align > 4096 refuses");

    // Zero-size and bad-align allocs are NULL, never UB.
    ano_mem_monotonic *m = ano_mem_monotonic_make(par, 0);
    ano_mem_multipool *mp = ano_mem_multipool_make(par, NULL);
    CHECK(m != NULL && mp != NULL, "valid make succeeds");
    if (m) {
        CHECK(ano_mem_monotonic_alloc(m, 0, 0) == NULL, "monotonic size 0 is NULL");
        CHECK(ano_mem_monotonic_alloc(m, 64, 3) == NULL, "monotonic align 3 is NULL");
        CHECK(ano_mem_monotonic_alloc(m, 64, 8192) == NULL, "monotonic align 8192 is NULL");
        CHECK(ano_mem_monotonic_alloc(m, SIZE_MAX - 8, 0) == NULL, "monotonic huge size is NULL");
    }
    if (mp) {
        CHECK(ano_mem_multipool_alloc(mp, 0) == NULL, "multipool size 0 is NULL");
        ano_mem_multipool_free(mp, NULL, 64);   // no-op
    }

    // NULL-allocator entry points are no-ops / zeroes.
    CHECK(ano_mem_monotonic_alloc(NULL, 64, 0) == NULL, "NULL monotonic alloc is NULL");
    CHECK(ano_mem_multipool_alloc(NULL, 64) == NULL, "NULL multipool alloc is NULL");
    CHECK(ano_mem_pool_alloc(NULL) == NULL, "NULL pool alloc is NULL");
    CHECK(ano_mem_pool_reserve(NULL, 4) == -1, "NULL pool reserve is -1");
    ano_mem_monotonic_reset(NULL);
    ano_mem_monotonic_destroy(NULL);
    ano_mem_multipool_destroy(NULL);
    ano_mem_pool_destroy(NULL);
    ano_mem_stats z = ano_mem_monotonic_stats(NULL);
    CHECK(z.live_bytes == 0 && z.chunk_count == 0, "NULL stats are zero");

    if (m)  ano_mem_monotonic_destroy(m);
    if (mp) ano_mem_multipool_destroy(mp);
}

// ---------------------------------------------------------------------------------------------

static void test_monotonic(void)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (!heap) { CHECK(false, "mi_heap_new"); return; }
    ano_mem_monotonic *m = ano_mem_monotonic_make(ano_mem_parent_heap(heap), 0);
    CHECK(m != NULL, "monotonic make");
    if (!m) return;

    // Alignment sweep: every pow2 up to 4096, bytes writable, no overlap between
    // consecutive allocations (write-then-verify at the end).
    enum { N = 13 };
    uint8_t *ptr[N];
    size_t   len[N];
    for (int i = 0; i < N; i++) {
        size_t align = (size_t)1 << i;                    // 1 .. 4096
        len[i] = 17 + (size_t)i * 13;
        ptr[i] = ano_mem_monotonic_alloc(m, len[i], align);
        CHECK(ptr[i] != NULL, "monotonic alloc non-NULL");
        if (!ptr[i]) return;
        CHECK(((uintptr_t)ptr[i] & (align - 1)) == 0, "monotonic alignment honored");
        memset(ptr[i], 0x40 + i, len[i]);
    }
    for (int i = 0; i < N; i++)
        for (size_t j = 0; j < len[i]; j++)
            if (ptr[i][j] != 0x40 + i) { CHECK(false, "monotonic blocks overlap"); return; }

    ano_mem_stats st = ano_mem_monotonic_stats(m);
    CHECK(st.live_blocks == N, "monotonic live_blocks counts allocs");
    CHECK(st.chunk_count >= 2, "monotonic acquired at least control + one slab");

    // Dedicated slab: far beyond the 8 MiB growth cap.
    uint8_t *big = ano_mem_monotonic_alloc(m, 9u << 20, 64);
    CHECK(big != NULL, "monotonic dedicated slab serves");
    if (big) { big[0] = 1; big[(9u << 20) - 1] = 2; }

    // Reset rewinds into the SAME slabs: the first post-reset alloc must land on the
    // first pre-reset address (same size, same align, same walk).
    void *first = ano_mem_monotonic_alloc(m, 64, 64);
    ano_mem_monotonic_reset(m);
    st = ano_mem_monotonic_stats(m);
    CHECK(st.live_bytes == 0 && st.live_blocks == 0, "reset rezeroes live stats");
    CHECK(st.chunk_count >= 2, "reset keeps the slabs");
    void *again = ano_mem_monotonic_alloc(m, 17, 1);
    CHECK(again == ptr[0], "reset reuses slab bytes from the start");
    (void)first;

    ano_mem_monotonic_destroy(m);
}

// ---------------------------------------------------------------------------------------------

// Shadow-oracle churn: every live block is filled with a pattern derived from its slot;
// verified byte-for-byte at free and at the end. Catches overlap, class confusion, and
// free-list corruption.
typedef struct { void *p; size_t size; uint8_t tag; } shadow_t;

static bool shadow_ok(const shadow_t *s)
{
    const uint8_t *b = s->p;
    for (size_t i = 0; i < s->size; i++)
        if (b[i] != (uint8_t)(s->tag + (i & 0x3F)))
            return false;
    return true;
}

static void shadow_fill(shadow_t *s)
{
    uint8_t *b = s->p;
    for (size_t i = 0; i < s->size; i++)
        b[i] = (uint8_t)(s->tag + (i & 0x3F));
}

static void churn_multipool(ano_mem_multipool *mp, uint32_t iters, uint32_t maxSize,
                            const char *label)
{
    enum { SLOTS = 512 };
    static shadow_t live[SLOTS];
    memset(live, 0, sizeof live);
    test_rng rng = rng_make(0x9E3779B9u);
    size_t liveCount = 0;

    for (uint32_t it = 0; it < iters; it++) {
        uint32_t slot = rng_below(&rng, SLOTS);
        if (live[slot].p != NULL) {
            if (!shadow_ok(&live[slot])) {
                CHECK(false, label);
                printf("  (pattern corrupt before free, slot %u it %u)\n", slot, it);
                return;
            }
            ano_mem_multipool_free(mp, live[slot].p, live[slot].size);
            live[slot].p = NULL;
            liveCount--;
        } else {
            size_t size = 1 + rng_below(&rng, maxSize);
            void *p = ano_mem_multipool_alloc(mp, size);
            CHECK(p != NULL, "multipool churn alloc serves");
            if (!p) return;
            // Documented alignment: min(serving class size, 4096).
            size_t cls = 16;
            while (cls < size) cls <<= 1;
            size_t align = cls < 4096 ? cls : 4096;
            if (((uintptr_t)p & (align - 1)) != 0) {
                CHECK(false, "multipool alignment honored");
                return;
            }
            live[slot] = (shadow_t){ p, size, (uint8_t)rng_next(&rng) };
            shadow_fill(&live[slot]);
            liveCount++;
        }
        if ((it & 0x3FF) == 0) {
            ano_mem_stats st = ano_mem_multipool_stats(mp);
            if (st.live_blocks != liveCount) {
                CHECK(false, "multipool live_blocks tracks the oracle");
                return;
            }
        }
    }
    for (uint32_t slot = 0; slot < SLOTS; slot++)
        if (live[slot].p != NULL) {
            if (!shadow_ok(&live[slot])) { CHECK(false, label); return; }
            ano_mem_multipool_free(mp, live[slot].p, live[slot].size);
        }
    ano_mem_stats st = ano_mem_multipool_stats(mp);
    CHECK(st.live_blocks == 0 && st.live_bytes == 0, "multipool drains to zero live");
    CHECK(st.peak_blocks > 0 && st.peak_bytes >= st.peak_blocks * 16, "multipool peaks recorded");
}

static void test_multipool(void)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (!heap) { CHECK(false, "mi_heap_new"); return; }
    ano_mem_multipool *mp = ano_mem_multipool_make(ano_mem_parent_heap(heap), NULL);
    CHECK(mp != NULL, "multipool make (defaults)");
    if (!mp) return;

    // LIFO recycling: free then alloc of the same class returns the same block.
    void *a = ano_mem_multipool_alloc(mp, 100);           // class 128
    CHECK(a != NULL, "multipool alloc");
    ano_mem_multipool_free(mp, a, 100);
    void *b = ano_mem_multipool_alloc(mp, 128);           // same class, different size
    CHECK(b == a, "free list recycles LIFO within a class");
    ano_mem_multipool_free(mp, b, 128);

    // Boundary sizes map sanely: 16 -> class 16; 17 -> class 32 (stride accounting).
    ano_mem_stats before = ano_mem_multipool_stats(mp);
    void *c16 = ano_mem_multipool_alloc(mp, 16);
    void *c17 = ano_mem_multipool_alloc(mp, 17);
    ano_mem_stats after = ano_mem_multipool_stats(mp);
    CHECK(after.live_bytes - before.live_bytes == 16 + 32, "stride accounting 16/17 -> 16+32");
    ano_mem_multipool_free(mp, c16, 16);
    ano_mem_multipool_free(mp, c17, 17);

    // Oversize passthrough: beyond max_block (1 MiB default), 4096-aligned, tracked.
    before = ano_mem_multipool_stats(mp);
    size_t ovsz = (1u << 20) + 12345;
    uint8_t *ov = ano_mem_multipool_alloc(mp, ovsz);
    CHECK(ov != NULL, "oversize serves");
    if (ov) {
        CHECK(((uintptr_t)ov & 4095) == 0, "oversize is 4096-aligned");
        ov[0] = 1; ov[ovsz - 1] = 2;
        after = ano_mem_multipool_stats(mp);
        CHECK(after.chunk_count == before.chunk_count + 1, "oversize tracked as a chunk");
        ano_mem_multipool_free(mp, ov, ovsz);
        after = ano_mem_multipool_stats(mp);
        CHECK(after.chunk_count == before.chunk_count, "oversize returned to parent");
    }

    churn_multipool(mp, 20000, 4096, "multipool churn oracle (small)");
    churn_multipool(mp, 4000, 128 * 1024, "multipool churn oracle (large classes)");

    ano_mem_multipool_destroy(mp);

    // Custom config: tight class range, oversize kicks in just past max_block.
    ano_mem_multipool_cfg cfg = { .min_block = 32, .max_block = 256 };
    ano_mem_multipool *tight = ano_mem_multipool_make(ano_mem_parent_heap(heap), &cfg);
    CHECK(tight != NULL, "multipool make (tight cfg)");
    if (tight) {
        void *in  = ano_mem_multipool_alloc(tight, 256);
        void *out = ano_mem_multipool_alloc(tight, 257);
        CHECK(in != NULL && out != NULL, "tight cfg serves both sides of max_block");
        CHECK(((uintptr_t)out & 4095) == 0, "past max_block is oversize-aligned");
        ano_mem_multipool_free(tight, in, 256);
        ano_mem_multipool_free(tight, out, 257);
        ano_mem_multipool_destroy(tight);
    }
}

// ---------------------------------------------------------------------------------------------

static void test_pool(void)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (!heap) { CHECK(false, "mi_heap_new"); return; }

    // The streaming shape: 512 KiB blocks, hard cap 4, false-on-empty.
    ano_mem_pool *p = ano_mem_pool_make(ano_mem_parent_heap(heap), 512u * 1024u, 0, 4);
    CHECK(p != NULL, "pool make (512 KiB x 4)");
    if (!p) return;
    CHECK(ano_mem_pool_reserve(p, 4) == 0, "reserve prewarms to cap");
    ano_mem_stats st = ano_mem_pool_stats(p);
    CHECK(st.chunk_bytes >= 4u * 512u * 1024u, "reserve acquired the capacity");

    void *blk[4];
    for (int i = 0; i < 4; i++) {
        blk[i] = ano_mem_pool_alloc(p);
        CHECK(blk[i] != NULL, "pool serves up to cap");
        if (blk[i]) {
            CHECK(((uintptr_t)blk[i] & 4095) == 0, "pool block 4096-aligned (natural)");
            memset(blk[i], i + 1, 512u * 1024u);
        }
    }
    CHECK(ano_mem_pool_alloc(p) == NULL, "pool at cap is false-on-empty");
    ano_mem_pool_free(p, blk[2]);
    void *re = ano_mem_pool_alloc(p);
    CHECK(re == blk[2], "freed block serves again");
    // Contents of the other blocks survived the churn.
    CHECK(((uint8_t *)blk[0])[123] == 1 && ((uint8_t *)blk[3])[456789] == 4,
          "pool blocks do not overlap");
    ano_mem_pool_destroy(p);

    // Tiny blocks: stride rounds up to hold the free-list link, alignment honored.
    ano_mem_pool *tiny = ano_mem_pool_make(ano_mem_parent_heap(heap), 4, 64, 0);
    CHECK(tiny != NULL, "pool make (tiny blocks)");
    if (tiny) {
        void *t1 = ano_mem_pool_alloc(tiny);
        void *t2 = ano_mem_pool_alloc(tiny);
        CHECK(t1 && t2 && t1 != t2, "tiny pool serves distinct blocks");
        CHECK(((uintptr_t)t1 & 63) == 0 && ((uintptr_t)t2 & 63) == 0, "tiny pool aligns to 64");
        ano_mem_pool_free(tiny, t1);
        ano_mem_pool_free(tiny, t2);
        ano_mem_pool_destroy(tiny);
    }
}

// ---------------------------------------------------------------------------------------------

static void test_composition(void)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (!heap) { CHECK(false, "mi_heap_new"); return; }

    // Multipool over monotonic: Lakos's AS11-14 shape. Chunks bump out of the arena,
    // frees recycle inside the multipool, nothing returns until the arena dies.
    ano_mem_monotonic *arena = ano_mem_monotonic_make(ano_mem_parent_heap(heap), 256 * 1024);
    CHECK(arena != NULL, "arena for composition");
    if (!arena) return;
    ano_mem_multipool *mp = ano_mem_multipool_make(ano_mem_parent_monotonic(arena), NULL);
    CHECK(mp != NULL, "multipool over monotonic");
    if (!mp) { ano_mem_monotonic_destroy(arena); return; }

    churn_multipool(mp, 10000, 2048, "composed churn oracle");

    // Oversize through a release-less parent: alloc + free must not blow up; the bytes
    // stay in the arena (chunk stats drop, arena live stays).
    size_t ovsz = (1u << 20) + 7;
    void *ov = ano_mem_multipool_alloc(mp, ovsz);
    CHECK(ov != NULL, "oversize through monotonic parent");
    ano_mem_multipool_free(mp, ov, ovsz);

    ano_mem_multipool_destroy(mp);     // bookkeeping no-op: parent has no release
    ano_mem_stats ast = ano_mem_monotonic_stats(arena);
    CHECK(ast.live_blocks > 0, "arena still holds the multipool's chunks");
    ano_mem_monotonic_destroy(arena);

    // Pool over monotonic, for completeness.
    ano_mem_monotonic *arena2 = ano_mem_monotonic_make(ano_mem_parent_heap(heap), 0);
    if (arena2) {
        ano_mem_pool *p = ano_mem_pool_make(ano_mem_parent_monotonic(arena2), 4096, 0, 8);
        CHECK(p != NULL, "pool over monotonic");
        if (p) {
            void *b1 = ano_mem_pool_alloc(p);
            void *b2 = ano_mem_pool_alloc(p);
            CHECK(b1 && b2 && b1 != b2, "composed pool serves");
            ano_mem_pool_free(p, b1);
            CHECK(ano_mem_pool_alloc(p) == b1, "composed pool recycles");
            ano_mem_pool_destroy(p);
        }
        ano_mem_monotonic_destroy(arena2);
    }
}

// ---------------------------------------------------------------------------------------------

static void test_winkout(void)
{
    // Build a whole allocator family over one heap, load it up, then let the heap die
    // with everything still "live". Nothing here may be touched afterwards, and the
    // sanitizer build (preset 6) must see no leak and no use-after-free.
    mi_heap_t *heap = mi_heap_new();
    CHECK(heap != NULL, "wink-out heap");
    if (!heap) return;

    ano_mem_monotonic *staging = ano_mem_monotonic_make(ano_mem_parent_heap(heap), 0);
    ano_mem_multipool *pools   = ano_mem_multipool_make(ano_mem_parent_heap(heap), NULL);
    ano_mem_pool      *chunks  = ano_mem_pool_make(ano_mem_parent_heap(heap), 64 * 1024, 0, 0);
    CHECK(staging && pools && chunks, "wink-out family built");
    if (staging && pools && chunks) {
        for (int i = 0; i < 512; i++) {
            void *s = ano_mem_monotonic_alloc(staging, 100 + (size_t)i * 7, 0);
            void *m = ano_mem_multipool_alloc(pools, 1 + (size_t)(i * 37) % 5000);
            void *c = ano_mem_pool_alloc(chunks);
            CHECK(s && m && c, "wink-out family serves");
            if (!(s && m && c)) break;
            memset(s, 1, 100 + (size_t)i * 7);
            memset(c, 3, 64 * 1024);
        }
    }
    mi_heap_destroy(heap);      // the wink-out: no destroys, no frees, no leaks
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    test_hostile_inputs();
    test_monotonic();
    test_multipool();
    test_pool();
    test_composition();
    test_winkout();

    if (failures == 0) { printf("anotest_mempools: all checks passed\n"); return 0; }
    printf("anotest_mempools: %d check(s) failed\n", failures);
    return 1;
}
