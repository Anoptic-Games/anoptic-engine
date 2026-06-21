/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* ===========================================================================
 *  EASTER EGG -- the original allocator experiment, preserved VERBATIM.
 *
 *  This is matei3d's first mimalloc + huge-pages probe from the strings module,
 *  back when CMake tests weren't wired up ("Currently more of a memory tesst").
 *  The struct-union-struct chariot (u128 aliasing lo/hi) and the __cleanup__
 *  attribute probe live here exactly as first written, silly comments and all.
 *
 *  It IS load-bearing trivia: the __cleanup__ destructors really do fire on
 *  their own (intCleanup prints at the inner-scope exit; LOCALHEAPATTR's
 *  ano_heap_release frees the heap at function exit). The maintained, asserted
 *  version of all this is tests/anotest_memory.c -- THIS file is the museum copy.
 *
 *  Optional: built so it can't rot, but DISABLED in ctest. Run it by hand:
 *      ./anotest_chariots
 *  The code is verbatim from here down to the harness near the foot of the file;
 *  a FINDINGS block (added later) follows main().
 * =========================================================================== */

#include "anoptic_strings.h"

#include <stdio.h>
#include <mimalloc.h>
#include <stdlib.h>
#include "anoptic_memory.h"

// Anoptic Strings Implementation

void intCleanup(const int *in) {

    printf("Cleanup function received number of value of: %d\n", *in);
}

/*
void heapCleanup(mi_heap_t **in) {
    mi_heap_destroy(*in);
}
*/

typedef void ano_void;
typedef _BitInt(128) u128;

typedef struct {
    union {
        struct {
            uint64_t tdLo;
            uint64_t tdHi;
        };
        u128 hhhahaf; // -IBM
    };
    uint64_t wpUUID;
    uint32_t irctdb;
    uint8_t wheels;
} mem_chariot_t;

// Currently more of a memory tesst. // TODO: Get CMake tests working properly.
int autoStringTest() {

    uint8_t someBytes[1024];
    uint8_t* stackBytes = ano_salloc(42);

    if (true) {
        int intVar __attribute__((__cleanup__(intCleanup))) = 64; // This SIMPLETON here...
    }

    // Supposedly scope-local and thread-local Heap
    if (true) {
        mi_heap_t *memHeap LOCALHEAPATTR = mi_heap_new();

        mem_chariot_t *memChariots = mi_heap_zalloc_aligned(memHeap,
                                                    4096 * 8192 * sizeof(mem_chariot_t),
                                                    sizeof(mem_chariot_t));
        mem_chariot_t **memPanzers = mi_heap_zalloc_aligned(memHeap,
                                                    4096 * sizeof(mem_chariot_t),
                                                    sizeof(mem_chariot_t));
        ano_void *memWagens = mi_heap_zalloc_aligned(memHeap,
                                                    4096 * sizeof(mem_chariot_t),
                                                    sizeof(mem_chariot_t));

        mem_chariot_t *firstOne = &memChariots[0];
        firstOne->tdLo = 0x1776177623237123;
        firstOne->tdHi = 0x1231776223444777;
        firstOne->wpUUID = 0x1776abab;
        firstOne->irctdb = 872282;
        firstOne->wheels = 8;
        printf("%llu\t%llu\n\n", firstOne->tdLo, firstOne->tdHi);
        printf("%llx\n", (unsigned long long)(firstOne->hhhahaf & 0xFFFFFFFFFFFFFFFF));

        memChariots[1].hhhahaf = 0x1776177613374444;
        memChariots[1].wpUUID = 0x1776abac;
        memChariots[1].irctdb = 2222;
        memChariots[1].wheels = 6;
        printf("%llu\t%llu\n\n", memChariots[1].tdLo, memChariots[1].tdHi);

        srand(0x1776);
        for (uint32_t i = 2; i < 4096; i++) {
            memChariots[i].tdLo = rand();
            memChariots[i].tdHi = rand();
            memChariots[i].wpUUID = rand();
            memChariots[i].irctdb = rand() % 40000;
            memChariots[i].wheels = rand() % 16;
        }
    }

    return 0;
}

/* --- easter-egg harness (not part of the original experiment) --- */
int main(void) {
    return autoStringTest();
}

/* ===========================================================================
 *  FINDINGS -- what this experiment was really probing, reverse-engineered later.
 *  The maintained, asserted distillation lives in tests/anotest_memory.c.
 *
 *  Layout, empirically confirmed (clang -std=c23, arm64):
 *    sizeof(mem_chariot_t) = 32   _Alignof = 16
 *    offsets: tdLo 0  tdHi 8  hhhahaf 0  wpUUID 16  irctdb 24  wheels 28
 *  hhhahaf overlaps tdLo|tdHi exactly: the u128 is the two 64-bit halves
 *  concatenated (low half == tdLo) on little-endian. Reading the union member
 *  you did not last write is well-defined in C (6.5.2.3) for _BitInt(128) -- no
 *  padding, no trap representations -- but the half<->whole correspondence is
 *  endian-dependent (holds on LE: arm64, x86-64; halves swap on BE).
 *
 *  The sizing is the Rosetta Stone: 4096 * 8192 = 2^25 elements * 32 B = exactly
 *  1 GiB. That matches the startup mi_reserve_huge_os_pages_at(4,0,...): on x86 a
 *  mimalloc "huge" OS page is 1 GiB, four were reserved at boot, and the table
 *  was dimensioned to land inside ONE of them. 32 B/record also tiles a 64 B
 *  cache line two-per-line, with a 16-aligned 128-bit field at offset 0.
 *
 *  what was poked                    what it proves for the engine
 *  --------------------------------  ------------------------------------------
 *  1 GiB == 2^25 * 32 B table        a giant component array fits one huge page
 *  huge pages reserved at boot       that array allocs/zeros instantly, sweeps
 *                                    TLB-free
 *  _BitInt(128) -> 32 B records      2-per-cache-line, SIMD-aligned ECS layout
 *  union {2x u64 <-> u128}           128-bit IDs as halves or whole, zero-copy
 *  mi_heap_new + LOCALHEAPATTR       scoped thread-local arena, bulk free at the
 *                                    brace (no per-block mi_free)
 *  __cleanup__ probe (intVar)        the RAII mechanism the arena leans on works
 *  someBytes[1024] / ano_salloc(42)  stack/alloca + byte-boundary scaffolding
 *
 *  Why it was "instant" with huge pages -- three costs collapse when 1 GiB lives
 *  in one pre-reserved page: (1) TLB: a single entry covers the whole table vs a
 *  page-walk storm; (2) page faults: pages are pre-faulted at boot, so no quarter-
 *  million minor faults on first touch; (3) zeroing: huge pages arrive OS-zeroed,
 *  so zalloc is a pointer handoff. NB the preserved loop only fills elems 2..4095
 *  -- the gigabyte is allocated/zeroed but mostly untouched; an earlier cut surely
 *  swept the full count.
 *
 *  TLB reach -- page-table entries to cover a 1 GiB linear sweep:
 *    mapping                       PTEs for 1 GiB
 *    ----------------------------  --------------
 *    x86 4 KiB pages                       262144
 *    Apple Silicon 16 KiB pages             65536
 *    Intel Mac 2 MiB superpage                512
 *    x86 1 GiB huge page                        1
 *
 *  macOS / Apple Silicon reality (this machine: M1, base page 16 KiB):
 *   - VM_FLAGS_SUPERPAGE_SIZE_2MB is in the SDK header but is an INTEL-only
 *     facility; arm64 ignores/rejects it. There is no public large-page API on
 *     Apple Silicon at all -- not 2 MiB, not 1 GiB. mi_reserve_huge_os_pages_at()
 *     just returns non-zero and reserves nothing (see the anotest_memory probe).
 *   - The base granule is already 16 KiB (4x the x86 4 KiB baseline), and the
 *     hardware folds aligned contiguous runs into single TLB entries (the ARMv8
 *     contiguous bit) transparently. With a 16 KiB granule an L2 block maps
 *     32 MiB -- exactly the 0x2000000 alignment mimalloc asked for in the "fall
 *     back to over-allocation" warning when serving the 1 GiB block: it aligns
 *     large allocations so the kernel can back them with one big mapping.
 *   - Upshot: do not architect around explicit huge pages on macOS (nothing to
 *     enable). Allocate big contiguous arenas + keep the cache-conscious layout
 *     and the M1 captures most of the benefit by default. Gate
 *     mi_reserve_huge_os_pages to Windows/Linux; skip it on macOS.
 * =========================================================================== */
