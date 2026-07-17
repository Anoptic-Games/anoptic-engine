/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the cadence-policy value domain at the public boundary. ano_music_set_override
// ("cadence_policy", v) refuses unknown names but accepts any value, casting (int8_t)v unchecked
// (music_host.c:193, docs/BUGS.md, Music / Interface-level); policy_of returns it verbatim ahead
// of every other source (music_conductor.c:132) and it lands in ctx.cadencePolicy on cadence and
// pre-cadence bars (:1034). Downstream guards are one-sided 〜 only ANO_CADENCE_NONE (-1) is
// excluded 〜 so any value outside {-1..2} indexes the [3]-sized policy tables out of bounds:
// CADENCE_TARGET/PRE_CADENCE_FUNCTION at music_harmony.c:119/:130 inside gen_chord, DEGS[3][2]
// at music_melody.c:517 (behind the != NONE guard at :745), and ARRIVE/APPROACH/PNAME at
// music_verify.c:752/:753/:762. The same raw value is republished to gameplay in
// AnoMusicMeaning.cadencePolicy, whose header contract says AnoCadencePolicy
// (anoptic_music.h:454) 〜 that public field is what the CHECKs assert on. Controls prove
// cadence bars occur and a LEGAL override rides the same path end to end, so a fix that drops
// the override wholesale, or rejects everything, cannot pass. Triggers pin one value past
// DECEPTIVE (3) and one below NONE (-3). Today the first trigger walk SEGFAULTS before any
// CHECK: CADENCE_TARGET[3] reads PRE_CADENCE_FUNCTION's 'D' (68) as the cadence chord degree,
// ano_chord_symbol derefs ROMAN[67] == NULL (music_theory.c:238 via :261) 〜 the crash IS the
// recorded failure; were it to survive, the out-of-enum meaning CHECKs fail instead.
// Deterministic: the override wins before any draw, and the phrase clock (8-bar phrases, clock
// features off) puts cadence slots at bars 7, 15, 23 regardless of RNG. Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>

#include <anoptic_music.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

typedef struct Walk
{
    int    cadenceBars;  // meaning.isCadence
    int    flaggedBars;  // meaning.cadencePolicy != NONE (cadence + pre-cadence)
    int    offPolicy;    // flagged bars not reporting `expect` (expect >= 0 only)
    int    outOfEnum;    // policies outside [NONE, DECEPTIVE]
    int8_t firstBad;     // first out-of-enum value seen
} Walk;

// 24 bars on the default static path (8-bar phrases, dramaturg off, melody layer added so the
// cadence formula 〜 the DEGS sink 〜 actually runs on every cadence bar). hasOverride pins
// cadence_policy to `ov` before the first bar; expect < 0 skips the uniformity count.
static Walk run_walk(bool hasOverride, double ov, int expect)
{
    static AnoMusicBar bar; // ~9 KB: keep it off the walk's frame
    Walk w = { .firstBad = 0 };

    AnoMusicConfig cfg = ano_music_config_default();
    cfg.params.layersActive |= (uint8_t)(1u << ANO_MUSIC_MELODY);
    AnoMusicEngine *e = ano_music_create(&cfg, 42);
    if (!e) {
        printf("FAIL: engine creation (%s:%d)\n", __FILE__, __LINE__);
        failures++;
        return w;
    }
    if (hasOverride)
        ano_music_set_override(e, "cadence_policy", ov);

    for (int b = 0; b < 24; ++b) {
        ano_music_advance_bar(e, &bar);
        int8_t p = bar.meaning.cadencePolicy;
        if (bar.meaning.isCadence)
            w.cadenceBars++;
        if (p != ANO_CADENCE_NONE) {
            w.flaggedBars++;
            if (expect >= 0 && p != (int8_t)expect)
                w.offPolicy++;
        }
        if (p < ANO_CADENCE_NONE || p > ANO_CADENCE_DECEPTIVE) {
            if (w.outOfEnum == 0)
                w.firstBad = p;
            w.outOfEnum++;
        }
    }
    ano_music_destroy(e);
    return w;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // the trigger may fault; keep FAIL lines visible

    // control: no override 〜 cadence bars occur and every reported policy is in the enum
    Walk w = run_walk(false, 0.0, -1);
    CHECK(w.cadenceBars >= 1, "default walk reaches at least one cadence bar in 24 bars");
    CHECK(w.flaggedBars >= 1, "default walk flags at least one cadence/pre-cadence bar");
    CHECK(w.outOfEnum == 0, "default walk reports only in-enum cadence policies");

    // control: a LEGAL override (HALF) rides the same seam end to end and lands verbatim,
    // so a fix that ignores or rejects the override wholesale cannot pass
    w = run_walk(true, (double)ANO_CADENCE_HALF, ANO_CADENCE_HALF);
    CHECK(w.cadenceBars >= 1, "HALF override: cadence bars still occur");
    CHECK(w.flaggedBars >= 1 && w.offPolicy == 0, "HALF override lands on every flagged bar");
    CHECK(w.outOfEnum == 0, "HALF override reports only in-enum cadence policies");

    // trigger: one past DECEPTIVE 〜 must not surface out-of-enum in the public meaning
    // (and must not index the [3] policy tables on the way); refusing or clamping both fix it.
    // today this walk SEGFAULTS: CADENCE_TARGET[3] reads 'D' (68) as the cadence chord degree
    // and ano_chord_symbol derefs ROMAN[67] == NULL 〜 the crash is the recorded failure
    printf("trigger: cadence_policy 3 walk\n");
    w = run_walk(true, 3.0, -1);
    CHECK(w.cadenceBars >= 1, "policy 3: cadence machinery must not vanish");
    if (w.outOfEnum)
        printf("  (policy 3 walk reported out-of-enum cadencePolicy %d on %d bars)\n",
               (int)w.firstBad, w.outOfEnum);
    CHECK(w.outOfEnum == 0, "policy 3: meaning.cadencePolicy stays a valid AnoCadencePolicy");

    // trigger: below NONE 〜 -3 passes every != NONE guard and indexes the tables backwards
    printf("trigger: cadence_policy -3 walk\n");
    w = run_walk(true, -3.0, -1);
    CHECK(w.cadenceBars >= 1, "policy -3: cadence machinery must not vanish");
    if (w.outOfEnum)
        printf("  (policy -3 walk reported out-of-enum cadencePolicy %d on %d bars)\n",
               (int)w.firstBad, w.outOfEnum);
    CHECK(w.outOfEnum == 0, "policy -3: meaning.cadencePolicy stays a valid AnoCadencePolicy");

    if (failures) {
        printf("anotest_musiccadenceguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musiccadenceguard: all passed\n");
    return 0;
}
