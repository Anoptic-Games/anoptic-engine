/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Thread Bridges -- typed interconnects over anoptic_collections.h, the
// general extension of anoptic_threads.h. A bridge is a named, policy-carrying
// channel: a declared topology picks the cheapest ring that satisfies it, an explicit
// backpressure policy says what "full" means, and an optional waiter packages the
// logger's park/wake discipline so a consumer thread can sleep on an empty channel
// without losing wakeups.
//
// Policies, by channel shape:
//   TRYFAIL  command/control channels: false-on-full, the caller knows whether a
//            message is droppable, coalescible, or worth retrying (the render-bridge
//            contract).
//   WAIT     work/completion channels: the logger's never-drop stance generalized --
//            escalating backoff while the consumer catches up, degrading to false
//            after a stall cap (a generic bridge cannot dump to a side channel the
//            way the logger does; the caller degrades explicitly, never silently).
//   Latest-wins lanes have no backpressure by definition: overwrite semantics.
//
// The specialty channels: anobridge_handles carries anores_t by value -- the loaded
// payloads stay in the resource manager's allocator hierarchy, ONLY handles cross
// threads (sharing is structural); anobridge_latest is the broadcast lane (one
// writer, any readers, newest wins).
//
// Threading contracts are the underlying ring's: SPSC = one thread each side, MPSC =
// any producers/one consumer, SPMC = one producer/any consumers (the fan-out shape --
// a loader thread publishing handles to worker pools pays no RMW on send), MPMC =
// any/any. The waiter serves ONE parked consumer.

#ifndef ANOPTICENGINE_ANOPTIC_THREADS_BRIDGES_H
#define ANOPTICENGINE_ANOPTIC_THREADS_BRIDGES_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "anoptic_collections.h"
#include "anoptic_resources.h"      // anores_t: the handle channel's POD
#include "anoptic_threads.h"

// ---------------------------------------------------------------------------------------------
// The waiter: the log drainer's park/wake, packaged. Parked flag goes up seq_cst
// FIRST, the channel is rechecked under it, then a capped timedwait -- a wakeup lost
// to the race window costs at most cap_us, never a hang. Producers pay one seq_cst
// load per send while the consumer is awake.

typedef struct anobridge_waiter {
    anothread_mutex_t mtx;
    anothread_cond_t  cv;
    atomic_bool       parked;
} anobridge_waiter;

// Output: 0, or -1 if either primitive refused (nothing to destroy on failure).
int  ano_bridge_waiter_init(anobridge_waiter *w);
void ano_bridge_waiter_destroy(anobridge_waiter *w);

// CONSUMER. Park until a wake or cap_us (0 picks the default, 1000 us). nonempty is
// rechecked under the parked flag: if work raced in, the park is a no-op.
void ano_bridge_park(anobridge_waiter *w, bool (*nonempty)(void *ctx), void *ctx,
                     uint32_t cap_us);

// PRODUCER, after publishing: one seq_cst load; signals only a parked consumer.
void ano_bridge_wake(anobridge_waiter *w);

// ---------------------------------------------------------------------------------------------
// The general-purpose bridge.

typedef enum {
    ANO_BRIDGE_SPSC = 0,
    ANO_BRIDGE_MPSC,
    ANO_BRIDGE_SPMC,
    ANO_BRIDGE_MPMC,
} anobridge_topo;
typedef enum { ANO_BRIDGE_TRYFAIL = 0, ANO_BRIDGE_WAIT } anobridge_policy;

typedef struct anobridge {
    anobridge_topo    topo;
    anobridge_policy  policy;
    anobridge_waiter *waiter;       // optional: wake-on-send when attached
    union {
        anoring_spsc spsc;
        anoring_mpsc mpsc;
        anoring_spmc spmc;
        anoring_mpmc mpmc;
    } ring;
} anobridge;

// capacity/stride/parent per the ring contracts; waiter may be NULL. false on bad
// args or parent exhaustion.
bool ano_bridge_init(anobridge *b, ano_mem_parent parent, anobridge_topo topo,
                     anobridge_policy policy, uint32_t capacity, uint32_t stride,
                     anobridge_waiter *waiter);
void ano_bridge_destroy(anobridge *b);

// Send stride bytes. TRYFAIL: one attempt, false-on-full. WAIT: escalating backoff
// (the log ring's 64 ns doubling to 8 us) while full; after the stall cap it returns
// false -- never a silent drop, never an unbounded block. Wakes an attached parked
// consumer on success.
bool ano_bridge_send(anobridge *b, const void *elem);

// Receive one element; false-on-empty. Topology contract: who may call this is the
// ring's consumer side.
bool ano_bridge_recv(anobridge *b, void *out);

// Batch receive up to max, stopping at empty/gap. MPMC drains by repeated claim.
uint32_t ano_bridge_drain(anobridge *b, void *out, uint32_t max);

// ---------------------------------------------------------------------------------------------
// Specialty: the resource-handle channel. anores_t rides by VALUE (16-byte POD slots
// on the SoA payload plane); the bytes those handles name never cross here. Size the
// capacity >= the outstanding-request cap upstream and full becomes unreachable by
// construction; WAIT is belt and braces on top.

typedef struct anobridge_handles {
    anobridge b;
} anobridge_handles;

bool ano_bridge_handles_init(anobridge_handles *hb, ano_mem_parent parent,
                             anobridge_topo topo, uint32_t capacity,
                             anobridge_waiter *waiter);
void ano_bridge_handles_destroy(anobridge_handles *hb);

static inline bool ano_bridge_handles_send(anobridge_handles *hb, anores_t h)
{
    return ano_bridge_send(&hb->b, &h);
}

static inline bool ano_bridge_handles_recv(anobridge_handles *hb, anores_t *out)
{
    return ano_bridge_recv(&hb->b, out);
}

// ---------------------------------------------------------------------------------------------
// Specialty: the broadcast / latest-wins lane. One writer, any readers, newest wins,
// never torn (anoseqpub underneath). No backpressure by definition.

typedef struct anobridge_latest {
    anoseqpub pub;
} anobridge_latest;

bool ano_bridge_latest_init(anobridge_latest *l, ano_mem_parent parent, uint32_t stride);
void ano_bridge_latest_destroy(anobridge_latest *l);

static inline void ano_bridge_latest_send(anobridge_latest *l, const void *v)
{
    ano_seqpub_publish(&l->pub, v);
}

static inline bool ano_bridge_latest_recv(anobridge_latest *l, void *out)
{
    return ano_seqpub_read(&l->pub, out);
}

#endif // ANOPTICENGINE_ANOPTIC_THREADS_BRIDGES_H
