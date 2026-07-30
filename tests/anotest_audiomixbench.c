/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Offline voice-render benchmark: invariant source shapes and a mixed 48-voice block.
// DISABLED in ctest. Run from an -O3 build.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_audio.h>

#include "templates/bench.h"

enum {
    RATE = 48000,
    FRAMES = 24000,
    BUFFER_FRAMES = 4096,
    REPS = 31,
};

typedef enum VoiceShape {
    SHAPE_TONE,
    SHAPE_MONO,
    SHAPE_MONO_POSITIONAL,
    SHAPE_STEREO,
    SHAPE_MIXED,
} VoiceShape;

static float g_mono[BUFFER_FRAMES];
static float g_stereo[BUFFER_FRAMES * 2];
static float g_sink;

static void make_material(void)
{
    for (uint32_t i = 0; i < BUFFER_FRAMES; ++i) {
        float a = 6.28318530717958647692f * (float)i / (float)BUFFER_FRAMES;
        g_mono[i] = 0.25f * sinf(17.0f * a);
        g_stereo[2u * i] = 0.25f * sinf(13.0f * a);
        g_stereo[2u * i + 1u] = 0.25f * cosf(19.0f * a);
    }
}

static uint32_t make_events(AnoAudioOfflineEvent events[ANO_AUDIO_MAX_SOURCES],
                            VoiceShape shape)
{
    uint32_t count = shape == SHAPE_MIXED ? 48u : 24u;
    for (uint32_t i = 0; i < count; ++i) {
        VoiceShape voice = shape;
        if (shape == SHAPE_MIXED)
            voice = (VoiceShape)(i & 3u);
        AnoAudioSourceDesc desc = {
            .kind = voice == SHAPE_TONE ? ANO_AUDIO_SOURCE_TONE : ANO_AUDIO_SOURCE_BUFFER,
            .bus = 0,
            .buffer_id = voice == SHAPE_STEREO ? 2u : 1u,
            .flags = voice == SHAPE_TONE ? 0u : ANO_AUDIO_SOURCE_LOOP,
            .gain = 0.0125f,
            .pan = (float)((int)(i % 9u) - 4) * 0.2f,
            .freqHz = 110.0f + 7.0f * (float)i,
            .rate = 0.75f + 0.025f * (float)(i % 17u),
            .position = { (float)((int)(i % 7u) - 3), 0.0f, -2.0f - (float)(i % 5u) },
            .durationFrames = FRAMES,
        };
        if (voice == SHAPE_MONO_POSITIONAL)
            desc.flags |= ANO_AUDIO_SOURCE_POSITIONAL;
        events[i] = (AnoAudioOfflineEvent){
            .frame = 0,
            .cmd = {
                .kind = ACMD_SOURCE_PLAY,
                .source_id = i + 1u,
                .desc = desc,
            },
        };
    }
    return count;
}

static bool render(float *out, const AnoAudioBusDesc *layout,
                   const AnoAudioOfflineEvent *events, uint32_t eventCount)
{
    static const AnoAudioOfflineBuffer buffers[] = {
        { .buffer_id = 1, .channels = 1, .frames = BUFFER_FRAMES, .data = g_mono },
        { .buffer_id = 2, .channels = 2, .frames = BUFFER_FRAMES, .data = g_stereo },
    };
    static const AnoAudioOfflineListener listener = {
        .frame = 0,
        .listener = {
            .pos = { 0.0f, 0.0f, 0.0f },
            .forward = { 0.0f, 0.0f, -1.0f },
            .up = { 0.0f, 1.0f, 0.0f },
        },
    };
    const AnoAudioOfflineDesc desc = {
        .sampleRate = RATE,
        .blockFrames = 512,
        .busCount = 1,
        .busLayout = layout,
        .events = events,
        .eventCount = eventCount,
        .buffers = buffers,
        .bufferCount = 2,
        .listeners = &listener,
        .listenerCount = 1,
    };
    return ano_audio_render_offline(&desc, out, FRAMES);
}

static bool run_events(const char *label, float *out, const AnoAudioBusDesc *layout,
                       const AnoAudioOfflineEvent *events, uint32_t eventCount)
{
    for (unsigned i = 0; i < 3; ++i)
        if (!render(out, layout, events, eventCount))
            return false;

    uint64_t samples[REPS];
    bench_lat lat;
    bench_lat_init(&lat, samples, REPS);
    for (unsigned i = 0; i < REPS; ++i) {
        uint64_t begin = bench_begin();
        if (!render(out, layout, events, eventCount))
            return false;
        bench_lat_add(&lat, bench_end(begin));
        g_sink += out[(i * 1543u) % (FRAMES * ANO_AUDIO_CHANNELS)];
    }
    bench_lat_row(label, bench_lat_stats(&lat));
    return true;
}

static bool run_series(const char *label, VoiceShape shape, float *out)
{
    AnoAudioOfflineEvent events[ANO_AUDIO_MAX_SOURCES];
    uint32_t count = make_events(events, shape);
    return run_events(label, out, NULL, events, count);
}

static bool run_filter_series(const char *label, AnoAudioFilterMode mode, float *out)
{
    const AnoAudioBusDesc layout = { .fx = { ANO_AUDIO_FX_FILTER } };
    AnoAudioOfflineEvent events[ANO_AUDIO_MAX_SOURCES + 2u] = {
        { .frame = 0, .cmd = {
            .kind = ACMD_FX_SET, .bus = 0, .fxSlot = 0,
            .paramId = ANO_AUDIO_P_FILTER_CUTOFF, .value = 2400.0f,
        } },
        { .frame = 0, .cmd = {
            .kind = ACMD_FX_SET, .bus = 0, .fxSlot = 0,
            .paramId = ANO_AUDIO_P_FILTER_MODE, .value = (float)mode,
        } },
    };
    uint32_t count = make_events(events + 2u, SHAPE_MONO);
    return run_events(label, out, &layout, events, count + 2u);
}

int main(void)
{
    make_material();
    float *out = static_cast<float *>(
        malloc((size_t)FRAMES * ANO_AUDIO_CHANNELS * sizeof *out));
    if (out == NULL)
        return 1;

    bench_lat_header();
    bool ok = run_series("24 tone voices", SHAPE_TONE, out)
           && run_series("24 mono loop voices", SHAPE_MONO, out)
           && run_series("24 positional voices", SHAPE_MONO_POSITIONAL, out)
           && run_series("24 stereo loop voices", SHAPE_STEREO, out)
           && run_series("48 mixed voices", SHAPE_MIXED, out)
           && run_filter_series("filter lowpass", ANO_AUDIO_FILTER_LOWPASS, out)
           && run_filter_series("filter highpass", ANO_AUDIO_FILTER_HIGHPASS, out)
           && run_filter_series("filter bandpass", ANO_AUDIO_FILTER_BANDPASS, out);
    free(out);
    if (!ok || !isfinite(g_sink)) {
        printf("anotest_audiomixbench: failed\n");
        return 1;
    }
    return 0;
}
