/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_ui_tile_build's offsets-cap guard vs uint32 wrap. ui_tiles.c:66 computes
// nTiles = tilesX * tilesY and :68 guards nTiles + 1 > offsetsCap, both in uint32, so
// 65536 x 65536 wraps nTiles to 0, the guard passes, and the call reports *ok true for a
// 2^32-tile grid the caller's buffers cannot hold 〜 with any prim in the scene, pass 1 then
// scatters counts into offsets[] far past offsetsCap (docs/BUGS.md, UI / Implementation).
// The trigger keeps the scene EMPTY so the lie is observed as *ok == true, not as an OOB
// write inside this test. The tilesX = UINT32_MAX, tilesY = 1 variant wraps nTiles + 1 to 0
// and zero-fills 2^32 offsets out of bounds 〜 same entry, deliberately not exercised.
// Controls pin the honest paths: a real 2x2 grid builds ok and a genuinely undersized
// offsets cap is refused, so a reject-everything fix cannot pass. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <anoptic_ui.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    AnoUiScene s = { 0 }; // empty scene: primCount 0, nothing to scatter

    // control: a real 2x2 grid with exact caps builds and reports ok
    {
        uint32_t offsets[5] = { 0 }, entries[4] = { 0 }, cursor[4] = { 0 };
        bool ok = false;
        uint32_t n = ano_ui_tile_build(&s, 0, 0, 2, 2, offsets, 5, entries, 4, cursor, &ok);
        CHECK(ok, "2x2 grid with exact caps reports ok");
        CHECK(n == 0, "empty scene emits no entries");
        CHECK(offsets[4] == 0, "prefix total is zero for an empty scene");
    }

    // control: the existing guard is live 〜 a genuinely small offsets cap is refused
    {
        uint32_t offsets[4] = { 0 }, entries[4] = { 0 }, cursor[4] = { 0 };
        bool ok = true;
        (void)ano_ui_tile_build(&s, 0, 0, 2, 2, offsets, 4, entries, 4, cursor, &ok);
        CHECK(!ok, "2x2 grid needs offsetsCap 5, cap 4 must refuse");
    }

    // trigger: 65536 x 65536 = 2^32 tiles wraps nTiles to 0 〜 the caps hold none of that
    // grid and *ok must come back false, but the wrapped guard passes and ok stays true
    {
        uint32_t offsets[8] = { 0 }, entries[4] = { 0 }, cursor[4] = { 0 };
        bool ok = true;
        (void)ano_ui_tile_build(&s, 0, 0, 65536u, 65536u, offsets, 8, entries, 4, cursor, &ok);
        CHECK(!ok, "2^32-tile request with offsetsCap 8 must report ok == false");
    }

    if (failures) {
        printf("anotest_uitileguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_uitileguard: all passed\n");
    return 0;
}
