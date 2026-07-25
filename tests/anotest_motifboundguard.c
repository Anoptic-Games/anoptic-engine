/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the authored-motif NOTE COUNT domain at the two config seams. AnoMotif carries
// rhythm[ANO_MOTIF_MAX] and contour[ANO_MOTIF_MAX] alongside a uint32_t n the game authors
// (anoptic_music.h:187-193), and n is the count every transform and realizer iterates while
// writing ANO_MOTIF_MAX-wide buffers 〜 ano_motif_invert (music_motif.c:186), pitches_at
// (:338), ano_realize_faithful (:399). expand() copied the library verbatim into the engine
// config (music_host.c:124) and ano_director_init did the same on the internal AnoEngineConfig
// path (music_signatures.c:19), so an authored n rode straight into those loops on the first
// ano_music_advance_bar: the director selects at every phrase boundary (music_conductor.c:1066)
// and best_transform runs each transform over n. n = ANO_MOTIF_MAX + 1 is a one-past stack
// write; n = 1 << 20 walks a megabyte off the frame. ASan is the oracle for the triggers 〜 a
// fault IS the recorded failure. The control pins that a well-formed 4-note cell still creates,
// still sounds and is still stated, so a fix that refuses or empties authored motifs cannot
// pass; the triggers additionally assert the clamped engine keeps emitting bars rather than
// going silent. Headless, single-threaded, no device. Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>

#include <anoptic_music.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define GUARD_SEED 42ull
#define GUARD_BARS 32

typedef struct Walk
{
    uint64_t notes;         // events summed over the walk
    int      barsWithNotes;
    int      statedBars;    // meaning.motifStated
    bool     created;
} Walk;

// Out: the well-formed 4-note arch cell the scene test authors (anotest_musicscene.c:48-55)
// 〜 slots and durations inside a 4/4 bar, diatonic contour offsets, n == 4.
static AnoMotif hero_motif(void)
{
    static const int RH[8] = { 0, 4, 4, 2, 6, 2, 8, 4 };
    static const int CT[4] = { 0, 2, 1, 0 };
    AnoMotif m = { 0 };
    m.n = 4;
    m.shape = ANO_SHAPE_ARCH;
    for (int i = 0; i < 4; ++i) {
        m.rhythm[i] = (AnoRhythmNote){ RH[2 * i], RH[2 * i + 1] };
        m.contour[i] = CT[i];
    }
    return m;
}

// In: poisonN (0 = keep the authored count, else overwrite motif.n with it), bar count.
// Out: what the walk produced. The cell's first four rhythm/contour entries are always
// well-formed 〜 only the count lies, so the walk isolates the count domain. Leniency 1.0
// and a standing request keep the director's forced arm live at every phrase boundary.
static Walk run_walk(uint32_t poisonN, int bars)
{
    static AnoMusicBar bar; // ~9 KB: keep it off the walk's frame
    Walk w = { 0 };

    AnoMusicConfig cfg = ano_music_config_default();
    cfg.params.layersActive |= (uint8_t)(1u << ANO_MUSIC_MELODY); // the realizers' sink
    cfg.motifLeniency = 1.0;
    AnoMotif hero = hero_motif();
    if (poisonN)
        hero.n = poisonN;
    cfg.motifLibrary[0] = (AnoSignatureMotif){ "hero", hero, 0.9 };
    cfg.motifLibraryCount = 1;

    AnoMusicEngine *e = ano_music_create(&cfg, GUARD_SEED);
    if (!e)
        return w;
    w.created = true;
    for (int b = 0; b < bars; ++b) {
        ano_music_request_motif(e, "hero"); // cleared once honoured; keep it standing
        ano_music_advance_bar(e, &bar);
        w.notes += bar.eventCount;
        if (bar.eventCount)
            w.barsWithNotes++;
        if (bar.meaning.motifStated)
            w.statedBars++;
    }
    ano_music_destroy(e);
    return w;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // the triggers may fault; keep FAIL lines visible

    // control: a well-formed n = 4 cell 〜 the engine builds, composes across the walk, and
    // actually states the authored motif; a fix that rejects authored motifs, or clamps the
    // count below the buffers they fit in, cannot pass
    Walk w = run_walk(0, GUARD_BARS);
    CHECK(w.created, "control: valid motif config creates an engine");
    CHECK(w.notes > 0, "control: valid motif config composes notes");
    CHECK(w.barsWithNotes >= GUARD_BARS / 2, "control: notes keep arriving across the walk");
    CHECK(w.statedBars > 0, "control: the authored motif is stated");

    // trigger: one past the buffers 〜 a single-element overrun in pitches_at /
    // ano_motif_invert / ano_realize_faithful on the first phrase boundary
    printf("trigger: motif.n = %u (ANO_MOTIF_MAX + 1)\n", (unsigned)ANO_MOTIF_MAX + 1u);
    w = run_walk((uint32_t)ANO_MOTIF_MAX + 1u, GUARD_BARS);
    CHECK(w.created, "n = MAX+1: engine still creates");
    CHECK(w.notes > 0, "n = MAX+1: the clamped engine still emits bars, not silence");

    // trigger: a count that runs a megabyte past every ANO_MOTIF_MAX-wide sink
    printf("trigger: motif.n = %u (1 << 20)\n", 1u << 20);
    w = run_walk(1u << 20, GUARD_BARS);
    CHECK(w.created, "n = 1<<20: engine still creates");
    CHECK(w.notes > 0, "n = 1<<20: the clamped engine still emits bars, not silence");

    if (failures) {
        printf("anotest_motifboundguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_motifboundguard: all passed\n");
    return 0;
}
