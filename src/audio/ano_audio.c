/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Audio lifecycle + public bridge endpoints. Hot-path push/pop stay in audio_bridge.h.

#include "audio_internal.h"
#include "audio_command_contract.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_log.h>

// Ring element size budget.
static_assert(sizeof(AnoAudioEvent) <= 32u, "AnoAudioEvent grew past 32 bytes; revisit the events ring");
static_assert(sizeof(AnoAudioCommand) <= 192u, "AnoAudioCommand grew past 192 bytes; revisit the command ring");

// Header + interleaved payload share one alloc; header must not misalign float payload.
static_assert(alignof(AnoAudioBlockHeader) >= alignof(float)
                   && sizeof(AnoAudioBlockHeader) % alignof(float) == 0u,
               "AnoAudioBlockHeader must not misalign the payload sharing its allocation");

// Audio world singleton.
static AnoAudioMixer *g_mixer;
static mi_heap_t     *g_heap;

static const AnoAudioDeviceApi *backend_api(AnoAudioBackend which)
{
    switch (which) {
    case ANO_AUDIO_BACKEND_NULL_DEV: return ano_audio_device_null();
#if defined(_WIN32)
    case ANO_AUDIO_BACKEND_WASAPI: return ano_audio_device_wasapi();
    case ANO_AUDIO_BACKEND_DSOUND: return ano_audio_device_dsound();
#elif defined(__APPLE__)
    case ANO_AUDIO_BACKEND_COREAUDIO: return ano_audio_device_coreaudio();
#elif defined(__linux__)
    case ANO_AUDIO_BACKEND_PIPEWIRE: return ano_audio_device_pipewire();
    case ANO_AUDIO_BACKEND_ALSA:     return ano_audio_device_alsa();
#endif
    case ANO_AUDIO_BACKEND_AUTO:
#if !defined(_WIN32)
    case ANO_AUDIO_BACKEND_WASAPI:
    case ANO_AUDIO_BACKEND_DSOUND:
#endif
#if !defined(__APPLE__)
    case ANO_AUDIO_BACKEND_COREAUDIO:
#endif
#if !defined(__linux__)
    case ANO_AUDIO_BACKEND_PIPEWIRE:
    case ANO_AUDIO_BACKEND_ALSA:
#endif
        return NULL;
    }
    return NULL;
}

// ANO_AUDIO_BACKEND env override.
static AnoAudioBackend backend_env_override(AnoAudioBackend want)
{
    const char *env = getenv("ANO_AUDIO_BACKEND");
    if (!env || !env[0])
        return want;
    for (size_t i = 0; i < ANO_AUDIO_BACKEND_COUNT; ++i)
        if (strcmp(env, ANO_AUDIO_BACKEND_NAMES.values[i]) == 0)
            return static_cast<AnoAudioBackend>(i + 1u);
    ano_log(ANO_WARN, "audio: unknown ANO_AUDIO_BACKEND '%s'; ignored.", env);
    return want;
}

bool ano_audio_bridge_init(AnoAudioBridge *bridge, mi_heap_t *heap,
                           uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2)
{
    if (!bridge || !heap)
        return false;
    const auto allocate = [heap](size_t count, size_t width) -> void* {
        return mi_heap_calloc(heap, count, width);
    };
    if (!bridge->commands.initialize(cmd_capacity_pow2, allocate))
        return false;
    if (!bridge->events.initialize(evt_capacity_pow2, allocate)) {
        bridge->commands.destroy([](void* memory) { mi_free(memory); });
        return false;
    }
    bridge->listener.initialize();
    bridge->telemetry.initialize();
    return true;
}

void ano_audio_bridge_destroy(AnoAudioBridge *bridge)
{
    if (!bridge)
        return;
    bridge->commands.destroy([](void* memory) { mi_free(memory); });
    bridge->events.destroy([](void* memory) { mi_free(memory); });
}

bool ano_audio_init(const AnoAudioConfig *cfg)
{
    if (g_mixer) {
        ano_log(ANO_WARN, "audio: init called with the audio world already up; ignored.");
        return false;
    }

    // zero fields -> defaults
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
    AnoAudioMixer *mx;
    AnoAudioBridge *bridge;
    uint32_t blockStride;
    AnoAudioBackend want;
    const AnoAudioFormat mixFormat = ano_audio_mix_format(rate);

    mi_heap_t *heap = mi_heap_new();
    if (!heap)
        return false;

    // ring cursors carry alignas(ANO_THREAD_LINE); heap owner must request it
    mx = static_cast<AnoAudioMixer *>(
        mi_heap_malloc_aligned(heap, sizeof *mx, alignof(AnoAudioMixer)));
    if (!mx)
        goto fail_heap;
    memset(mx, 0, sizeof *mx);
    mx->heap            = heap;
    mx->sampleRate      = rate;
    mx->blockFrames     = bf;
    mx->busCount        = buses;
    mx->smoothCoef      = expf(-1.0f / (0.030f * (float)rate));
    mx->smoothCoefBlock = expf(-(float)bf / (0.030f * (float)rate));
    mx->generator         = c.generator;
    mx->generatorUser     = c.generatorUser;
    mx->generatorControl  = c.generatorControl;
    mx->generatorPoll     = c.generatorPoll;
    mx->generatorStats    = c.generatorStats;
    mx->generatorCommands = c.generatorCommands;
    atomic_init(&mx->underruns, 0u);
    atomic_init(&mx->mixerRun, false);
    atomic_init(&mx->deviceRun, false);

    bridge = static_cast<AnoAudioBridge *>(
        mi_heap_malloc_aligned(heap, sizeof *bridge, alignof(AnoAudioBridge)));
    if (!bridge)
        goto fail_heap;
    memset(bridge, 0, sizeof *bridge);
    if (!ano_audio_bridge_init(bridge, heap, cmdCap, evtCap))
        goto fail_heap;
    mx->bridge = bridge;

    blockStride = bf * ANO_AUDIO_CHANNELS * (uint32_t)sizeof(float);
    if (!mx->blockRing.initialize(devBlocks, blockStride,
            [heap](size_t count, size_t width) -> void* {
                return mi_heap_calloc(heap, count, width);
            }))
        goto fail_heap;
    mx->blockScratch = static_cast<float *>(
        mi_heap_calloc(heap, (size_t)bf * ANO_AUDIO_CHANNELS, sizeof(float)));
    mx->deviceScratch = static_cast<float *>(
        mi_heap_calloc(heap, (size_t)bf * ANO_AUDIO_CHANNELS, sizeof(float)));
    if (!mx->blockScratch || !mx->deviceScratch)
        goto fail_heap;
    if (!ano_audio_graph_init(mx, c.busLayout)) {
        ano_log(ANO_ERROR, "audio: bad bus layout (a parent must precede its children).");
        goto fail_heap;
    }

    // AUTO cascades to null. Named backend must open.
    want = backend_env_override((AnoAudioBackend)c.backend);
    if (want == ANO_AUDIO_BACKEND_AUTO) {
        static const AnoAudioBackend cascade[] = {
#if defined(_WIN32)
            ANO_AUDIO_BACKEND_WASAPI,
            ANO_AUDIO_BACKEND_DSOUND,
#elif defined(__APPLE__)
            ANO_AUDIO_BACKEND_COREAUDIO,
#elif defined(__linux__)
            ANO_AUDIO_BACKEND_PIPEWIRE,
            ANO_AUDIO_BACKEND_ALSA,
#endif
            ANO_AUDIO_BACKEND_NULL_DEV,
        };
        for (size_t i = 0; i < sizeof cascade / sizeof cascade[0] && !mx->device; ++i) {
            const AnoAudioDeviceApi *api = backend_api(cascade[i]);
            if (api && ano_audio_backend_supports(api->backend, mixFormat) && api->start(mx))
                mx->device = api;
        }
    } else {
        const AnoAudioDeviceApi *api = backend_api(want);
        if (!api) {
            ano_log(ANO_ERROR, "audio: backend %u is not available on this platform.", want);
            goto fail_heap;
        }
        if (!ano_audio_backend_supports(api->backend, mixFormat)) {
            ano_log(ANO_ERROR, "audio: backend %s does not support the engine mix contract.",
                    ano_audio_backend_name(api->backend));
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
    ano_log(ANO_INFO, "audio: up 〜 backend=%s, %u Hz, block %u (%u deep), %u buses.",
            ano_audio_backend_name(mx->device->backend), rate, bf, devBlocks, buses);
    return true;

fail_heap:
    mi_heap_destroy(heap);
    return false;
}

// Free adopted blocks (cmd regs, unpolled retires, owned slots) via ano_audio_block_free.
// in:  mx joined, device stopped, producer idle
// out: bridge rings empty; owned buffer slots FREE
// inv: single-threaded; populations disjoint; module-heap untouched; borrowed stays with producer
static void audio_discharge_blocks(AnoAudioMixer *mx)
{
    AnoAudioCommand cmd;
    while (mx->bridge->commands.pop(cmd)) {
        const AnoAudioCommandContract *contract = ano::audio_contract::commands.find(cmd.kind);
        if (contract && contract->ownership == AnoAudioPayloadOwnership::adopted)
            ano_audio_block_free(
                const_cast<void *>(ano::audio_contract::pointer_payload(cmd)));
    }

    AnoAudioEvent evt;
    while (mx->bridge->events.pop(evt)) {
        const AnoAudioEventContract *contract = ano::audio_contract::events.find(static_cast<size_t>(evt.kind));
        if (contract && contract->ownership == AnoAudioPayloadOwnership::returned)
            ano_audio_block_free(evt.u.buffer.block);
    }

    for (uint32_t i = 0; i < ANO_AUDIO_MAX_BUFFERS; ++i) {
        AnoAudioBufferSlot *slot = &mx->buffers[i];
        if (slot->state == ANO_AUDIO_BUF_FREE || !slot->owned)
            continue; // borrowed data is the producer's
        ano_audio_block_free(slot->block);
        memset(slot, 0, sizeof *slot);
    }
}

void ano_audio_shutdown(void)
{
    if (!g_mixer)
        return;
    AnoAudioMixer *mx = g_mixer;

    // Stop mixer (producer) then device (consumer).
    atomic_store_explicit(&mx->mixerRun, false, memory_order_release);
    ano_thread_join(mx->mixerThread, NULL);
    mx->device->stop(mx);

    // Both threads are down: nothing else can reach the rings or the buffer table.
    audio_discharge_blocks(mx);

    ano_audio_bridge_destroy(mx->bridge);
    mx->blockRing.destroy([](void* memory) { mi_free(memory); });
    g_mixer = NULL;
    mi_heap_destroy(g_heap);
    g_heap = NULL;
    ano_log(ANO_INFO, "audio: down.");
}

AnoAudioBridge *anoAudioBridge(void)
{
    return g_mixer ? g_mixer->bridge : NULL;
}

/* Public producer endpoints */

bool ano_audio_submit(AnoAudioBridge *bridge, const AnoAudioCommand *cmd)
{
    return bridge->commands.push(*cmd);
}

bool ano_audio_poll_event(AnoAudioBridge *bridge, AnoAudioEvent *out)
{
    return bridge->events.pop(*out);
}

void ano_audio_publish_listener(AnoAudioBridge *bridge, const AnoAudioListener *l)
{
    bridge->listener.publish(*l);
}

bool ano_audio_acquire_telemetry(AnoAudioBridge *bridge, AnoAudioTelemetry *out)
{
    return bridge->telemetry.acquire(*out);
}

/* Buffer producer endpoints */

bool ano_audio_buffer_register(AnoAudioBridge *bridge, uint32_t buffer_id,
                               const float *interleaved, uint64_t frames, uint32_t channels)
{
    if (!bridge || !interleaved || frames == 0u || channels < 1u || channels > 2u)
        return false;
    // Overflow guard: divide before multiply.
    const uint64_t stride = (uint64_t)channels * sizeof(float);
    if (frames > (SIZE_MAX - sizeof(AnoAudioBlockHeader)) / stride)
        return false;
    uint64_t bytes64 = frames * stride;
    AnoAudioBlockHeader *h = static_cast<AnoAudioBlockHeader *>(
        mi_malloc(sizeof *h + (size_t)bytes64));
    if (!h)
        return false;
    h->frames   = frames;
    h->channels = channels;
    h->pad      = 0u;
    memcpy(h + 1, interleaved, (size_t)bytes64);
    AnoAudioCommand c = { .kind = ACMD_BUFFER_REGISTER, .source_id = buffer_id, .block = h };
    if (!bridge->commands.push(c)) {
        mi_free(h);
        return false; // backpressure
    }
    return true;
}

bool ano_audio_buffer_release(AnoAudioBridge *bridge, uint32_t buffer_id)
{
    AnoAudioCommand c = { .kind = ACMD_BUFFER_RELEASE, .source_id = buffer_id };
    return bridge->commands.push(c);
}

void ano_audio_block_free(void *block)
{
    mi_free(block);
}
