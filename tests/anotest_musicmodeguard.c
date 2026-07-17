/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the mode value domain at the public boundary. ano_music_set_override("mode", v)
// refuses unknown names but accepts any value, casting (int)v unchecked (music_host.c:194,
// docs/BUGS.md, Music / Interface-level); the config seam copies cfg.mode just as blind
// (music_host.c:58) and the conductor pins either verbatim 〜 the only value refused is exactly
// ANO_MODE_NONE (music_conductor.c:882-886 per phrase with mapper, :728-731 at init) 〜 then the
// (uint8_t) casts at :734/:892 launder negatives into 0..255 instead of rejecting. Every bar
// hands scale.mode to ano_mode_intervals, whose `return table[mode]` (music_theory.c:58) indexes
// a static [ANO_MODE_COUNT][7] table unchecked: mode 99 hands ano_scale_pcs a pointer ~650 bytes
// past the 49-byte table, mode -3 (laundered to 253) ~1.77 KB past, read seven-wide on the
// composing thread every bar. The raw value is republished to gameplay in AnoMusicMeaning.mode
// against its documented AnoMode contract (anoptic_music.h:451) and rides AEVT_MUSIC_BAR
// engine-wide 〜 that public field is what the CHECKs assert on (the downstream engine HUD at
// main.c:614 survives only because % 7 of a non-negative int happens to land in range). Controls
// prove a LEGAL config mode and a LEGAL override ride the same seams end to end, so a fix that
// drops the seams wholesale, or rejects everything, cannot pass. Deterministic: the pin wins at
// bar 0 phrase pos 0 before any draw. A crash inside the trigger walks is a valid failure signal
// (the OOB table read runs inside ano_music_advance_bar). Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>

#include <anoptic_music.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

typedef struct Walk
{
    int bars;      // bars advanced
    int outOfEnum; // meaning.mode outside [IONIAN, COUNT)
    int offMode;   // bars whose meaning.mode != expect (expect >= 0 only)
    int firstBad;  // first out-of-enum value seen
} Walk;

// 4 bars; cfgMode lands on the config seam, useMapper + hasOverride/ov on the override seam.
// expect >= 0 counts bars that stray from it.
static Walk run_walk(int cfgMode, bool useMapper, bool hasOverride, double ov, int expect)
{
    static AnoMusicBar bar; // ~9 KB: keep it off the walk's frame
    Walk w = { .firstBad = 0 };

    AnoMusicConfig cfg = ano_music_config_default();
    cfg.mode = cfgMode;
    if (useMapper) {
        cfg.hasMapper = true;
        cfg.mapper = ano_mapping_table_default();
    }
    AnoMusicEngine *e = ano_music_create(&cfg, 42);
    if (!e) {
        printf("FAIL: engine creation (%s:%d)\n", __FILE__, __LINE__);
        failures++;
        return w;
    }
    if (hasOverride)
        CHECK(ano_music_set_override(e, "mode", ov), "override name \"mode\" is known");

    for (int b = 0; b < 4; ++b) {
        ano_music_advance_bar(e, &bar);
        int m = bar.meaning.mode;
        w.bars++;
        if (m < ANO_MODE_IONIAN || m >= ANO_MODE_COUNT) {
            if (w.outOfEnum == 0)
                w.firstBad = m;
            w.outOfEnum++;
        }
        if (expect >= 0 && m != expect)
            w.offMode++;
    }
    ano_music_destroy(e);
    return w;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // the trigger may fault; keep FAIL lines visible

    // control: a LEGAL config mode rides the config seam verbatim on the static path
    Walk w = run_walk(ANO_MODE_DORIAN, false, false, 0.0, ANO_MODE_DORIAN);
    CHECK(w.bars == 4 && w.offMode == 0, "config DORIAN lands in meaning.mode on every bar");
    CHECK(w.outOfEnum == 0, "config DORIAN walk reports only in-enum modes");

    // control: a LEGAL override rides the mapper seam end to end and lands verbatim,
    // so a fix that ignores or rejects the override wholesale cannot pass
    w = run_walk((int)ANO_MODE_NONE, true, true, (double)ANO_MODE_LYDIAN, ANO_MODE_LYDIAN);
    CHECK(w.bars == 4 && w.offMode == 0, "override LYDIAN lands in meaning.mode on every bar");
    CHECK(w.outOfEnum == 0, "override LYDIAN walk reports only in-enum modes");

    // trigger: config seam, mode 99 〜 init pins it verbatim (music_conductor.c:731), every bar
    // reads table[99] (music_theory.c:58) and republishes 99 in meaning.mode
    printf("trigger: config mode 99 walk\n");
    w = run_walk(99, false, false, 0.0, -1);
    if (w.outOfEnum)
        printf("  (config 99 walk reported out-of-enum meaning.mode %d on %d bars)\n",
               w.firstBad, w.outOfEnum);
    CHECK(w.outOfEnum == 0, "config 99: meaning.mode stays a valid AnoMode");

    // trigger: override seam, mode 99 〜 accepted by name, pinned at bar 0 phrase pos 0
    printf("trigger: override mode 99 walk\n");
    w = run_walk((int)ANO_MODE_NONE, true, true, 99.0, -1);
    if (w.outOfEnum)
        printf("  (override 99 walk reported out-of-enum meaning.mode %d on %d bars)\n",
               w.firstBad, w.outOfEnum);
    CHECK(w.outOfEnum == 0, "override 99: meaning.mode stays a valid AnoMode");

    // trigger: override seam, mode -3 〜 passes the != ANO_MODE_NONE refusal, the (uint8_t)
    // cast launders it to 253 and table[253] is read ~1.77 KB past the table
    printf("trigger: override mode -3 walk\n");
    w = run_walk((int)ANO_MODE_NONE, true, true, -3.0, -1);
    if (w.outOfEnum)
        printf("  (override -3 walk reported out-of-enum meaning.mode %d on %d bars)\n",
               w.firstBad, w.outOfEnum);
    CHECK(w.outOfEnum == 0, "override -3: meaning.mode stays a valid AnoMode");

    if (failures) {
        printf("anotest_musicmodeguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musicmodeguard: all passed\n");
    return 0;
}
