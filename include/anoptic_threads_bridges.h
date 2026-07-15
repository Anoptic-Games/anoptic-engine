/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Named policy-carrying channels over anoptic_collections.h rings.
// TRYFAIL: false-on-full. WAIT: backoff then false (no silent drop). Latest-wins: overwrite.
// Handles: anores_t by value. Topology = ring contract. Waiter serves one parked consumer.

#ifndef ANOPTICENGINE_ANOPTIC_THREADS_BRIDGES_H
#define ANOPTICENGINE_ANOPTIC_THREADS_BRIDGES_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "anoptic_collections.h"
#include "anoptic_resources.h"      // anores_t: handle channel POD
#include "anoptic_threads.h"


/* Waiter */

// Parked seq_cst first, recheck, capped timedwait. Lost wake <= cap_us.
// Producer pays one seq_cst load per wake while the consumer is awake.

typedef struct anobridge_waiter {
    anothread_mutex_t mtx;
    anothread_cond_t  cv;
    atomic_bool       parked;
} anobridge_waiter;

// Out: 0, or -1 (nothing to destroy on failure).
int  ano_bridge_waiter_init(anobridge_waiter *w);
void ano_bridge_waiter_destroy(anobridge_waiter *w);

// CONSUMER. Park until wake or cap_us (0 -> 1000 us). Recheck nonempty under parked.
void ano_bridge_park(anobridge_waiter *w, bool (*nonempty)(void *ctx), void *ctx,
                     uint32_t cap_us);

// PRODUCER after publish: one seq_cst load, signal only if parked.
void ano_bridge_wake(anobridge_waiter *w);


/* Bridge */

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
    anobridge_waiter *waiter;       // optional: wake-on-send
    union {
        anoring_spsc spsc;
        anoring_mpsc mpsc;
        anoring_spmc spmc;
        anoring_mpmc mpmc;
    } ring;
} anobridge;

// capacity/stride/parent per ring contracts. waiter may be NULL. false on bad args or OOM.
bool ano_bridge_init(anobridge *b, ano_mem_parent parent, anobridge_topo topo,
                     anobridge_policy policy, uint32_t capacity, uint32_t stride,
                     anobridge_waiter *waiter);
void ano_bridge_destroy(anobridge *b);

// Send stride bytes. TRYFAIL: false-on-full. WAIT: 64..8192 ns doubling, false after 4096 stalls. Wake parked consumer on success.
bool ano_bridge_send(anobridge *b, const void *elem);

// Recv one. false-on-empty. Consumer-side only.
bool ano_bridge_recv(anobridge *b, void *out);

// Batch recv up to max. MPSC: ring drain. Else: repeated recv.
uint32_t ano_bridge_drain(anobridge *b, void *out, uint32_t max);


/* Resource Handles */

// anores_t by value (POD). WAIT policy; size capacity so full is unreachable. Payload bytes stay in the resource allocator.

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


/* Latest-Wins */

// One writer, any readers, never torn (anoseqpub). No backpressure.

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
