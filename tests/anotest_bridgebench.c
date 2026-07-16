/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Bench: logic<->mixer bridge hot ops under a live mixer (synth + composer + null
// device). Times every logic-side ano_audio_publish_listener / acquire_telemetry
// call and collects the mixer's own per-block blockCpuNs (deduped by blockIndex).
// Percentile table; a mean cannot falsify a tail. A/B vehicle for lane-layout work.
// DISABLED in ctest; run by hand. argv[1] = soak seconds (default 20, min 5).
// Exit 0 unless the mixer never heartbeats or no blocks are observed.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_audio.h>
#include <anoptic_music.h>
#include <anoptic_synth.h>
#include <anoptic_time.h>

#include "templates/bench.h"
#include "templates/rng.h"

#define RATE 48000u

static int failures = 0;
#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            failures++;                                              \
        }                                                            \
    } while (0)

// blockCpuNs samples are already nanoseconds.
static uint64_t ns_identity(uint64_t v) { return v; }

// Two distinct block indices prove the mixer thread is rendering.
static bool wait_heartbeat(AnoAudioBridge *b, uint32_t ms)
{
    AnoAudioTelemetry t;
    uint64_t first = UINT64_MAX;
    uint32_t start = ano_timestamp_ms();
    while (ano_timestamp_ms() - start < ms) {
        if (ano_audio_acquire_telemetry(b, &t)) {
            if (first == UINT64_MAX)
                first = t.blockIndex;
            else if (t.blockIndex != first)
                return true;
        }
        ano_sleep(1000);
    }
    return false;
}

int main(int argc, char **argv)
{
    uint32_t seconds = 20u;
    if (argc > 1)
        seconds = (uint32_t)atoi(argv[1]);
    if (seconds < 5u)
        seconds = 5u;

    // Composer + host synth, every mixer hook wired: real per-block work.
    AnoMusicConfig cfg = ano_music_config_default();
    cfg.hasMapper = true;
    cfg.mapper    = ano_mapping_table_default();

    AnoSynth *syn = ano_synth_create(&(AnoSynthDesc){ .sampleRate = RATE });
    AnoMusicEngine *music = ano_music_create(&cfg, 31337u);
    CHECK(syn && music, "synth + composer");
    if (!syn || !music)
        return 1;
    CHECK(ano_synth_attach_music(syn, music), "composer drives generator");

    AnoAudioBusDesc layout[ANO_SYNTH_CONSOLE_BUSES];
    uint32_t busCount = ano_synth_console_layout(layout, ANO_SYNTH_CONSOLE_BUSES);
    AnoAudioConfig acfg = {
        .sampleRate = RATE, .busCount = busCount, .busLayout = layout,
        .generator         = ano_synth_generator,
        .generatorUser     = syn,
        .generatorControl  = ano_synth_control,
        .generatorPoll     = ano_synth_poll,
        .generatorStats    = ano_synth_stats,
        .generatorCommands = ano_synth_commands,
    };
    CHECK(ano_audio_init(&acfg), "audio world up");
    AnoAudioBridge *b = anoAudioBridge();
    CHECK(b != NULL, "bridge valid");
    if (!b || failures)
        return 1;
    CHECK(wait_heartbeat(b, 3000), "mixer heartbeat");
    if (failures)
        return 1;

    static AnoAudioOfflineEvent setup[64];
    uint32_t setupCount = ano_synth_console_setup(setup, 64);
    for (uint32_t i = 0; i < setupCount; ++i)
        (void)ano_audio_submit(b, &setup[i].cmd);

    AnoAudioTelemetry t;
    CHECK(ano_audio_acquire_telemetry(b, &t), "telemetry frame");
    ano_synth_transport_start(syn, (t.blockIndex + 8u) * t.blockFrames);

    // ~1 kHz logic tick: acquire + publish every tick, affect every 64th.
    size_t   opCap  = (size_t)seconds * 1200u;
    size_t   blkCap = (size_t)seconds * 200u;
    uint64_t *acqBuf = malloc(opCap * sizeof *acqBuf);
    uint64_t *pubBuf = malloc(opCap * sizeof *pubBuf);
    uint64_t *blkBuf = malloc(blkCap * sizeof *blkBuf);
    CHECK(acqBuf && pubBuf && blkBuf, "sample buffers");
    if (failures)
        return 1;
    bench_lat acq, pub, blk;
    bench_lat_init(&acq, acqBuf, opCap);
    bench_lat_init(&pub, pubBuf, opCap);
    bench_lat_init(&blk, blkBuf, blkCap);

    test_rng rng = rng_make(0xB1D6Eu);
    uint64_t lastBlock = UINT64_MAX;
    uint64_t seq = 0;
    const uint32_t startMs = ano_timestamp_ms();
    const uint32_t runMs   = seconds * 1000u;
    for (uint64_t tick = 0; ano_timestamp_ms() - startMs < runMs; ++tick) {
        uint64_t t0 = bench_begin();
        bool ok = ano_audio_acquire_telemetry(b, &t);
        bench_lat_add(&acq, bench_end(t0));
        if (ok && t.blockIndex != lastBlock) {
            lastBlock = t.blockIndex;
            bench_lat_add(&blk, t.blockCpuNs);
        }

        float a = (float)(tick % 6283u) / 1000.0f; // slow orbit
        AnoAudioListener l = {
            .pos = { 3.0f * sinf(a), 0.0f, 3.0f * cosf(a) },
            .forward = { -sinf(a), 0.0f, -cosf(a) },
            .up = { 0.0f, 1.0f, 0.0f },
            .seq = ++seq,
        };
        t0 = bench_begin();
        ano_audio_publish_listener(b, &l);
        bench_lat_add(&pub, bench_end(t0));

        if (tick % 64u == 0u) {
            AnoAudioCommand affect = { .kind = ACMD_MUSIC_AFFECT,
                .affect = { (float)rng_below(&rng, 1000u) / 1000.0f,
                            (float)rng_below(&rng, 1000u) / 1000.0f,
                            (float)rng_below(&rng, 1000u) / 1000.0f } };
            (void)ano_audio_submit(b, &affect);
        }
        ano_sleep(1000);
    }

    ano_synth_transport_stop(syn);
    ano_sleep(50000); // tails ring down
    ano_audio_shutdown();

    CHECK(blk.n > 0, "mixer blocks observed");

    bench_lat_header();
    bench_lat_row("acquire_telemetry", bench_lat_stats(&acq));
    bench_lat_row("publish_listener", bench_lat_stats(&pub));
    bench_lat_row("mixer blockCpu", bench_lat_stats_with(&blk, ns_identity));

    free(acqBuf); free(pubBuf); free(blkBuf);
    ano_music_destroy(music);
    ano_synth_destroy(syn);
    if (failures) {
        printf("anotest_bridgebench: %d FAILURE(S)\n", failures);
        return 1;
    }
    return 0;
}
