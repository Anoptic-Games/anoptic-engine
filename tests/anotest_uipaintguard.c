/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: paint_push's stop-table fullness guard vs uint32 wrap. ui_build.c:236 tests
// b->stopCount + stopCount > b->stopCap in uint32 before copying, so once any stops are
// resident a stopCount near UINT32_MAX wraps the sum under stopCap, the guard passes, and
// the :239 copy loop writes ~2^32 32-byte stops past the caller's array (docs/BUGS.md,
// UI / Implementation) 〜 reached from the public ano_ui_paint_linear, and a direct breach
// of the header's "Full array -> ANO_UI_REF_NONE, no mutation" (anoptic_ui.h:121). The crash
// IS the failure signal (mirrors anotest_uipathguard): once the guard computes without wrap,
// the trigger returns ANO_UI_REF_NONE with no mutation and the run reaches exit 0. Controls
// first: a real gradient pushes and sorts, and the guard refuses an honest overflow without
// mutating, so a reject-everything fix cannot pass. Deterministic, no threads. Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>

#include <anoptic_ui.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define STOP_CAP 16u

static AnoUiStop make_stop(float t)
{
    AnoUiStop st = { .color = { t, t, t, 1.0f }, .t = t };
    return st;
}

int main(void)
{
    AnoUiPaint paints[4];
    AnoUiStop stops[STOP_CAP];
    AnoUiBuilder b;
    ano_ui_builder_init(&b, NULL, 0, NULL, 0, paints, 4, stops, STOP_CAP);

    const float p0[2] = { 0.0f, 0.0f }, p1[2] = { 8.0f, 0.0f };

    // control: 8 stops (deliberately reversed) push, sort ascending, land as paint 0
    AnoUiStop src[8];
    for (int i = 0; i < 8; i++)
        src[i] = make_stop((float)(7 - i) / 7.0f);
    uint32_t idx = ano_ui_paint_linear(&b, p0, p1, src, 8);
    CHECK(idx == 0, "first gradient lands at paint 0");
    CHECK(b.stopCount == 8, "8 stops resident");
    for (int i = 1; i < 8; i++)
        CHECK(stops[i - 1].t <= stops[i].t, "stops sorted ascending");

    // control: the guard is live for an honest overflow 〜 16 into 8 free slots refuses, no mutation
    AnoUiStop big[16];
    for (int i = 0; i < 16; i++)
        big[i] = make_stop((float)i / 15.0f);
    uint32_t rej = ano_ui_paint_linear(&b, p0, p1, big, 16);
    CHECK(rej == ANO_UI_REF_NONE, "honest overflow returns NONE");
    CHECK(b.stopCount == 8 && b.paintCount == 1, "refused push mutates nothing");

    // trigger: stopCount UINT32_MAX-4 wraps 8 + (2^32 - 5) to 3 <= stopCap 16 〜 the guard
    // passes and the copy loop runs off the 16-entry array (access violation today).
    // Correct code returns NONE with no mutation and the run reaches the checks below.
    uint32_t wrapped = ano_ui_paint_linear(&b, p0, p1, src, UINT32_MAX - 4u);
    CHECK(wrapped == ANO_UI_REF_NONE, "wrapping stopCount returns NONE");
    CHECK(b.stopCount == 8 && b.paintCount == 1, "wrapping stopCount mutates nothing");

    if (failures) {
        printf("anotest_uipaintguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_uipaintguard: all passed\n");
    return 0;
}
