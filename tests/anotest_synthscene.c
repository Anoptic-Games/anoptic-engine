/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Phase 4 live scene (audible by hand): the journey-demo fixture playing in
 * REALTIME through the full stack — synth generator on the mixer thread,
 * music console (strips -> sends -> returns -> glue master), device backend.
 * The logic thread paces the per-bar console automation against telemetry,
 * exactly how a game would drive it. You should hear the calm explore theme
 * darken through threat into combat and resolve to calm. Asserts only what
 * holds on ANY backend (CI cascades to null): mixer heartbeat, the synth
 * becoming audible after transport start, and a clean wind-down.
 * ANO_AUDIO_BACKEND=pipewire|alsa|null pins the backend. argv[1] caps the
 * scene length in seconds (default 30; 0 = play the whole piece).
 * Exit 0 == pass. */

#include <stdio.h>
#include <stdlib.h>

#include <anoptic_audio.h>
#include <anoptic_synth.h>
#include <anoptic_time.h>

#include "templates/synthfix.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RATE 48000u

#ifndef ANO_FIXTURE_DIR
#define ANO_FIXTURE_DIR "."
#endif

typedef bool (*telem_pred)(const AnoAudioTelemetry *t);
static bool wait_telemetry(AnoAudioBridge *b, telem_pred pred, uint32_t timeoutMs)
{
    uint32_t start = ano_timestamp_ms();
    for (;;) {
        AnoAudioTelemetry t;
        if (ano_audio_acquire_telemetry(b, &t) && pred(&t))
            return true;
        if (ano_timestamp_ms() - start > timeoutMs)
            return false;
        ano_sleep(5000);
    }
}

static bool pred_heartbeat(const AnoAudioTelemetry *t) { return t->blockIndex >= 3u; }
static bool pred_audible(const AnoAudioTelemetry *t)   { return t->masterPeak > 0.02f; }

static void must_submit(AnoAudioBridge *b, const AnoAudioCommand *c)
{
    while (!ano_audio_submit(b, c))
        ano_sleep(1000);
}

int main(int argc, char **argv)
{
    uint32_t seconds = 30;
    if (argc > 1)
        seconds = (uint32_t)atoi(argv[1]); // 0 = full piece

    AnoSynth *syn = ano_synth_create(&(AnoSynthDesc){ .sampleRate = RATE });
    CHECK(syn != NULL, "synth world up");
    if (!syn) return 1;
    CHECK(synthfix_load(syn, ANO_FIXTURE_DIR "/journey_s42.anofix"), "journey fixture loads");

    AnoAudioBusDesc layout[ANO_SYNTH_CONSOLE_BUSES];
    uint32_t busCount = ano_synth_console_layout(layout, ANO_SYNTH_CONSOLE_BUSES);
    CHECK(busCount == ANO_SYNTH_CONSOLE_BUSES, "console layout");

    AnoAudioConfig cfg = {
        .sampleRate = RATE, .busCount = busCount, .busLayout = layout,
        .generator = ano_synth_generator, .generatorUser = syn,
    };
    CHECK(ano_audio_init(&cfg), "audio world up (music console + synth)");
    AnoAudioBridge *b = anoAudioBridge();
    CHECK(b != NULL, "bridge valid");
    if (!b) return 1;
    CHECK(wait_telemetry(b, pred_heartbeat, 3000), "mixer heartbeat");

    // console setup + per-bar automation, score-relative frames
    static AnoAudioOfflineEvent evts[64u + 80u * 9u];
    uint32_t evtCount = ano_synth_console_setup(evts, 64);
    evtCount += ano_synth_console_automation(syn, evts + evtCount,
                                             (uint32_t)(sizeof evts / sizeof *evts) - evtCount);
    CHECK(evtCount > 64u, "automation emitted");

    // setup lands before the transport starts
    uint32_t cursor = 0;
    while (cursor < evtCount && evts[cursor].frame == 0u)
        must_submit(b, &evts[cursor++].cmd);

    // start the score a few blocks out, then pace automation ~0.5 s ahead
    AnoAudioTelemetry t;
    CHECK(ano_audio_acquire_telemetry(b, &t), "telemetry frame");
    uint64_t transport = (t.blockIndex + 8u) * t.blockFrames;
    ano_synth_transport_start(syn, transport);

    uint64_t scoreFrames = ano_synth_score_frames(syn, 2.5f);
    uint64_t playFrames  = seconds ? (uint64_t)seconds * RATE : scoreFrames;
    if (playFrames > scoreFrames) playFrames = scoreFrames;

    CHECK(wait_telemetry(b, pred_audible, 5000), "synth audible after transport start");

    uint32_t lastInfoBar = UINT32_MAX;
    for (;;) {
        if (!ano_audio_acquire_telemetry(b, &t))
            break;
        uint64_t playhead = t.blockIndex * t.blockFrames;
        uint64_t scoreF = playhead > transport ? playhead - transport : 0u;
        if (scoreF >= playFrames)
            break;
        while (cursor < evtCount
               && evts[cursor].frame <= scoreF + RATE / 2u)
            must_submit(b, &evts[cursor++].cmd);
        uint32_t bar = (uint32_t)((double)scoreF / RATE
                                  / ano_synth_time_at(syn, 4.0) + 0.001);
        if (bar != lastInfoBar && bar % 8u == 0u) {
            lastInfoBar = bar;
            printf("info: ~bar %u │ peak %.3f │ underruns %u\n",
                   bar + 1u, (double)t.masterPeak, t.underruns);
        }
        ano_sleep(20000);
    }

    ano_synth_transport_stop(syn);
    ano_sleep(50000); // let the mixer pass the stop and the tails ring down

    if (ano_audio_acquire_telemetry(b, &t))
        printf("info: scene — blocks %llu, cpu %llu ns/block, underruns %u, clipped %u, dropped %u\n",
               (unsigned long long)t.blockIndex, (unsigned long long)t.blockCpuNs,
               t.underruns, t.clippedSamples, ano_synth_dropped(syn));

    ano_audio_shutdown();
    ano_synth_destroy(syn);

    if (failures) {
        printf("anotest_synthscene: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_synthscene: all passed (%u s)\n", seconds);
    return 0;
}
