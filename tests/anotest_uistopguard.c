/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the UI bridge's producer-side validator vs paint stop windows. ui_prim_valid
// (ano_render_bridge.c:204) bounds every other block-local reference 〜 clipRef, paintRef,
// GLYPHS windows, the full PATH curve walk 〜 but never a paint's [stopFirst, +stopCount)
// window against the block's stop table, and no later layer recovers: apply.c:325 adopts
// the block blind, ui_compose rebases pa.stopFirst += ns unchecked (ui_raster.c:123), and
// the GPU evaluator checks only stopCount != 0 (uicoverage.glsl:176) although its comment
// claims the out-of-range stop window "fails CLOSED. Mirrors ano_ui_ref_paint" 〜 the
// mirror's range check lives solely in the CPU ref (ui_raster_ref.c:229), so ui_stop_color
// walks the stop SSBO out of bounds on every painted pixel (docs/BUGS.md, Render / Vulkan
// backend / Interlink-Composition). Controls first: the engine's own ref evaluator fails
// closed on the poisoned window (pinning the invariant the shader claims to mirror), a
// valid block enqueues intact, and an out-of-range paintRef takes the documented drop path
// 〜 so a reject-everything fix cannot pass. Trigger: the same valid block with stop
// windows {1000,2} and {UINT32_MAX,2} (the uint32-wrap shape that also slips the ref
// guard's own first + count arithmetic) must be dropped like every sibling reference
// (return true, nothing enqueued); today both enqueue verbatim. Deterministic, no
// threads, no GPU device. Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>

#include <mimalloc.h>

#include <anoptic_ui.h>
#include "render_bridge/render_bridge.h" // private transport: bridge + pop endpoint

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// One-prim scene: RRECT filled by paint 0 over a two-stop table, stop window parametrized.
static AnoUiPrim  g_prim;
static AnoUiPaint g_paint;
static AnoUiStop  g_stops[2];

static AnoUiBuilder make_ui(uint32_t stopFirst, uint32_t stopCount)
{
    g_prim = (AnoUiPrim){
        .inv = { 1.0f, 0.0f, 0.0f, 1.0f }, .origin = { 10.0f, 10.0f },
        .kind = ANO_UI_RRECT, .half = { 5.0f, 5.0f },
        .color = { 1.0f, 1.0f, 1.0f, 1.0f },
        .paintRef = 0, .clipRef = ANO_UI_REF_NONE,
    };
    g_paint = (AnoUiPaint){
        .kind = ANO_UI_GRAD_LINEAR, .stopFirst = stopFirst, .stopCount = stopCount,
        .xform = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, // t = px
    };
    g_stops[0] = (AnoUiStop){ .color = { 0.0f, 0.0f, 0.0f, 1.0f }, .t = 0.0f };
    g_stops[1] = (AnoUiStop){ .color = { 1.0f, 1.0f, 1.0f, 1.0f }, .t = 1.0f };
    return (AnoUiBuilder){
        .prims = &g_prim, .primCap = 1, .primCount = 1,
        .paints = &g_paint, .paintCap = 1, .paintCount = 1,
        .stops = g_stops, .stopCap = 2, .stopCount = 2,
    };
}

// Submit one block, then assert the drop arm: true returned, nothing enqueued. A leaked
// command is reported with the window it carried, and its adopted block freed.
static void expect_dropped(AnoRenderBridge *bridge, uint32_t ui_id, uint32_t stopFirst,
                           const char *what)
{
    AnoUiBuilder ui = make_ui(stopFirst, 2);
    CHECK(ano_render_ui_set(bridge, ui_id, 0, &ui, NULL, 0), "drop semantics return true");
    RenderCommand cmd;
    bool leaked = ano_render_next_command(bridge, &cmd);
    CHECK(!leaked, what);
    if (leaked && cmd.kind == RCMD_UI_SET && cmd.ui != NULL) {
        printf("      (stop window {%u,%u} enqueued verbatim over a %u-stop table)\n",
               cmd.ui->paints[0].stopFirst, cmd.ui->paints[0].stopCount, cmd.ui->stopCount);
        mi_free((void *)cmd.ui);
    }
}

int main(void)
{
    static AnoRenderBridge bridge;
    CHECK(ano_render_bridge_init(&bridge, mi_heap_get_default(), 8, 8), "bridge init");

    // control: the engine's own ref evaluator 〜 the mirror the GPU comment cites 〜
    // resolves the valid window and fails CLOSED on the poisoned one. Pins the invariant.
    {
        float base[4] = { 1.0f, 1.0f, 1.0f, 1.0f }, out[4];
        AnoUiBuilder ok = make_ui(0, 2);
        AnoUiScene sOk = ano_ui_scene(&ok);
        ano_ui_ref_paint(&sOk, 0, 0.5f, 0.0f, base, out);
        CHECK(out[3] > 0.9f, "ref evaluator resolves the valid stop window");
        AnoUiBuilder bad = make_ui(1000, 2);
        AnoUiScene sBad = ano_ui_scene(&bad);
        ano_ui_ref_paint(&sBad, 0, 0.5f, 0.0f, base, out);
        CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f && out[3] == 0.0f,
              "ref evaluator fails closed on the out-of-range stop window");
    }

    // control: a valid block passes validation and enqueues intact.
    {
        AnoUiBuilder ui = make_ui(0, 2);
        CHECK(ano_render_ui_set(&bridge, 1, 0, &ui, NULL, 0), "valid block returns true");
        RenderCommand cmd;
        CHECK(ano_render_next_command(&bridge, &cmd), "valid block enqueues");
        CHECK(cmd.kind == RCMD_UI_SET && cmd.ui_id == 1, "RCMD_UI_SET for ui_id 1");
        CHECK(cmd.ui != NULL && cmd.ui->paintCount == 1 && cmd.ui->stopCount == 2,
              "tables packed into the render-owned block");
        if (cmd.ui != NULL) {
            CHECK(cmd.ui->paints[0].stopFirst == 0 && cmd.ui->paints[0].stopCount == 2,
                  "paint copied verbatim");
            mi_free((void *)cmd.ui);
        }
    }

    // control: the documented drop path is live for the sibling reference class 〜 an
    // out-of-range paintRef returns true with nothing enqueued.
    {
        AnoUiBuilder ui = make_ui(0, 2);
        g_prim.paintRef = 7; // paintCount == 1
        CHECK(ano_render_ui_set(&bridge, 2, 0, &ui, NULL, 0), "invalid paintRef returns true");
        RenderCommand cmd;
        CHECK(!ano_render_next_command(&bridge, &cmd), "invalid paintRef is dropped");
    }

    // trigger: stop window 1000..1001 of a 2-stop table 〜 the same class of block-local
    // reference must take the same drop path. Today it enqueues and rides to the GPU.
    expect_dropped(&bridge, 3, 1000u,
                   "out-of-range stop window is dropped, not enqueued");

    // trigger: the wrap shape 〜 stopFirst UINT32_MAX with stopCount 2 wraps a naive
    // first + count bound (the ref guard's own arithmetic at ui_raster_ref.c:229), so the
    // fix must compute without wrap.
    expect_dropped(&bridge, 4, UINT32_MAX,
                   "wrapping stop window is dropped, not enqueued");

    ano_render_bridge_destroy(&bridge);

    if (failures) {
        printf("anotest_uistopguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_uistopguard: all passed\n");
    return 0;
}
