/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Phase 2 live scene (the exit gate, audible by hand): a looping ambient bed
 * anchored in front of the world origin while the listener orbits it, click
 * one-shots firing from random directions, and a lowpass sweeping the SFX bus.
 * You should hear the bed swing around your head, the clicks land at distinct
 * bearings, and the sweep glide with no zipper. Asserts only what holds on ANY
 * backend (CI cascades to null): heartbeat, audibility, every click retiring,
 * the ambient stop, and both sample blocks coming home on release.
 * ANO_AUDIO_BACKEND=pipewire|alsa|null pins the backend. argv[1] scales the
 * scene length in seconds (default 8). Exit 0 == pass. */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <anoptic_audio.h>
#include <anoptic_time.h>

#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RATE 48000u
#define BED_FRAMES   24000u // 0.5 s, loop-clean (integer cycles of both partials)
#define CLICK_FRAMES 2400u

static float g_bed[BED_FRAMES];
static float g_click[CLICK_FRAMES];

static void make_material(void)
{
    test_rng rng = rng_make(0x5CE9Eu);
    for (uint32_t i = 0; i < BED_FRAMES; i++) {
        float t = (float)i / (float)RATE;
        g_bed[i] = 0.45f * sinf(6.2831853f * 110.0f * t)
                 + 0.25f * sinf(6.2831853f * 220.0f * t);
    }
    for (uint32_t i = 0; i < CLICK_FRAMES; i++) {
        float n = ((float)rng_below(&rng, 65536u) / 32768.0f) - 1.0f;
        g_click[i] = n * expf(-(float)i / 300.0f);
    }
}

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
static bool pred_audible(const AnoAudioTelemetry *t)   { return t->masterPeak > 0.01f; }
static bool pred_quiet(const AnoAudioTelemetry *t)     { return t->sourcesActive == 0u; }

// submit with the backpressure contract honored
static void must_submit(AnoAudioBridge *b, const AnoAudioCommand *c)
{
    while (!ano_audio_submit(b, c))
        ano_sleep(1000);
}

int main(int argc, char **argv)
{
    uint32_t seconds = 8;
    if (argc > 1) {
        int s = atoi(argv[1]);
        if (s > 0) seconds = (uint32_t)s;
    }
    make_material();

    CHECK(ano_audio_init(NULL), "audio world up");
    AnoAudioBridge *b = anoAudioBridge();
    CHECK(b != NULL, "bridge valid");
    if (!b) return 1;
    CHECK(wait_telemetry(b, pred_heartbeat, 3000), "mixer heartbeat");

    while (!ano_audio_buffer_register(b, 1, g_bed, BED_FRAMES, 1)) ano_sleep(1000);
    while (!ano_audio_buffer_register(b, 2, g_click, CLICK_FRAMES, 1)) ano_sleep(1000);

    // SFX bus: lowpass insert, swept during the scene
    AnoAudioCommand busSet = { .kind = ACMD_BUS_SET, .bus = 1,
        .fields = ANO_AUDIO_FIELD_CUTOFF | ANO_AUDIO_FIELD_Q | ANO_AUDIO_FIELD_FILTER_MODE,
        .cutoffHz = 8000.0f, .q = 0.8f, .filterMode = ANO_AUDIO_FILTER_LOWPASS };
    must_submit(b, &busSet);

    // the bed: looping, positional, fixed ahead of the origin
    AnoAudioCommand bed = { .kind = ACMD_SOURCE_PLAY, .source_id = 100,
        .desc = { .kind = ANO_AUDIO_SOURCE_BUFFER, .buffer_id = 1, .bus = 1, .gain = 0.5f,
                  .flags = ANO_AUDIO_SOURCE_LOOP | ANO_AUDIO_SOURCE_POSITIONAL,
                  .position = { 0.0f, 0.0f, -3.0f }, .minDist = 1.5f } };
    must_submit(b, &bed);
    CHECK(wait_telemetry(b, pred_audible, 2000), "bed audible");

    // orbit the listener twice around the origin; clicks from random bearings
    test_rng rng = rng_make(0x0Bu);
    const uint32_t stepMs = 20;
    const uint32_t steps = seconds * 1000u / stepMs;
    uint32_t clicksFired = 0, clicksRetired = 0, nextClickId = 200;
    for (uint32_t i = 0; i < steps; i++) {
        float th = 2.0f * 6.2831853f * (float)i / (float)steps; // two orbits
        AnoAudioListener l = {
            .pos = { 4.0f * sinf(th), 0.0f, 4.0f * cosf(th) },
            .forward = { -sinf(th), 0.0f, -cosf(th) }, // facing the origin
            .up = { 0.0f, 1.0f, 0.0f },
            .seq = i,
        };
        ano_audio_publish_listener(b, &l);

        if (i % (400u / stepMs) == 0u) { // a click every 400 ms
            float a = (float)rng_below(&rng, 6283u) / 1000.0f;
            AnoAudioCommand click = { .kind = ACMD_SOURCE_PLAY, .source_id = nextClickId++,
                .desc = { .kind = ANO_AUDIO_SOURCE_BUFFER, .buffer_id = 2, .bus = 1, .gain = 0.7f,
                          .flags = ANO_AUDIO_SOURCE_POSITIONAL,
                          .position = { 5.0f * sinf(a), 0.0f, 5.0f * cosf(a) },
                          .rate = 0.8f + 0.05f * (float)rng_below(&rng, 9u) } };
            must_submit(b, &click);
            clicksFired++;
        }
        if (i % (500u / stepMs) == 0u) { // sweep the lowpass, glide between retargets
            float ph = 6.2831853f * (float)i / (float)steps;
            AnoAudioCommand sweep = { .kind = ACMD_BUS_SET, .bus = 1,
                .fields = ANO_AUDIO_FIELD_CUTOFF,
                .cutoffHz = 800.0f + 7200.0f * (0.5f + 0.5f * sinf(3.0f * ph)) };
            must_submit(b, &sweep);
        }
        AnoAudioEvent e;
        while (ano_audio_poll_event(b, &e))
            if (e.kind == AEVT_SOURCE_RETIRED && e.u.source_id >= 200u)
                clicksRetired++;
        ano_sleep(stepMs * 1000u);
    }

    // wind down: stop the bed, release both buffers, collect every fact
    AnoAudioCommand stopBed = { .kind = ACMD_SOURCE_STOP, .source_id = 100 };
    must_submit(b, &stopBed);
    while (!ano_audio_buffer_release(b, 1)) ano_sleep(1000);
    while (!ano_audio_buffer_release(b, 2)) ano_sleep(1000);

    bool bedRetired = false;
    uint32_t blocksHome = 0;
    uint32_t start = ano_timestamp_ms();
    while ((clicksRetired < clicksFired || !bedRetired || blocksHome < 2u)
           && ano_timestamp_ms() - start < 5000u) {
        AnoAudioEvent e;
        while (ano_audio_poll_event(b, &e)) {
            if (e.kind == AEVT_SOURCE_RETIRED) {
                if (e.u.source_id == 100u) bedRetired = true;
                else if (e.u.source_id >= 200u) clicksRetired++;
            }
            if (e.kind == AEVT_BUFFER_RETIRED) {
                ano_audio_block_free(e.u.buffer.block);
                blocksHome++;
            }
        }
        ano_sleep(5000);
    }
    CHECK(clicksRetired == clicksFired, "every click retired");
    CHECK(bedRetired, "the bed retired after stop");
    CHECK(blocksHome == 2u, "both sample blocks came home");
    CHECK(wait_telemetry(b, pred_quiet, 2000), "voice pool drains to zero");

    AnoAudioTelemetry t;
    if (ano_audio_acquire_telemetry(b, &t))
        printf("info: scene — %u clicks, blocks %llu, cpu %llu ns/block, underruns %u, clipped %u\n",
               clicksFired, (unsigned long long)t.blockIndex,
               (unsigned long long)t.blockCpuNs, t.underruns, t.clippedSamples);

    ano_audio_shutdown();

    if (failures) {
        printf("anotest_audioscene: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_audioscene: all passed (%u s scene)\n", seconds);
    return 0;
}
