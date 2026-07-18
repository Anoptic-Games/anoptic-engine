/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_text_measure_runs trailing no-op run guard. anoptic_text.h:113 promises
// "byteCount 0 is a no-op", but shape_core (text_shape.c:126) sets the final line step from
// runs[runCount-1].sizePx unconditionally, and measure_runs (text_shape.c:195) returns
// penY + that step. A trailing byteCount-0 run is simultaneously "a no-op" and "the last run",
// so its sizePx sets the measured height even though it styles nothing (docs/BUGS.md, Text /
// Interface). The bug is isolated to height: penY only moves on '\n', so single-line text with
// out-of-bake codepoints keeps penY == 0 and the whole height IS the endStep term.
//
// No font, no FreeType, no window: a stack AnoFontBake with rangeCount 0 makes every codepoint
// an out-of-bake gap (ano_text_bake_slot returns SLOT_NONE without touching the NULL arrays),
// so measure_runs advances the pen and computes height purely from lineHeight and the runs.
// Controls first: the single-run measure pins the correct height, and a LEADING no-op run
// (the twin the shaper DOES handle transparently) measures identically 〜 so a fix that just
// rejected no-op runs, or disabled endStep, cannot pass. The trigger is the trailing no-op.
// Deterministic float ops, exact compare. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_text.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // Minimal valid bake: no ranges -> every codepoint is an out-of-bake gap. The NULL
    // points/glyphs/ranges/kerns arrays are never dereferenced on the gap path.
    AnoFontBake bake = { 0 };
    bake.lineHeight = 1.0f; // em per line; keeps the arithmetic a clean multiple of size

    const anostr_t text = anostr_lit("AA"); // two single-line, out-of-bake codepoints

    // control: one run styles the whole string. Single line -> height is exactly lineHeight*size.
    AnoTextRun single[1] = { { .byteCount = 2, .sizePx = 32.0f, .color = { 0 } } };
    float wSingle = -1.0f, hSingle = -1.0f;
    ano_text_measure_runs(&bake, text, single, 1, &wSingle, &hSingle);
    CHECK(hSingle == 32.0f, "single run: one line measures lineHeight * sizePx");

    // control: a LEADING no-op run (byteCount 0) must be transparent. The shaper skips it, so
    // this matches the single-run measure today 〜 the no-op contract holds on the front edge.
    AnoTextRun lead[2] = {
        { .byteCount = 0, .sizePx = 64.0f, .color = { 0 } },
        { .byteCount = 2, .sizePx = 32.0f, .color = { 0 } },
    };
    float wLead = -1.0f, hLead = -1.0f;
    ano_text_measure_runs(&bake, text, lead, 2, &wLead, &hLead);
    CHECK(hLead == hSingle, "leading no-op run does not change measured height");
    CHECK(wLead == wSingle, "leading no-op run does not change measured width");

    // trigger: a TRAILING no-op run styles nothing but is runs[runCount-1]; its 64px size must
    // NOT set the final-line height. Contract: byteCount 0 is a no-op, so this equals single.
    AnoTextRun trail[2] = {
        { .byteCount = 2, .sizePx = 32.0f, .color = { 0 } },
        { .byteCount = 0, .sizePx = 64.0f, .color = { 0 } },
    };
    float wTrail = -1.0f, hTrail = -1.0f;
    ano_text_measure_runs(&bake, text, trail, 2, &wTrail, &hTrail);
    CHECK(wTrail == wSingle, "trailing no-op run does not change measured width");
    CHECK(hTrail == hSingle, "trailing no-op run does not change measured height");

    if (failures) {
        printf("anotest_textguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_textguard: all passed\n");
    return 0;
}
