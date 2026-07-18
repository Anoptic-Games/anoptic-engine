/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: music -> synth seam event contract. AnoNoteEvent documents velocity 1..127
// (anoptic_music.h:45); the synth renders amp = powf(velocity/127, 1.5) trusting it
// (synth_voices.c:353). The arp lane clamps velocity to 127 THEN adds its +4 slot accent
// with no re-clamp (music_arp.c:87/106, header admits "no re-clamp"), so any reachable
// velocityCenter >= 140 makes every accented arp slot emit 128..131. The public control
// plane delivers exactly that: ano_music_set_override("velocity_center", v) stores the
// raw double (music_host.c:183, no domain check; ACMD_MUSIC_OVERRIDE feeds it verbatim
// at ano_synth.c:703) and mapped_params uses it unclamped (music_conductor.c:599/607).
// The host copies event cores into AnoMusicBar untouched (music_host.c:233) and the
// synth's one-sided guard (velocity == 0 only, ano_synth.c:246/429) stages the hot
// event 〜 amp lands ~4.8% above the ceiling any legal velocity can produce. Controls
// pin the clean half (default dynamics stay in range across the same span, arp lane
// live) so a mute-the-arp fix cannot pass. Trigger drives the composer with the
// override and walks every event across the real seam APIs, asserting the documented
// ranges and that the synth staged no out-of-contract event. Deterministic: fixed
// seed, offline score API, no device, no threads. argv[1] scales bars for a soak.
// Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <anoptic_music.h>
#include <anoptic_synth.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define GUARD_SEED 42ull
#define GUARD_BARS 16u

typedef struct Tally
{
    uint32_t arpEvents;   // arp-lane events seen (non-vacuity)
    uint32_t maxVelocity; // max velocity crossing the seam
    uint32_t maxPitch;    // max pitch crossing the seam
    uint32_t hotStaged;   // out-of-contract events the synth ACCEPTED
    bool     plumbingOk;  // score API calls all succeeded
} Tally;

// Inputs: bar span, hot flag (pin velocity_center 150 via the public override), *out tally.
// Drives the real seam: ano_music_advance_bar produces, ano_synth_score_* consumes 〜 the
// same AnoMusicBar payload music_pump hands ano_synth_live_bar (identical guard shape).
static void drive_seam(uint32_t bars, bool hot, Tally *out)
{
    *out = (Tally){ .plumbingOk = true };

    // mapper path, energy 0.95: every gated layer on from bar 0 (arp gate 0.62)
    AnoMusicConfig cfg = ano_music_config_default();
    cfg.hasMapper = true;
    cfg.mapper = ano_mapping_table_default();
    cfg.energy = 0.95f;
    AnoMusicEngine *eng = ano_music_create(&cfg, GUARD_SEED);
    if (hot)
        ano_music_set_override(eng, "velocity_center", 150.0); // ACMD_MUSIC_OVERRIDE shape

    AnoSynth *syn = ano_synth_create(NULL);
    double barQ = ano_music_bar_quarters(eng);
    out->plumbingOk &= ano_synth_score_begin(syn, barQ, bars,
                                             bars * ANO_MUSIC_MAX_TEMPO,
                                             bars * ANO_MUSIC_MAX_BAR_EVENTS);

    AnoMusicBar *bar = malloc(sizeof *bar);
    for (uint32_t b = 0; b < bars; ++b) {
        ano_music_advance_bar(eng, bar);
        for (uint32_t t = 0; t < bar->tempoCount; ++t)
            out->plumbingOk &= ano_synth_score_tempo(syn, bar->tempo[t].beat,
                                                     bar->tempo[t].bpm);
        out->plumbingOk &= ano_synth_score_bar(syn, b, &bar->params, &bar->affect);
        for (uint32_t i = 0; i < bar->eventCount; ++i) {
            const AnoNoteEvent *ev = &bar->events[i];
            if (ev->layer == ANO_MUSIC_ARP)
                out->arpEvents++;
            if (ev->velocity > out->maxVelocity)
                out->maxVelocity = ev->velocity;
            if (ev->pitch > out->maxPitch)
                out->maxPitch = ev->pitch;
            bool staged = ano_synth_score_event(syn, ev);
            bool legal = ev->start >= 0.0 && ev->dur > 0.0 && ev->pitch <= 127u
                      && ev->velocity >= 1u && ev->velocity <= 127u
                      && ev->layer < ANO_MUSIC_LAYER_COUNT;
            if (staged && !legal)
                out->hotStaged++;
        }
    }
    out->plumbingOk &= ano_synth_score_end(syn);

    free(bar);
    ano_synth_destroy(syn);
    ano_music_destroy(eng);
}

int main(int argc, char **argv)
{
    uint32_t scale = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 1u;
    if (scale < 1u)
        scale = 1u;
    uint32_t bars = GUARD_BARS * scale;

    // control: default dynamics 〜 the same span, config, and seam stay in contract
    Tally clean;
    drive_seam(bars, false, &clean);
    CHECK(clean.plumbingOk, "control: score API plumbing");
    CHECK(clean.arpEvents > 0u, "control: arp lane live (gate open at energy 0.95)");
    CHECK(clean.maxVelocity <= 127u && clean.maxPitch <= 127u,
          "control: default-dynamics events honor AnoNoteEvent ranges");
    CHECK(clean.hotStaged == 0u, "control: no out-of-contract event staged");

    // trigger: public velocity_center override 150 〜 arp clamps to 127 then adds
    // its +4 accent, and the seam carries 131 into the schedule
    Tally hotT;
    drive_seam(bars, true, &hotT);
    CHECK(hotT.plumbingOk, "trigger: score API plumbing");
    CHECK(hotT.arpEvents > 0u, "trigger: arp lane live");
    printf("seam max velocity: clean %u, hot %u (contract 1..127)\n",
           clean.maxVelocity, hotT.maxVelocity);
    CHECK(hotT.maxVelocity <= 127u,
          "composer events crossing the seam honor AnoNoteEvent velocity 1..127");
    CHECK(hotT.hotStaged == 0u,
          "synth stages no out-of-contract composer event");

    if (failures) {
        printf("anotest_musicsynthguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musicsynthguard: all passed\n");
    return 0;
}
