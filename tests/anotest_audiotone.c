/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Live smoke: AUTO backend (pipewire -> alsa -> null), two-tone figure.
// Asserts any-backend: init, heartbeat, audible peak, retire, shutdown.
// ANO_AUDIO_BACKEND=pipewire|alsa|null. argv[1] = play seconds. Exit 0 == pass.

#include <stdio.h>
#include <stdlib.h>

#include <anoptic_audio.h>
#include <anoptic_time.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

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

int main(int argc, char **argv)
{
    uint32_t seconds = 2;
    if (argc > 1) {
        int s = atoi(argv[1]);
        if (s > 0) seconds = (uint32_t)s;
    }

    CHECK(ano_audio_init(NULL), "audio world up");
    AnoAudioBridge *b = anoAudioBridge();
    CHECK(b != NULL, "bridge valid");
    if (!b) return 1;

    CHECK(wait_telemetry(b, pred_heartbeat, 3000), "mixer heartbeat");

    // Fifth: A440, then E330 after half the run.
    AnoAudioCommand play = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
        .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 1, .gain = 0.30f, .pan = -0.4f,
                  .freqHz = 440.0f } };
    CHECK(ano_audio_submit(b, &play), "submit tone A");
    CHECK(wait_telemetry(b, pred_audible, 2000), "tone audible in telemetry");

    ano_sleep((uint64_t)seconds * 500000ull);

    AnoAudioCommand play2 = { .kind = ACMD_SOURCE_PLAY, .source_id = 2,
        .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 1, .gain = 0.25f, .pan = 0.4f,
                  .freqHz = 330.0f } };
    CHECK(ano_audio_submit(b, &play2), "submit tone E");

    ano_sleep((uint64_t)seconds * 500000ull);

    // Stop both; drain until both retire.
    AnoAudioCommand stop1 = { .kind = ACMD_SOURCE_STOP, .source_id = 1 };
    AnoAudioCommand stop2 = { .kind = ACMD_SOURCE_STOP, .source_id = 2 };
    CHECK(ano_audio_submit(b, &stop1), "submit stop A");
    CHECK(ano_audio_submit(b, &stop2), "submit stop E");

    uint32_t retired = 0;
    uint32_t start = ano_timestamp_ms();
    while (retired < 2u && ano_timestamp_ms() - start < 4000u) {
        AnoAudioEvent e;
        while (ano_audio_poll_event(b, &e))
            if (e.kind == AEVT_SOURCE_RETIRED)
                retired++;
        if (retired < 2u)
            ano_sleep(5000);
    }
    CHECK(retired == 2u, "both tones retired after stop");

    AnoAudioTelemetry t;
    if (ano_audio_acquire_telemetry(b, &t))
        printf("info: %u Hz, block %u — blocks %llu, cpu %llu ns/block, peak %.3f, underruns %u\n",
               t.sampleRate, t.blockFrames, (unsigned long long)t.blockIndex,
               (unsigned long long)t.blockCpuNs, (double)t.masterPeak, t.underruns);

    ano_audio_shutdown();

    if (failures) {
        printf("anotest_audiotone: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_audiotone: all passed\n");
    return 0;
}
