/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_synth_score_tempo schedule-state guard. The header documents the load order
// (begin -> tempo -> bars -> events -> end, anoptic_synth.h:80) and every sibling entry point
// rejects an out-of-order call with false: score_bar and score_event through their zero-cap
// checks, score_end through barCount, live_bar through !live, score_frames/time_at through
// scoreReady. score_tempo alone validates only bpm and hands straight to clock_add
// (ano_synth.c:227), which unconditionally dereferences the last anchor (:157) 〜 on a fresh
// synth anchors is NULL and anchorMask 0, so the one misuse the bool return exists to report
// is a deterministic NULL deref instead (docs/BUGS.md, Synth / Interface-level). Controls
// pin the happy path (proper order accepts, non-monotonic beat and bpm 0 reject) and the
// sibling reject convention on the same fresh synth, printed and flushed before the trigger
// since the crash swallows buffered FAIL lines. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_synth.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // control synth: the documented order works end to end
    AnoSynth *a = ano_synth_create(NULL);
    CHECK(a != NULL, "control synth created");
    if (!a) return 1;
    CHECK(ano_synth_score_begin(a, 4.0, 1, 2, 4), "score_begin accepted");
    CHECK(ano_synth_score_tempo(a, 0.0, 120.0), "in-order score_tempo accepted");
    CHECK(ano_synth_score_tempo(a, 2.0, 90.0), "second monotonic tempo point accepted");
    CHECK(!ano_synth_score_tempo(a, 1.0, 100.0), "non-monotonic beat rejected");
    CHECK(!ano_synth_score_tempo(a, 3.0, 0.0), "bpm 0 rejected");

    // fresh synth: the sibling entry points all report the misuse as false
    AnoSynth *b = ano_synth_create(NULL);
    CHECK(b != NULL, "fresh synth created");
    if (!b) return 1;
    AnoMusicalParams params = { .tempoBpm = 120.0 };
    AnoMusicAffect affect = { 0 };
    CHECK(!ano_synth_score_bar(b, 0, &params, &affect), "score_bar before begin returns false");
    AnoNoteEvent ev = { .start = 0.0, .dur = 1.0, .pitch = 60,
                        .velocity = 100, .layer = ANO_MUSIC_MELODY, .tie = ANO_MUSIC_TIE_NONE };
    CHECK(!ano_synth_score_event(b, &ev), "score_event before begin returns false");
    CHECK(!ano_synth_score_end(b), "score_end before begin returns false");

    printf("controls done: %d failure(s); triggering score_tempo before begin\n", failures);
    fflush(stdout);

    // trigger: the one sibling without the state guard. Must report false like the others;
    // today clock_add dereferences the NULL anchor array and the process dies here.
    CHECK(!ano_synth_score_tempo(b, 0.0, 120.0), "score_tempo before begin returns false");

    ano_synth_destroy(b);
    ano_synth_destroy(a);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
