/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Logic<->audio transport (private to src/audio/): SPSC rings + seqlock lanes.
// Completes opaque AnoAudioBridge. Public contract: include/anoptic_audio.h.
//   logic --AnoAudioCommand--> mixer   (commands, lossless)
//   mixer --AnoAudioEvent----> logic   (events. retirement re-emits until landed)
//   logic publishes AnoAudioListener   (seqlock, latest-wins)
//   mixer publishes AnoAudioTelemetry  (seqlock, latest-wins)

#ifndef ANO_AUDIO_BRIDGE_INTERNAL_H
#define ANO_AUDIO_BRIDGE_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <mimalloc.h>
#include <anoptic_audio.h>
#include <anoptic_threads_typed.h>


/* Bridge */

static_assert(ano::TransportData<AnoAudioCommand>);
static_assert(ano::TransportData<AnoAudioEvent>);
static_assert(ano::TransportData<AnoAudioListener>);
static_assert(ano::TransportData<AnoAudioTelemetry>);

struct AnoAudioBridge
{
    ano::SpscRing<AnoAudioCommand> commands; // logic -> mixer
    ano::SpscRing<AnoAudioEvent> events;     // mixer -> logic

    ano::SeqPub<AnoAudioListener> listener;   // logic -> mixer
    ano::SeqPub<AnoAudioTelemetry> telemetry; // mixer -> logic
};

// Rings from heap. Destroy bridge before releasing heap.
bool ano_audio_bridge_init(AnoAudioBridge *bridge, mi_heap_t *heap,
                           uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2);

void ano_audio_bridge_destroy(AnoAudioBridge *bridge);

// Public producer endpoints live non-inline in ano_audio.c.


/* Mixer endpoints */

static inline bool ano_audio_next_command(AnoAudioBridge *bridge, AnoAudioCommand *out)
{
    return bridge->commands.pop(*out);
}

// false if full. Must not block: drop CAPACITY; re-emit retirement next block.
static inline bool ano_audio_emit_event(AnoAudioBridge *bridge, const AnoAudioEvent *evt)
{
    return bridge->events.push(*evt);
}

static inline void ano_audio_publish_telemetry(AnoAudioBridge *bridge, const AnoAudioTelemetry *t)
{
    bridge->telemetry.publish(*t);
}

// false before first listener publish.
static inline bool ano_audio_acquire_listener(AnoAudioBridge *bridge, AnoAudioListener *out)
{
    return bridge->listener.acquire(*out);
}

#endif // ANO_AUDIO_BRIDGE_INTERNAL_H
