/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Audio world lifecycle + the logic-side bridge endpoints. The hot-path
 * push/pop and the mixer-side endpoints stay inlined in the private
 * audio_bridge.h; only the cold init/shutdown and the public (non-inline)
 * endpoints live here. Platform-agnostic and device-free (the null backend);
 * real device backends join per-platform in later phases.
 * Public contract: include/anoptic_audio.h. */

#include "audio_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_logging.h>

// Guard the ring element sizes (copied per push/pop, sized capacity * these).
_Static_assert(sizeof(AnoAudioEvent) <= 32u, "AnoAudioEvent grew past 32 bytes; revisit the events ring");
_Static_assert(sizeof(AnoAudioCommand) <= 160u, "AnoAudioCommand grew past 160 bytes; revisit the command ring");

// The audio world singleton. One per program, owned by the engine entry point.
static AnoAudioMixer *g_mixer;
static mi_heap_t     *g_heap;

static const AnoAudioDeviceApi *backend_api(AnoAudioBackend which)
{
    switch (which) {
    case ANO_AUDIO_BACKEND_NULL_DEV: return ano_audio_device_null();
#if defined(__linux__)
    case ANO_AUDIO_BACKEND_PIPEWIRE: return ano_audio_device_pipewire();
    case ANO_AUDIO_BACKEND_ALSA:     return ano_audio_device_alsa();
#endif
    default: return NULL; // not built on this platform
    }
}

// ANO_AUDIO_BACKEND (pipewire | alsa | null) overrides the config for testing.
static AnoAudioBackend backend_env_override(AnoAudioBackend want)
{
    const char *env = getenv("ANO_AUDIO_BACKEND");
    if (!env || !env[0])
        return want;
    if (strcmp(env, "null") == 0)     return ANO_AUDIO_BACKEND_NULL_DEV;
    if (strcmp(env, "pipewire") == 0) return ANO_AUDIO_BACKEND_PIPEWIRE;
    if (strcmp(env, "alsa") == 0)     return ANO_AUDIO_BACKEND_ALSA;
    ano_log(ANO_WARN, "audio: unknown ANO_AUDIO_BACKEND '%s'; ignored.", env);
    return want;
}

bool ano_audio_bridge_init(AnoAudioBridge *bridge, mi_heap_t *heap,
                           uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2)
{
    if (!bridge || !heap)
        return false;
    if (!ano_audio_ring_init(&bridge->commands, heap, cmd_capacity_pow2, (uint32_t)sizeof(AnoAudioCommand)))
        return false;
    if (!ano_audio_ring_init(&bridge->events, heap, evt_capacity_pow2, (uint32_t)sizeof(AnoAudioEvent))) {
        ano_audio_ring_destroy(&bridge->commands);
        return false;
    }
    // Published latest-wins lanes start unpublished (version 0): until logic
    // publishes a listener the mixer keeps its default field, and until the
    // mixer publishes a block logic's telemetry acquire fails.
    memset(&bridge->listener, 0, sizeof bridge->listener);
    memset(&bridge->telemetry, 0, sizeof bridge->telemetry);
    atomic_init(&bridge->listenerVersion, 0u);
    atomic_init(&bridge->telemetryVersion, 0u);
    return true;
}

void ano_audio_bridge_destroy(AnoAudioBridge *bridge)
{
    if (!bridge)
        return;
    ano_audio_ring_destroy(&bridge->commands);
    ano_audio_ring_destroy(&bridge->events);
}

bool ano_audio_init(const AnoAudioConfig *cfg)
{
    if (g_mixer) {
        ano_log(ANO_WARN, "audio: init called with the audio world already up; ignored.");
        return false;
    }

    // granted configuration: zero fields take defaults
    AnoAudioConfig c = cfg ? *cfg : (AnoAudioConfig){0};
    uint32_t rate      = c.sampleRate ? c.sampleRate : 48000u;
    uint32_t bf        = c.blockFrames ? c.blockFrames : 512u;
    if (bf < 32u) bf = 32u;
    if (bf > 4096u) bf = 4096u;
    uint32_t devBlocks = c.deviceBlocks ? c.deviceBlocks : 4u;
    uint32_t cmdCap    = c.cmdCapacity ? c.cmdCapacity : 1024u;
    uint32_t evtCap    = c.evtCapacity ? c.evtCapacity : 1024u;
    uint32_t buses     = c.busCount ? c.busCount : 2u;
    if (buses > ANO_AUDIO_MAX_BUSES) {
        ano_log(ANO_WARN, "audio: busCount %u clamped to %u.", buses, ANO_AUDIO_MAX_BUSES);
        buses = ANO_AUDIO_MAX_BUSES;
    }

    mi_heap_t *heap = mi_heap_new();
    if (!heap)
        return false;

    // the ring cursors carry _Alignas(ANO_THREAD_LINE); a heap owner must request it
    AnoAudioMixer *mx = mi_heap_malloc_aligned(heap, sizeof *mx, _Alignof(AnoAudioMixer));
    if (!mx)
        goto fail_heap;
    memset(mx, 0, sizeof *mx);
    mx->heap            = heap;
    mx->sampleRate      = rate;
    mx->blockFrames     = bf;
    mx->busCount        = buses;
    mx->smoothCoef      = expf(-1.0f / (0.030f * (float)rate));
    mx->smoothCoefBlock = expf(-(float)bf / (0.030f * (float)rate));
    atomic_init(&mx->underruns, 0u);
    atomic_init(&mx->mixerRun, false);
    atomic_init(&mx->deviceRun, false);

    AnoAudioBridge *bridge = mi_heap_malloc_aligned(heap, sizeof *bridge, _Alignof(AnoAudioBridge));
    if (!bridge)
        goto fail_heap;
    memset(bridge, 0, sizeof *bridge);
    if (!ano_audio_bridge_init(bridge, heap, cmdCap, evtCap))
        goto fail_heap;
    mx->bridge = bridge;

    // cooked-block lane + both sides' scratch
    const uint32_t blockStride = bf * ANO_AUDIO_CHANNELS * (uint32_t)sizeof(float);
    if (!ano_audio_ring_init(&mx->blockRing, heap, devBlocks, blockStride))
        goto fail_heap;
    mx->blockScratch  = mi_heap_calloc(heap, (size_t)bf * ANO_AUDIO_CHANNELS, sizeof(float));
    mx->deviceScratch = mi_heap_calloc(heap, (size_t)bf * ANO_AUDIO_CHANNELS, sizeof(float));
    if (!mx->blockScratch || !mx->deviceScratch)
        goto fail_heap;
    if (!ano_audio_graph_init(mx, c.busLayout)) {
        ano_log(ANO_ERROR, "audio: bad bus layout (a parent must precede its children).");
        goto fail_heap;
    }

    // backend selection: explicit config (or env override) demands one backend;
    // AUTO cascades platform-best-first and cannot fail (null always opens)
    AnoAudioBackend want = backend_env_override((AnoAudioBackend)c.backend);
    if (want == ANO_AUDIO_BACKEND_AUTO) {
        static const AnoAudioBackend cascade[] = {
#if defined(__linux__)
            ANO_AUDIO_BACKEND_PIPEWIRE,
            ANO_AUDIO_BACKEND_ALSA,
#endif
            ANO_AUDIO_BACKEND_NULL_DEV,
        };
        for (size_t i = 0; i < sizeof cascade / sizeof cascade[0] && !mx->device; ++i) {
            const AnoAudioDeviceApi *api = backend_api(cascade[i]);
            if (api && api->start(mx))
                mx->device = api;
        }
    } else {
        const AnoAudioDeviceApi *api = backend_api(want);
        if (!api) {
            ano_log(ANO_ERROR, "audio: backend %u is not available on this platform.", want);
            goto fail_heap;
        }
        if (api->start(mx))
            mx->device = api;
    }
    if (!mx->device) {
        ano_log(ANO_ERROR, "audio: no device backend could start.");
        goto fail_heap;
    }

    atomic_store_explicit(&mx->mixerRun, true, memory_order_release);
    if (ano_thread_create(&mx->mixerThread, NULL, ano_audio_mixer_main, mx) != 0) {
        mx->device->stop(mx);
        goto fail_heap;
    }

    g_mixer = mx;
    g_heap  = heap;
    ano_log(ANO_INFO, "audio: up — backend=%s, %u Hz, block %u (%u deep), %u buses.",
            mx->device->name, rate, bf, devBlocks, buses);
    return true;

fail_heap:
    mi_heap_destroy(heap); // releases mixer, bridge, rings, scratch in one sweep
    return false;
}

void ano_audio_shutdown(void)
{
    if (!g_mixer)
        return;
    AnoAudioMixer *mx = g_mixer;

    // stop the producer first, then the consumer; no submit may race teardown
    atomic_store_explicit(&mx->mixerRun, false, memory_order_release);
    ano_thread_join(mx->mixerThread, NULL);
    mx->device->stop(mx);

    ano_audio_bridge_destroy(mx->bridge);
    ano_audio_ring_destroy(&mx->blockRing);
    g_mixer = NULL;
    mi_heap_destroy(g_heap); // releases everything else in one sweep
    g_heap = NULL;
    ano_log(ANO_INFO, "audio: down.");
}

AnoAudioBridge *anoAudioBridge(void)
{
    return g_mixer ? g_mixer->bridge : NULL;
}

// Public producer endpoints (anoptic_audio.h). Non-inline, reached through the
// opaque handle. The mixer-side halves stay inlined in audio_bridge.h.
bool ano_audio_submit(AnoAudioBridge *bridge, const AnoAudioCommand *cmd)
{
    return ano_audio_ring_push(&bridge->commands, cmd);
}

bool ano_audio_poll_event(AnoAudioBridge *bridge, AnoAudioEvent *out)
{
    return ano_audio_ring_pop(&bridge->events, out);
}

void ano_audio_publish_listener(AnoAudioBridge *bridge, const AnoAudioListener *l)
{
    ano_audio_seq_store(&bridge->listener, &bridge->listenerVersion, l, sizeof *l);
}

bool ano_audio_acquire_telemetry(AnoAudioBridge *bridge, AnoAudioTelemetry *out)
{
    return ano_audio_seq_load(&bridge->telemetry, &bridge->telemetryVersion, out, sizeof *out);
}

// Buffer producer endpoints. Registration packs the samples behind a block
// header into one owned allocation the mixer adopts; the caller's array need
// only live until the call returns. The block rides home for freeing in
// AEVT_BUFFER_RETIRED after release. Same backpressure contract as submit.
bool ano_audio_buffer_register(AnoAudioBridge *bridge, uint32_t buffer_id,
                               const float *interleaved, uint64_t frames, uint32_t channels)
{
    if (!bridge || !interleaved || frames == 0u || channels < 1u || channels > 2u)
        return false;
    uint64_t bytes64 = frames * channels * sizeof(float);
    if (bytes64 > SIZE_MAX - sizeof(AnoAudioBlockHeader))
        return false;
    AnoAudioBlockHeader *h = mi_malloc(sizeof *h + (size_t)bytes64);
    if (!h)
        return false;
    h->frames   = frames;
    h->channels = channels;
    h->pad      = 0u;
    memcpy(h + 1, interleaved, (size_t)bytes64);
    AnoAudioCommand c = { .kind = ACMD_BUFFER_REGISTER, .source_id = buffer_id, .block = h };
    if (!ano_audio_ring_push(&bridge->commands, &c)) {
        mi_free(h);
        return false; // backpressure: caller retries
    }
    return true;
}

bool ano_audio_buffer_release(AnoAudioBridge *bridge, uint32_t buffer_id)
{
    AnoAudioCommand c = { .kind = ACMD_BUFFER_RELEASE, .source_id = buffer_id };
    return ano_audio_ring_push(&bridge->commands, &c);
}

void ano_audio_block_free(void *block)
{
    mi_free(block);
}
