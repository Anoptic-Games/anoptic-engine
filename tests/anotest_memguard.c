/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_aligned_malloc degenerate-argument contract. The header promises "Returns
// pointer, or NULL on failure. NULL if size or alignment is 0" 〜 but the implementation is a
// bare forward to mi_malloc_aligned (docs/BUGS.md, Memory / Implementation,
// memalign_linux.c:13 + macos/win64 twins), and mimalloc follows malloc's zero-size
// convention: size 0 hands back a live minimum-size block. Only the alignment-0 half of the
// sentence holds (mimalloc's power-of-two reject), so the documented zero-size sentinel never
// fires and a caller branching on NULL to reject count*stride==0 gets silent success plus a
// block it believes cannot exist. Controls pin the alignment-0 reject and a sane aligned
// round-trip, so a reject-everything fix cannot pass. Any live block the failing probe
// receives is freed, so the failing run stays leak-clean. Headless, deterministic.
// Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_memory.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // control: a sane request round-trips (the path is live, not reject-everything)
    uint8_t *sane = ano_aligned_malloc(64, 64);
    CHECK(sane != NULL, "64B/64-aligned allocation succeeds");
    if (sane) {
        CHECK(((uintptr_t)sane % 64) == 0, "returned block honors the requested alignment");
        for (int i = 0; i < 64; i++) sane[i] = (uint8_t)i; // must not fault
        CHECK(sane[63] == 63, "block is writable across its full size");
        ano_aligned_free(sane);
    }

    // control: alignment 0 is documented NULL and already behaves (pins the working half)
    CHECK(ano_aligned_malloc(8, 0) == NULL, "alignment 0 returns NULL per header contract");

    // bug probe: size 0 is documented NULL; the bare mi_malloc_aligned forward returns a live block
    void *zero = ano_aligned_malloc(0, 16);
    CHECK(zero == NULL, "size 0 returns NULL per header contract");
    if (zero) ano_aligned_free(zero); // keep the failing run leak-clean

    if (failures) {
        printf("anotest_memguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_memguard: all passed\n");
    return 0;
}
