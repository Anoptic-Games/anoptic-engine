/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Mixer world (private to src/audio/): pools, block-loop state, device API.
// Mixer thread owns the graph after init. Device thread: cooked ring + underruns.
// Pools from module heap at init. Steady-state block loop: no alloc, no locks.
// Adopted sample blocks return via AEVT_BUFFER_RETIRED (reject-on-full frees at boundary).
// Teardown: shutdown frees resident rings and buffer table.

#ifndef ANO_AUDIO_INTERNAL_H
#define ANO_AUDIO_INTERNAL_H

#include <anoptic_atomic.h>
#include <anoptic_threads.h>
#include "audio_bridge.h"
#include "audio_format.h"
#include "audio_fx.h" // effect chains + dsp/smooth.h (AnoAudioSmooth)
#include "audio_source.h"
#include "dsp/noise.h"

// Master clip guard ceiling.
#define ANO_AUDIO_CLIP_CEIL 0.98f

typedef struct AnoAudioBus
{
    uint32_t parent; // fold target. bus 0 ignores
    AnoAudioSmooth gain;
    float *mix; // blockFrames * CHANNELS scratch

    AnoAudioFx fx[ANO_AUDIO_MAX_FX];

    struct {
        uint32_t       target; // 0 = unused
        AnoAudioSmooth level;
    } sends[ANO_AUDIO_MAX_SENDS];
} AnoAudioBus;

// LIVE + owned -> AEVT_BUFFER_RETIRED when released and unreferenced. Borrowed never freed here.
typedef enum AnoAudioBufferState
{
    ANO_AUDIO_BUF_FREE = 0,
    ANO_AUDIO_BUF_LIVE,
    ANO_AUDIO_BUF_RETIRING,
} AnoAudioBufferState;

typedef struct AnoAudioBufferSlot
{
    uint32_t     state;     // AnoAudioBufferState
    uint32_t     buffer_id;
    uint32_t     channels;  // 1 or 2
    bool         owned;     // adopted block
    uint64_t     frames;
    const float *data;      // interleaved f32
    void        *block;     // header + data. NULL if borrowed
} AnoAudioBufferSlot;

// Adopted block: header then frames*channels floats.
typedef struct AnoAudioBlockHeader
{
    uint64_t frames;
    uint32_t channels;
    uint32_t pad;
} AnoAudioBlockHeader;

typedef struct AnoAudioMixer AnoAudioMixer;

// Device consumes mx->blockRing. start/stop own the consumer. Never touches the graph.
typedef struct AnoAudioDeviceApi
{
    AnoAudioBackend backend;
    bool (*start)(AnoAudioMixer *mx);
    void (*stop)(AnoAudioMixer *mx);
} AnoAudioDeviceApi;

// Drain ring at nominal cadence. Headless / CI / tests.
const AnoAudioDeviceApi *ano_audio_device_null(void);

#if defined(_WIN32)
const AnoAudioDeviceApi *ano_audio_device_wasapi(void);
const AnoAudioDeviceApi *ano_audio_device_dsound(void);
#elif defined(__APPLE__)
const AnoAudioDeviceApi *ano_audio_device_coreaudio(void);
#elif defined(__linux__)
const AnoAudioDeviceApi *ano_audio_device_pipewire(void);
const AnoAudioDeviceApi *ano_audio_device_alsa(void);
#endif

struct AnoAudioMixer
{
    uint32_t sampleRate;
    uint32_t blockFrames;
    uint32_t busCount;
    float    smoothCoef;      // per-sample ~30 ms pole
    float    smoothCoefBlock; // per-block pole

    AnoAudioBus        buses[ANO_AUDIO_MAX_BUSES];
    AnoAudioSource     sources[ANO_AUDIO_MAX_SOURCES];
    AnoAudioBufferSlot buffers[ANO_AUDIO_MAX_BUFFERS];
    uint64_t           blockIndex;
    uint32_t           sourcesActive;
    float              masterPeak;
    uint32_t           clippedSamples;

    AnoAudioGenerator        generator;
    void                    *generatorUser;
    AnoAudioGeneratorControl  generatorControl;
    AnoAudioGeneratorPoll     generatorPoll;
    AnoAudioGeneratorStats    generatorStats;
    AnoAudioGeneratorCommands generatorCommands;

    // Staged generator events. Re-offer until the ring takes them.
#define ANO_AUDIO_GEN_EVENTS 16u
#define ANO_AUDIO_GEN_CMDS   16u
    AnoAudioEvent genPending[ANO_AUDIO_GEN_EVENTS];
    uint32_t      genPendingCount;

    AnoAudioListener listener;
    bool             listenerValid;

    AnoAudioBridge *bridge; // NULL = offline (no events, no telemetry)

    // Cooked-block lane (realtime). stride = blockFrames * CHANNELS * sizeof(float).
    AnoAudioRing blockRing;
    float       *blockScratch;  // mixer render target before push
    float       *deviceScratch; // device pop / pull carry (one backend at a time)
    // Own cache line.
    alignas(ANO_THREAD_LINE) ANO_ATOMIC(uint32_t) underruns; // device increments; mixer reports in telemetry

    const AnoAudioDeviceApi *device;
    void       *deviceState; // backend-private; allocated in start, freed in stop
    atomic_bool mixerRun;
    atomic_bool deviceRun;
    anothread_t mixerThread;
    anothread_t deviceThread;

    mi_heap_t *heap;
};


/* audio_mixer.c */

// Bus mix scratch + parents/gains from layout (NULL = flat into master). Parents < index.
bool ano_audio_graph_init(AnoAudioMixer *mx, const AnoAudioBusDesc *layout);

// Apply one command at a block boundary.
void ano_audio_apply(AnoAudioMixer *mx, const AnoAudioCommand *cmd);

// Render one block. Emits events via bridge when non-NULL.
void ano_audio_render_block(AnoAudioMixer *mx, float *out);

// Realtime mixer thread entry. arg = AnoAudioMixer*.
void *ano_audio_mixer_main(void *arg);

// Final f32 -> signed-16 quantization shared by ALSA and DirectSound.
void ano_audio_convert_f32_s16(const float *input, int16_t *output,
                               uint32_t sampleCount, AnoDspRng *dither);

#endif // ANO_AUDIO_INTERNAL_H
