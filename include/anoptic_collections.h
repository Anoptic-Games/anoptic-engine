/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Lock-Free Collections

// Bounded lock-free rings + seqpub. C23 atomics only. docs/references/lockfree.md.

// Topology (cheapest wins):
//   anoticket_t   wait-free unique tickets (relaxed FAA)
//   anoring_spsc  owner cursors, no CAS: 1P/1C
//   anoring_mpsc  Vyukov seq, consumer-private head + drain: NP/1C
//   anoring_spmc  producer-private tail, CAS head: 1P/NC
//   anoring_mpmc  CAS both cursors: NP/NC
//   anoseqpub     latest-wins epoch: 1W/NR

// Vyukov order: claim relaxed -> acquire on seq -> plain payload stores -> one release seq publish.
// Seq = u64 position. No ABA before 2^64. Lap lives in the word.

// Hazard: MPSC/MPMC lock-free in practice, not strict (Vyukov). claim->publish = plain stores only. Preempt self-heals. Dead producer wedges slot.
// aarch64 sans LSE: FAA/CAS are LL/SC. Apple: -mcpu with LDADD, verify disasm.

// POD <= ~64B by value. Bigger by pointer + ownership in the message.
// Hot cursors _Alignas(ANO_THREAD_LINE). HEAP owner must match that align.

// Init: hostile input -> false, never UB. Destroy zeroed/failed = no-op.
// Push/pop on failed-init ring = UB.

#ifndef ANOPTIC_COLLECTIONS_H
#define ANOPTIC_COLLECTIONS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "anoptic_memory.h"
#include "anoptic_memory_pools.h"   // ano_mem_parent: plane allocator

/* Ticket Dispenser */

// Unique numbers via FAA.

typedef struct anoticket_t {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t next;
} anoticket_t;

// Start dispenser at first.
// No allocation.
static inline void ano_ticket_init(anoticket_t *t, uint64_t first)
{
    atomic_init(&t->next, first);
}

// Unique monotone ticket. Wait-free: one relaxed FAA.
// Uniqueness only. Visibility of named data is a separate release/acquire.
static inline uint64_t ano_ticket_next(anoticket_t *t)
{
    return atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
}

/* SPSC */

// Fixed-stride bounded ring. Owner cursors, no CAS.
// Producer owns tail, consumer owns head. Acquire peer, release self.

typedef struct anoring_spsc {
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t tail;    // producer-owned: next write
    _Alignas(ANO_THREAD_LINE) _Atomic uint32_t head;    // consumer-owned: next read
    _Alignas(ANO_THREAD_LINE) uint32_t mask;            // capacity - 1, pow2, immutable
    uint32_t       stride;                              // element size in bytes
    uint8_t       *buf;                                 // capacity * stride
    ano_mem_parent parent;
} anoring_spsc;

// capacity -> pow2 >= 2 (<= 2^31). Buffer from parent, cache-line aligned.
// false on bad args or exhaustion.
bool ano_ring_spsc_init(anoring_spsc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);

// parent.release the buffer (NULL release = wink-out no-op). Zero the struct.
// Safe on zeroed/failed.
void ano_ring_spsc_destroy(anoring_spsc *r);

// PRODUCER only. Copy stride bytes in. false-on-full.
static inline bool ano_ring_spsc_push(anoring_spsc *r, const void *elem)
{
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    if ((tail - head) > r->mask)                // full
        return false;
    memcpy(r->buf + (size_t)(tail & r->mask) * r->stride, elem, r->stride);
    atomic_store_explicit(&r->tail, tail + 1u, memory_order_release);
    return true;
}

// CONSUMER only. Copy next element into out (>= stride). false-on-empty.
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

/* MPSC / SPMC / MPMC */

// Vyukov bounded ring, fixed stride, SoA planes.
// Fullness reads seq words, never the far cursor.
// Single-owner side drops its CAS: MPSC consumer owns head, SPMC producer owns tail.
// seq[i] life:
//   i            free, lap 0        (init: seq[i] = i)
//   pos + 1      committed at pos   (producer release publish)
//   pos + cap    free, next lap     (consumer release reuse)

typedef struct anoring_mpsc {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t tail;    // producers' claim cursor (CAS)
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t head;    // consumer-private drain cursor
    _Alignas(ANO_THREAD_LINE) uint64_t mask;            // capacity - 1, pow2, immutable
    uint32_t          stride;
    _Atomic uint64_t *seq;                              // sequence plane
    uint8_t          *data;                             // payload plane
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

// capacity -> pow2 >= 2 (<= 2^31). Planes from parent, cache-line aligned. seq[i] = i.
// false on bad args or exhaustion. Nothing left allocated on failure.
bool ano_ring_mpsc_init(anoring_mpsc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);
bool ano_ring_spmc_init(anoring_spmc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);
bool ano_ring_mpmc_init(anoring_mpmc *r, ano_mem_parent parent,
                        uint32_t capacity, uint32_t stride);

// parent.release both planes (NULL release = wink-out no-op). Zero struct.
// Safe on zeroed/failed. Caller quiescence required.
void ano_ring_mpsc_destroy(anoring_mpsc *r);
void ano_ring_spmc_destroy(anoring_spmc *r);
void ano_ring_mpmc_destroy(anoring_mpmc *r);

// Any thread. Claim slot (tail CAS relaxed), plain fill, ONE release seq publish.
// false-on-full via lagging previous-lap seq, no head touch.
// claim->publish = payload copy only.
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

// CONSUMER only. One element. false on empty or uncommitted gap (FIFO holds, retry).
// head consumer-private: relaxed, no CAS.
// Slot release reuse pairs with future producer acquire.
static inline bool ano_ring_mpsc_pop(anoring_mpsc *r, void *out)
{
    uint64_t pos  = atomic_load_explicit(&r->head, memory_order_relaxed);
    size_t   slot = (size_t)(pos & r->mask);
    uint64_t s    = atomic_load_explicit(&r->seq[slot], memory_order_acquire);
    if ((int64_t)s - (int64_t)(pos + 1u) < 0)
        return false;                           // empty or head mid-publish
    memcpy(out, r->data + slot * r->stride, r->stride);
    atomic_store_explicit(&r->seq[slot], pos + r->mask + 1u, memory_order_release);
    atomic_store_explicit(&r->head, pos + 1u, memory_order_relaxed);
    return true;
}

// CONSUMER only. Batch drain into out (max slots). Stops at first gap. Returns count.
static inline uint32_t ano_ring_mpsc_drain(anoring_mpsc *r, void *out, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && ano_ring_mpsc_pop(r, (uint8_t *)out + (size_t)n * r->stride))
        n++;
    return n;
}

// PRODUCER only. Tail producer-private: no RMW.
// Acquire seq -> plain copy -> release publish -> relaxed cursor. false-on-full.
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

// Any thread. Claim head by CAS (relaxed: seq acquire carries payload).
// Copy out. Release next lap. false-on-empty.
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

// Any thread. MPSC push protocol. Claim count irrelevant.
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

// Any thread. Claim head by CAS (relaxed: seq acquire carries payload).
// Copy out. Release next lap. false-on-empty.
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

/* Seqpub */

// Latest-wins epoch publication. One producer, any readers. Never torn.
// version: even = stable, odd = mid-write, 0 = never published.

typedef struct anoseqpub {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t version;
    uint32_t       stride;
    void          *value;                       // published plane, parent-owned
    ano_mem_parent parent;
} anoseqpub;

// stride > 0. Value plane from parent, cache-line aligned.
// false on bad args or exhaustion.
bool ano_seqpub_init(anoseqpub *p, ano_mem_parent parent, uint32_t stride);
void ano_seqpub_destroy(anoseqpub *p);

// PRODUCER only. Publish: odd marker (relaxed) -> release fence -> plain stores -> even release.
static inline void ano_seqpub_publish(anoseqpub *p, const void *v)
{
    uint64_t s = atomic_load_explicit(&p->version, memory_order_relaxed);
    atomic_store_explicit(&p->version, s + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    memcpy(p->value, v, p->stride);
    atomic_store_explicit(&p->version, s + 2u, memory_order_release);
}

// Any thread. false (out untouched) before first publish.
// Else: acquire version -> plain copy -> acquire fence -> relaxed recheck.
// Retry if odd or version moved.
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
