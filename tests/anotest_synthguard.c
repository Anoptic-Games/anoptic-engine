/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_synth_score_event field-range guard. AnoNoteEvent documents pitch as MIDI
// 0..127 and velocity as 1..127 (anoptic_music.h), and the guard is the validation gate 〜
// it already rejects dur <= 0, layer >= COUNT, and velocity == 0 〜 but it checks no upper
// bound on velocity and no bound at all on pitch (docs/BUGS.md, Synth / Implementation,
// ano_synth.c:246). An accepted velocity 200 renders at powf(200/127, 1.5) ≈ 1.98, double
// the contract's amplitude ceiling; an accepted pitch 130 lands in merge_ties' chains keyed
// on pitch & 0x7F, aliasing pitch 2's tie chain and silently merging two different-pitch
// notes into one. Controls pin the live half of the gate (a sane event passes; velocity 0
// and dur 0 are rejected) so a reject-everything fix cannot pass. Offline score API only:
// no device, no generator, deterministic. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_synth.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    AnoSynth *syn = ano_synth_create(NULL);
    CHECK(syn != NULL, "synth created with defaults");
    if (!syn) return 1;

    // documented order: begin -> tempo -> bar -> events
    CHECK(ano_synth_score_begin(syn, 4.0, 1, 1, 8), "score_begin accepted");
    CHECK(ano_synth_score_tempo(syn, 0.0, 120.0), "score_tempo accepted");
    AnoMusicalParams params = { .tempoBpm = 120.0 };
    AnoMusicAffect affect = { 0 };
    CHECK(ano_synth_score_bar(syn, 0, &params, &affect), "score_bar accepted");

    // control: a contract-clean event passes (the gate is live, not reject-everything)
    AnoNoteEvent ok = { .start = 0.0, .dur = 1.0, .pitch = 60,
                        .velocity = 100, .layer = ANO_MUSIC_MELODY, .tie = ANO_MUSIC_TIE_NONE };
    CHECK(ano_synth_score_event(syn, &ok), "in-range event accepted");

    // control: the bounds the gate DOES enforce still reject
    AnoNoteEvent vel0 = ok;
    vel0.velocity = 0;
    CHECK(!ano_synth_score_event(syn, &vel0), "velocity 0 rejected (existing lower bound)");
    AnoNoteEvent dur0 = ok;
    dur0.dur = 0.0;
    CHECK(!ano_synth_score_event(syn, &dur0), "dur 0 rejected (existing guard)");

    // trigger: velocity above the documented 1..127 must be rejected, not staged to render
    // at ~2x the amplitude ceiling
    AnoNoteEvent hot = ok;
    hot.velocity = 200;
    CHECK(!ano_synth_score_event(syn, &hot), "velocity 200 rejected as out of contract");

    // trigger: pitch above MIDI 127 must be rejected, not staged to alias pitch & 0x7F
    // (130 & 0x7F == 2) in the tie chains
    AnoNoteEvent high = ok;
    high.pitch = 130;
    CHECK(!ano_synth_score_event(syn, &high), "pitch 130 rejected as out of contract");

    ano_synth_destroy(syn);

    if (failures) {
        printf("anotest_synthguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_synthguard: all passed\n");
    return 0;
}
