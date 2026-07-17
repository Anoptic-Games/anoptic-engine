/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: engine boot telemetry handshake (main.c:391; docs/BUGS.md, Engine /
// Interlink-Composition). music_world_start spins ano_audio_acquire_telemetry at most
// 200 times (:389) and then seeds ano_synth_transport_start from AnoAudioTelemetry t
// (:388) whether or not any acquire ever succeeded; the acquire contract returns false
// with *out untouched until the mixer's first publish (anoptic_audio.h:375,
// audio_bridge.h:144), so the timeout arm consumes t uninitialized and stages a garbage
// transport start frame. This TU compiles the REAL src/engine/main.c with two seam
// tokens interposed:
//   ano_audio_acquire_telemetry -> a contract-faithful shim (false and *out untouched
//     until "published", then a fixed telemetry), so the never-publish arm 〜 in the
//     field, >1 s of mixer-thread starvation after a successful ano_audio_init 〜 runs
//     deterministically against the verbatim engine code;
//   ano_synth_transport_start -> a capture recording (called, worldFrame); the synth
//     stays idle so teardown is clean either way.
// CONTROL first: with telemetry published, boot must seed exactly (blockIndex + 8) *
// blockFrames, so a reject-everything fix cannot pass. TRIGGER: with nothing published,
// a correct boot must not stage a transport start from the failed handshake at all.
// Real audio world both times (device cascade to null); the synth never starts.
// Exit 0 == pass.

#define main                        anotest_engine_main_unused
#define ano_audio_acquire_telemetry anotest_acquire_telemetry_shim
#define ano_synth_transport_start   anotest_transport_start_capture

#include "engine/main.c"

#undef main
#undef ano_audio_acquire_telemetry
#undef ano_synth_transport_start

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Shim state: inputs set per phase, counters observed by the checks.
static bool     g_shimPublished;   // telemetry "published" yet?
static uint64_t g_shimBlockIndex;
static uint32_t g_shimBlockFrames;
static uint32_t g_shimAcquireCalls;
static bool     g_capTransportCalled;
static uint64_t g_capTransportFrame;

// Contract-faithful stand-in for ano_audio_acquire_telemetry (anoptic_audio.h:375):
// false with *out untouched before the first publish, the fixed telemetry after.
bool anotest_acquire_telemetry_shim(AnoAudioBridge *bridge, AnoAudioTelemetry *out)
{
    (void)bridge;
    g_shimAcquireCalls++;
    if (!g_shimPublished)
        return false;
    *out = (AnoAudioTelemetry){ .blockIndex = g_shimBlockIndex,
                                .sampleRate = MUSIC_RATE,
                                .blockFrames = g_shimBlockFrames };
    return true;
}

// Capture for ano_synth_transport_start: records the staged start; the synth stays idle.
void anotest_transport_start_capture(AnoSynth *s, uint64_t worldFrame)
{
    (void)s;
    g_capTransportCalled = true;
    g_capTransportFrame  = worldFrame;
}

int main(void)
{
    // CONTROL: published telemetry 〜 boot must seed (blockIndex + 8) * blockFrames.
    g_shimPublished      = true;
    g_shimBlockIndex     = 5u;
    g_shimBlockFrames    = 512u;
    g_shimAcquireCalls   = 0u;
    g_capTransportCalled = false;
    bool up = music_world_start();
    CHECK(up, "CONTROL: music world comes up on the real device cascade");
    CHECK(g_shimAcquireCalls >= 1u, "CONTROL: boot reached the telemetry handshake");
    CHECK(g_capTransportCalled, "CONTROL: transport seeded when telemetry is published");
    CHECK(g_capTransportFrame == (5u + 8u) * 512u,
          "CONTROL: start frame is (blockIndex + 8) * blockFrames");
    music_world_stop();

    // TRIGGER: nothing ever published 〜 the 200-spin loop exhausts (~1 s of 5 ms sleeps)
    // and main.c:391 consumes the uninitialized t; a correct boot must not stage a
    // transport start from a failed handshake.
    g_shimPublished      = false;
    g_shimAcquireCalls   = 0u;
    g_capTransportCalled = false;
    g_capTransportFrame  = 0u;
    (void)music_world_start(); // a fixed shape may return false; only the seed matters
    CHECK(g_shimAcquireCalls >= 1u, "TRIGGER: boot reached the telemetry handshake");
    if (g_capTransportCalled)
        printf("  transport seeded from uninitialized telemetry: worldFrame %llu\n",
               (unsigned long long)g_capTransportFrame);
    CHECK(!g_capTransportCalled, "TRIGGER: no transport seed from a failed telemetry handshake");
    music_world_stop();

    if (failures) {
        printf("anotest_boottelemetryguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_boottelemetryguard: all passed\n");
    return 0;
}
