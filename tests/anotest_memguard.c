/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the ano_aligned_malloc zero contract. anoptic_memory.h:47 promises "NULL if size
// or alignment is 0", but all three platform implementations (memalign_win64.c:13 and the
// linux/macos twins) forward straight to mi_malloc_aligned with no guard, and mimalloc hands
// back a live unique pointer for size 0 (docs/BUGS.md, Memory / Implementation). Controls pin
// the live path 〜 a real aligned alloc returns aligned writable memory and frees 〜 so a
// reject-everything fix cannot pass. The alignment-0 half of the contract holds today only by
// mimalloc coincidence (alloc-aligned.c:177 refuses non-power-of-two) and is pinned too.
// Deterministic, no threads, no files. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "anoptic_memory.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // control: a real aligned allocation is live, aligned, writable, freeable
    {
        void *p = ano_aligned_malloc(24, 64);
        CHECK(p != NULL, "aligned_malloc(24, 64) allocates");
        if (p) {
            CHECK(((uintptr_t)p & 63u) == 0, "block is 64-byte aligned");
            memset(p, 0xA5, 24); // writable or nothing
            ano_aligned_free(p);
        }
    }

    // control: a page-sized alignment still round-trips
    {
        void *p = ano_aligned_malloc(4096, 4096);
        CHECK(p != NULL, "aligned_malloc(4096, 4096) allocates");
        if (p) {
            CHECK(((uintptr_t)p & 4095u) == 0, "block is page aligned");
            ano_aligned_free(p);
        }
    }

    // contract pin: alignment 0 is documented NULL and must stay NULL once a guard lands
    CHECK(ano_aligned_malloc(16, 0) == NULL, "alignment 0 returns NULL per anoptic_memory.h:47");

    // trigger: size 0 must be NULL per the header 〜 today mimalloc returns a live pointer
    {
        void *p = ano_aligned_malloc(0, 16);
        CHECK(p == NULL, "size 0 returns NULL per anoptic_memory.h:47");
        if (p) ano_aligned_free(p);
    }
    {
        void *p = ano_aligned_malloc(0, 64);
        CHECK(p == NULL, "size 0 returns NULL at any alignment");
        if (p) ano_aligned_free(p);
    }

    if (failures) {
        printf("anotest_memguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_memguard: all passed\n");
    return 0;
}
