/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_collections.h: the promoted SPSC, the Vyukov MPSC/MPMC rings,
 * seqpub, and the ticket dispenser.
 *   - compile-time: the cursor false-sharing separation is real, not aspirational
 *     (offsetof distances >= ANO_THREAD_LINE, struct alignment inherited);
 *   - unit: FIFO order, false-on-full exactly at capacity, false-on-empty, >= 4 laps
 *     of wrap, SPSC cursor wrap across UINT32_MAX (transparent structs let the test
 *     preload cursors -- a test-only seam), init totality (NULL/0/oversize refuse,
 *     destroy of a zeroed ring is a no-op), seqpub torn-read protocol;
 *   - stress: 8P x 8C MPMC and 8P x 1C MPSC(drain) with count + sum + xor
 *     conservation (no loss, no dup, no tear) and per-producer FIFO at every
 *     consumer; 1P x 8C SPMC with the stronger oracle one producer permits (every
 *     consumer sees a strictly increasing slice of ONE global sequence); 1P x 1C
 *     SPSC mirror; 8-thread ticket uniqueness via a bitmap.
 * The stress battery is the TSan gate's food: this test carries the concurrency
 * label and every ring op runs under build.sh 7. Exit 0 == pass. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

#include "anoptic_collections.h"
#include "anoptic_threads.h"
#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// ---------------------------------------------------------------------------------------------
// The separation the header promises must hold in the layout the compiler actually built.

static_assert(offsetof(anoring_spsc, head) - offsetof(anoring_spsc, tail)
              >= ANO_THREAD_LINE, "spsc cursor separation");
static_assert(offsetof(anoring_mpsc, head) - offsetof(anoring_mpsc, tail)
              >= ANO_THREAD_LINE, "mpsc cursor separation");
static_assert(offsetof(anoring_spmc, head) - offsetof(anoring_spmc, tail)
              >= ANO_THREAD_LINE, "spmc cursor separation");
static_assert(offsetof(anoring_mpmc, head) - offsetof(anoring_mpmc, tail)
              >= ANO_THREAD_LINE, "mpmc cursor separation");
static_assert(_Alignof(anoring_spsc) >= ANO_THREAD_LINE, "spsc struct alignment");
static_assert(_Alignof(anoring_mpsc) >= ANO_THREAD_LINE, "mpsc struct alignment");
static_assert(_Alignof(anoring_spmc) >= ANO_THREAD_LINE, "spmc struct alignment");
static_assert(_Alignof(anoring_mpmc) >= ANO_THREAD_LINE, "mpmc struct alignment");
static_assert(_Alignof(anoticket_t)  >= ANO_THREAD_LINE, "ticket alignment");

// ---------------------------------------------------------------------------------------------
// Unit: single-threaded semantics, every flavor.

static void test_ticket_unit(void)
{
    anoticket_t t;
    ano_ticket_init(&t, 5u);
    CHECK(ano_ticket_next(&t) == 5u, "ticket starts at first");
    CHECK(ano_ticket_next(&t) == 6u, "ticket is monotone");
    CHECK(ano_ticket_next(&t) == 7u, "ticket increments by one");
}

static void test_spsc_unit(void)
{
    anoring_spsc r;
    CHECK(!ano_ring_spsc_init(NULL, ano_mem_parent_default(), 4, 8), "NULL ring refuses");
    CHECK(!ano_ring_spsc_init(&r, ano_mem_parent_default(), 4, 0), "stride 0 refuses");
    CHECK(!ano_ring_spsc_init(&r, ano_mem_parent_default(), UINT32_MAX, 8),
          "oversize capacity refuses");

    CHECK(ano_ring_spsc_init(&r, ano_mem_parent_default(), 3, 8), "init rounds 3 -> 4");
    CHECK(r.mask == 3u, "capacity rounded to pow2");
    uint64_t v;
    for (uint64_t i = 0; i < 4; i++) {
        v = 100 + i;
        CHECK(ano_ring_spsc_push(&r, &v), "push to capacity");
    }
    v = 999;
    CHECK(!ano_ring_spsc_push(&r, &v), "false-on-full exactly at capacity");
    for (uint64_t i = 0; i < 4; i++) {
        CHECK(ano_ring_spsc_pop(&r, &v), "pop");
        CHECK(v == 100 + i, "FIFO order");
    }
    CHECK(!ano_ring_spsc_pop(&r, &v), "false-on-empty");

    // Four laps of wrap through a small ring.
    for (uint64_t i = 0; i < 16; i++) {
        v = i;
        CHECK(ano_ring_spsc_push(&r, &v), "lap push");
        CHECK(ano_ring_spsc_pop(&r, &v) && v == i, "lap pop FIFO");
    }

    // Cursor wrap across UINT32_MAX: transparent struct, test-only preload.
    atomic_store_explicit(&r.tail, UINT32_MAX - 1, memory_order_relaxed);
    atomic_store_explicit(&r.head, UINT32_MAX - 1, memory_order_relaxed);
    for (uint64_t i = 0; i < 8; i++) {
        v = 7000 + i;
        CHECK(ano_ring_spsc_push(&r, &v), "push across u32 wrap");
        CHECK(ano_ring_spsc_pop(&r, &v) && v == 7000 + i, "pop across u32 wrap");
    }
    ano_ring_spsc_destroy(&r);
    CHECK(r.buf == NULL, "destroy zeroes");
    ano_ring_spsc_destroy(&r);              // double destroy: no-op, ASan watches
}

static void test_mpsc_unit(void)
{
    anoring_mpsc r;
    CHECK(!ano_ring_mpsc_init(&r, ano_mem_parent_default(), 8, 0), "stride 0 refuses");
    CHECK(ano_ring_mpsc_init(&r, ano_mem_parent_default(), 8, 4), "init");
    uint32_t v;
    for (uint32_t i = 0; i < 8; i++) {
        v = i;
        CHECK(ano_ring_mpsc_push(&r, &v), "push to capacity");
    }
    v = 99;
    CHECK(!ano_ring_mpsc_push(&r, &v), "false-on-full");
    static uint32_t batch[8];
    CHECK(ano_ring_mpsc_drain(&r, batch, 3) == 3, "drain caps at max");
    CHECK(batch[0] == 0 && batch[1] == 1 && batch[2] == 2, "drain FIFO");
    CHECK(ano_ring_mpsc_drain(&r, batch, 8) == 5, "drain stops at empty");
    CHECK(batch[4] == 7, "drain tail FIFO");
    CHECK(!ano_ring_mpsc_pop(&r, &v), "false-on-empty");
    for (uint32_t i = 0; i < 40; i++) {     // five laps: sequence-word reuse
        v = 1000 + i;
        CHECK(ano_ring_mpsc_push(&r, &v), "lap push");
        CHECK(ano_ring_mpsc_pop(&r, &v) && v == 1000 + i, "lap pop");
    }
    ano_ring_mpsc_destroy(&r);
    ano_ring_mpsc_destroy(&r);
}

static void test_spmc_unit(void)
{
    anoring_spmc r;
    CHECK(!ano_ring_spmc_init(&r, ano_mem_parent_default(), 4, 0), "stride 0 refuses");
    CHECK(ano_ring_spmc_init(&r, ano_mem_parent_default(), 4, 8), "init");
    uint64_t v;
    for (uint64_t i = 0; i < 4; i++) {
        v = 200 + i;
        CHECK(ano_ring_spmc_push(&r, &v), "push to capacity");
    }
    v = 999;
    CHECK(!ano_ring_spmc_push(&r, &v), "false-on-full");
    for (uint64_t i = 0; i < 4; i++) {
        CHECK(ano_ring_spmc_pop(&r, &v), "pop");
        CHECK(v == 200 + i, "FIFO order");
    }
    CHECK(!ano_ring_spmc_pop(&r, &v), "false-on-empty");
    for (uint64_t i = 0; i < 20; i++) {     // five laps: sequence-word reuse
        v = 3000 + i;
        CHECK(ano_ring_spmc_push(&r, &v), "lap push");
        CHECK(ano_ring_spmc_pop(&r, &v) && v == 3000 + i, "lap pop");
    }
    ano_ring_spmc_destroy(&r);
    ano_ring_spmc_destroy(&r);
}

static void test_mpmc_unit(void)
{
    anoring_mpmc r;
    CHECK(ano_ring_mpmc_init(&r, ano_mem_parent_default(), 4, 16), "init");
    struct { uint64_t a, b; } e;
    for (uint64_t i = 0; i < 4; i++) {
        e.a = i; e.b = ~i;
        CHECK(ano_ring_mpmc_push(&r, &e), "push to capacity");
    }
    CHECK(!ano_ring_mpmc_push(&r, &e), "false-on-full");
    for (uint64_t i = 0; i < 4; i++) {
        CHECK(ano_ring_mpmc_pop(&r, &e), "pop");
        CHECK(e.a == i && e.b == ~i, "FIFO + both words intact");
    }
    CHECK(!ano_ring_mpmc_pop(&r, &e), "false-on-empty");
    for (uint64_t i = 0; i < 20; i++) {     // five laps
        e.a = i; e.b = i * 3;
        CHECK(ano_ring_mpmc_push(&r, &e), "lap push");
        CHECK(ano_ring_mpmc_pop(&r, &e) && e.a == i && e.b == i * 3, "lap pop");
    }
    ano_ring_mpmc_destroy(&r);
    ano_ring_mpmc_destroy(&r);
}

static void test_seqpub_unit(void)
{
    anoseqpub p;
    CHECK(!ano_seqpub_init(&p, ano_mem_parent_default(), 0), "stride 0 refuses");
    CHECK(ano_seqpub_init(&p, ano_mem_parent_default(), 24), "init");
    uint8_t out[24];
    CHECK(!ano_seqpub_read(&p, out), "false before first publish");
    uint8_t v[24];
    for (int gen = 1; gen <= 5; gen++) {
        memset(v, gen, sizeof v);
        ano_seqpub_publish(&p, v);
        CHECK(ano_seqpub_read(&p, out), "read after publish");
        CHECK(out[0] == gen && out[23] == gen, "latest wins, untorn");
    }
    ano_seqpub_destroy(&p);
    ano_seqpub_destroy(&p);
}

// ---------------------------------------------------------------------------------------------
// Stress: conservation oracles across real threads. An item carries its producer id,
// a per-producer sequence, and a checksum; the sums prove no loss, no dup, no tear.

#define ST_PRODUCERS 8u
#define ST_CONSUMERS 8u
#define ST_PER_PROD  100000u
#define ST_TOTAL     (ST_PRODUCERS * ST_PER_PROD)
#define ST_CAP       1024u

typedef struct st_item {
    uint32_t producer;
    uint32_t seq;
    uint64_t check;                         // f(producer, seq): the tear detector
} st_item;

static uint64_t st_check(uint32_t p, uint32_t s)
{
    uint64_t x = ((uint64_t)p << 32) | s;
    x ^= x >> 33; x *= UINT64_C(0xff51afd7ed558ccd); x ^= x >> 33;
    return x;
}

static anoring_mpmc     st_mpmc;
static anoring_mpsc     st_mpsc;
static _Atomic uint32_t st_consumed;
static _Atomic uint64_t st_sum_out, st_xor_out;
static _Atomic uint32_t st_order_violations;

typedef struct st_ctx { uint32_t id; bool mpmc; } st_ctx;

static void *st_producer(void *arg)
{
    st_ctx *c = arg;
    st_item it = { .producer = c->id };
    for (uint32_t s = 0; s < ST_PER_PROD; s++) {
        it.seq   = s;
        it.check = st_check(c->id, s);
        if (c->mpmc)
            while (!ano_ring_mpmc_push(&st_mpmc, &it)) { /* spin: consumer lags */ }
        else
            while (!ano_ring_mpsc_push(&st_mpsc, &it)) { /* spin */ }
    }
    return NULL;
}

static void *st_mpmc_consumer(void *arg)
{
    (void)arg;
    uint32_t last[ST_PRODUCERS];
    for (uint32_t i = 0; i < ST_PRODUCERS; i++)
        last[i] = UINT32_MAX;               // "nothing seen": seq 0 must pass
    st_item it;
    uint64_t sum = 0, xr = 0;
    for (;;) {
        if (!ano_ring_mpmc_pop(&st_mpmc, &it)) {
            if (atomic_load_explicit(&st_consumed, memory_order_acquire) >= ST_TOTAL)
                break;
            continue;
        }
        if (it.check != st_check(it.producer, it.seq)
            || it.producer >= ST_PRODUCERS
            || (last[it.producer] != UINT32_MAX && it.seq <= last[it.producer]))
            atomic_fetch_add_explicit(&st_order_violations, 1u, memory_order_relaxed);
        last[it.producer] = it.seq;
        sum += it.check;
        xr  ^= it.check;
        atomic_fetch_add_explicit(&st_consumed, 1u, memory_order_release);
    }
    atomic_fetch_add_explicit(&st_sum_out, sum, memory_order_relaxed);
    atomic_fetch_xor_explicit(&st_xor_out, xr, memory_order_relaxed);
    return NULL;
}

static void test_mpmc_stress(void)
{
    CHECK(ano_ring_mpmc_init(&st_mpmc, ano_mem_parent_default(), ST_CAP, sizeof(st_item)),
          "stress ring init");
    atomic_store(&st_consumed, 0u);
    atomic_store(&st_sum_out, 0u);
    atomic_store(&st_xor_out, 0u);
    atomic_store(&st_order_violations, 0u);

    uint64_t sum_in = 0, xor_in = 0;
    for (uint32_t p = 0; p < ST_PRODUCERS; p++)
        for (uint32_t s = 0; s < ST_PER_PROD; s++) {
            sum_in += st_check(p, s);
            xor_in ^= st_check(p, s);
        }

    anothread_t prod[ST_PRODUCERS], cons[ST_CONSUMERS];
    st_ctx ctx[ST_PRODUCERS];
    for (uint32_t i = 0; i < ST_CONSUMERS; i++)
        CHECK(ano_thread_create(&cons[i], NULL, st_mpmc_consumer, NULL) == 0, "consumer");
    for (uint32_t i = 0; i < ST_PRODUCERS; i++) {
        ctx[i] = (st_ctx){ .id = i, .mpmc = true };
        CHECK(ano_thread_create(&prod[i], NULL, st_producer, &ctx[i]) == 0, "producer");
    }
    for (uint32_t i = 0; i < ST_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    for (uint32_t i = 0; i < ST_CONSUMERS; i++)
        ano_thread_join(cons[i], NULL);

    CHECK(atomic_load(&st_consumed) == ST_TOTAL, "mpmc: count conserved");
    CHECK(atomic_load(&st_sum_out) == sum_in, "mpmc: sum conserved (no loss/dup)");
    CHECK(atomic_load(&st_xor_out) == xor_in, "mpmc: xor conserved (no tear)");
    CHECK(atomic_load(&st_order_violations) == 0, "mpmc: per-producer FIFO everywhere");
    ano_ring_mpmc_destroy(&st_mpmc);
}

static void *st_mpsc_consumer(void *arg)
{
    (void)arg;
    uint32_t last[ST_PRODUCERS];
    for (uint32_t i = 0; i < ST_PRODUCERS; i++)
        last[i] = UINT32_MAX;
    static st_item batch[64];
    uint64_t sum = 0, xr = 0;
    uint32_t got = 0;
    while (got < ST_TOTAL) {
        uint32_t n = ano_ring_mpsc_drain(&st_mpsc, batch, 64);
        for (uint32_t i = 0; i < n; i++) {
            st_item *it = &batch[i];
            if (it->check != st_check(it->producer, it->seq)
                || it->producer >= ST_PRODUCERS
                || (last[it->producer] != UINT32_MAX && it->seq <= last[it->producer]))
                atomic_fetch_add_explicit(&st_order_violations, 1u, memory_order_relaxed);
            last[it->producer] = it->seq;
            sum += it->check;
            xr  ^= it->check;
        }
        got += n;
    }
    atomic_store(&st_sum_out, sum);
    atomic_store(&st_xor_out, xr);
    atomic_store(&st_consumed, got);
    return NULL;
}

static void test_mpsc_stress(void)
{
    CHECK(ano_ring_mpsc_init(&st_mpsc, ano_mem_parent_default(), ST_CAP, sizeof(st_item)),
          "stress ring init");
    atomic_store(&st_consumed, 0u);
    atomic_store(&st_order_violations, 0u);

    uint64_t sum_in = 0, xor_in = 0;
    for (uint32_t p = 0; p < ST_PRODUCERS; p++)
        for (uint32_t s = 0; s < ST_PER_PROD; s++) {
            sum_in += st_check(p, s);
            xor_in ^= st_check(p, s);
        }

    anothread_t prod[ST_PRODUCERS], cons;
    st_ctx ctx[ST_PRODUCERS];
    CHECK(ano_thread_create(&cons, NULL, st_mpsc_consumer, NULL) == 0, "consumer");
    for (uint32_t i = 0; i < ST_PRODUCERS; i++) {
        ctx[i] = (st_ctx){ .id = i, .mpmc = false };
        CHECK(ano_thread_create(&prod[i], NULL, st_producer, &ctx[i]) == 0, "producer");
    }
    for (uint32_t i = 0; i < ST_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    ano_thread_join(cons, NULL);

    CHECK(atomic_load(&st_consumed) == ST_TOTAL, "mpsc: count conserved");
    CHECK(atomic_load(&st_sum_out) == sum_in, "mpsc: sum conserved");
    CHECK(atomic_load(&st_xor_out) == xor_in, "mpsc: xor conserved");
    CHECK(atomic_load(&st_order_violations) == 0, "mpsc: per-producer FIFO");
    ano_ring_mpsc_destroy(&st_mpsc);
}

// SPMC fan-out: one producer, eight consumers. One producer means ONE global
// sequence, so the oracle strengthens: every consumer's slice must be strictly
// increasing, on top of count + sum + xor conservation.

static anoring_spmc st_spmc;

static void *st_spmc_producer(void *arg)
{
    (void)arg;
    st_item it = { .producer = 0 };
    for (uint32_t s = 0; s < ST_TOTAL; s++) {
        it.seq   = s;
        it.check = st_check(0, s);
        while (!ano_ring_spmc_push(&st_spmc, &it)) { /* spin: consumers lag */ }
    }
    return NULL;
}

static void *st_spmc_consumer(void *arg)
{
    (void)arg;
    uint32_t last = UINT32_MAX;                 // "nothing seen": seq 0 must pass
    st_item it;
    uint64_t sum = 0, xr = 0;
    for (;;) {
        if (!ano_ring_spmc_pop(&st_spmc, &it)) {
            if (atomic_load_explicit(&st_consumed, memory_order_acquire) >= ST_TOTAL)
                break;
            continue;
        }
        if (it.check != st_check(0, it.seq) || it.producer != 0
            || (last != UINT32_MAX && it.seq <= last))
            atomic_fetch_add_explicit(&st_order_violations, 1u, memory_order_relaxed);
        last = it.seq;
        sum += it.check;
        xr  ^= it.check;
        atomic_fetch_add_explicit(&st_consumed, 1u, memory_order_release);
    }
    atomic_fetch_add_explicit(&st_sum_out, sum, memory_order_relaxed);
    atomic_fetch_xor_explicit(&st_xor_out, xr, memory_order_relaxed);
    return NULL;
}

static void test_spmc_stress(void)
{
    CHECK(ano_ring_spmc_init(&st_spmc, ano_mem_parent_default(), ST_CAP, sizeof(st_item)),
          "stress ring init");
    atomic_store(&st_consumed, 0u);
    atomic_store(&st_sum_out, 0u);
    atomic_store(&st_xor_out, 0u);
    atomic_store(&st_order_violations, 0u);

    uint64_t sum_in = 0, xor_in = 0;
    for (uint32_t s = 0; s < ST_TOTAL; s++) {
        sum_in += st_check(0, s);
        xor_in ^= st_check(0, s);
    }

    anothread_t prod, cons[ST_CONSUMERS];
    for (uint32_t i = 0; i < ST_CONSUMERS; i++)
        CHECK(ano_thread_create(&cons[i], NULL, st_spmc_consumer, NULL) == 0, "consumer");
    CHECK(ano_thread_create(&prod, NULL, st_spmc_producer, NULL) == 0, "producer");
    ano_thread_join(prod, NULL);
    for (uint32_t i = 0; i < ST_CONSUMERS; i++)
        ano_thread_join(cons[i], NULL);

    CHECK(atomic_load(&st_consumed) == ST_TOTAL, "spmc: count conserved");
    CHECK(atomic_load(&st_sum_out) == sum_in, "spmc: sum conserved (no loss/dup)");
    CHECK(atomic_load(&st_xor_out) == xor_in, "spmc: xor conserved (no tear)");
    CHECK(atomic_load(&st_order_violations) == 0,
          "spmc: strictly increasing at every consumer");
    ano_ring_spmc_destroy(&st_spmc);
}

// SPSC mirror: one producer, one consumer, a million checksummed items.
#define SP_TOTAL 1000000u
static anoring_spsc sp_ring;

static void *sp_producer(void *arg)
{
    (void)arg;
    st_item it = { .producer = 0 };
    for (uint32_t s = 0; s < SP_TOTAL; s++) {
        it.seq   = s;
        it.check = st_check(0, s);
        while (!ano_ring_spsc_push(&sp_ring, &it)) { /* spin */ }
    }
    return NULL;
}

static void test_spsc_stress(void)
{
    CHECK(ano_ring_spsc_init(&sp_ring, ano_mem_parent_default(), 256, sizeof(st_item)),
          "spsc ring init");
    anothread_t prod;
    CHECK(ano_thread_create(&prod, NULL, sp_producer, NULL) == 0, "producer");
    st_item it;
    uint64_t xr = 0, expect = 0;
    uint32_t next = 0;
    while (next < SP_TOTAL) {
        if (!ano_ring_spsc_pop(&sp_ring, &it))
            continue;
        CHECK(it.seq == next, "spsc: strict FIFO");
        if (it.seq != next)
            break;                          // one line of noise, not a million
        xr ^= it.check;
        expect ^= st_check(0, next);
        next++;
    }
    ano_thread_join(prod, NULL);
    CHECK(next == SP_TOTAL && xr == expect, "spsc: all items, untorn");
    ano_ring_spsc_destroy(&sp_ring);
}

// Ticket uniqueness: 8 threads, 100k each, every ticket lands one bitmap bit.
#define TK_THREADS 8u
#define TK_PER     100000u
static anoticket_t      tk;
static _Atomic uint32_t tk_bitmap[TK_THREADS * TK_PER / 32];
static _Atomic uint32_t tk_dups;

static void *tk_worker(void *arg)
{
    (void)arg;
    for (uint32_t i = 0; i < TK_PER; i++) {
        uint64_t t = ano_ticket_next(&tk);
        if (t >= (uint64_t)TK_THREADS * TK_PER) {
            atomic_fetch_add_explicit(&tk_dups, 1u, memory_order_relaxed);
            continue;
        }
        uint32_t old = atomic_fetch_or_explicit(&tk_bitmap[t / 32],
                                                1u << (t % 32), memory_order_relaxed);
        if (old & (1u << (t % 32)))
            atomic_fetch_add_explicit(&tk_dups, 1u, memory_order_relaxed);
    }
    return NULL;
}

static void test_ticket_stress(void)
{
    ano_ticket_init(&tk, 0u);
    memset((void *)tk_bitmap, 0, sizeof tk_bitmap);
    atomic_store(&tk_dups, 0u);
    anothread_t th[TK_THREADS];
    for (uint32_t i = 0; i < TK_THREADS; i++)
        CHECK(ano_thread_create(&th[i], NULL, tk_worker, NULL) == 0, "ticket thread");
    for (uint32_t i = 0; i < TK_THREADS; i++)
        ano_thread_join(th[i], NULL);
    CHECK(atomic_load(&tk_dups) == 0, "tickets unique and in range");
    uint32_t holes = 0;
    for (size_t i = 0; i < sizeof tk_bitmap / sizeof tk_bitmap[0]; i++)
        if (atomic_load_explicit(&tk_bitmap[i], memory_order_relaxed) != UINT32_MAX)
            holes++;
    CHECK(holes == 0, "ticket range is dense (no skips)");
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    test_ticket_unit();
    test_spsc_unit();
    test_mpsc_unit();
    test_spmc_unit();
    test_mpmc_unit();
    test_seqpub_unit();

    test_mpmc_stress();
    test_mpsc_stress();
    test_spmc_stress();
    test_spsc_stress();
    test_ticket_stress();

    if (failures == 0) { printf("anotest_collections: all checks passed\n"); return 0; }
    printf("anotest_collections: %d check(s) failed\n", failures);
    return 1;
}
