/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Waiter, policy bridge, specialty constructors. C23 atomics + anothread_* only.

#include <anoptic_threads_bridges.h>

#include <anoptic_time.h>

#include <string.h>
#include <time.h>

// WAIT backoff: 64..8192 ns doubling, false after BRIDGE_STALL_LIMIT.
#define BRIDGE_BACKOFF_MIN_NS 64u
#define BRIDGE_BACKOFF_MAX_NS 8192u
#define BRIDGE_STALL_LIMIT    4096u

#define BRIDGE_PARK_DEFAULT_US 1000u


/* Waiter */

int ano_bridge_waiter_init(anobridge_waiter *w)
{
    if (w == NULL)
        return -1;
    if (ano_mutex_init(&w->mtx, NULL) != 0)
        return -1;
    if (ano_thread_cond_init(&w->cv, NULL) != 0) {
        ano_mutex_destroy(&w->mtx);
        return -1;
    }
    atomic_init(&w->parked, false);
    return 0;
}

void ano_bridge_waiter_destroy(anobridge_waiter *w)
{
    if (w == NULL)
        return;
    ano_thread_cond_destroy(&w->cv);
    ano_mutex_destroy(&w->mtx);
}

void ano_bridge_park(anobridge_waiter *w, bool (*nonempty)(void *ctx), void *ctx,
                     uint32_t cap_us)
{
    if (w == NULL)
        return;
    if (cap_us == 0)
        cap_us = BRIDGE_PARK_DEFAULT_US;
    ano_mutex_lock(&w->mtx);
    // parked seq_cst first, then recheck. Cap bounds the race window.
    atomic_store_explicit(&w->parked, true, memory_order_seq_cst);
    if (nonempty == NULL || !nonempty(ctx)) {
        struct timespec ts;
        timespec_get(&ts, TIME_UTC);
        uint64_t ns = (uint64_t)ts.tv_nsec + (uint64_t)cap_us * 1000u;
        ts.tv_sec  += (time_t)(ns / 1000000000u);
        ts.tv_nsec  = (long)(ns % 1000000000u);
        ano_thread_cond_timedwait(&w->cv, &w->mtx, &ts);
    }
    atomic_store_explicit(&w->parked, false, memory_order_relaxed);
    ano_mutex_unlock(&w->mtx);
}

void ano_bridge_wake(anobridge_waiter *w)
{
    if (w == NULL)
        return;
    if (!atomic_load_explicit(&w->parked, memory_order_seq_cst))
        return;                                 // awake: one load, no lock
    ano_mutex_lock(&w->mtx);
    ano_thread_cond_signal(&w->cv);
    ano_mutex_unlock(&w->mtx);
}


/* Bridge */

bool ano_bridge_init(anobridge *b, ano_mem_parent parent, anobridge_topo topo,
                     anobridge_policy policy, uint32_t capacity, uint32_t stride,
                     anobridge_waiter *waiter)
{
    if (b == NULL)
        return false;
    memset(b, 0, sizeof *b);
    b->topo   = topo;
    b->policy = policy;
    b->waiter = waiter;
    switch (topo) {
    case ANO_BRIDGE_SPSC: return ano_ring_spsc_init(&b->ring.spsc, parent, capacity, stride);
    case ANO_BRIDGE_MPSC: return ano_ring_mpsc_init(&b->ring.mpsc, parent, capacity, stride);
    case ANO_BRIDGE_SPMC: return ano_ring_spmc_init(&b->ring.spmc, parent, capacity, stride);
    case ANO_BRIDGE_MPMC: return ano_ring_mpmc_init(&b->ring.mpmc, parent, capacity, stride);
    }
    return false;
}

void ano_bridge_destroy(anobridge *b)
{
    if (b == NULL)
        return;
    switch (b->topo) {
    case ANO_BRIDGE_SPSC: ano_ring_spsc_destroy(&b->ring.spsc); break;
    case ANO_BRIDGE_MPSC: ano_ring_mpsc_destroy(&b->ring.mpsc); break;
    case ANO_BRIDGE_SPMC: ano_ring_spmc_destroy(&b->ring.spmc); break;
    case ANO_BRIDGE_MPMC: ano_ring_mpmc_destroy(&b->ring.mpmc); break;
    }
    memset(b, 0, sizeof *b);
}

static bool bridge_push(anobridge *b, const void *elem)
{
    switch (b->topo) {
    case ANO_BRIDGE_SPSC: return ano_ring_spsc_push(&b->ring.spsc, elem);
    case ANO_BRIDGE_MPSC: return ano_ring_mpsc_push(&b->ring.mpsc, elem);
    case ANO_BRIDGE_SPMC: return ano_ring_spmc_push(&b->ring.spmc, elem);
    case ANO_BRIDGE_MPMC: return ano_ring_mpmc_push(&b->ring.mpmc, elem);
    }
    return false;
}

bool ano_bridge_send(anobridge *b, const void *elem)
{
    if (b == NULL || elem == NULL)
        return false;
    bool sent = bridge_push(b, elem);
    if (!sent && b->policy == ANO_BRIDGE_WAIT) {
        // WAIT: escalate backoff, then degrade.
        uint64_t backoff = BRIDGE_BACKOFF_MIN_NS;
        for (uint32_t stall = 0; stall < BRIDGE_STALL_LIMIT; stall++) {
            ano_busywait(backoff);
            if (backoff < BRIDGE_BACKOFF_MAX_NS)
                backoff *= 2;
            if ((sent = bridge_push(b, elem)))
                break;
        }
    }
    if (sent && b->waiter != NULL)
        ano_bridge_wake(b->waiter);
    return sent;
}

bool ano_bridge_recv(anobridge *b, void *out)
{
    if (b == NULL || out == NULL)
        return false;
    switch (b->topo) {
    case ANO_BRIDGE_SPSC: return ano_ring_spsc_pop(&b->ring.spsc, out);
    case ANO_BRIDGE_MPSC: return ano_ring_mpsc_pop(&b->ring.mpsc, out);
    case ANO_BRIDGE_SPMC: return ano_ring_spmc_pop(&b->ring.spmc, out);
    case ANO_BRIDGE_MPMC: return ano_ring_mpmc_pop(&b->ring.mpmc, out);
    }
    return false;
}

uint32_t ano_bridge_drain(anobridge *b, void *out, uint32_t max)
{
    if (b == NULL || out == NULL)
        return 0;
    if (b->topo == ANO_BRIDGE_MPSC)
        return ano_ring_mpsc_drain(&b->ring.mpsc, out, max);
    uint32_t stride;
    switch (b->topo) {
    case ANO_BRIDGE_SPSC: stride = b->ring.spsc.stride; break;
    case ANO_BRIDGE_SPMC: stride = b->ring.spmc.stride; break;
    default:              stride = b->ring.mpmc.stride; break;
    }
    uint32_t n = 0;
    while (n < max && ano_bridge_recv(b, (uint8_t *)out + (size_t)n * stride))
        n++;
    return n;
}


/* Specialty Constructors */

bool ano_bridge_handles_init(anobridge_handles *hb, ano_mem_parent parent,
                             anobridge_topo topo, uint32_t capacity,
                             anobridge_waiter *waiter)
{
    if (hb == NULL)
        return false;
    // WAIT: capacity sized upstream so full is unreachable.
    return ano_bridge_init(&hb->b, parent, topo, ANO_BRIDGE_WAIT, capacity,
                           (uint32_t)sizeof(anores_t), waiter);
}

void ano_bridge_handles_destroy(anobridge_handles *hb)
{
    if (hb != NULL)
        ano_bridge_destroy(&hb->b);
}

bool ano_bridge_latest_init(anobridge_latest *l, ano_mem_parent parent, uint32_t stride)
{
    return l != NULL && ano_seqpub_init(&l->pub, parent, stride);
}

void ano_bridge_latest_destroy(anobridge_latest *l)
{
    if (l != NULL)
        ano_seqpub_destroy(&l->pub);
}
