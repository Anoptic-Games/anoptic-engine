/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Standing UI demo scene: GPU self-test target (docs/ui/ui-render.md §7).
// Deterministic, reference-evaluable (rrect/shadow/clip/paint/path; no IMAGE/GLYPHS). Keep bitwise-stable.

#include "anoptic_ui.h"


/* Demo Scene */

// Premultiplied linear from straight rgba.
#define PM(r, g, b, a) { (r) * (a), (g) * (a), (b) * (a), (a) }

// In: builder (caps >= 16 prims / 4 clips / 1 paint / 2 stops / 16 curve words), panel origin.
// Emission order is paint order. Gradient/path absent if their tables are unattached.
void ano_ui_demo_scene(AnoUiBuilder *b, float ox, float oy)
{
    const float shadow[4]   = PM(0.00f, 0.00f, 0.00f, 0.55f);
    const float plate[4]    = PM(0.086f, 0.098f, 0.117f, 0.96f);
    const float rim[4]      = PM(0.55f, 0.58f, 0.62f, 1.00f);
    const float well[4]     = PM(0.043f, 0.047f, 0.055f, 1.00f);
    const float bar[4]      = PM(0.85f, 0.55f, 0.15f, 0.90f);
    const float inset[4]    = PM(0.00f, 0.00f, 0.00f, 0.60f);
    const float pill[4]     = PM(0.25f, 0.60f, 0.35f, 1.00f);
    const float pillRim[4]  = PM(0.70f, 0.70f, 0.75f, 1.00f);
    const float glow[4]     = { 0.10f, 0.28f, 0.55f, 0.0f }; // ADD: rgb is the added light
    const float button[4]   = PM(0.16f, 0.42f, 0.85f, 1.00f);
    const float btnRim[4]   = PM(0.80f, 0.88f, 1.00f, 0.90f);
    const float ghost[4]    = PM(0.60f, 0.62f, 0.66f, 1.00f);
    const float wash[4]     = PM(1.00f, 1.00f, 1.00f, 0.06f);
    const float white[4]    = PM(1.00f, 1.00f, 1.00f, 1.00f);
    const float icon[4]     = PM(0.86f, 0.90f, 0.96f, 1.00f);

    const float r14[4] = { 14, 14, 14, 14 };
    const float r10[4] = { 10, 10, 10, 10 };
    const float r8[4] = { 8, 8, 8, 8 };
    const float r12[4] = { 12, 12, 12, 12 };
    const float r6[4] = { 6, 6, 6, 6 };

#define BOX(x0, y0, x1, y1) (float[2]){ ox + (x0), oy + (y0) }, (float[2]){ ox + (x1), oy + (y1) }

    // Panel: drop shadow, plate fill, border ring.
    ano_ui_shadow(b, BOX(6, 10, 366, 250), 14.0f, 8.0f, shadow, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(0, 0, 360, 240), r14, plate, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(0, 0, 360, 240), r14, rim, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

    // Header accent: linear gradient strip (blue -> teal).
    AnoUiStop gstops[2] = {
        { .color = PM(0.10f, 0.30f, 0.58f, 1.0f), .t = 0.0f },
        { .color = PM(0.14f, 0.56f, 0.52f, 1.0f), .t = 1.0f },
    };
    uint32_t grad = ano_ui_paint_linear(b, (float[2]){ ox + 16.0f, oy + 20.0f },
                                        (float[2]){ ox + 344.0f, oy + 20.0f }, gstops, 2);
    ano_ui_rrect(b, BOX(16, 20, 344, 40), r6, white, 0.0f, grad, ANO_UI_REF_NONE, 0);

    // Viewport: rect clip, well, overflow bar, inner shadow, capsules.
    uint32_t view = ano_ui_clip(b, BOX(16, 48, 344, 148), NULL, NULL, NULL);
    ano_ui_rrect(b, BOX(16, 48, 344, 148), r8, well, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(60, 64, 520, 100), r10, bar, 0.0f, ANO_UI_REF_NONE, view, 0);
    ano_ui_shadow(b, BOX(16, 48, 344, 148), 8.0f, 5.0f, inset, ANO_UI_REF_NONE, ANO_UI_FLAG_INNER);
    ano_ui_rrect(b, BOX(32, 112, 120, 136), r12, pill, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(132, 112, 220, 136), r12, pillRim, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

    // Buttons: additive glow under primary, ring-only ghost.
    ano_ui_shadow(b, BOX(24, 168, 160, 212), 10.0f, 10.0f, glow, ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
    ano_ui_rrect(b, BOX(24, 168, 160, 212), r10, button, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(24, 168, 160, 212), r10, btnRim, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(176, 168, 312, 212), r10, ghost, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

    // Play-triangle path icon inside the ghost button.
    AnoUiPathSeg tri[3] = {
        { ANO_UI_SEG_MOVE, { ox + 228.0f, oy + 176.0f, 0.0f, 0.0f } },
        { ANO_UI_SEG_LINE, { ox + 264.0f, oy + 190.0f, 0.0f, 0.0f } },
        { ANO_UI_SEG_LINE, { ox + 228.0f, oy + 204.0f, 0.0f, 0.0f } },
    };
    ano_ui_path_fill(b, tri, 3, icon, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

    // Footer wash clipped by the panel's rounded silhouette.
    uint32_t sil = ano_ui_clip(b, BOX(0, 0, 360, 240), BOX(0, 0, 360, 240), r14);
    ano_ui_rrect(b, BOX(0, 208, 360, 240), NULL, wash, 0.0f, ANO_UI_REF_NONE, sil, 0);

#undef BOX
}
