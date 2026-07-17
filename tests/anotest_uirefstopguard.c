/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_ui_ref_paint's own stop-window guard vs uint32 wrap. The reject arm at
// ui_raster_ref.c:229 tests pa->stopFirst + pa->stopCount > s->stopCount in uint32, so
// stopFirst UINT32_MAX with stopCount 2 wraps the sum to 1, the guard passes, and
// ui_stop_color reads s->stops[UINT32_MAX] ~137 GB past the table (docs/BUGS.md, UI /
// Implementation) 〜 a direct breach of the header's "Out-of-range ref fails CLOSED
// (transparent)" promise (anoptic_ui.h:275), on the CPU mirror the GPU evaluator cites as
// its fail-closed reference; the bridge census entry (ano_render_bridge.c:204) shows no
// upstream layer catches this shape either. The crash IS the failure signal (mirrors
// anotest_uipathguard); once the guard computes without wrap the trigger falls into the
// transparent arm and the run reaches exit 0. Controls first: a valid window interpolates
// mid-gradient, NONE passes base through, an out-of-range paintRef, a plain non-wrapping
// out-of-range stop window, and stopCount 0 all fail closed 〜 so a reject-everything fix
// cannot pass. Deterministic, no threads, no GPU device. Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include <anoptic_ui.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Two-stop table (black t=0 -> white t=1) under one linear paint, stop window parametrized.
static AnoUiStop  g_stops[2];
static AnoUiPaint g_paint;

static AnoUiScene make_scene(uint32_t stopFirst, uint32_t stopCount)
{
    g_stops[0] = (AnoUiStop){ .color = { 0.0f, 0.0f, 0.0f, 1.0f }, .t = 0.0f };
    g_stops[1] = (AnoUiStop){ .color = { 1.0f, 1.0f, 1.0f, 1.0f }, .t = 1.0f };
    g_paint = (AnoUiPaint){
        .kind = ANO_UI_GRAD_LINEAR, .stopFirst = stopFirst, .stopCount = stopCount,
        .xform = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, // t = px
    };
    return (AnoUiScene){ .paints = &g_paint, .paintCount = 1,
                         .stops = g_stops, .stopCount = 2 };
}

static bool transparent(const float out[4])
{
    return out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f && out[3] == 0.0f;
}

int main(void)
{
    const float base[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float out[4];

    // control: the valid window resolves 〜 t = 0.5 lands mid-gradient, alpha rides base.
    AnoUiScene s = make_scene(0, 2);
    ano_ui_ref_paint(&s, 0, 0.5f, 0.0f, base, out);
    CHECK(fabsf(out[0] - 0.5f) < 1e-4f && fabsf(out[1] - 0.5f) < 1e-4f
          && fabsf(out[2] - 0.5f) < 1e-4f && fabsf(out[3] - 1.0f) < 1e-4f,
          "valid stop window interpolates mid-gradient");

    // control: NONE passes base through.
    ano_ui_ref_paint(&s, ANO_UI_REF_NONE, 0.5f, 0.0f, base, out);
    CHECK(out[0] == 1.0f && out[1] == 1.0f && out[2] == 1.0f && out[3] == 1.0f,
          "paintRef NONE returns base");

    // control: out-of-range paintRef takes the documented fail-closed arm.
    ano_ui_ref_paint(&s, 7, 0.5f, 0.0f, base, out);
    CHECK(transparent(out), "out-of-range paintRef fails closed");

    // control: the guard is live for the honest shape 〜 window 1000..1001 of a 2-stop
    // table fails closed without wrapping.
    s = make_scene(1000u, 2);
    ano_ui_ref_paint(&s, 0, 0.5f, 0.0f, base, out);
    CHECK(transparent(out), "plain out-of-range stop window fails closed");

    // control: stopCount 0 fails closed.
    s = make_scene(0, 0);
    ano_ui_ref_paint(&s, 0, 0.5f, 0.0f, base, out);
    CHECK(transparent(out), "stopCount 0 fails closed");

    // trigger: stopFirst UINT32_MAX with stopCount 2 wraps first + count to 1, the :229
    // guard passes, and ui_stop_color reads st[UINT32_MAX] 〜 an access violation today.
    // Correct code fails closed and the run reaches exit 0.
    s = make_scene(UINT32_MAX, 2);
    out[0] = out[1] = out[2] = out[3] = -1.0f;
    ano_ui_ref_paint(&s, 0, 0.5f, 0.0f, base, out);
    CHECK(transparent(out), "wrapping stop window fails closed");

    if (failures) {
        printf("anotest_uirefstopguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_uirefstopguard: all passed\n");
    return 0;
}
