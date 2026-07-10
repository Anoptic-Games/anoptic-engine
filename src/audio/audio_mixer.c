/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* The mixer core: command application, block rendering, the realtime block
 * loop, and the offline driver. Everything here runs on exactly one thread at
 * a time — the mixer thread (realtime) or the caller (offline) — so the graph
 * needs no synchronization beyond the bridge rings feeding it. */

#include "audio_internal.h"

#include <math.h>
#include <string.h>

#include <anoptic_logging.h>
#include <anoptic_time.h>

#define ANO_AUDIO_TAU_F  6.28318530717958647692f // 2*pi
#define ANO_AUDIO_PI4_F  0.78539816339744830962f // pi/4

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

bool ano_audio_graph_init(AnoAudioMixer *mx)
{
    const size_t samples = (size_t)mx->blockFrames * ANO_AUDIO_CHANNELS;
    for (uint32_t b = 0; b < mx->busCount; ++b) {
        mx->buses[b].mix = mi_heap_calloc(mx->heap, samples, sizeof(float));
        if (!mx->buses[b].mix)
            return false;
        ano_audio_smooth_snap(&mx->buses[b].gain, 1.0f);
        mx->buses[b].gain.coef = mx->smoothCoef;
    }
    // Source pool starts all-FREE (the mixer struct is zeroed at birth).
    return true;
}

// in:  slot in PLAYING/STOPPING, its bus accumulation buffer, frames, 1/rate
// out: slot state advanced; may flip to RETIRING mid-block (rest of block silent)
// inv: smoothers advance once per FRAME so a skipped sample can never shift them
static void source_render(AnoAudioSource *s, float *mix, uint32_t frames, float fsInv)
{
    for (uint32_t i = 0; i < frames; ++i) {
        // duration expiry begins the same release ramp a STOP does
        if (s->remaining == 0u && s->state == ANO_AUDIO_SRC_PLAYING) {
            s->state = ANO_AUDIO_SRC_STOPPING;
            s->gain.target = 0.0f;
        }
        float g = ano_audio_smooth_step(&s->gain);
        if (s->state == ANO_AUDIO_SRC_STOPPING && g < ANO_AUDIO_RETIRE_EPS) {
            s->state = ANO_AUDIO_SRC_RETIRING;
            return;
        }
        float p = ano_audio_smooth_step(&s->pan);
        float f = ano_audio_smooth_step(&s->freq);
        s->phase += (double)(f * fsInv);
        if (s->phase >= 1.0)
            s->phase -= 1.0;
        float smp = sinf((float)s->phase * ANO_AUDIO_TAU_F) * g;
        // constant-power pan
        float a = (p + 1.0f) * ANO_AUDIO_PI4_F;
        mix[2u * i]      += smp * cosf(a);
        mix[2u * i + 1u] += smp * sinf(a);
        if (s->remaining != UINT64_MAX)
            s->remaining--;
    }
}

void ano_audio_apply(AnoAudioMixer *mx, const AnoAudioCommand *cmd)
{
    const float nyquist = 0.5f * (float)mx->sampleRate;

    switch ((AnoAudioCommandKind)cmd->kind) {

    case ACMD_SOURCE_PLAY: {
        const AnoAudioSourceDesc *d = &cmd->desc;
        if (d->bus >= mx->busCount) {
            ano_debug_log(ANO_WARN, "audio: PLAY id %u names bus %u of %u; dropped.",
                          cmd->source_id, d->bus, mx->busCount);
            return;
        }
        if (d->kind != ANO_AUDIO_SOURCE_TONE) {
            ano_debug_log(ANO_WARN, "audio: PLAY id %u has unsupported kind %u; dropped.",
                          cmd->source_id, d->kind);
            return;
        }
        // restart in place when the id is already resident (supersedes any pending retirement)
        AnoAudioSource *slot = NULL;
        for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
            if (mx->sources[i].state != ANO_AUDIO_SRC_FREE && mx->sources[i].source_id == cmd->source_id) {
                slot = &mx->sources[i];
                break;
            }
        }
        if (!slot) {
            for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
                if (mx->sources[i].state == ANO_AUDIO_SRC_FREE) {
                    slot = &mx->sources[i];
                    break;
                }
            }
        }
        if (!slot) {
            // voice pool exhausted: drop the cue, advise best-effort
            if (mx->bridge) {
                AnoAudioEvent evt = { .kind = AEVT_CAPACITY };
                ano_audio_emit_event(mx->bridge, &evt);
            }
            ano_debug_log(ANO_WARN, "audio: voice pool full; PLAY id %u dropped.", cmd->source_id);
            return;
        }
        slot->state     = ANO_AUDIO_SRC_PLAYING;
        slot->source_id = cmd->source_id;
        slot->bus       = d->bus;
        slot->kind      = d->kind;
        slot->phase     = 0.0;
        slot->remaining = d->durationFrames ? d->durationFrames : UINT64_MAX;
        // gain fades in from silence; pan and freq snap (nothing sounds yet)
        slot->gain = (AnoAudioSmooth){ .y = 0.0f, .target = clampf(d->gain, 0.0f, 4.0f), .coef = mx->smoothCoef };
        slot->pan.coef  = mx->smoothCoef;
        slot->freq.coef = mx->smoothCoef;
        ano_audio_smooth_snap(&slot->pan, clampf(d->pan, -1.0f, 1.0f));
        ano_audio_smooth_snap(&slot->freq, clampf(d->freqHz, 0.0f, nyquist));
        return;
    }

    case ACMD_SOURCE_UPDATE: {
        for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
            AnoAudioSource *s = &mx->sources[i];
            if (s->state != ANO_AUDIO_SRC_PLAYING || s->source_id != cmd->source_id)
                continue;
            if (cmd->fields & ANO_AUDIO_FIELD_GAIN)
                s->gain.target = clampf(cmd->gain, 0.0f, 4.0f);
            if (cmd->fields & ANO_AUDIO_FIELD_PAN)
                s->pan.target = clampf(cmd->pan, -1.0f, 1.0f);
            if (cmd->fields & ANO_AUDIO_FIELD_FREQ)
                s->freq.target = clampf(cmd->freqHz, 0.0f, nyquist);
            return;
        }
        return; // unknown / already-retired id: no-op (idempotent)
    }

    case ACMD_SOURCE_STOP: {
        for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
            AnoAudioSource *s = &mx->sources[i];
            if (s->state == ANO_AUDIO_SRC_PLAYING && s->source_id == cmd->source_id) {
                s->state = ANO_AUDIO_SRC_STOPPING;
                s->gain.target = 0.0f;
                return;
            }
        }
        return; // unknown / already-stopping id: no-op (idempotent)
    }

    case ACMD_BUS_SET: {
        if (cmd->bus >= mx->busCount) {
            ano_debug_log(ANO_WARN, "audio: BUS_SET names bus %u of %u; dropped.", cmd->bus, mx->busCount);
            return;
        }
        if (cmd->fields & ANO_AUDIO_FIELD_GAIN)
            mx->buses[cmd->bus].gain.target = clampf(cmd->gain, 0.0f, 4.0f);
        return;
    }

    default:
        ano_debug_log(ANO_WARN, "audio: unknown command kind %u; dropped.", cmd->kind);
        return;
    }
}

void ano_audio_render_block(AnoAudioMixer *mx, float *out)
{
    const uint32_t frames  = mx->blockFrames;
    const size_t   bytes   = (size_t)frames * ANO_AUDIO_CHANNELS * sizeof(float);
    const float    fsInv   = 1.0f / (float)mx->sampleRate;

    for (uint32_t b = 0; b < mx->busCount; ++b)
        memset(mx->buses[b].mix, 0, bytes);

    // voices accumulate into their bus
    uint32_t active = 0;
    for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
        AnoAudioSource *s = &mx->sources[i];
        if (s->state == ANO_AUDIO_SRC_PLAYING || s->state == ANO_AUDIO_SRC_STOPPING) {
            source_render(s, mx->buses[s->bus].mix, frames, fsInv);
            ++active;
        }
    }
    mx->sourcesActive = active;

    // aux buses fold into master through their smoothed gains
    float *master = mx->buses[0].mix;
    for (uint32_t b = 1; b < mx->busCount; ++b) {
        AnoAudioBus *bus = &mx->buses[b];
        for (uint32_t i = 0; i < frames; ++i) {
            float g = ano_audio_smooth_step(&bus->gain);
            master[2u * i]      += bus->mix[2u * i] * g;
            master[2u * i + 1u] += bus->mix[2u * i + 1u] * g;
        }
    }

    // master gain, output write, peak meter
    float peak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float g = ano_audio_smooth_step(&mx->buses[0].gain);
        float l = master[2u * i] * g;
        float r = master[2u * i + 1u] * g;
        out[2u * i]      = l;
        out[2u * i + 1u] = r;
        float al = l < 0.0f ? -l : l;
        float ar = r < 0.0f ? -r : r;
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
    }
    mx->masterPeak = peak;

    // retirement pass: lossless facts re-emit until the ring takes them
    for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
        AnoAudioSource *s = &mx->sources[i];
        if (s->state != ANO_AUDIO_SRC_RETIRING)
            continue;
        if (mx->bridge) {
            AnoAudioEvent evt = { .kind = AEVT_SOURCE_RETIRED, .u.source_id = s->source_id };
            if (!ano_audio_emit_event(mx->bridge, &evt))
                continue; // events ring full: keep the slot, retry next block
        }
        s->state = ANO_AUDIO_SRC_FREE;
    }

    mx->blockIndex++;
}

void *ano_audio_mixer_main(void *arg)
{
    AnoAudioMixer *mx = arg;
    while (atomic_load_explicit(&mx->mixerRun, memory_order_acquire)) {
        // structural change lands only here, at the block boundary
        AnoAudioCommand c;
        while (ano_audio_next_command(mx->bridge, &c))
            ano_audio_apply(mx, &c);

        // pace off ring occupancy: the device drains one block per period
        if (ano_audio_ring_full(&mx->blockRing)) {
            ano_sleep(1000);
            continue;
        }

        uint64_t t0 = ano_timestamp_ticks();
        ano_audio_render_block(mx, mx->blockScratch);
        uint64_t t1 = ano_timestamp_ticks();
        ano_audio_ring_push(&mx->blockRing, mx->blockScratch); // sole producer; space checked above

        AnoAudioTelemetry t = {
            .blockIndex    = mx->blockIndex,
            .blockCpuNs    = ano_ticks_to_ns(t1 - t0),
            .masterPeak    = mx->masterPeak,
            .underruns     = atomic_load_explicit(&mx->underruns, memory_order_relaxed),
            .sourcesActive = mx->sourcesActive,
            .sampleRate    = mx->sampleRate,
            .blockFrames   = mx->blockFrames,
        };
        ano_audio_publish_telemetry(mx->bridge, &t);
    }
    return NULL;
}

// Offline driver: the same graph and apply/render core, driven synchronously on
// the calling thread with no device, no telemetry, and no clock reads —
// identical desc and frame count render byte-identical output (the CI gate).
bool ano_audio_render_offline(const AnoAudioOfflineDesc *desc, float *out, uint64_t frames)
{
    if (!out)
        return false;
    AnoAudioOfflineDesc d = desc ? *desc : (AnoAudioOfflineDesc){0};
    if (d.eventCount > 0u && !d.events)
        return false;
    uint32_t rate = d.sampleRate ? d.sampleRate : 48000u;
    uint32_t bf   = d.blockFrames ? d.blockFrames : 512u;
    if (bf < 32u) bf = 32u;
    if (bf > 4096u) bf = 4096u;
    uint32_t buses = d.busCount ? d.busCount : 2u;
    if (buses > ANO_AUDIO_MAX_BUSES)
        return false;

    // scoped heap: every allocation below dies with it, on every return path
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (!heap)
        return false;
    AnoAudioMixer *mx = mi_heap_malloc_aligned(heap, sizeof *mx, _Alignof(AnoAudioMixer));
    if (!mx)
        return false;
    memset(mx, 0, sizeof *mx);
    mx->heap        = heap;
    mx->sampleRate  = rate;
    mx->blockFrames = bf;
    mx->busCount    = buses;
    mx->smoothCoef  = expf(-1.0f / (0.030f * (float)rate));
    mx->bridge      = NULL;
    if (!ano_audio_graph_init(mx))
        return false;
    float *scratch = mi_heap_calloc(heap, (size_t)bf * ANO_AUDIO_CHANNELS, sizeof(float));
    if (!scratch)
        return false;

    uint32_t ei   = 0;
    uint64_t done = 0;
    while (done < frames) {
        // commands apply at the first block boundary at-or-after their stamp
        while (ei < d.eventCount && d.events[ei].frame <= done)
            ano_audio_apply(mx, &d.events[ei++].cmd);
        ano_audio_render_block(mx, scratch);
        uint64_t n = frames - done;
        if (n > bf) n = bf;
        memcpy(out + done * ANO_AUDIO_CHANNELS, scratch, (size_t)n * ANO_AUDIO_CHANNELS * sizeof(float));
        done += n;
    }
    return true;
}
