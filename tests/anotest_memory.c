/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_memory.h and the mimalloc allocator wiring. These are the
 * allocator smoke checks that used to live untestably inside main()'s DEBUG_BUILD
 * block and the old strings-module autoStringTest():
 *   - ano_salloc (stack alloca) and the __cleanup__ attribute behind LOCALHEAPATTR;
 *   - scope-bound heaps (LOCALHEAPATTR -> ano_heap_release) with aligned, zeroed
 *     allocation and a 128-bit union member (no field tearing);
 *   - plain mi_malloc round-trips;
 *   - a best-effort huge-page reservation probe.
 * Exit 0 == pass.
 *
 * The original, untouched experiment these checks were distilled from is kept as
 * an easter egg in anotest_chariots.c (built, but DISABLED in ctest). */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <mimalloc.h>

#include "anoptic_memory.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// __cleanup__ fires its destructor at end of scope; we can only observe it via a
// side effect, so flip a flag. This is the mechanism LOCALHEAPATTR is built on.
static int g_cleanup_fired = 0;
static void mark_cleanup(const int *in) { (void)in; g_cleanup_fired = 1; }

// 16-byte-aligned POD with a 128-bit member: exercises mi_*_aligned and the union
// aliasing the old memory test relied on (lo/hi vs the full u128).
typedef _BitInt(128) u128;
typedef struct {
    union {
        struct { uint64_t lo; uint64_t hi; };
        u128 wide;
    };
    uint64_t uuid;
    uint32_t tag;
    uint8_t  wheels;
} mem_chariot_t;

static void test_salloc_and_scope_cleanup(void)
{
    // ano_salloc == alloca: a non-NULL, writable block in this frame.
    uint8_t *stackBytes = ano_salloc(42);
    CHECK(stackBytes != NULL, "ano_salloc returned non-NULL");
    for (int i = 0; i < 42; i++) stackBytes[i] = (uint8_t)i; // must not fault
    CHECK(stackBytes[41] == 41, "ano_salloc block is writable");

    g_cleanup_fired = 0;
    { int scoped __attribute__((__cleanup__(mark_cleanup))) = 64; (void)scoped; }
    CHECK(g_cleanup_fired == 1, "__cleanup__ destructor fired at scope exit");
}

static void test_scoped_heap_aligned(void)
{
    // LOCALHEAPATTR ties the heap to this scope: ano_heap_release runs on exit and
    // frees every allocation below in one shot (see the closing brace).
    mi_heap_t *memHeap LOCALHEAPATTR = mi_heap_new();
    CHECK(memHeap != NULL, "mi_heap_new returned a heap");
    if (!memHeap) return;

    const uint32_t count = 4096; // the working set the old test actually touched
    mem_chariot_t *chariots = mi_heap_zalloc_aligned(memHeap,
                                                     (size_t)count * sizeof(mem_chariot_t),
                                                     _Alignof(mem_chariot_t));
    CHECK(chariots != NULL, "aligned zalloc returned non-NULL");
    if (!chariots) return;

    CHECK(((uintptr_t)chariots % _Alignof(mem_chariot_t)) == 0, "allocation is aligned");
    CHECK(chariots[0].lo == 0 && chariots[0].hi == 0 && chariots[0].uuid == 0,
          "zalloc zero-initialized the block");

    // Write through one union view, read back through the other (little-endian: the
    // low 64 bits of `wide` alias `lo`).
    chariots[0].lo = 0x1776177623237123ull;
    chariots[0].hi = 0x1231776223444777ull;
    CHECK((uint64_t)(chariots[0].wide & 0xFFFFFFFFFFFFFFFFull) == 0x1776177623237123ull,
          "u128 union low half aliases lo");

    chariots[1].wide = (u128)0x1776177613374444ull;
    CHECK(chariots[1].lo == 0x1776177613374444ull, "u128 union write reads back via lo");

    // Fill the remainder deterministically; verify the tail survives (no overlap).
    srand(0x1776);
    for (uint32_t i = 2; i < count; i++) {
        chariots[i].lo     = (uint64_t)rand();
        chariots[i].tag    = (uint32_t)(rand() % 40000);
        chariots[i].wheels = (uint8_t)(rand() % 16);
    }
    CHECK(chariots[count - 1].wheels < 16, "tail element intact after fill");
} // ano_heap_release(&memHeap) runs here

static void test_basic_malloc(void)
{
    const int n = 128;
    int *nums = mi_malloc((size_t)n * sizeof(int));
    CHECK(nums != NULL, "mi_malloc returned non-NULL");
    if (!nums) return;

    for (int i = 0; i < n; i++) nums[i] = i + 1;
    bool intact = true;
    for (int i = 0; i < n; i++) if (nums[i] != i + 1) intact = false;
    CHECK(intact, "mi_malloc block holds written contents");

    mi_free(nums);
}

static void test_huge_pages_probe(void)
{
    // Best effort: huge/large OS pages are environment-gated (commonly unavailable
    // on CI and on macOS), so a non-zero status is NOT a failure -- we only prove
    // the call is wired and does not crash. Reduced from the original 4-page / 10s
    // experiment so it cannot stall or commit gigabytes in the suite.
    mi_option_set(mi_option_reserve_huge_os_pages, 1);
    int status = mi_reserve_huge_os_pages_at(1, 0, 500);
    printf("huge-page reservation probe: status=%d (non-zero is acceptable)\n", status);
}

int main(void)
{
    test_salloc_and_scope_cleanup();
    test_scoped_heap_aligned();
    test_basic_malloc();
    test_huge_pages_probe();

    if (failures == 0) { printf("anotest_memory: all checks passed\n"); return 0; }
    printf("anotest_memory: %d check(s) failed\n", failures);
    return 1;
}
