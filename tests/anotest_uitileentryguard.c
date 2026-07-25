/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_ui_ref_eval_tiled's tile-entry index domain. The entry word is a 31-bit prim
// index plus the solid bit, and it arrives from the caller (a stale, hand-built or corrupted
// stream is ordinary input 〜 ano_ui_tile_build is not the only producer). shade_entry's solid
// branch dereferenced s->prims[idx] unchecked (ui_raster_ref.c:334) and the blend select
// re-decoded the same word to read s->prims[entry & INDEX_MASK].flags (:362), while every
// sibling on the same domain 〜 ano_ui_ref_shade (:258), clip_cov (:173), ano_ui_ref_paint
// (:224) 〜 bounds-checks, and the header contracts "Out-of-range ref fails CLOSED
// (transparent)" (anoptic_ui.h:275). idx == ANO_UI_ENTRY_INDEX_MASK reads ~192 GB past the
// prim table (docs/BUGS_DONE.md, UI / Implementation). The crash IS the failure signal (mirrors
// anotest_uipathguard / anotest_uirefstopguard); once shade_entry is the sole decode and
// validates once, the trigger takes the transparent arm and the run reaches exit 0.
// Controls first: a module-built stream matches brute ano_ui_ref_eval bitwise, a solid entry
// at the LAST valid index still flat-fills, and a valid non-solid ADD entry accumulates
// additively 〜 so a reject-everything fix cannot pass. The two solid-CLEAR triggers pin the
// blend select specifically (ano_ui_ref_shade already fails closed there, the flags read did
// not). Deterministic, no threads, no GPU device. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <anoptic_ui.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Four shadow-free rrects (radius 0, no clip, no paint) over a 64x64 grid of 8x8 tiles.
// Prim 0 blankets the grid and is solid over the interior tiles; prim 2 is the ADD glow.
#define GRID_TILES 8u
#define GRID_PX    64

static AnoUiPrim g_prims[4];

static const float COLOR_A[4] = { 0.10f, 0.20f, 0.30f, 0.40f };
static const float COLOR_B[4] = { 0.25f, 0.05f, 0.15f, 0.50f };
static const float COLOR_C[4] = { 0.30f, 0.10f, 0.20f, 0.25f };
static const float COLOR_D[4] = { 0.05f, 0.35f, 0.15f, 0.60f };

static AnoUiScene make_scene(void)
{
    AnoUiBuilder b;
    ano_ui_builder_init(&b, g_prims, 4, NULL, 0, NULL, 0, NULL, 0);
    const float r0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ano_ui_rrect(&b, (float[2]){ 0.0f, 0.0f }, (float[2]){ 64.0f, 64.0f }, r0, COLOR_A,
                 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, ANO_UI_BLEND_OVER);
    ano_ui_rrect(&b, (float[2]){ 16.0f, 16.0f }, (float[2]){ 48.0f, 48.0f }, r0, COLOR_B,
                 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, ANO_UI_BLEND_OVER);
    ano_ui_rrect(&b, (float[2]){ 8.0f, 40.0f }, (float[2]){ 56.0f, 60.0f }, r0, COLOR_C,
                 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
    ano_ui_rrect(&b, (float[2]){ 24.0f, 4.0f }, (float[2]){ 40.0f, 60.0f }, r0, COLOR_D,
                 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, ANO_UI_BLEND_OVER);
    return ano_ui_scene(&b);
}

static bool transparent(const float out[4])
{
    return out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f && out[3] == 0.0f;
}

// One tile at (ox,oy) holding `n` caller-supplied entry words, evaluated at (px,py).
static void eval_one_tile(const AnoUiScene *s, int32_t ox, int32_t oy, const uint32_t *entries,
                          uint32_t n, int32_t px, int32_t py, float out[4])
{
    const uint32_t offsets[2] = { 0u, n };
    ano_ui_ref_eval_tiled(s, ox, oy, 1, 1, offsets, entries, px, py, out);
}

int main(void)
{
    AnoUiScene s = make_scene();
    CHECK(s.primCount == 4, "scene builds four prims");

    uint32_t offsets[GRID_TILES * GRID_TILES + 1] = { 0 };
    uint32_t cursor[GRID_TILES * GRID_TILES] = { 0 };
    uint32_t entries[512] = { 0 };
    bool ok = false;
    uint32_t nEntries = ano_ui_tile_build(&s, 0, 0, GRID_TILES, GRID_TILES, offsets,
                                          GRID_TILES * GRID_TILES + 1, entries, 512,
                                          cursor, &ok);
    CHECK(ok, "tile build fits the caps");
    CHECK(nEntries > 0, "tile build emits entries");

    // control: the module's own stream reproduces brute painter's order BIT-IDENTICALLY
    // inside the grid 〜 the property the whole tiled lane exists to keep.
    {
        uint32_t bad = 0;
        for (int32_t py = 0; py < GRID_PX; py++)
            for (int32_t px = 0; px < GRID_PX; px++) {
                float brute[4], tiled[4];
                ano_ui_ref_eval(&s, (float)px, (float)py, brute);
                ano_ui_ref_eval_tiled(&s, 0, 0, GRID_TILES, GRID_TILES, offsets, entries,
                                      px, py, tiled);
                if (memcmp(brute, tiled, sizeof brute) != 0)
                    bad++;
            }
        CHECK(bad == 0, "module-built stream matches brute eval bitwise");
    }

    // control: a solid entry at the last valid index flat-fills 〜 pixel (4,4) is far outside
    // prim 3's box, so a non-zero result proves the SDF was skipped and index 3 was accepted.
    {
        const uint32_t e[1] = { 3u | ANO_UI_ENTRY_SOLID };
        float out[4];
        eval_one_tile(&s, 0, 0, e, 1, 4, 4, out);
        CHECK(memcmp(out, COLOR_D, sizeof out) == 0, "solid entry at last valid index flat-fills");
    }

    // control: a valid non-solid ADD entry accumulates rgb on top of the OVER register and
    // leaves alpha alone. Grid shifted to (24,44) so pixel (28,48) sits inside both prims.
    {
        const uint32_t e[2] = { 0u, 2u };
        float src0[4], src2[4], want[4], out[4];
        ano_ui_ref_shade(&s, 0, 28.0f, 48.0f, src0);
        ano_ui_ref_shade(&s, 2, 28.0f, 48.0f, src2);
        for (int k = 0; k < 4; k++) want[k] = src0[k];
        for (int k = 0; k < 3; k++) want[k] += src2[k];
        eval_one_tile(&s, 24, 44, e, 2, 28, 48, out);
        CHECK(memcmp(out, want, sizeof out) == 0, "ADD entry accumulates additively");
        CHECK(out[0] > src0[0], "ADD entry raised rgb above the OVER register");
        CHECK(out[3] == src0[3], "ADD entry left alpha untouched");
    }

    // triggers: a valid entry followed by an out-of-domain one. The bad entry must contribute
    // nothing and leave the register intact; today it indexes s->prims past primCount 〜 solid
    // set in shade_entry (:334), solid clear in the blend select (:362).
    {
        const uint32_t bad[4] = {
            4u,                                            // == primCount, solid clear
            4u | ANO_UI_ENTRY_SOLID,                       // == primCount, solid set
            ANO_UI_ENTRY_INDEX_MASK,                       // max index, solid clear
            ANO_UI_ENTRY_INDEX_MASK | ANO_UI_ENTRY_SOLID,  // max index, solid set
        };
        const char *name[4] = {
            "idx == primCount (solid clear) contributes nothing",
            "idx == primCount (solid set) contributes nothing",
            "idx == INDEX_MASK (solid clear) contributes nothing",
            "idx == INDEX_MASK (solid set) contributes nothing",
        };
        float want[4];
        ano_ui_ref_shade(&s, 0, 4.0f, 4.0f, want);
        for (int i = 0; i < 4; i++) {
            const uint32_t e[2] = { 0u, bad[i] };
            float out[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
            eval_one_tile(&s, 0, 0, e, 2, 4, 4, out);
            CHECK(memcmp(out, want, sizeof out) == 0, name[i]);
        }

        // and alone in the tile: the whole result is the transparent arm.
        for (int i = 0; i < 4; i++) {
            float out[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
            eval_one_tile(&s, 0, 0, &bad[i], 1, 4, 4, out);
            CHECK(transparent(out), "lone out-of-domain entry evaluates transparent");
        }
    }

    // trigger: stale stream 〜 entries built from the 4-prim scene, evaluated against a
    // 1-prim scene view over the same buffers (a shrunk scene with a compose-stale tile list).
    // Pixel (28,28) sits in tile (3,3), whose list is prim 0 (solid), prim 1 (solid), prim 3.
    // Only prim 0 is in domain; the rest must fail closed, so the result is the 1-prim eval.
    {
        AnoUiScene s1 = s;
        s1.primCount = 1;
        float want[4], out[4];
        ano_ui_ref_eval(&s1, 28.0f, 28.0f, want);
        ano_ui_ref_eval_tiled(&s1, 0, 0, GRID_TILES, GRID_TILES, offsets, entries, 28, 28, out);
        CHECK(memcmp(out, want, sizeof out) == 0, "stale entries past primCount fail closed");
    }

    if (failures) {
        printf("anotest_uitileentryguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_uitileentryguard: all passed\n");
    return 0;
}
