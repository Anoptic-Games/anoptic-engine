/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_ui_path_fill contour-count guard. The baker bounds its quad budget (qn at
// ui_path.c:95/122/139) but never the contour counter (cn): every MOVE segment writes
// cstart[cn++] into the fixed 513-entry stack array cstart[UI_PATH_MAX_QUADS + 1] unchecked
// (ui_path.c:99/108), and the :151 seal write adds one more (docs/BUGS.md, UI / Implementation).
// A path with more than ~512 empty contours 〜 legal input the contract promises
// ANO_UI_REF_NONE for ("the path is empty") 〜 sprays cstart past its end over the live quad
// buffer q[] that sits just above it, and ano_ui_path_fill dies reading the corrupted geometry
// back out in its emit pass. Controls first: a real square fills, commits, and evaluates
// opaque, and the EXISTING quad-budget guard (the twin bound that DOES exist) rejects an
// over-length contour cleanly and commits nothing, so a fix that simply disabled the baker
// cannot pass. The crash IS the failure signal (mirrors anotest_logflood's drain-pass crash):
// once the cn bound lands, the trigger returns without corruption and the run reaches exit 0.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <anoptic_ui.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// One contour, 600 line segments: trips the 512-quad budget, must reject cleanly.
#define ZIG_LINES 600u

// Empty contours after one real triangle: cn runs past cstart[512], the seal at :151 and the
// loop writes spray over q[] above it. 2000 is well past the overrun threshold (~515) yet short
// of the point where the spray would clobber the loop bound and stop itself early.
#define TRIGGER_EMPTY_MOVES 2000u

// Second trigger: the empirically deterministic crash window 〜 with a large curve scratch the
// emit pass survives capacity rejection long enough to chase the corrupted cstart into wild
// q[] reads. K in 515..520 with an 8192-word scratch faulted 4/4 runs during the census hunt.
#define CRASH_K_LO 515u
#define CRASH_K_HI 520u
#define BIG_CURVE_CAP 8192u

int main(void)
{
    AnoUiPrim prims[4];
    uint32_t curves[256];
    AnoUiBuilder b;
    ano_ui_builder_init(&b, prims, 4, NULL, 0, NULL, 0, NULL, 0);
    ano_ui_builder_curves(&b, curves, 256);

    const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

    // control: a real square bakes, commits one prim, and evaluates opaque inside
    const AnoUiPathSeg square[4] = {
        { ANO_UI_SEG_MOVE, { 2.0f, 2.0f } },
        { ANO_UI_SEG_LINE, { 12.0f, 2.0f } },
        { ANO_UI_SEG_LINE, { 12.0f, 12.0f } },
        { ANO_UI_SEG_LINE, { 2.0f, 12.0f } },   // auto-closes to (2,2)
    };
    uint32_t sq = ano_ui_path_fill(&b, square, 4, red, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    CHECK(sq == 0, "square path accepted as prim 0");
    CHECK(b.primCount == 1, "square committed exactly one prim");
    CHECK(b.curveCount == 9, "square stream is start word + 4 curve pairs");
    if (failures == 0) {
        AnoUiScene s = ano_ui_scene(&b);
        float px[4];
        ano_ui_ref_eval(&s, 6.0f, 6.0f, px);
        CHECK(fabsf(px[3] - 1.0f) < 1e-3f, "square interior evaluates opaque");
    }

    // control: the EXISTING quad budget rejects a 600-line contour cleanly, nothing committed
    static AnoUiPathSeg zig[ZIG_LINES + 1];
    zig[0] = (AnoUiPathSeg){ ANO_UI_SEG_MOVE, { 0.0f, 0.0f } };
    for (uint32_t i = 1; i <= ZIG_LINES; i++)
        zig[i] = (AnoUiPathSeg){ ANO_UI_SEG_LINE, { (float)i, (float)(i & 1u) } };
    uint32_t zg = ano_ui_path_fill(&b, zig, ZIG_LINES + 1, red, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    CHECK(zg == ANO_UI_REF_NONE, "over-budget quad count rejected");
    CHECK(b.primCount == 1 && b.curveCount == 9, "builder untouched by quad-budget rejection");

    // trigger: one real triangle (so qn > 0 and the bbox is valid, forcing the emit pass to run)
    // followed by TRIGGER_EMPTY_MOVES empty contours. Correct baker: cn is bounded, extra empty
    // contours yield ANO_UI_REF_NONE, no overrun. Buggy baker: cstart[cn++] runs off the array
    // into q[] and the emit pass reads back corrupted geometry 〜 access violation here.
    uint32_t nseg = 4u + TRIGGER_EMPTY_MOVES;
    AnoUiPathSeg *segs = malloc((size_t)nseg * sizeof *segs);
    CHECK(segs != NULL, "trigger buffer allocated");
    if (segs != NULL) {
        segs[0] = (AnoUiPathSeg){ ANO_UI_SEG_MOVE, {  0.0f,  0.0f } };
        segs[1] = (AnoUiPathSeg){ ANO_UI_SEG_LINE, { 20.0f,  0.0f } };
        segs[2] = (AnoUiPathSeg){ ANO_UI_SEG_LINE, { 20.0f, 20.0f } };
        segs[3] = (AnoUiPathSeg){ ANO_UI_SEG_LINE, {  0.0f, 20.0f } };
        for (uint32_t i = 0; i < TRIGGER_EMPTY_MOVES; i++)
            segs[4 + i] = (AnoUiPathSeg){ ANO_UI_SEG_MOVE,
                                          { (float)(i & 7u), (float)((i >> 3) & 7u) } };
        printf("trigger: triangle + %u empty contours (must return, not overrun cstart)\n",
               (unsigned)TRIGGER_EMPTY_MOVES);
        fflush(stdout);
        uint32_t mv = ano_ui_path_fill(&b, segs, nseg, red,
                                       ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
        // Reached only once the bound lands: any non-crashing outcome is acceptable.
        CHECK(mv == ANO_UI_REF_NONE || b.primCount >= 1, "trigger returned without corruption");
        free(segs);
    }

    // trigger 2: the deterministic crash window. Fresh builder per K, big curve scratch so the
    // emit pass outlives capacity rejection and walks the sprayed cstart into wild q[] reads.
    static uint32_t bigCurves[BIG_CURVE_CAP];
    static AnoUiPathSeg segs2[4u + CRASH_K_HI];
    segs2[0] = (AnoUiPathSeg){ ANO_UI_SEG_MOVE, {  0.0f,  0.0f } };
    segs2[1] = (AnoUiPathSeg){ ANO_UI_SEG_LINE, { 20.0f,  0.0f } };
    segs2[2] = (AnoUiPathSeg){ ANO_UI_SEG_LINE, { 20.0f, 20.0f } };
    segs2[3] = (AnoUiPathSeg){ ANO_UI_SEG_LINE, {  0.0f, 20.0f } };
    for (uint32_t k = CRASH_K_LO; k <= CRASH_K_HI; k++) {
        AnoUiPrim prims2[4];
        AnoUiBuilder b2;
        ano_ui_builder_init(&b2, prims2, 4, NULL, 0, NULL, 0, NULL, 0);
        ano_ui_builder_curves(&b2, bigCurves, BIG_CURVE_CAP);
        for (uint32_t i = 0; i < k; i++)
            segs2[4 + i] = (AnoUiPathSeg){ ANO_UI_SEG_MOVE,
                                           { (float)(i & 7u), (float)((i >> 3) & 7u) } };
        printf("trigger 2: triangle + %u empty contours, cap %u (must return, not fault)\n",
               (unsigned)k, (unsigned)BIG_CURVE_CAP);
        fflush(stdout);
        uint32_t mv2 = ano_ui_path_fill(&b2, segs2, 4u + k, red,
                                        ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
        CHECK(mv2 == ANO_UI_REF_NONE || b2.primCount >= 1, "trigger 2 returned without corruption");
    }

    if (failures) {
        printf("anotest_uipathguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_uipathguard: all passed\n");
    return 0;
}
