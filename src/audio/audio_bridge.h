/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Logic<->audio transport (private to src/audio/): SPSC rings + seqlock lanes.
// Completes opaque AnoAudioBridge. Public contract: include/anoptic_audio.h.
// Ring/seqlock mirror src/render_bridge/render_bridge.h (audio-local names).
//
//   logic --AnoAudioCommand--> mixer   (commands, lossless)
//   mixer --AnoAudioEvent----> logic   (events. retirement re-emits until landed)
//   logic publishes AnoAudioListener   (seqlock, latest-wins)
//   mixer publishes AnoAudioTelemetry  (seqlock, latest-wins)

#ifndef ANO_AUDIO_BRIDGE_INTERNAL_H
#define ANO_AUDIO_BRIDGE_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <mimalloc.h>
#include <anoptic_memory.h> // ANO_CACHE_LINE / ANO_THREAD_LINE
#include <anoptic_audio.h>


/* Bounded SPSC ring */

// Lock-free SPSC over fixed POD. capacity pow2. Producer owns tail, consumer head.
// Each reads the other with acquire and publishes its own with release.
// Heap owners: allocate the ring owner with ANO_THREAD_LINE alignment so tail/head stay apart.
typedef struct AnoAudioRing
{
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t tail; // next write index
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t head; // next read index
    _Alignas(ANO_THREAD_LINE) uint32_t mask;         // capacity - 1
    uint32_t                          stride;       // element bytes
    uint8_t                          *buffer;       // capacity * stride
} AnoAudioRing;

// Smallest pow2 >= v, floor 2. 0 on overflow (v > 2^31).
static inline uint32_t ano_audio_next_pow2(uint32_t v)
{
    if (v < 2u) return 2u;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u; // wraps to 0 if v was > 2^31
}

// capacity rounded up to pow2 (>= 2). false on bad args or alloc failure.
static inline bool ano_audio_ring_init(AnoAudioRing *ring, mi_heap_t *heap,
                                       uint32_t capacity_pow2, uint32_t stride)
{
    if (!ring || !heap || stride == 0u) return false;
    uint32_t cap = ano_audio_next_pow2(capacity_pow2);
    if (cap == 0u) return false;                       // capacity overflow
    if ((size_t)cap > SIZE_MAX / stride) return false; // cap*stride overflow
    uint8_t *buffer = mi_heap_calloc(heap, cap, stride);
    if (!buffer) return false;
    atomic_init(&ring->tail, 0u);
    atomic_init(&ring->head, 0u);
    ring->mask   = cap - 1u;
    ring->stride = stride;
    ring->buffer = buffer;
    return true;
}

// Free ring buffer. Does not free the heap.
static inline void ano_audio_ring_destroy(AnoAudioRing *ring)
{
    if (!ring) return;
    if (ring->buffer) {
        mi_free(ring->buffer);
        ring->buffer = NULL;
    }
    ring->mask   = 0u;
    ring->stride = 0u;
    atomic_store_explicit(&ring->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0u, memory_order_relaxed);
}

// PRODUCER. Copy stride bytes. false if full.
static inline bool ano_audio_ring_push(AnoAudioRing *ring, const void *elem)
{
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    if ((tail - head) > ring->mask) // (tail - head) == capacity means full
        return false;
    uint8_t *slot = ring->buffer + (size_t)(tail & ring->mask) * ring->stride;
    for (uint32_t i = 0; i < ring->stride; ++i)
        slot[i] = ((const uint8_t *)elem)[i];
    atomic_store_explicit(&ring->tail, tail + 1u, memory_order_release);
    return true;
}

// CONSUMER. Copy next element into out. false if empty.
static inline bool ano_audio_ring_pop(AnoAudioRing *ring, void *out)
{
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (head == tail)
        return false;
    const uint8_t *slot = ring->buffer + (size_t)(head & ring->mask) * ring->stride;
    for (uint32_t i = 0; i < ring->stride; ++i)
        ((uint8_t *)out)[i] = slot[i];
    atomic_store_explicit(&ring->head, head + 1u, memory_order_release);
    return true;
}

// PRODUCER. true when push would fail.
static inline bool ano_audio_ring_full(const AnoAudioRing *ring)
{
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    return (tail - head) > ring->mask;
}


/* Latest-wins seqlock */

// Even version = stable. Odd = mid-write. version 0 = never published.
// value: _Atomic word lane, sizeof(payload)/8 entries (asserted at the bridge).
// Relaxed word copies keep concurrent store/load defined; torn loads are
// discarded by the version recheck, exactly as before.
static inline void ano_audio_seq_store(_Atomic uint64_t *value, _Atomic uint64_t *version, const void *v, size_t stride)
{
    uint64_t s = atomic_load_explicit(version, memory_order_relaxed);
    atomic_store_explicit(version, s + 1u, memory_order_relaxed); // enter (odd)
    atomic_thread_fence(memory_order_release);
    for (size_t i = 0; i < stride / 8u; ++i) {
        uint64_t w;
        memcpy(&w, (const uint8_t *)v + 8u * i, 8u);
        atomic_store_explicit(&value[i], w, memory_order_relaxed);
    }
    atomic_store_explicit(version, s + 2u, memory_order_release); // exit (even)
}

// false if never published. Else copies an untorn value.
static inline bool ano_audio_seq_load(const _Atomic uint64_t *value, const _Atomic uint64_t *version, void *out, size_t stride)
{
    for (;;) {
        uint64_t s1 = atomic_load_explicit(version, memory_order_acquire);
        if (s1 == 0u) return false;
        if (s1 & 1u) continue;
        for (size_t i = 0; i < stride / 8u; ++i) {
            uint64_t w = atomic_load_explicit(&value[i], memory_order_relaxed);
            memcpy((uint8_t *)out + 8u * i, &w, 8u);
        }
        atomic_thread_fence(memory_order_acquire);
        uint64_t s2 = atomic_load_explicit(version, memory_order_relaxed);
        if (s1 == s2) return true;
    }
}


/* Bridge */

_Static_assert(sizeof(_Atomic uint64_t) == sizeof(uint64_t), "seqlock lanes assume plain-width atomics");
_Static_assert(sizeof(AnoAudioListener)  % 8u == 0, "seqlock lane copies whole 64-bit words");
_Static_assert(sizeof(AnoAudioTelemetry) % 8u == 0, "seqlock lane copies whole 64-bit words");

struct AnoAudioBridge
{
    AnoAudioRing commands; // logic -> mixer
    AnoAudioRing events;   // mixer -> logic

    // Seqlock lanes store object representation as atomic words; typed access
    // only ever happens through the publish/acquire copies.
    _Atomic uint64_t listener[sizeof(AnoAudioListener) / sizeof(uint64_t)];
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t listenerVersion;
    _Atomic uint64_t telemetry[sizeof(AnoAudioTelemetry) / sizeof(uint64_t)];
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t telemetryVersion;
};

// Rings from heap. Destroy bridge before releasing heap.
bool ano_audio_bridge_init(AnoAudioBridge *bridge, mi_heap_t *heap,
                           uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2);

void ano_audio_bridge_destroy(AnoAudioBridge *bridge);

// Public producer endpoints live non-inline in ano_audio.c.


/* Mixer endpoints */

static inline bool ano_audio_next_command(AnoAudioBridge *bridge, AnoAudioCommand *out)
{
    return ano_audio_ring_pop(&bridge->commands, out);
}

// false if full. Must not block: drop CAPACITY; re-emit retirement next block.
static inline bool ano_audio_emit_event(AnoAudioBridge *bridge, const AnoAudioEvent *evt)
{
    return ano_audio_ring_push(&bridge->events, evt);
}

static inline void ano_audio_publish_telemetry(AnoAudioBridge *bridge, const AnoAudioTelemetry *t)
{
    ano_audio_seq_store(bridge->telemetry, &bridge->telemetryVersion, t, sizeof *t);
}

// false before first listener publish.
static inline bool ano_audio_acquire_listener(AnoAudioBridge *bridge, AnoAudioListener *out)
{
    return ano_audio_seq_load(bridge->listener, &bridge->listenerVersion, out, sizeof *out);
}

#endif // ANO_AUDIO_BRIDGE_INTERNAL_H
