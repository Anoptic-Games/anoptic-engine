/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Standing UI demo scene: the GPU self-test target (docs/ui/ui-render.md §7 step 4).
// Deterministic and reference-evaluable (RRECT/SHADOW/clips only), exercising every
// evaluator path: drop shadow, plate fill, border ring, rect clip with visible
// overflow cut, inner shadow, capsule fill/ring, additive glow, and a rounded
// silhouette clip. Both the render-side demo compose and the offline screenshot
// compare build exactly this; keep it bitwise-stable.

#include "anoptic_ui.h"

// Premultiplied linear from straight rgba.
#define PM(r, g, b, a) { (r) * (a), (g) * (a), (b) * (a), (a) }

// In: builder with caps >= 16 prims / 4 clips, panel origin in overlay px.
// Emission order is paint order; see the table in the file banner.
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

    const float r14[4] = { 14, 14, 14, 14 };
    const float r10[4] = { 10, 10, 10, 10 };
    const float r8[4] = { 8, 8, 8, 8 };
    const float r12[4] = { 12, 12, 12, 12 };

#define BOX(x0, y0, x1, y1) (float[2]){ ox + (x0), oy + (y0) }, (float[2]){ ox + (x1), oy + (y1) }

    // Panel plate with offset drop shadow and border ring.
    ano_ui_shadow(b, BOX(6, 10, 366, 250), 14.0f, 8.0f, shadow, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(0, 0, 360, 240), r14, plate, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(0, 0, 360, 240), r14, rim, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

    // Inner viewport: rect clip, sunken backdrop, an overflowing bar cut by the clip,
    // an inner shadow, and two capsules (fill + ring).
    uint32_t view = ano_ui_clip(b, BOX(16, 48, 344, 148), NULL, NULL, NULL);
    ano_ui_rrect(b, BOX(16, 48, 344, 148), r8, well, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(60, 64, 520, 100), r10, bar, 0.0f, ANO_UI_REF_NONE, view, 0);
    ano_ui_shadow(b, BOX(16, 48, 344, 148), 8.0f, 5.0f, inset, ANO_UI_REF_NONE, ANO_UI_FLAG_INNER);
    ano_ui_rrect(b, BOX(32, 112, 120, 136), r12, pill, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(132, 112, 220, 136), r12, pillRim, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

    // Buttons: additive glow under the primary, ring-only ghost beside it.
    ano_ui_shadow(b, BOX(24, 168, 160, 212), 10.0f, 10.0f, glow, ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
    ano_ui_rrect(b, BOX(24, 168, 160, 212), r10, button, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(24, 168, 160, 212), r10, btnRim, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(b, BOX(176, 168, 312, 212), r10, ghost, 2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

    // Footer wash: a sharp rect clipped by the panel's rounded silhouette, so its
    // bottom corners follow the plate's curvature.
    uint32_t sil = ano_ui_clip(b, BOX(0, 0, 360, 240), BOX(0, 0, 360, 240), r14);
    ano_ui_rrect(b, BOX(0, 208, 360, 240), NULL, wash, 0.0f, ANO_UI_REF_NONE, sil, 0);

#undef BOX
}
