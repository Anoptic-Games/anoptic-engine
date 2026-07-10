/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * audio_bridge.h (private to src/)
 * The logic<->audio transport: the SPSC rings that carry the protocol, the
 * seqlock lanes for latest-wins state, and the bridge struct completing the
 * opaque AnoAudioBridge handle. Consumed only within src/audio/. Never include
 * from include/. The PUBLIC contract (command/event protocol, opaque handle,
 * ano_audio_submit) is include/anoptic_audio.h.
 *
 * The ring and seqlock are deliberate copies of src/render_bridge/render_bridge.h
 * with audio-local names (the originals export non-inline init symbols, and
 * private headers do not cross modules). Same semantics, same alignment
 * discipline. Migrate BOTH bridges to anoptic_collections.h when the generic
 * lock-free collections land — the audio bridge is the second consumer that
 * justifies the promotion.
 *
 * Two one-way streams plus two published lanes:
 *   logic master --AnoAudioCommand--> mixer thread   (commands ring, lossless)
 *   mixer thread --AnoAudioEvent----> logic master   (events ring; retirement
 *                                     facts are re-emitted until they land)
 *   logic publishes AnoAudioListener  (seqlock, latest-wins)
 *   mixer publishes AnoAudioTelemetry (seqlock, latest-wins)
 */

#ifndef ANO_AUDIO_BRIDGE_INTERNAL_H
#define ANO_AUDIO_BRIDGE_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <mimalloc.h>
#include <anoptic_memory.h> // ANO_CACHE_LINE / ANO_THREAD_LINE
#include <anoptic_audio.h>  // command/event protocol + opaque AnoAudioBridge

// ---------------------------------------------------------------------------
// Bounded SPSC ring (copy of render_bridge.h AnoSpscRing; see header comment)
// ---------------------------------------------------------------------------

// Single-producer/single-consumer bounded ring over fixed-size POD elements.
// Lock-free and wait-free both ends, capacity a power of two so index wrap is a
// mask. The producer owns `tail`, the consumer owns `head`. Each reads the
// other with acquire and publishes its own with release. tail and head sit on
// separate ANO_THREAD_LINE regions; a HEAP owner must request that alignment
// (e.g. mi_heap_malloc_aligned) for the separation to hold.
typedef struct AnoAudioRing
{
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t tail; // producer-owned cursor: next index to write
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t head; // consumer-owned cursor: next index to read
    _Alignas(ANO_THREAD_LINE) uint32_t mask;         // capacity - 1 (immutable after init)
    uint32_t                          stride;       // element size in bytes
    uint8_t                          *buffer;       // capacity * stride bytes
} AnoAudioRing;

// Smallest power of two >= v, floor of 2. Returns 0 on overflow (v > 2^31).
static inline uint32_t ano_audio_next_pow2(uint32_t v)
{
    if (v < 2u) return 2u;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u; // wraps to 0 if v was > 2^31
}

// in:  ring, heap, capacity_pow2 (rounded up to a power of two, >= 2), stride (> 0)
// out: true on success; false on bad args or allocation failure
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

// Releases the ring buffer. Does not release the backing heap.
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

// PRODUCER only. Copies `stride` bytes from `elem` into the ring.
// out: false if the ring is full (caller decides: drop, retry, or backpressure upstream).
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

// CONSUMER only. Copies the next element into `out` (>= stride bytes).
// out: false if the ring is empty.
static inline bool ano_audio_ring_pop(AnoAudioRing *ring, void *out)
{
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (head == tail) // empty
        return false;
    const uint8_t *slot = ring->buffer + (size_t)(head & ring->mask) * ring->stride;
    for (uint32_t i = 0; i < ring->stride; ++i)
        ((uint8_t *)out)[i] = slot[i];
    atomic_store_explicit(&ring->head, head + 1u, memory_order_release);
    return true;
}

// PRODUCER only. true when a push would fail — lets the mixer skip rendering a
// block it could not deliver.
static inline bool ano_audio_ring_full(const AnoAudioRing *ring)
{
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    return (tail - head) > ring->mask;
}

// ---------------------------------------------------------------------------
// Lock-free latest-wins seqlock (copy of render_bridge.h; see header comment)
// ---------------------------------------------------------------------------
// A single producer writes the value guarded by an even/odd version: even ==
// stable, odd == mid-write. The consumer copies and retries iff the version
// moved across its copy. version == 0 means "never published".
static inline void ano_audio_seq_store(void *value, _Atomic uint64_t *version, const void *v, size_t stride)
{
    uint64_t s = atomic_load_explicit(version, memory_order_relaxed); // single producer owns version
    atomic_store_explicit(version, s + 1u, memory_order_relaxed);     // enter write (odd)
    atomic_thread_fence(memory_order_release);                        // odd marker before the value writes
    for (size_t i = 0; i < stride; ++i) ((uint8_t *)value)[i] = ((const uint8_t *)v)[i];
    atomic_store_explicit(version, s + 2u, memory_order_release);     // exit write (even) + publish writes
}

// out: false (out untouched) if nothing published yet. Otherwise copies a CONSISTENT (untorn) value.
static inline bool ano_audio_seq_load(const void *value, const _Atomic uint64_t *version, void *out, size_t stride)
{
    for (;;) {
        uint64_t s1 = atomic_load_explicit(version, memory_order_acquire);
        if (s1 == 0u) return false; // never published
        if (s1 & 1u) continue;      // producer mid-write, re-read
        for (size_t i = 0; i < stride; ++i) ((uint8_t *)out)[i] = ((const uint8_t *)value)[i];
        atomic_thread_fence(memory_order_acquire);                    // value reads before the recheck
        uint64_t s2 = atomic_load_explicit(version, memory_order_relaxed);
        if (s1 == s2) return true;  // version unmoved across the copy -> consistent
    }
}

// ---------------------------------------------------------------------------
// The bridge
// ---------------------------------------------------------------------------

// Completes the opaque AnoAudioBridge declared in anoptic_audio.h.
struct AnoAudioBridge
{
    AnoAudioRing commands; // logic -> mixer (AnoAudioCommand)
    AnoAudioRing events;   // mixer -> logic (AnoAudioEvent)

    // Published latest-wins state, each a seqlock with its version on a private cache line.
    // listener: logic publishes, mixer acquires. telemetry: mixer publishes, logic acquires.
    AnoAudioListener listener;
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t listenerVersion;
    AnoAudioTelemetry telemetry;
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t telemetryVersion;
};

// in:  bridge, heap, cmd_capacity_pow2, evt_capacity_pow2
// out: true on success; false on allocation failure
// inv: both rings allocate from `heap`; destroy the bridge before releasing it.
bool ano_audio_bridge_init(AnoAudioBridge *bridge, mi_heap_t *heap,
                           uint32_t cmd_capacity_pow2, uint32_t evt_capacity_pow2);

void ano_audio_bridge_destroy(AnoAudioBridge *bridge);

// --- Logic master endpoints (anoptic_audio.h) ---
// ano_audio_submit, ano_audio_poll_event, ano_audio_publish_listener, and
// ano_audio_acquire_telemetry are public, defined non-inline in ano_audio.c.

// --- Mixer endpoints (consumes commands + listener, produces events + telemetry) ---

// Dequeue one command into `out`. false if no command pending.
static inline bool ano_audio_next_command(AnoAudioBridge *bridge, AnoAudioCommand *out)
{
    return ano_audio_ring_pop(&bridge->commands, out);
}

// Enqueue one event. false if the event ring is full. The mixer must NOT block:
// it drops CAPACITY advisories and re-emits retirement facts next block.
static inline bool ano_audio_emit_event(AnoAudioBridge *bridge, const AnoAudioEvent *evt)
{
    return ano_audio_ring_push(&bridge->events, evt);
}

// Publish this block's telemetry frame for the logic master.
static inline void ano_audio_publish_telemetry(AnoAudioBridge *bridge, const AnoAudioTelemetry *t)
{
    ano_audio_seq_store(&bridge->telemetry, &bridge->telemetryVersion, t, sizeof *t);
}

// Read the latest listener pose logic published. false (out untouched) before its first publish.
static inline bool ano_audio_acquire_listener(AnoAudioBridge *bridge, AnoAudioListener *out)
{
    return ano_audio_seq_load(&bridge->listener, &bridge->listenerVersion, out, sizeof *out);
}

#endif // ANO_AUDIO_BRIDGE_INTERNAL_H
