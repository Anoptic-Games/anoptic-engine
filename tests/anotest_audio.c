/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for the audio stack, Phase 0 (public include/anoptic_audio.h; private
 * transport src/audio/audio_bridge.h):
 *  - transport layout: the ring cursors really sit on separate cache lines;
 *  - offline determinism (THE Phase 0 exit gate): a smoothed tone routed
 *    through an aux bus, retargeted, stopped, and retired renders byte-identical
 *    across renders interleaved with deliberate heap churn. Oracle: memcmp == 0.
 *  - signal sanity: fade-in from silence, sane peak, actual signal present;
 *  - WAV writer: IEEE-float header fields + exact file size oracle;
 *  - live world round-trip on the null backend: init, telemetry heartbeat,
 *    PLAY -> audible peak in telemetry -> AEVT_SOURCE_RETIRED, listener publish,
 *    double-init rejection, shutdown idempotence, re-init after shutdown.
 * argv[1] scales the churn/render soak rounds. Exit 0 == pass. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <anoptic_audio.h>
#include <anoptic_memory.h>
#include <anoptic_time.h>
#include "audio/audio_bridge.h" // private transport: layout asserts only

#include "templates/rng.h"
#include "templates/scratch.h"

// The false-sharing avoidance must be real, not aspirational (mirrors the
// render_bridge transport test; the audio ring is its deliberate copy).
_Static_assert(offsetof(AnoAudioRing, head) - offsetof(AnoAudioRing, tail) >= ANO_CACHE_LINE,
               "audio SPSC head/tail must live on separate cache lines");
#define ANO_MIN_LINE (ANO_CACHE_LINE < ANO_THREAD_LINE ? ANO_CACHE_LINE : ANO_THREAD_LINE)
_Static_assert(_Alignof(AnoAudioRing) >= ANO_MIN_LINE,
               "audio SPSC ring must be cache-line aligned");

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RATE   48000u
#define FRAMES 48000u // one second
#define SAMPLES (FRAMES * ANO_AUDIO_CHANNELS)

// Deliberate heap churn: scatter live allocations of random sizes, dirty their
// pages, free in random order. Renders on either side of this must not differ.
static void churn_heap(test_rng *rng, uint32_t rounds)
{
    enum { SLOTS = 256 };
    void *ptr[SLOTS] = {0};
    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t i = 0; i < SLOTS; i++) {
            uint32_t slot = rng_below(rng, SLOTS);
            if (ptr[slot]) {
                mi_free(ptr[slot]);
                ptr[slot] = NULL;
            } else {
                size_t bytes = 1u + rng_below(rng, 8192u);
                ptr[slot] = mi_malloc(bytes);
                if (ptr[slot])
                    memset(ptr[slot], (int)(rng_next(rng) & 0xFFu), bytes);
            }
        }
    }
    for (uint32_t i = 0; i < SLOTS; i++)
        if (ptr[i]) mi_free(ptr[i]);
}

// The offline score: one sustained tone on the aux bus with mid-flight retargets
// and a stop (exercising the release ramp and the offline retire path), one
// short direct-to-master tone whose duration expires in-render.
static const AnoAudioOfflineEvent k_events[] = {
    { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
        .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 1, .gain = 0.5f, .pan = -0.25f, .freqHz = 440.0f } } },
    { .frame = 0, .cmd = { .kind = ACMD_BUS_SET, .bus = 1,
        .fields = ANO_AUDIO_FIELD_GAIN, .gain = 0.9f } },
    { .frame = 12000, .cmd = { .kind = ACMD_SOURCE_UPDATE, .source_id = 1,
        .fields = ANO_AUDIO_FIELD_GAIN | ANO_AUDIO_FIELD_FREQ | ANO_AUDIO_FIELD_PAN,
        .gain = 0.25f, .freqHz = 660.0f, .pan = 0.5f } },
    { .frame = 24000, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 2,
        .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 0, .gain = 0.3f, .pan = 0.5f,
                  .freqHz = 220.0f, .durationFrames = 4800 } } },
    { .frame = 30000, .cmd = { .kind = ACMD_SOURCE_STOP, .source_id = 1 } },
};

static bool render_score(float *out)
{
    AnoAudioOfflineDesc desc = {
        .sampleRate = RATE,
        .blockFrames = 512,
        .busCount = 2,
        .events = k_events,
        .eventCount = (uint32_t)(sizeof k_events / sizeof k_events[0]),
    };
    return ano_audio_render_offline(&desc, out, FRAMES);
}

static float peak_of(const float *buf, uint64_t samples)
{
    float peak = 0.0f;
    for (uint64_t i = 0; i < samples; i++) {
        float a = buf[i] < 0.0f ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    return peak;
}

// --- section 1: offline determinism on a churned heap (the exit gate) ---

static void test_offline_determinism(uint32_t soak)
{
    float *golden = mi_malloc(SAMPLES * sizeof(float));
    float *again  = mi_malloc(SAMPLES * sizeof(float));
    if (!golden || !again) {
        CHECK(false, "test buffers failed to allocate");
        return;
    }

    CHECK(render_score(golden), "offline render (golden)");

    // signal sanity: the gate must never pass vacuously on silence
    float peak = peak_of(golden, SAMPLES);
    CHECK(peak > 0.05f && peak < 1.5f, "peak within a sane audible range");
    float first = golden[0] < 0.0f ? -golden[0] : golden[0];
    CHECK(first < 1.0e-3f, "first sample near silence (smoothed fade-in)");
    CHECK(peak_of(golden, 256) < peak, "fade-in: opening frames quieter than the whole");

    test_rng rng = rng_make(0xC0FFEEu);
    for (uint32_t round = 0; round < soak; round++) {
        churn_heap(&rng, 64);
        memset(again, 0x5A, SAMPLES * sizeof(float)); // poison: a partial render must not pass
        CHECK(render_score(again), "offline render (churned heap)");
        CHECK(memcmp(golden, again, SAMPLES * sizeof(float)) == 0,
              "churned-heap re-render is byte-identical");
    }

    // degenerate inputs stay well-defined
    CHECK(ano_audio_render_offline(NULL, again, 100), "NULL desc renders defaults");
    CHECK(!ano_audio_render_offline(NULL, NULL, 100), "NULL out rejected");

    mi_free(golden);
    mi_free(again);
}

// --- section 2: WAV writer oracle ---

static void test_wav(void)
{
    float *buf = mi_malloc(SAMPLES * sizeof(float));
    if (!buf) {
        CHECK(false, "wav buffer failed to allocate");
        return;
    }
    CHECK(render_score(buf), "offline render (wav source)");

    scratch_make_dir("anotest_audio_scratch");
    const char *path = "anotest_audio_scratch/tone.wav";
    CHECK(ano_audio_wav_write(path, buf, FRAMES, ANO_AUDIO_CHANNELS, RATE), "wav write");

    // header + size oracle: 58-byte header, IEEE-float tag, exact data size
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "wav reopens");
    if (f) {
        uint8_t h[58] = {0};
        CHECK(fread(h, 1, sizeof h, f) == sizeof h, "wav header reads back");
        CHECK(memcmp(h + 0, "RIFF", 4) == 0, "RIFF magic");
        CHECK(memcmp(h + 8, "WAVE", 4) == 0, "WAVE magic");
        CHECK(h[20] == 3u && h[21] == 0u, "format tag is IEEE float");
        uint32_t dataBytes = (uint32_t)h[54] | ((uint32_t)h[55] << 8)
                           | ((uint32_t)h[56] << 16) | ((uint32_t)h[57] << 24);
        CHECK(dataBytes == SAMPLES * sizeof(float), "data chunk size exact");
        fseek(f, 0, SEEK_END);
        CHECK((uint64_t)ftell(f) == 58u + (uint64_t)dataBytes, "file size == header + data");
        fclose(f);
    }
    CHECK(!ano_audio_wav_write(NULL, buf, FRAMES, 2, RATE), "NULL path rejected");

    remove(path);
    scratch_remove_dir("anotest_audio_scratch");
    mi_free(buf);
}

// --- section 3: live world round-trip on the null backend ---

// Poll telemetry until pred(t) or deadline. Returns true on pred.
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

static void test_live_world(void)
{
    CHECK(ano_audio_init(NULL), "audio world up (defaults, null backend)");
    AnoAudioBridge *b = anoAudioBridge();
    CHECK(b != NULL, "bridge handle valid after init");
    if (!b) return;

    CHECK(!ano_audio_init(NULL), "double init rejected");

    CHECK(wait_telemetry(b, pred_heartbeat, 2000), "mixer heartbeat (blocks advancing)");

    // one short cue: PLAY -> audible in telemetry -> retirement event lands
    AnoAudioCommand play = { .kind = ACMD_SOURCE_PLAY, .source_id = 7,
        .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 1, .gain = 0.4f,
                  .freqHz = 330.0f, .durationFrames = 4800 } };
    CHECK(ano_audio_submit(b, &play), "submit PLAY");
    CHECK(wait_telemetry(b, pred_audible, 2000), "cue audible in telemetry peak");

    bool retired = false;
    uint32_t start = ano_timestamp_ms();
    while (!retired && ano_timestamp_ms() - start < 4000u) {
        AnoAudioEvent e;
        while (ano_audio_poll_event(b, &e))
            if (e.kind == AEVT_SOURCE_RETIRED && e.u.source_id == 7u)
                retired = true;
        if (!retired)
            ano_sleep(5000);
    }
    CHECK(retired, "AEVT_SOURCE_RETIRED for the finished cue");
    CHECK(wait_telemetry(b, pred_quiet, 2000), "voice pool drains to zero");

    // exercise the listener lane's producer side (mixer consumes it in Phase 2)
    AnoAudioListener l = { .pos = {1, 2, 3}, .forward = {0, 0, -1}, .up = {0, 1, 0}, .seq = 1 };
    ano_audio_publish_listener(b, &l);

    AnoAudioTelemetry t;
    if (ano_audio_acquire_telemetry(b, &t))
        printf("info: live telemetry — blocks %llu, cpu %llu ns/block, underruns %u\n",
               (unsigned long long)t.blockIndex, (unsigned long long)t.blockCpuNs, t.underruns);

    ano_audio_shutdown();
    ano_audio_shutdown(); // idempotent
    CHECK(anoAudioBridge() == NULL, "bridge handle NULL after shutdown");

    // the world restarts cleanly
    CHECK(ano_audio_init(NULL), "re-init after shutdown");
    CHECK(anoAudioBridge() != NULL, "bridge valid after re-init");
    ano_audio_shutdown();
}

int main(int argc, char **argv)
{
    scratch_anchor_to_exe();
    uint32_t soak = 1;
    if (argc > 1) {
        int s = atoi(argv[1]);
        if (s > 0) soak = (uint32_t)s;
    }

    test_offline_determinism(soak);
    test_wav();
    test_live_world();

    if (failures) {
        printf("anotest_audio: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_audio: all passed (soak x%u)\n", soak);
    return 0;
}
