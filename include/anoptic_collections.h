/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Lock-Free Collections
//
// Bounded lock-free rings and publication primitives over C23 atomics -- the logging
// ring's cache-aligned architecture (docs/references/lockfree.md) generalized to fixed
// stride, plus the render bridge's proven SPSC and seqlock promoted. Platform-free:
// no OS calls, nothing but <stdatomic.h> and the memory tier.
//
// Four shapes, picked by topology, cheapest wins:
//   anoticket_t   a wait-free unique-number dispenser (one relaxed FAA).
//   anoring_spsc  owner cursors, no CAS at all: one producer, one consumer.
//   anoring_mpsc  Vyukov per-slot sequence protocol, consumer-private head + batch
//                 drain: many producers, one consumer (the command-queue shape).
//   anoring_spmc  the same protocol mirrored: producer-private tail, CAS-contended
//                 head -- the single producer pays NO RMW at all on push (the fan-out
//                 shape: one loader thread publishing to worker pools).
//   anoring_mpmc  CAS-contended on both cursors: many of both.
//   anoseqpub     latest-wins epoch publication: one writer, any readers, newest wins.
//
// The ordering discipline, everywhere (lockfree.md section 8): a claim carries no data
// (relaxed), an acquire on the slot's sequence word proves the slot's previous life is
// over, payload copies are plain stores, and ONE release store of the sequence word is
// the publish gate. Sequence words are 64-bit monotonic positions: ABA cannot occur
// this side of 2^64 operations, and slot reuse needs no zeroing -- the lap is encoded
// in the word (the log ring's cycle trick).
//
// Hazards, named: the MPSC/MPMC rings are lock-free in practice, not in the strict
// sense (Vyukov's own caveat) -- the claim->publish window must contain only plain
// stores; a producer preempted there self-heals in a scheduler quantum, a producer
// that DIES there wedges its slot for the consumer side. Channels whose producers can
// genuinely stall mid-publish (a future event bus) are where SCQ/LPRQ replace these
// internals behind the same API, picked on benchmark. On aarch64 without LSE atomics
// the FAA/CAS below compile to LL/SC loops and the contention argument quietly
// degrades -- build Apple targets with an -mcpu that inlines LDADD and verify in
// disassembly before trusting any ARM scalability number.
//
// Value discipline: PODs up to ~64 bytes ride a ring by value; anything bigger rides
// by pointer with ownership transferred through the message (anoptic_memory_pools.h:
// sharing is structural). Struct placement: the hot cursors carry
// _Alignas(ANO_THREAD_LINE); a HEAP owner must request that alignment for the
// false-sharing separation to actually hold (the render-bridge rule).
//
// Totality: init tolerates hostile-but-typed input and fails false, never UB; destroy
// of a zeroed/failed ring is a no-op; push/pop on a ring that failed init are UB, same
// contract as every other handle in the engine.

#ifndef ANOPTIC_COLLECTIONS_H
#define ANOPTIC_COLLECTIONS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_memory_pools.h"   // ano_mem_parent: where the planes come from

// ---------------------------------------------------------------------------------------------
// Ticket dispenser: "atomically take a unique number", the pure-FAA case.

typedef struct anoticket_t {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t next;
} anoticket_t;

// Start the dispenser at first. The struct is the whole state; no allocation.
static inline void ano_ticket_init(anoticket_t *t, uint64_t first)
{
    atomic_init(&t->next, first);
}

// A unique monotone ticket, wait-free: one relaxed FAA. Uniqueness ONLY -- visibility
// of whatever the ticket names is a separate release/acquire contract on that data.
static inline uint64_t ano_ticket_next(anoticket_t *t)
{
    return atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------
// SPSC: fixed-stride bounded ring, owner cursors, no CAS (the render bridge's
// AnoSpscRing, allocator-parameterized). Producer owns tail, consumer owns head; each
// reads the other with acquire and publishes its own with release.

typedef struct anoring_spsc {
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t tail;    // producer-owned: next write
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t head;    // consumer-owned: next read
    _Alignas(ANO_THREAD_LINE) uint32_t mask;            // capacity - 1, pow2, immutable
    uint32_t       stride;                              // element size in bytes
    uint8_t       *buf;                                 // capacity * stride
    ano_mem_parent parent;
} anoring_spsc;

// capacity rounds up to a power of two >= 2 (<= 2^31: u32 difference arithmetic);
// the buffer comes from parent, cache-line aligned. false on bad args or exhaustion.
bool ano_ring_spsc_init(anoring_spsc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);

// Return the buffer via parent.release (a NULL release is the wink-out no-op) and
// zero the struct. Safe on a zeroed/failed ring.
void ano_ring_spsc_destroy(anoring_spsc *r);

// PRODUCER only. Copies stride bytes in. false-on-full: the caller owns the policy
// (drop, coalesce, retry -- the bridge layer packages WAIT).
static inline bool ano_ring_spsc_push(anoring_spsc *r, const void *elem)
{
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    if ((tail - head) > r->mask)                // == capacity: full
        return false;
    memcpy(r->buf + (size_t)(tail & r->mask) * r->stride, elem, r->stride);
    atomic_store_explicit(&r->tail, tail + 1u, memory_order_release);
    return true;
}

// CONSUMER only. Copies the next element into out (>= stride bytes). false-on-empty.
static inline bool ano_ring_spsc_pop(anoring_spsc *r, void *out)
{
    uint32_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (head == tail)
        return false;
    memcpy(out, r->buf + (size_t)(head & r->mask) * r->stride, r->stride);
    atomic_store_explicit(&r->head, head + 1u, memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------------------------
// MPSC / SPMC / MPMC: the Vyukov bounded ring at fixed stride, SoA planes -- one
// _Atomic u64 sequence word per slot (the commit/lap gate) beside a dense POD payload
// column. Fullness reads the sequence words, never the far cursor. The single-owner
// side of each asymmetric variant drops its CAS: MPSC's consumer owns head, SPMC's
// producer owns tail. seq[i] life cycle:
//   i            free, lap 0        (init plants seq[i] = i)
//   pos + 1      committed at pos   (producer's release publish)
//   pos + cap    free, next lap     (consumer's release reuse)

typedef struct anoring_mpsc {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t tail;    // producers' claim cursor (CAS)
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t head;    // consumer-private drain cursor
    _Alignas(ANO_THREAD_LINE) uint64_t mask;            // capacity - 1, pow2, immutable
    uint32_t          stride;
    _Atomic uint64_t *seq;                              // the sequence plane
    uint8_t          *data;                             // the payload plane (SoA column)
    ano_mem_parent    parent;
} anoring_mpsc;

typedef struct anoring_spmc {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t tail;    // producer-private write cursor
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t head;    // consumers' claim cursor (CAS)
    _Alignas(ANO_THREAD_LINE) uint64_t mask;
    uint32_t          stride;
    _Atomic uint64_t *seq;
    uint8_t          *data;
    ano_mem_parent    parent;
} anoring_spmc;

typedef struct anoring_mpmc {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t tail;    // producers' claim cursor (CAS)
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t head;    // consumers' claim cursor (CAS)
    _Alignas(ANO_THREAD_LINE) uint64_t mask;
    uint32_t          stride;
    _Atomic uint64_t *seq;
    uint8_t          *data;
    ano_mem_parent    parent;
} anoring_mpmc;

// capacity rounds up to a power of two >= 2 (<= 2^31); both planes come from parent,
// cache-line aligned; the sequence plane is planted seq[i] = i. false on bad args or
// parent exhaustion (nothing stays allocated on failure).
bool ano_ring_mpsc_init(anoring_mpsc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);
bool ano_ring_spmc_init(anoring_spmc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);
bool ano_ring_mpmc_init(anoring_mpmc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);

// Return both planes via parent.release (NULL release = wink-out no-op), zero the
// struct. Safe on a zeroed/failed ring. The caller guarantees quiescence.
void ano_ring_mpsc_destroy(anoring_mpsc *r);
void ano_ring_spmc_destroy(anoring_spmc *r);
void ano_ring_mpmc_destroy(anoring_mpmc *r);

// Any thread. Claim a slot (tail CAS relaxed: uniqueness only), fill with plain
// stores, publish with ONE release store of the slot's sequence word (pairs with the
// consumer's acquire). false-on-full -- fullness is a lagging previous-lap sequence
// word, decided without touching head. The claim->publish window must contain only
// the payload copy: see the wedged-producer hazard in the header.
static inline bool ano_ring_mpsc_push(anoring_mpsc *r, const void *elem)
{
    uint64_t pos = atomic_load_explicit(&r->tail, memory_order_relaxed);
    for (;;) {
        size_t   slot = (size_t)(pos & r->mask);
        uint64_t s    = atomic_load_explicit(&r->seq[slot], memory_order_acquire);
        int64_t  dif  = (int64_t)s - (int64_t)pos;
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&r->tail, &pos, pos + 1u,
                    memory_order_relaxed, memory_order_relaxed))
                break;                          // slot [pos] is ours
        } else if (dif < 0) {
            return false;                       // previous lap undrained: full
        } else {
            pos = atomic_load_explicit(&r->tail, memory_order_relaxed);
        }
    }
    memcpy(r->data + (size_t)(pos & r->mask) * r->stride, elem, r->stride);
    atomic_store_explicit(&r->seq[pos & r->mask], pos + 1u, memory_order_release);
    return true;
}

// CONSUMER only. One element; false on empty or on the first uncommitted gap (a
// producer mid-publish: FIFO holds, try again). head is consumer-private -- relaxed,
// no CAS; the slot's release reuse-store pairs with a future producer's acquire.
static inline bool ano_ring_mpsc_pop(anoring_mpsc *r, void *out)
{
    uint64_t pos  = atomic_load_explicit(&r->head, memory_order_relaxed);
    size_t   slot = (size_t)(pos & r->mask);
    uint64_t s    = atomic_load_explicit(&r->seq[slot], memory_order_acquire);
    if ((int64_t)s - (int64_t)(pos + 1u) < 0)
        return false;                           // empty, or the head slot is mid-publish
    memcpy(out, r->data + slot * r->stride, r->stride);
    atomic_store_explicit(&r->seq[slot], pos + r->mask + 1u, memory_order_release);
    atomic_store_explicit(&r->head, pos + 1u, memory_order_relaxed);
    return true;
}

// CONSUMER only. Batch drain into out (max elements' worth of space): walks committed
// slots in claim order, stops at the first gap (the log drain's bounded walk).
// Returns the number copied.
static inline uint32_t ano_ring_mpsc_drain(anoring_mpsc *r, void *out, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && ano_ring_mpsc_pop(r, (uint8_t *)out + (size_t)n * r->stride))
        n++;
    return n;
}

// PRODUCER only. The MPSC pop's ownership mirrored: tail is producer-private, so the
// single producer pays no RMW at all -- one acquire on the slot's sequence word (the
// consumers' release reuse), a plain copy, one release publish, one relaxed cursor
// store. false-on-full.
static inline bool ano_ring_spmc_push(anoring_spmc *r, const void *elem)
{
    uint64_t pos  = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t   slot = (size_t)(pos & r->mask);
    uint64_t s    = atomic_load_explicit(&r->seq[slot], memory_order_acquire);
    if ((int64_t)s - (int64_t)pos < 0)
        return false;                           // previous lap undrained: full
    memcpy(r->data + slot * r->stride, elem, r->stride);
    atomic_store_explicit(&r->seq[slot], pos + 1u, memory_order_release);
    atomic_store_explicit(&r->tail, pos + 1u, memory_order_relaxed);
    return true;
}

// Any thread. Claim the head slot by CAS (relaxed: the acquire on seq carries the
// payload), copy out, release the slot into its next lap. false-on-empty.
static inline bool ano_ring_spmc_pop(anoring_spmc *r, void *out)
{
    uint64_t pos = atomic_load_explicit(&r->head, memory_order_relaxed);
    for (;;) {
        size_t   slot = (size_t)(pos & r->mask);
        uint64_t s    = atomic_load_explicit(&r->seq[slot], memory_order_acquire);
        int64_t  dif  = (int64_t)s - (int64_t)(pos + 1u);
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&r->head, &pos, pos + 1u,
                    memory_order_relaxed, memory_order_relaxed))
                break;                          // element [pos] is ours
        } else if (dif < 0) {
            return false;                       // nothing committed at head
        } else {
            pos = atomic_load_explicit(&r->head, memory_order_relaxed);
        }
    }
    size_t slot = (size_t)(pos & r->mask);
    memcpy(out, r->data + slot * r->stride, r->stride);
    atomic_store_explicit(&r->seq[slot], pos + r->mask + 1u, memory_order_release);
    return true;
}

// Any thread. The MPSC push verbatim -- the protocol does not care how many claim.
static inline bool ano_ring_mpmc_push(anoring_mpmc *r, const void *elem)
{
    uint64_t pos = atomic_load_explicit(&r->tail, memory_order_relaxed);
    for (;;) {
        size_t   slot = (size_t)(pos & r->mask);
        uint64_t s    = atomic_load_explicit(&r->seq[slot], memory_order_acquire);
        int64_t  dif  = (int64_t)s - (int64_t)pos;
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&r->tail, &pos, pos + 1u,
                    memory_order_relaxed, memory_order_relaxed))
                break;
        } else if (dif < 0) {
            return false;
        } else {
            pos = atomic_load_explicit(&r->tail, memory_order_relaxed);
        }
    }
    memcpy(r->data + (size_t)(pos & r->mask) * r->stride, elem, r->stride);
    atomic_store_explicit(&r->seq[pos & r->mask], pos + 1u, memory_order_release);
    return true;
}

// Any thread. Claim the head slot by CAS (relaxed: the acquire on seq carries the
// payload), copy out, release the slot into its next lap. false-on-empty.
static inline bool ano_ring_mpmc_pop(anoring_mpmc *r, void *out)
{
    uint64_t pos = atomic_load_explicit(&r->head, memory_order_relaxed);
    for (;;) {
        size_t   slot = (size_t)(pos & r->mask);
        uint64_t s    = atomic_load_explicit(&r->seq[slot], memory_order_acquire);
        int64_t  dif  = (int64_t)s - (int64_t)(pos + 1u);
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&r->head, &pos, pos + 1u,
                    memory_order_relaxed, memory_order_relaxed))
                break;                          // element [pos] is ours
        } else if (dif < 0) {
            return false;                       // nothing committed at head
        } else {
            pos = atomic_load_explicit(&r->head, memory_order_relaxed);
        }
    }
    size_t slot = (size_t)(pos & r->mask);
    memcpy(out, r->data + slot * r->stride, r->stride);
    atomic_store_explicit(&r->seq[slot], pos + r->mask + 1u, memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------------------------
// seqpub: latest-wins epoch publication (the render bridge's seqlock, owned value
// plane). One producer, any readers; a reader never observes a torn value at any
// stride or scheduling. version: even = stable, odd = mid-write, 0 = never published.

typedef struct anoseqpub {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t version;
    uint32_t       stride;
    void          *value;                       // the published plane, parent-owned
    ano_mem_parent parent;
} anoseqpub;

// stride > 0; the value plane comes from parent, cache-line aligned. false on bad
// args or exhaustion.
bool ano_seqpub_init(anoseqpub *p, ano_mem_parent parent, uint32_t stride);
void ano_seqpub_destroy(anoseqpub *p);

// PRODUCER only. Publish a whole new value: odd marker (relaxed) -> release fence ->
// plain value stores -> even release store (the publish gate).
static inline void ano_seqpub_publish(anoseqpub *p, const void *v)
{
    uint64_t s = atomic_load_explicit(&p->version, memory_order_relaxed);
    atomic_store_explicit(&p->version, s + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    memcpy(p->value, v, p->stride);
    atomic_store_explicit(&p->version, s + 2u, memory_order_release);
}

// Any thread. false (out untouched) before the first publish; otherwise a consistent
// copy: acquire the version, plain copy, acquire fence, relaxed recheck -- retry iff
// the version moved across the copy.
static inline bool ano_seqpub_read(const anoseqpub *p, void *out)
{
    for (;;) {
        uint64_t s1 = atomic_load_explicit(&p->version, memory_order_acquire);
        if (s1 == 0u)
            return false;
        if (s1 & 1u)
            continue;
        memcpy(out, p->value, p->stride);
        atomic_thread_fence(memory_order_acquire);
        uint64_t s2 = atomic_load_explicit(&p->version, memory_order_relaxed);
        if (s1 == s2)
            return true;
    }
}

#endif // ANOPTIC_COLLECTIONS_H
