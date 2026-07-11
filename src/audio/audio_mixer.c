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

bool ano_audio_graph_init(AnoAudioMixer *mx, const AnoAudioBusDesc *layout)
{
    const size_t samples = (size_t)mx->blockFrames * ANO_AUDIO_CHANNELS;
    for (uint32_t b = 0; b < mx->busCount; ++b) {
        AnoAudioBus *bus = &mx->buses[b];
        uint32_t parent = 0u;
        float    gain   = 1.0f;
        if (layout) {
            if (b > 0u && layout[b].parent >= b)
                return false; // parents must precede children (acyclic by construction)
            parent = layout[b].parent;
            if (layout[b].gain != 0.0f)
                gain = layout[b].gain;
        }
        bus->parent = b == 0u ? 0u : parent;
        bus->mix = mi_heap_calloc(mx->heap, samples, sizeof(float));
        if (!bus->mix)
            return false;
        ano_audio_smooth_snap(&bus->gain, gain);
        bus->gain.coef = mx->smoothCoef;

        // insert chain: from the layout, or the default lone FILTER on aux buses
        for (uint32_t s = 0; s < ANO_AUDIO_MAX_FX; ++s) {
            uint32_t kind = ANO_AUDIO_FX_NONE;
            if (layout)
                kind = layout[b].fx[s];
            else if (b > 0u && s == 0u)
                kind = ANO_AUDIO_FX_FILTER;
            if (!ano_audio_fx_init(&bus->fx[s], kind, mx->heap, mx->sampleRate, mx->smoothCoefBlock))
                return false;
        }

        // post-fader sends: targets must precede the sender; 0 = unused
        for (uint32_t s = 0; s < ANO_AUDIO_MAX_SENDS; ++s) {
            uint32_t target = layout ? layout[b].sendTarget[s] : 0u;
            float    level  = layout ? layout[b].sendLevel[s] : 0.0f;
            if (target != 0u && (b == 0u || target >= b || target >= mx->busCount))
                return false; // master cannot send; sends must point at earlier buses
            bus->sends[s].target = target;
            ano_audio_smooth_snap(&bus->sends[s].level, clampf(level, 0.0f, 4.0f));
            bus->sends[s].level.coef = mx->smoothCoef;
        }
    }
    // Source and buffer pools start all-FREE (the mixer struct is zeroed at birth).
    return true;
}

// ---------------------------------------------------------------------------
// Spatialization
// ---------------------------------------------------------------------------
// Per block, per positional voice: derive pan (lateral component against the
// listener frame), distance attenuation (clamped inverse-distance), and an
// air-absorption lowpass target. The derived values glide through the voice's
// smoothers, so listener/source motion never zippers. Front/back is flat
// (no HRTF yet — the pan stage is that seam).

static void source_spatialize(AnoAudioMixer *mx, AnoAudioSource *s)
{
    float panT = 0.0f, spatT = 1.0f, airT = 20000.0f;
    if ((s->flags & ANO_AUDIO_SOURCE_POSITIONAL) && mx->listenerValid) {
        const AnoAudioListener *l = &mx->listener;
        float dx = s->position[0] - l->pos[0];
        float dy = s->position[1] - l->pos[1];
        float dz = s->position[2] - l->pos[2];
        float d  = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > 1.0e-4f) {
            // listener right = forward x up
            float rx = l->forward[1] * l->up[2] - l->forward[2] * l->up[1];
            float ry = l->forward[2] * l->up[0] - l->forward[0] * l->up[2];
            float rz = l->forward[0] * l->up[1] - l->forward[1] * l->up[0];
            float rl = sqrtf(rx * rx + ry * ry + rz * rz);
            if (rl > 1.0e-6f) {
                panT = clampf((dx * rx + dy * ry + dz * rz) / (d * rl), -1.0f, 1.0f);
            }
            float dc = clampf(d, s->minDist, s->maxDist);
            spatT = s->minDist / (s->minDist + s->rolloff * (dc - s->minDist));
            airT  = clampf(20000.0f * (s->minDist / dc), 1200.0f, 20000.0f); // tuning
        }
    } else if (!(s->flags & ANO_AUDIO_SOURCE_POSITIONAL)) {
        return; // pan/spatGain stay caller-driven
    }
    s->pan.target       = panT;
    s->spatGain.target  = spatT;
    s->airCutoff.target = airT;
    float fc   = ano_audio_smooth_step(&s->airCutoff); // per-block cadence
    s->airCoef = 1.0f - expf(-ANO_AUDIO_TAU_F * fc / (float)mx->sampleRate);
    if (s->airLp < 1.0e-20f && s->airLp > -1.0e-20f)
        s->airLp = 0.0f; // denormal flush
}

// ---------------------------------------------------------------------------
// Voice rendering
// ---------------------------------------------------------------------------

// in:  slot in PLAYING/STOPPING, its bus accumulation buffer, frames, 1/rate
// out: slot state advanced; may flip to RETIRING mid-block (rest of block silent)
// inv: smoothers advance once per FRAME so a skipped sample can never shift them
static void source_render(AnoAudioSource *s, float *mix, uint32_t frames, float fsInv)
{
    const bool loop       = (s->flags & ANO_AUDIO_SOURCE_LOOP) != 0u;
    const bool positional = (s->flags & ANO_AUDIO_SOURCE_POSITIONAL) != 0u;

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
        float p   = ano_audio_smooth_step(&s->pan);
        float sg  = ano_audio_smooth_step(&s->spatGain);
        float amp = g * sg;

        float vl, vr;
        if (s->kind == ANO_AUDIO_SOURCE_TONE) {
            float f = ano_audio_smooth_step(&s->freq);
            s->phase += (double)(f * fsInv);
            if (s->phase >= 1.0)
                s->phase -= 1.0;
            vl = vr = sinf((float)s->phase * ANO_AUDIO_TAU_F);
        } else { // BUFFER
            float r = ano_audio_smooth_step(&s->rate);
            uint64_t i0 = (uint64_t)s->cursor;
            if (i0 >= s->bufFrames) {
                if (!loop) {
                    // data ended: release ramp over silence
                    if (s->state == ANO_AUDIO_SRC_PLAYING) {
                        s->state = ANO_AUDIO_SRC_STOPPING;
                        s->gain.target = 0.0f;
                    }
                    vl = vr = 0.0f;
                    goto place;
                }
                while (s->cursor >= (double)s->bufFrames)
                    s->cursor -= (double)s->bufFrames;
                i0 = (uint64_t)s->cursor;
            }
            uint64_t i1 = i0 + 1u;
            if (i1 >= s->bufFrames)
                i1 = loop ? 0u : s->bufFrames - 1u;
            float fr = (float)(s->cursor - (double)i0);
            const float *d = s->bufData;
            if (s->bufChannels == 1u) {
                float v = d[i0] + (d[i1] - d[i0]) * fr;
                vl = vr = v;
            } else {
                vl = d[2u * i0] + (d[2u * i1] - d[2u * i0]) * fr;
                vr = d[2u * i0 + 1u] + (d[2u * i1 + 1u] - d[2u * i0 + 1u]) * fr;
            }
            s->cursor += (double)r;
        place:;
        }

        if (positional && s->bufChannels <= 1u) {
            // air absorption on the mono signal, pre-pan
            s->airLp += s->airCoef * (vl - s->airLp);
            vl = vr = s->airLp;
        }

        if (s->kind == ANO_AUDIO_SOURCE_BUFFER && s->bufChannels == 2u) {
            // stereo material: pan is a linear balance, unity at center
            float bl = p > 0.0f ? 1.0f - p : 1.0f;
            float br = p < 0.0f ? 1.0f + p : 1.0f;
            mix[2u * i]      += vl * amp * bl;
            mix[2u * i + 1u] += vr * amp * br;
        } else {
            // mono signal: constant-power pan
            float a = (p + 1.0f) * ANO_AUDIO_PI4_F;
            mix[2u * i]      += vl * amp * cosf(a);
            mix[2u * i + 1u] += vr * amp * sinf(a);
        }
        if (s->remaining != UINT64_MAX)
            s->remaining--;
    }
}

// ---------------------------------------------------------------------------
// Command application
// ---------------------------------------------------------------------------

static AnoAudioBufferSlot *buffer_find(AnoAudioMixer *mx, uint32_t buffer_id, uint32_t state)
{
    for (uint32_t i = 0; i < ANO_AUDIO_MAX_BUFFERS; ++i)
        if (mx->buffers[i].state == state && mx->buffers[i].buffer_id == buffer_id)
            return &mx->buffers[i];
    return NULL;
}

// Reject an adopted block: send it home for freeing, or (events ring full /
// offline) free it here — the documented control-path exception.
static void buffer_reject(AnoAudioMixer *mx, uint32_t buffer_id, const void *block)
{
    if (mx->bridge) {
        AnoAudioEvent evt = { .kind = AEVT_BUFFER_RETIRED,
                              .u.buffer = { .buffer_id = buffer_id, .block = (void *)block } };
        if (ano_audio_emit_event(mx->bridge, &evt))
            return;
    }
    mi_free((void *)block);
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
        AnoAudioBufferSlot *buf = NULL;
        if (d->kind == ANO_AUDIO_SOURCE_BUFFER) {
            buf = buffer_find(mx, d->buffer_id, ANO_AUDIO_BUF_LIVE);
            if (!buf) {
                ano_debug_log(ANO_WARN, "audio: PLAY id %u names unknown buffer %u; dropped.",
                              cmd->source_id, d->buffer_id);
                return;
            }
        } else if (d->kind != ANO_AUDIO_SOURCE_TONE) {
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

        memset(slot, 0, sizeof *slot);
        slot->state     = ANO_AUDIO_SRC_PLAYING;
        slot->source_id = cmd->source_id;
        slot->bus       = d->bus;
        slot->kind      = d->kind;
        slot->flags     = d->flags;
        slot->remaining = d->durationFrames ? d->durationFrames : UINT64_MAX;
        if (buf) {
            slot->bufSlot     = (uint32_t)(buf - mx->buffers);
            slot->bufChannels = buf->channels;
            slot->bufFrames   = buf->frames;
            slot->bufData     = buf->data;
            if (buf->channels == 2u && (slot->flags & ANO_AUDIO_SOURCE_POSITIONAL)) {
                ano_debug_log(ANO_WARN, "audio: PLAY id %u: stereo buffers are not positional.",
                              cmd->source_id);
                slot->flags &= ~(uint32_t)ANO_AUDIO_SOURCE_POSITIONAL;
            }
        }
        // spatial model inputs (defaults are tuning)
        slot->position[0] = d->position[0];
        slot->position[1] = d->position[1];
        slot->position[2] = d->position[2];
        slot->minDist = d->minDist > 0.01f ? d->minDist : 1.0f;
        slot->maxDist = d->maxDist > slot->minDist ? d->maxDist : (d->maxDist == 0.0f ? 50.0f : slot->minDist);
        slot->rolloff = d->rolloff > 0.0f ? d->rolloff : 1.0f;

        // gain fades in from silence; everything else snaps (nothing sounds yet)
        slot->gain = (AnoAudioSmooth){ .y = 0.0f, .target = clampf(d->gain, 0.0f, 4.0f), .coef = mx->smoothCoef };
        slot->pan.coef      = mx->smoothCoef;
        slot->freq.coef     = mx->smoothCoef;
        slot->rate.coef     = mx->smoothCoef;
        slot->spatGain.coef = mx->smoothCoef;
        slot->airCutoff.coef = mx->smoothCoefBlock;
        ano_audio_smooth_snap(&slot->pan, clampf(d->pan, -1.0f, 1.0f));
        ano_audio_smooth_snap(&slot->freq, clampf(d->freqHz, 0.0f, nyquist));
        ano_audio_smooth_snap(&slot->rate, clampf(d->rate == 0.0f ? 1.0f : d->rate, 0.05f, 8.0f));
        ano_audio_smooth_snap(&slot->spatGain, 1.0f);
        ano_audio_smooth_snap(&slot->airCutoff, 20000.0f);
        slot->airCoef = 1.0f;
        // positional voices enter at their spatial truth, not a glide from center
        if (slot->flags & ANO_AUDIO_SOURCE_POSITIONAL) {
            source_spatialize(mx, slot);
            ano_audio_smooth_snap(&slot->pan, slot->pan.target);
            ano_audio_smooth_snap(&slot->spatGain, slot->spatGain.target);
            ano_audio_smooth_snap(&slot->airCutoff, slot->airCutoff.target);
        }
        return;
    }

    case ACMD_SOURCE_UPDATE: {
        for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
            AnoAudioSource *s = &mx->sources[i];
            if (s->state != ANO_AUDIO_SRC_PLAYING || s->source_id != cmd->source_id)
                continue;
            if (cmd->fields & ANO_AUDIO_FIELD_GAIN)
                s->gain.target = clampf(cmd->gain, 0.0f, 4.0f);
            if ((cmd->fields & ANO_AUDIO_FIELD_PAN) && !(s->flags & ANO_AUDIO_SOURCE_POSITIONAL))
                s->pan.target = clampf(cmd->pan, -1.0f, 1.0f);
            if (cmd->fields & ANO_AUDIO_FIELD_FREQ)
                s->freq.target = clampf(cmd->freqHz, 0.0f, nyquist);
            if (cmd->fields & ANO_AUDIO_FIELD_RATE)
                s->rate.target = clampf(cmd->rate == 0.0f ? 1.0f : cmd->rate, 0.05f, 8.0f);
            if (cmd->fields & ANO_AUDIO_FIELD_POSITION) {
                s->position[0] = cmd->position[0];
                s->position[1] = cmd->position[1];
                s->position[2] = cmd->position[2];
            }
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
        AnoAudioBus *bus = &mx->buses[cmd->bus];
        if (cmd->fields & ANO_AUDIO_FIELD_GAIN)
            bus->gain.target = clampf(cmd->gain, 0.0f, 4.0f);
        if ((cmd->fields & ANO_AUDIO_FIELD_SEND0) && bus->sends[0].target != 0u)
            bus->sends[0].level.target = clampf(cmd->send[0], 0.0f, 4.0f);
        if ((cmd->fields & ANO_AUDIO_FIELD_SEND1) && bus->sends[1].target != 0u)
            bus->sends[1].level.target = clampf(cmd->send[1], 0.0f, 4.0f);
        return;
    }

    case ACMD_FX_SET: {
        if (cmd->bus >= mx->busCount || cmd->fxSlot >= ANO_AUDIO_MAX_FX) {
            ano_debug_log(ANO_WARN, "audio: FX_SET names bus %u slot %u; dropped.", cmd->bus, cmd->fxSlot);
            return;
        }
        AnoAudioFx *fx = &mx->buses[cmd->bus].fx[cmd->fxSlot];
        if (fx->kind == ANO_AUDIO_FX_NONE) {
            ano_debug_log(ANO_WARN, "audio: FX_SET on empty slot %u of bus %u; dropped.",
                          cmd->fxSlot, cmd->bus);
            return;
        }
        ano_audio_fx_set(fx, cmd->paramId, cmd->value);
        return;
    }

    case ACMD_BUFFER_REGISTER: {
        if (!mx->bridge) {
            ano_debug_log(ANO_WARN, "audio: BUFFER_REGISTER ignored offline (use desc.buffers).");
            return;
        }
        if (!cmd->block)
            return;
        const AnoAudioBlockHeader *h = cmd->block;
        if (h->frames == 0u || h->channels < 1u || h->channels > 2u) {
            ano_debug_log(ANO_WARN, "audio: buffer %u rejected (bad header).", cmd->source_id);
            buffer_reject(mx, cmd->source_id, cmd->block);
            return;
        }
        if (buffer_find(mx, cmd->source_id, ANO_AUDIO_BUF_LIVE)
            || buffer_find(mx, cmd->source_id, ANO_AUDIO_BUF_RETIRING)) {
            ano_debug_log(ANO_WARN, "audio: buffer id %u already resident; rejected.", cmd->source_id);
            buffer_reject(mx, cmd->source_id, cmd->block);
            return;
        }
        AnoAudioBufferSlot *slot = NULL;
        for (uint32_t i = 0; i < ANO_AUDIO_MAX_BUFFERS; ++i) {
            if (mx->buffers[i].state == ANO_AUDIO_BUF_FREE) {
                slot = &mx->buffers[i];
                break;
            }
        }
        if (!slot) {
            AnoAudioEvent evt = { .kind = AEVT_CAPACITY };
            ano_audio_emit_event(mx->bridge, &evt);
            ano_debug_log(ANO_WARN, "audio: buffer table full; %u rejected.", cmd->source_id);
            buffer_reject(mx, cmd->source_id, cmd->block);
            return;
        }
        slot->state     = ANO_AUDIO_BUF_LIVE;
        slot->buffer_id = cmd->source_id;
        slot->channels  = h->channels;
        slot->owned     = true;
        slot->frames    = h->frames;
        slot->data      = (const float *)(h + 1);
        slot->block     = (void *)cmd->block;
        return;
    }

    case ACMD_BUFFER_RELEASE: {
        AnoAudioBufferSlot *slot = buffer_find(mx, cmd->source_id, ANO_AUDIO_BUF_LIVE);
        if (!slot)
            return; // unknown / already retiring: no-op (idempotent)
        slot->state = ANO_AUDIO_BUF_RETIRING;
        uint32_t idx = (uint32_t)(slot - mx->buffers);
        for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
            AnoAudioSource *s = &mx->sources[i];
            if (s->state == ANO_AUDIO_SRC_PLAYING && s->kind == ANO_AUDIO_SOURCE_BUFFER
                && s->bufSlot == idx) {
                s->state = ANO_AUDIO_SRC_STOPPING;
                s->gain.target = 0.0f;
            }
        }
        return;
    }

    case ACMD_MUSIC_AFFECT:
    case ACMD_MUSIC_KEY:
    case ACMD_MUSIC_MOTIF:
    case ACMD_MUSIC_OVERRIDE:
    case ACMD_MUSIC_RELEASE:
        // the mixer owns no music: it forwards these, it does not read them
        if (mx->generatorControl)
            mx->generatorControl(mx->generatorUser, cmd);
        return;

    default:
        ano_debug_log(ANO_WARN, "audio: unknown command kind %u; dropped.", cmd->kind);
        return;
    }
}

// ---------------------------------------------------------------------------
// Block rendering
// ---------------------------------------------------------------------------

// The bus's insert chain over its accumulated block, in place. Parameters
// glide at block cadence (~86 steps/s through the 30 ms pole — no zipper).
static void bus_chain_block(AnoAudioMixer *mx, AnoAudioBus *bus, uint32_t frames)
{
    for (uint32_t s = 0; s < ANO_AUDIO_MAX_FX; ++s)
        ano_audio_fx_process(&bus->fx[s], bus->mix, frames, mx->sampleRate);
}

void ano_audio_render_block(AnoAudioMixer *mx, float *out)
{
    const uint32_t frames = mx->blockFrames;
    const size_t   bytes  = (size_t)frames * ANO_AUDIO_CHANNELS * sizeof(float);
    const float    fsInv  = 1.0f / (float)mx->sampleRate;

    for (uint32_t b = 0; b < mx->busCount; ++b)
        memset(mx->buses[b].mix, 0, bytes);

    // attached generator writes into the zeroed bus mixes ahead of the fold
    if (mx->generator) {
        float *busMix[ANO_AUDIO_MAX_BUSES];
        for (uint32_t b = 0; b < mx->busCount; ++b)
            busMix[b] = mx->buses[b].mix;
        mx->generator(mx->generatorUser, busMix, mx->busCount, frames,
                      mx->blockIndex * (uint64_t)frames);
    }

    // voices: spatial targets per block, then accumulate into their bus
    uint32_t active = 0;
    for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES; ++i) {
        AnoAudioSource *s = &mx->sources[i];
        if (s->state == ANO_AUDIO_SRC_PLAYING || s->state == ANO_AUDIO_SRC_STOPPING) {
            source_spatialize(mx, s);
            source_render(s, mx->buses[s->bus].mix, frames, fsInv);
            ++active;
        }
    }
    mx->sourcesActive = active;

    // bus tree fold: children (higher index) run their chain, then the faded
    // signal feeds the parent and any post-fader sends (targets precede the
    // sender, so a send lands in a bus not yet processed this block)
    for (uint32_t b = mx->busCount; b-- > 1u;) {
        AnoAudioBus *bus = &mx->buses[b];
        bus_chain_block(mx, bus, frames);
        float *parent = mx->buses[bus->parent].mix;
        float *sm0 = bus->sends[0].target ? mx->buses[bus->sends[0].target].mix : NULL;
        float *sm1 = bus->sends[1].target ? mx->buses[bus->sends[1].target].mix : NULL;
        for (uint32_t i = 0; i < frames; ++i) {
            float g = ano_audio_smooth_step(&bus->gain);
            float l = bus->mix[2u * i] * g;
            float r = bus->mix[2u * i + 1u] * g;
            parent[2u * i]      += l;
            parent[2u * i + 1u] += r;
            if (sm0) {
                float s0 = ano_audio_smooth_step(&bus->sends[0].level);
                sm0[2u * i]      += l * s0;
                sm0[2u * i + 1u] += r * s0;
            }
            if (sm1) {
                float s1 = ano_audio_smooth_step(&bus->sends[1].level);
                sm1[2u * i]      += l * s1;
                sm1[2u * i + 1u] += r * s1;
            }
        }
    }

    // master: chain, gain, peak meter, clip guard, output write
    AnoAudioBus *master = &mx->buses[0];
    bus_chain_block(mx, master, frames);
    float peak = 0.0f;
    uint32_t clipped = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        float g = ano_audio_smooth_step(&master->gain);
        for (uint32_t ch = 0; ch < ANO_AUDIO_CHANNELS; ++ch) {
            float v = master->mix[ANO_AUDIO_CHANNELS * i + ch] * g;
            float a = v < 0.0f ? -v : v;
            if (a > peak) peak = a;
            if (v > ANO_AUDIO_CLIP_CEIL) { v = ANO_AUDIO_CLIP_CEIL; clipped++; }
            else if (v < -ANO_AUDIO_CLIP_CEIL) { v = -ANO_AUDIO_CLIP_CEIL; clipped++; }
            out[ANO_AUDIO_CHANNELS * i + ch] = v;
        }
    }
    mx->masterPeak = peak;
    mx->clippedSamples += clipped;

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

    // buffer retirement: only once no sounding voice references the slot
    for (uint32_t b = 0; b < ANO_AUDIO_MAX_BUFFERS; ++b) {
        AnoAudioBufferSlot *slot = &mx->buffers[b];
        if (slot->state != ANO_AUDIO_BUF_RETIRING)
            continue;
        bool inUse = false;
        for (uint32_t i = 0; i < ANO_AUDIO_MAX_SOURCES && !inUse; ++i) {
            AnoAudioSource *s = &mx->sources[i];
            inUse = (s->state == ANO_AUDIO_SRC_PLAYING || s->state == ANO_AUDIO_SRC_STOPPING)
                    && s->kind == ANO_AUDIO_SOURCE_BUFFER && s->bufSlot == b;
        }
        if (inUse)
            continue;
        if (slot->owned && mx->bridge) {
            AnoAudioEvent evt = { .kind = AEVT_BUFFER_RETIRED,
                                  .u.buffer = { .buffer_id = slot->buffer_id, .block = slot->block } };
            if (!ano_audio_emit_event(mx->bridge, &evt))
                continue; // retry next block; the block stays valid meanwhile
        }
        memset(slot, 0, sizeof *slot); // FREE
    }

    // generator events (a composing generator's facts about the block that just
    // sounded), lossless on the same terms: staged here, offered until taken.
    if (mx->generatorPoll && mx->bridge) {
        if (mx->genPendingCount < ANO_AUDIO_GEN_EVENTS)
            mx->genPendingCount += mx->generatorPoll(
                mx->generatorUser, mx->genPending + mx->genPendingCount,
                ANO_AUDIO_GEN_EVENTS - mx->genPendingCount);
        uint32_t sent = 0;
        while (sent < mx->genPendingCount
               && ano_audio_emit_event(mx->bridge, &mx->genPending[sent]))
            sent++;
        if (sent) {
            mx->genPendingCount -= sent;
            memmove(mx->genPending, mx->genPending + sent,
                    (size_t)mx->genPendingCount * sizeof *mx->genPending);
        }
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
        AnoAudioListener l;
        if (ano_audio_acquire_listener(mx->bridge, &l)) {
            mx->listener = l;
            mx->listenerValid = true;
        }

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
            .blockIndex     = mx->blockIndex,
            .blockCpuNs     = ano_ticks_to_ns(t1 - t0),
            .masterPeak     = mx->masterPeak,
            .underruns      = atomic_load_explicit(&mx->underruns, memory_order_relaxed),
            .sourcesActive  = mx->sourcesActive,
            .sampleRate     = mx->sampleRate,
            .blockFrames    = mx->blockFrames,
            .clippedSamples = mx->clippedSamples,
        };
        if (mx->generatorStats)
            mx->generatorStats(mx->generatorUser, &t); // the generator meters itself
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
    if ((d.eventCount > 0u && !d.events) || (d.bufferCount > 0u && !d.buffers)
        || (d.listenerCount > 0u && !d.listeners))
        return false;
    if (d.bufferCount > ANO_AUDIO_MAX_BUFFERS)
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
    mx->heap            = heap;
    mx->sampleRate      = rate;
    mx->blockFrames     = bf;
    mx->busCount        = buses;
    mx->smoothCoef      = expf(-1.0f / (0.030f * (float)rate));
    mx->smoothCoefBlock = expf(-(float)bf / (0.030f * (float)rate));
    mx->bridge          = NULL;
    mx->generator        = d.generator;
    mx->generatorUser    = d.generatorUser;
    mx->generatorControl = d.generatorControl;
    if (!ano_audio_graph_init(mx, d.busLayout))
        return false;
    float *scratch = mi_heap_calloc(heap, (size_t)bf * ANO_AUDIO_CHANNELS, sizeof(float));
    if (!scratch)
        return false;

    // borrowed buffers, pre-registered
    for (uint32_t i = 0; i < d.bufferCount; ++i) {
        const AnoAudioOfflineBuffer *ob = &d.buffers[i];
        if (!ob->data || ob->frames == 0u || ob->channels < 1u || ob->channels > 2u)
            return false;
        mx->buffers[i] = (AnoAudioBufferSlot){
            .state     = ANO_AUDIO_BUF_LIVE,
            .buffer_id = ob->buffer_id,
            .channels  = ob->channels,
            .owned     = false,
            .frames    = ob->frames,
            .data      = ob->data,
            .block     = NULL,
        };
    }

    uint32_t ei = 0, li = 0;
    uint64_t done = 0;
    while (done < frames) {
        // commands and listener poses apply at the first block boundary at-or-after their stamp
        while (li < d.listenerCount && d.listeners[li].frame <= done) {
            mx->listener = d.listeners[li++].listener;
            mx->listenerValid = true;
        }
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
