/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * audio_internal.h (private to src/audio/)
 * The mixer world: source, bus, and buffer pools, the block-loop state, and
 * the device backend interface. Owned exclusively by the mixer thread after
 * init; the device thread touches only the cooked-block ring and the underrun
 * counter.
 *
 * Allocation rule: every pool here is carved from the module heap at init.
 * The block loop's steady state performs zero allocation and takes zero locks.
 * Sample-block memory adopted from the logic thread is returned there for
 * freeing via AEVT_BUFFER_RETIRED — the one deliberate exception is freeing a
 * REJECTED registration block at a block boundary when the events ring is full.
 */

#ifndef ANO_AUDIO_INTERNAL_H
#define ANO_AUDIO_INTERNAL_H

#include <stdatomic.h>
#include <anoptic_threads.h>
#include "audio_bridge.h"
#include "audio_fx.h" // effect chains + dsp/smooth.h (AnoAudioSmooth)

// Release ramps retire a voice once its smoothed gain falls below this (-80 dBFS).
#define ANO_AUDIO_RETIRE_EPS 1.0e-4f

// Master clip guard ceiling (the last line behind the limiter).
#define ANO_AUDIO_CLIP_CEIL 0.98f

// Voice lifecycle. "Allocate" is FREE -> PLAYING, a state flip in the
// preallocated pool. RETIRING holds the slot until its retirement event lands
// on the (possibly full) events ring; only then may the id be recycled.
typedef enum AnoAudioSourceState
{
    ANO_AUDIO_SRC_FREE = 0,
    ANO_AUDIO_SRC_PLAYING,
    ANO_AUDIO_SRC_STOPPING, // release ramp running (STOP command, duration or data expiry)
    ANO_AUDIO_SRC_RETIRING, // silent; retirement event not yet delivered
} AnoAudioSourceState;

typedef struct AnoAudioSource
{
    uint32_t state;     // AnoAudioSourceState
    uint32_t source_id; // producer-owned logical handle
    uint32_t bus;       // target bus index
    uint32_t kind;      // AnoAudioSourceKind
    uint32_t flags;     // AnoAudioSourceFlags

    // TONE
    double phase; // oscillator phase in cycles [0, 1)

    // BUFFER: data snapshot from the registry slot (the block outlives every
    // voice on it — retirement waits for silence) plus the read cursor.
    uint32_t     bufSlot;
    uint32_t     bufChannels;
    uint64_t     bufFrames;
    const float *bufData;
    double       cursor; // fractional frame

    uint64_t remaining; // frames until release starts; UINT64_MAX = until STOP

    // per-sample smoothers
    AnoAudioSmooth gain;
    AnoAudioSmooth pan;      // -1 .. +1 (positional: driven by spatialization)
    AnoAudioSmooth freq;     // TONE Hz
    AnoAudioSmooth rate;     // BUFFER playback-rate multiplier
    AnoAudioSmooth spatGain; // distance attenuation (1 when non-positional)

    // spatialization inputs + air-absorption filter
    float position[3];
    float minDist, maxDist, rolloff;
    AnoAudioSmooth airCutoff; // per-BLOCK smoother feeding the one-pole coef
    float airCoef;            // per-block one-pole coefficient
    float airLp;              // filter state (mono, pre-pan)
} AnoAudioSource;

typedef struct AnoAudioBus
{
    uint32_t parent; // fold target; bus 0 ignores it
    AnoAudioSmooth gain;
    float *mix; // blockFrames * ANO_AUDIO_CHANNELS accumulation scratch (module heap)

    // insert chain, fixed at init; parameters retarget via ACMD_FX_SET
    AnoAudioFx fx[ANO_AUDIO_MAX_FX];

    // post-fader sends into earlier buses (returns); target 0 = unused
    struct {
        uint32_t       target;
        AnoAudioSmooth level; // per-sample smoother
    } sends[ANO_AUDIO_MAX_SENDS];
} AnoAudioBus;

// Registered sample buffers. Adopted blocks (realtime path) ride home for
// freeing via AEVT_BUFFER_RETIRED once released AND no voice references the
// slot; borrowed buffers (offline desc) are never freed here.
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
    bool         owned;     // true = adopted block, retire it home
    uint64_t     frames;
    const float *data;      // interleaved f32
    void        *block;     // the adopted allocation (header + data); NULL when borrowed
} AnoAudioBufferSlot;

// Adopted-block layout: this header, then frames*channels interleaved floats.
typedef struct AnoAudioBlockHeader
{
    uint64_t frames;
    uint32_t channels;
    uint32_t pad;
} AnoAudioBlockHeader;

typedef struct AnoAudioMixer AnoAudioMixer;

// A device backend consumes cooked blocks from mx->blockRing on its own thread
// (or the OS's callback thread). start spawns/wires the consumer; stop joins
// it. Backends never touch the graph.
typedef struct AnoAudioDeviceApi
{
    const char *name;
    bool (*start)(AnoAudioMixer *mx);
    void (*stop)(AnoAudioMixer *mx);
} AnoAudioDeviceApi;

// Null backend: drains the ring at the nominal block cadence, discarding
// samples. Headless runs, CI, tests.
const AnoAudioDeviceApi *ano_audio_device_null(void);

#if defined(_WIN32)
// audio_win64.c — WASAPI (event-driven shared mode, IAudioClient3 low latency
// when the mix rate matches) and DirectSound (cursor-chase fallback). Both
// runtime-load their libraries and fail cleanly so AUTO can cascade.
const AnoAudioDeviceApi *ano_audio_device_wasapi(void);
const AnoAudioDeviceApi *ano_audio_device_dsound(void);
#elif defined(__APPLE__)
// audio_macos.c — the default-output AudioUnit (AUHAL converts rate/format
// and follows default-device changes).
const AnoAudioDeviceApi *ano_audio_device_coreaudio(void);
#elif defined(__linux__)
// audio_linux.c — both dlopen their library at start() and fail cleanly
// (returning false) when it is absent, so AUTO can cascade.
const AnoAudioDeviceApi *ano_audio_device_pipewire(void);
const AnoAudioDeviceApi *ano_audio_device_alsa(void);
#endif

// The mixer world. One instance behind ano_audio_init (realtime) and one
// short-lived local instance per ano_audio_render_offline call.
struct AnoAudioMixer
{
    // Granted configuration (immutable after init).
    uint32_t sampleRate;
    uint32_t blockFrames;
    uint32_t busCount;
    float    smoothCoef;      // per-sample pole for the ~30 ms window
    float    smoothCoefBlock; // per-block pole for the same window

    // Graph pools (mixer-thread-owned).
    AnoAudioBus        buses[ANO_AUDIO_MAX_BUSES];
    AnoAudioSource     sources[ANO_AUDIO_MAX_SOURCES];
    AnoAudioBufferSlot buffers[ANO_AUDIO_MAX_BUFFERS];
    uint64_t           blockIndex;
    uint32_t           sourcesActive;  // sounding voices in the latest block
    float              masterPeak;     // |peak| of the latest block, pre clip guard
    uint32_t           clippedSamples; // clip-guard hits since init

    // Attached block generator (the synth seam); immutable while running.
    AnoAudioGenerator        generator;
    void                    *generatorUser;
    AnoAudioGeneratorControl generatorControl;
    AnoAudioGeneratorPoll    generatorPoll;
    AnoAudioGeneratorStats   generatorStats;

    // Generator events, staged. Lossless like the retirement passes: what the
    // events ring will not take this block stays here and is offered again.
#define ANO_AUDIO_GEN_EVENTS 16u
    AnoAudioEvent genPending[ANO_AUDIO_GEN_EVENTS];
    uint32_t      genPendingCount;

    // Listener (latest applied; realtime: acquired from the seqlock per block).
    AnoAudioListener listener;
    bool             listenerValid;

    // Transport. NULL bridge == offline drive (no events, no telemetry).
    AnoAudioBridge *bridge;

    // Cooked-block lane toward the device (realtime only).
    AnoAudioRing blockRing;     // stride = blockFrames * ANO_AUDIO_CHANNELS * sizeof(float)
    float       *blockScratch;  // mixer-side render target before push
    float       *deviceScratch; // device-side pop target
    _Atomic uint32_t underruns; // device increments, mixer reports in telemetry

    // Threads + backend (realtime only).
    const AnoAudioDeviceApi *device;
    void       *deviceState; // backend-private state (allocated in start, freed in stop)
    atomic_bool mixerRun;
    atomic_bool deviceRun;
    anothread_t mixerThread;
    anothread_t deviceThread;

    mi_heap_t *heap; // owns every allocation above
};

// --- audio_mixer.c ---

// Carve the bus mix scratch from mx->heap and seed bus parents/gains from
// `layout` (NULL = flat: every aux folds into master, gain 1). Requires
// layout parents < their index. false on bad layout or allocation failure.
bool ano_audio_graph_init(AnoAudioMixer *mx, const AnoAudioBusDesc *layout);

// Apply one command at a block boundary (mixer thread / offline driver only).
void ano_audio_apply(AnoAudioMixer *mx, const AnoAudioCommand *cmd);

// Render one block into out[blockFrames * ANO_AUDIO_CHANNELS] and run the
// retirement passes. Pure of transport except event emission via mx->bridge
// (skipped when NULL).
void ano_audio_render_block(AnoAudioMixer *mx, float *out);

// Realtime block loop (mixer thread entry). arg = AnoAudioMixer*.
void *ano_audio_mixer_main(void *arg);

#endif // ANO_AUDIO_INTERNAL_H
