/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_threads_bridges.h: the waiter, the policy bridge, and the
 * specialty channels.
 *   - anores_t is the 16-byte POD the handle channel promises (static assert);
 *   - unit: topology dispatch (send/recv/drain through all three rings), TRYFAIL
 *     reports full truthfully, WAIT rides out a slow consumer and degrades (not
 *     hangs) against a dead one, latest lane overwrite semantics;
 *   - park/wake soak: a consumer that parks on every empty pass against producers
 *     pushing at randomized intervals -- including the adversarial
 *     publish-just-before-park window; oracles: nothing lost, and wall time is
 *     bounded (a lost wakeup costs at most the park cap, never a hang);
 *   - handle channel: N fake loader threads send deterministic anores_t values, one
 *     pump collects; exactly-once oracle by checksum conservation.
 * Concurrency label: every op runs under the TSan net (build.sh 7). Exit 0 == pass. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "anoptic_threads_bridges.h"
#include "anoptic_time.h"
#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static_assert(sizeof(anores_t) == 16, "the handle channel's slot promise");

// ---------------------------------------------------------------------------------------------
// Unit: dispatch and policy.

static void test_bridge_unit(void)
{
    // TRYFAIL over each topology: send/recv round trip, truthful full.
    anobridge_topo topos[4] = { ANO_BRIDGE_SPSC, ANO_BRIDGE_MPSC,
                                ANO_BRIDGE_SPMC, ANO_BRIDGE_MPMC };
    for (int t = 0; t < 4; t++) {
        anobridge b;
        CHECK(ano_bridge_init(&b, ano_mem_parent_default(), topos[t],
                              ANO_BRIDGE_TRYFAIL, 4, 8, NULL), "init");
        uint64_t v;
        for (uint64_t i = 0; i < 4; i++) {
            v = 100 + i;
            CHECK(ano_bridge_send(&b, &v), "send to capacity");
        }
        v = 999;
        CHECK(!ano_bridge_send(&b, &v), "TRYFAIL reports full truthfully");
        static uint64_t batch[4];
        CHECK(ano_bridge_drain(&b, batch, 4) == 4, "drain all");
        CHECK(batch[0] == 100 && batch[3] == 103, "drain FIFO");
        CHECK(!ano_bridge_recv(&b, &v), "empty recv refuses");
        ano_bridge_destroy(&b);
    }

    // WAIT against a DEAD consumer: degrades to false after the stall cap, no hang.
    anobridge dead;
    CHECK(ano_bridge_init(&dead, ano_mem_parent_default(), ANO_BRIDGE_MPSC,
                          ANO_BRIDGE_WAIT, 2, 8, NULL), "init");
    uint64_t v = 1;
    CHECK(ano_bridge_send(&dead, &v), "fill 1");
    CHECK(ano_bridge_send(&dead, &v), "fill 2");
    CHECK(!ano_bridge_send(&dead, &v), "WAIT degrades against a dead consumer");
    ano_bridge_destroy(&dead);

    // Latest lane: newest wins, never torn, false before first publish.
    anobridge_latest l;
    CHECK(ano_bridge_latest_init(&l, ano_mem_parent_default(), 32), "latest init");
    uint8_t out[32], val[32];
    CHECK(!ano_bridge_latest_recv(&l, out), "false before first publish");
    for (int gen = 1; gen <= 3; gen++) {
        memset(val, gen, sizeof val);
        ano_bridge_latest_send(&l, val);
    }
    CHECK(ano_bridge_latest_recv(&l, out), "recv after publish");
    CHECK(out[0] == 3 && out[31] == 3, "latest wins");
    ano_bridge_latest_destroy(&l);
}

// ---------------------------------------------------------------------------------------------
// Park/wake soak. One consumer parks on every empty pass; producers push in bursts
// with gaps longer than the park window, hammering the publish-vs-park race.

#define PW_PRODUCERS 4u
#define PW_PER_PROD  20000u
#define PW_TOTAL     (PW_PRODUCERS * PW_PER_PROD)

static anobridge        pw_bridge;
static anobridge_waiter pw_waiter;
static _Atomic uint32_t pw_got;
static _Atomic uint64_t pw_sum_out;

typedef struct pw_ctx { uint32_t id; } pw_ctx;

static void *pw_producer(void *arg)
{
    pw_ctx *c = arg;
    test_rng rng = rng_make(0xB00F + c->id);
    for (uint32_t s = 0; s < PW_PER_PROD; s++) {
        uint64_t item = ((uint64_t)c->id << 32) | s;
        while (!ano_bridge_send(&pw_bridge, &item)) { /* WAIT degraded: retry */ }
        if ((s & 0x3FF) == 0)
            ano_busywait(1000u * (1 + rng_below(&rng, 3)));    // gaps: force real parks
    }
    return NULL;
}

static bool pw_nonempty(void *ctx)
{
    (void)ctx;
    // The consumer-private probe: anything committed at head?
    anoring_mpsc *r = &pw_bridge.ring.mpsc;
    uint64_t pos = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint64_t s   = atomic_load_explicit(&r->seq[pos & r->mask], memory_order_acquire);
    return (int64_t)s - (int64_t)(pos + 1u) >= 0;
}

static void *pw_consumer(void *arg)
{
    (void)arg;
    static uint64_t batch[64];
    uint64_t sum = 0;
    uint32_t got = 0;
    while (got < PW_TOTAL) {
        uint32_t n = ano_bridge_drain(&pw_bridge, batch, 64);
        if (n == 0) {
            ano_bridge_park(&pw_waiter, pw_nonempty, NULL, 0);
            continue;
        }
        for (uint32_t i = 0; i < n; i++)
            sum += batch[i];
        got += n;
    }
    atomic_store(&pw_got, got);
    atomic_store(&pw_sum_out, sum);
    return NULL;
}

static void test_park_wake_soak(void)
{
    CHECK(ano_bridge_waiter_init(&pw_waiter) == 0, "waiter init");
    CHECK(ano_bridge_init(&pw_bridge, ano_mem_parent_default(), ANO_BRIDGE_MPSC,
                          ANO_BRIDGE_WAIT, 256, 8, &pw_waiter), "bridge init");

    uint64_t sum_in = 0;
    for (uint32_t p = 0; p < PW_PRODUCERS; p++)
        for (uint32_t s = 0; s < PW_PER_PROD; s++)
            sum_in += ((uint64_t)p << 32) | s;

    anothread_t cons, prod[PW_PRODUCERS];
    pw_ctx ctx[PW_PRODUCERS];
    CHECK(ano_thread_create(&cons, NULL, pw_consumer, NULL) == 0, "consumer");
    for (uint32_t i = 0; i < PW_PRODUCERS; i++) {
        ctx[i].id = i;
        CHECK(ano_thread_create(&prod[i], NULL, pw_producer, &ctx[i]) == 0, "producer");
    }
    for (uint32_t i = 0; i < PW_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    ano_thread_join(cons, NULL);
    // Joining at all IS the liveness oracle: a lost wakeup costs one park cap, a
    // hang would trip the ctest TIMEOUT.
    CHECK(atomic_load(&pw_got) == PW_TOTAL, "park/wake: nothing lost");
    CHECK(atomic_load(&pw_sum_out) == sum_in, "park/wake: sum conserved");
    ano_bridge_destroy(&pw_bridge);
    ano_bridge_waiter_destroy(&pw_waiter);
}

// ---------------------------------------------------------------------------------------------
// The handle channel: fake loaders publish anores_t values, one pump collects.
// Exactly-once by rid-sum conservation; payloads never cross, only handles.

#define HC_LOADERS 4u
#define HC_PER     25000u
#define HC_TOTAL   (HC_LOADERS * HC_PER)

static anobridge_handles hc;
static _Atomic uint64_t  hc_rid_sum;
static _Atomic uint32_t  hc_got;

static anores_t hc_make(uint32_t loader, uint32_t i)
{
    // Deterministic fake handles in the anores_t grammar (rid/slot/gen).
    return (anores_t){ .rid  = 0x9E3779B97F4A7C15ull * (((uint64_t)loader << 32) | (i + 1)),
                       .slot = i, .gen = 2 * i + 1 };
}

static void *hc_loader(void *arg)
{
    uint32_t id = (uint32_t)(uintptr_t)arg;
    for (uint32_t i = 0; i < HC_PER; i++) {
        anores_t h = hc_make(id, i);
        while (!ano_bridge_handles_send(&hc, h)) { /* WAIT degraded: retry */ }
    }
    return NULL;
}

static void *hc_pump(void *arg)
{
    (void)arg;
    uint64_t sum = 0;
    uint32_t got = 0;
    anores_t h;
    while (got < HC_TOTAL) {
        if (!ano_bridge_handles_recv(&hc, &h))
            continue;
        sum += h.rid + h.slot + h.gen;
        got++;
    }
    atomic_store(&hc_rid_sum, sum);
    atomic_store(&hc_got, got);
    return NULL;
}

static void test_handle_channel(void)
{
    CHECK(ano_bridge_handles_init(&hc, ano_mem_parent_default(), ANO_BRIDGE_MPSC,
                                  512, NULL), "handle channel init");
    uint64_t sum_in = 0;
    for (uint32_t l = 0; l < HC_LOADERS; l++)
        for (uint32_t i = 0; i < HC_PER; i++) {
            anores_t h = hc_make(l, i);
            sum_in += h.rid + h.slot + h.gen;
        }

    anothread_t pump, loaders[HC_LOADERS];
    CHECK(ano_thread_create(&pump, NULL, hc_pump, NULL) == 0, "pump");
    for (uint32_t i = 0; i < HC_LOADERS; i++)
        CHECK(ano_thread_create(&loaders[i], NULL, hc_loader,
                                (void *)(uintptr_t)i) == 0, "loader");
    for (uint32_t i = 0; i < HC_LOADERS; i++)
        ano_thread_join(loaders[i], NULL);
    ano_thread_join(pump, NULL);

    CHECK(atomic_load(&hc_got) == HC_TOTAL, "handles: exactly once (count)");
    CHECK(atomic_load(&hc_rid_sum) == sum_in, "handles: exactly once (sum)");
    ano_bridge_handles_destroy(&hc);
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    test_bridge_unit();
    test_park_wake_soak();
    test_handle_channel();

    if (failures == 0) { printf("anotest_bridges: all checks passed\n"); return 0; }
    printf("anotest_bridges: %d check(s) failed\n", failures);
    return 1;
}
