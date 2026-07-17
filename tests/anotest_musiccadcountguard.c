/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the cadence-cycle COUNT domain at the public config seam. AnoMusicConfig carries
// cadencePolicies[8] plus cadencePolicyCount (anoptic_music.h:388-389), expand copies the count
// unvalidated (music_host.c:66, docs/BUGS.md, Music / Interface-level) while its sibling
// motifLibraryCount is clamped to ANO_SIG_MAX a few lines below (:101), and ano_engine_init
// adopts the config wholesale (music_conductor.c:702). policy_of's explicit-cycle arm then
// indexes cadencePolicies[phrase % (int)count] (music_conductor.c:147), so any count > 8 reads
// past the int8_t [8] array: count 12 makes phrase 8 read the count field's own low byte (12)
// as the policy 〜 outside AnoCadencePolicy 〜 and counts >= 2^31 flip the (int) cast negative
// so the modulo returns the raw phrase, an index that grows without bound as the piece runs.
// The out-of-enum value rides the same one-sided (!= NONE) guard chain anotest_musiccadenceguard
// documents (CADENCE_TARGET at music_harmony.c:119, DEGS at music_melody.c:517, ARRIVE/APPROACH/
// PNAME at music_verify.c:752, ROMAN NULL-deref segfault territory) and is republished to
// gameplay in AnoMusicMeaning.cadencePolicy against its documented AnoCadencePolicy contract
// (anoptic_music.h:454) 〜 that public field is what the CHECKs assert on. Controls prove a
// legal count-4 cycle lands per phrase and the full-capacity count 8 stays accepted with every
// slot read, so a fix that rejects explicit cycles wholesale 〜 or clamps below the array they
// fit in 〜 cannot pass. The trigger walks 80 bars (8-bar phrases, clock features off, so
// phrase 8's pre-cadence/cadence bars sit at 70/71 regardless of RNG); a crash inside the
// policy tables IS the recorded failure, and if it survives, the out-of-enum meaning CHECKs
// fail. Exit 0 == pass.

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
    int    offPlan;      // flagged bars not matching cycle[phrase % count] (checkPlan only)
    int    outOfEnum;    // policies outside [NONE, DECEPTIVE]
    int8_t firstBad;     // first out-of-enum value seen
} Walk;

// Walk `bars` bars on the default static path (8-bar phrases, dramaturg off, melody layer added
// so the cadence formula 〜 the DEGS sink 〜 runs on every cadence bar) with an explicit cadence
// cycle: cycle[0..7] fills cadencePolicies, `count` goes in verbatim. checkPlan compares each
// flagged bar against cycle[(bar / 8) % count] 〜 only meaningful for legal counts <= 8.
static Walk run_walk(const int8_t cycle[8], uint32_t count, int bars, bool checkPlan)
{
    static AnoMusicBar bar; // ~9 KB: keep it off the walk's frame
    Walk w = { .firstBad = 0 };

    AnoMusicConfig cfg = ano_music_config_default();
    cfg.params.layersActive |= (uint8_t)(1u << ANO_MUSIC_MELODY);
    for (int i = 0; i < 8; ++i)
        cfg.cadencePolicies[i] = cycle[i];
    cfg.cadencePolicyCount = count;
    AnoMusicEngine *e = ano_music_create(&cfg, 42);
    if (!e) {
        printf("FAIL: engine creation (%s:%d)\n", __FILE__, __LINE__);
        failures++;
        return w;
    }

    for (int b = 0; b < bars; ++b) {
        ano_music_advance_bar(e, &bar);
        int8_t p = bar.meaning.cadencePolicy;
        if (bar.meaning.isCadence)
            w.cadenceBars++;
        if (p != ANO_CADENCE_NONE) {
            w.flaggedBars++;
            if (checkPlan && p != cycle[(bar.meaning.bar / 8) % (int)count])
                w.offPlan++;
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

    // control: a legal 4-policy cycle 〜 cadence bars occur, every flagged bar reports the
    // planned policy for its phrase, nothing out of enum; a fix that drops or refuses explicit
    // cycles wholesale cannot pass
    static const int8_t CYCLE4[8] = {
        ANO_CADENCE_HALF, ANO_CADENCE_DECEPTIVE, ANO_CADENCE_HALF, ANO_CADENCE_AUTHENTIC,
        ANO_CADENCE_AUTHENTIC, ANO_CADENCE_AUTHENTIC, ANO_CADENCE_AUTHENTIC, ANO_CADENCE_AUTHENTIC,
    };
    Walk w = run_walk(CYCLE4, 4, 32, true);
    CHECK(w.cadenceBars >= 2, "count 4: explicit cycle reaches cadence bars in 32 bars");
    CHECK(w.flaggedBars >= 2, "count 4: cadence/pre-cadence bars are flagged");
    CHECK(w.offPlan == 0, "count 4: every flagged bar reports its phrase's planned policy");
    CHECK(w.outOfEnum == 0, "count 4: only in-enum cadence policies reported");

    // control (future-fix invariant): count 8 fills the array exactly 〜 all eight slots are
    // read in 64 bars and stay in enum, so a fix that clamps or rejects the full capacity
    // cannot pass
    static const int8_t CYCLE8[8] = {
        ANO_CADENCE_AUTHENTIC, ANO_CADENCE_HALF, ANO_CADENCE_DECEPTIVE, ANO_CADENCE_HALF,
        ANO_CADENCE_AUTHENTIC, ANO_CADENCE_DECEPTIVE, ANO_CADENCE_HALF, ANO_CADENCE_AUTHENTIC,
    };
    w = run_walk(CYCLE8, 8, 64, true);
    CHECK(w.cadenceBars >= 2, "count 8: full-capacity cycle reaches cadence bars in 64 bars");
    CHECK(w.offPlan == 0, "count 8: every flagged bar reports its phrase's planned policy");
    CHECK(w.outOfEnum == 0, "count 8: only in-enum cadence policies reported");

    // trigger: count 12 with a fully legal array 〜 phrases 0..7 read real slots, phrase 8
    // indexes cadencePolicies[8] past the array (the count's own low byte, 12) and phrases
    // 9..11 read its upper bytes; clamping the count or rejecting it at the seam both fix
    // this, so the plan itself is deliberately NOT asserted here 〜 only the enum domain of
    // the republished meaning. today the walk either faults in the [3]-sized policy tables
    // (the crash is the recorded failure) or reports 12 on phrase 8's flagged bars
    static const int8_t ALL_HALF[8] = {
        ANO_CADENCE_HALF, ANO_CADENCE_HALF, ANO_CADENCE_HALF, ANO_CADENCE_HALF,
        ANO_CADENCE_HALF, ANO_CADENCE_HALF, ANO_CADENCE_HALF, ANO_CADENCE_HALF,
    };
    printf("trigger: cadencePolicyCount 12 walk (80 bars, poison phrase 8)\n");
    w = run_walk(ALL_HALF, 12, 80, false);
    CHECK(w.cadenceBars >= 2, "count 12: cadence machinery must not vanish");
    if (w.outOfEnum)
        printf("  (count 12 walk reported out-of-enum cadencePolicy %d on %d bars)\n",
               (int)w.firstBad, w.outOfEnum);
    CHECK(w.outOfEnum == 0, "count 12: meaning.cadencePolicy stays a valid AnoCadencePolicy");

    if (failures) {
        printf("anotest_musiccadcountguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musiccadcountguard: all passed\n");
    return 0;
}
