/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * audio_internal.h (private to src/audio/)
 * The mixer world: source and bus pools, the block-loop state, and the device
 * backend interface. Owned exclusively by the mixer thread after init; the
 * device thread touches only the cooked-block ring and the underrun counter.
 *
 * Allocation rule: every pool here is carved from the module heap at init.
 * The block loop's steady state performs zero allocation and takes zero locks.
 */

#ifndef ANO_AUDIO_INTERNAL_H
#define ANO_AUDIO_INTERNAL_H

#include <stdatomic.h>
#include <anoptic_threads.h>
#include "audio_bridge.h"

// Release ramps retire a voice once its smoothed gain falls below this (-80 dBFS).
#define ANO_AUDIO_RETIRE_EPS 1.0e-4f

// One-pole parameter smoother: every audible parameter glides through one of
// these (~30 ms), so retargets never zipper. Snap-on-converge kills denormals.
typedef struct AnoAudioSmooth
{
    float y;      // current value
    float target; // retarget destination
    float coef;   // per-sample pole, expf(-1 / (tau * rate))
} AnoAudioSmooth;

// Advance one frame and return the smoothed value.
static inline float ano_audio_smooth_step(AnoAudioSmooth *s)
{
    float d = s->y - s->target;
    if (d < 1.0e-7f && d > -1.0e-7f) { s->y = s->target; return s->y; }
    s->y = s->target + d * s->coef;
    return s->y;
}

static inline void ano_audio_smooth_snap(AnoAudioSmooth *s, float v)
{
    s->y = v;
    s->target = v;
}

// Voice lifecycle. "Allocate" is FREE -> PLAYING, a state flip in the
// preallocated pool. RETIRING holds the slot until its retirement event lands
// on the (possibly full) events ring; only then may the id be recycled.
typedef enum AnoAudioSourceState
{
    ANO_AUDIO_SRC_FREE = 0,
    ANO_AUDIO_SRC_PLAYING,
    ANO_AUDIO_SRC_STOPPING, // release ramp running (STOP command or duration expiry)
    ANO_AUDIO_SRC_RETIRING, // silent; retirement event not yet delivered
} AnoAudioSourceState;

typedef struct AnoAudioSource
{
    uint32_t state;     // AnoAudioSourceState
    uint32_t source_id; // producer-owned logical handle
    uint32_t bus;       // target bus index
    uint32_t kind;      // AnoAudioSourceKind
    double   phase;     // TONE: oscillator phase in cycles [0, 1)
    uint64_t remaining; // frames until release starts; UINT64_MAX = until STOP
    AnoAudioSmooth gain;
    AnoAudioSmooth pan;  // -1 .. +1
    AnoAudioSmooth freq; // Hz
} AnoAudioSource;

typedef struct AnoAudioBus
{
    AnoAudioSmooth gain;
    float *mix; // blockFrames * ANO_AUDIO_CHANNELS accumulation scratch (module heap)
} AnoAudioBus;

typedef struct AnoAudioMixer AnoAudioMixer;

// A device backend consumes cooked blocks from mx->blockRing on its own thread
// (or the OS's callback thread). start spawns/wires the consumer; stop joins
// it. Backends never touch the graph. Phase 0 ships null only; PipeWire/ALSA,
// WASAPI/DirectSound, and CoreAudio join per-platform in later phases.
typedef struct AnoAudioDeviceApi
{
    const char *name;
    bool (*start)(AnoAudioMixer *mx);
    void (*stop)(AnoAudioMixer *mx);
} AnoAudioDeviceApi;

// Null backend: drains the ring at the nominal block cadence, discarding
// samples. Headless runs, CI, tests.
const AnoAudioDeviceApi *ano_audio_device_null(void);

// The mixer world. One instance behind ano_audio_init (realtime) and one
// short-lived local instance per ano_audio_render_offline call.
struct AnoAudioMixer
{
    // Granted configuration (immutable after init).
    uint32_t sampleRate;
    uint32_t blockFrames;
    uint32_t busCount;
    float    smoothCoef; // shared per-sample pole for the default ~30 ms window

    // Graph pools (mixer-thread-owned).
    AnoAudioBus    buses[ANO_AUDIO_MAX_BUSES];
    AnoAudioSource sources[ANO_AUDIO_MAX_SOURCES];
    uint64_t       blockIndex;
    uint32_t       sourcesActive; // sounding voices in the latest block
    float          masterPeak;    // |peak| of the latest block, post master gain

    // Transport. NULL bridge == offline drive (no events, no telemetry).
    AnoAudioBridge *bridge;

    // Cooked-block lane toward the device (realtime only).
    AnoAudioRing blockRing;     // stride = blockFrames * ANO_AUDIO_CHANNELS * sizeof(float)
    float       *blockScratch;  // mixer-side render target before push
    float       *deviceScratch; // device-side pop target
    _Atomic uint32_t underruns; // device increments, mixer reports in telemetry

    // Threads + backend (realtime only).
    const AnoAudioDeviceApi *device;
    atomic_bool mixerRun;
    atomic_bool deviceRun;
    anothread_t mixerThread;
    anothread_t deviceThread;

    mi_heap_t *heap; // owns every allocation above
};

// --- audio_mixer.c ---

// Carve the bus mix scratch from mx->heap using the granted config already
// written into mx, and snap every bus gain to unity. false on allocation failure.
bool ano_audio_graph_init(AnoAudioMixer *mx);

// Apply one command at a block boundary (mixer thread / offline driver only).
void ano_audio_apply(AnoAudioMixer *mx, const AnoAudioCommand *cmd);

// Render one block into out[blockFrames * ANO_AUDIO_CHANNELS] and run the
// retirement pass. Pure of transport except event emission via mx->bridge
// (skipped when NULL).
void ano_audio_render_block(AnoAudioMixer *mx, float *out);

// Realtime block loop (mixer thread entry). arg = AnoAudioMixer*.
void *ano_audio_mixer_main(void *arg);

#endif // ANO_AUDIO_INTERNAL_H
