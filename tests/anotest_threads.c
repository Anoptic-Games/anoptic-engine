/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// anoptic_threads.h: barrier init domain, exactly-count cohorts, over-subscribed reuse.
// Watchdog kills a stalled tally. Canaries fence the barrier object. Exit 0 == pass.

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <anoptic_threads.h>
#include <anoptic_time.h>

#include "templates/sanopts.h"   // ASan alt-stack opt-out; see header

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define CANARY           0x5AFEC0DEu
#define WATCHDOG_POLL_US (250u * 1000u)
#define WATCHDOG_STALLS  40u             // 10s of a frozen tally


/* Barrier fence */

// The barrier under test, walled by canaries: init and wait may touch nothing else.
static struct {
    unsigned            lead[4];
    anothread_barrier_t bar;
    unsigned            tail[4];
} fenced;

static void arm_fence(void)
{
    for (int i = 0; i < 4; ++i) { fenced.lead[i] = CANARY; fenced.tail[i] = CANARY; }
}

static bool fence_intact(void)
{
    for (int i = 0; i < 4; ++i)
        if (fenced.lead[i] != CANARY || fenced.tail[i] != CANARY) return false;
    return true;
}


/* Watchdog */

// Fires on a frozen tally, never on wall clock: an oversubscribed host is slow, not deadlocked,
// and a run killed on elapsed time cannot tell the two apart. ctest's TIMEOUT is the outer bound.

static atomic_uint arrivals, releases, serials, early, cwork;

static void *watchdog(void *arg)
{
    (void)arg;
    unsigned long long last = 0;
    for (unsigned stalls = 0; stalls < WATCHDOG_STALLS; ) {
        ano_sleep(WATCHDOG_POLL_US);
        // any section's tally moving counts as progress; a reset between sections reads as a move
        unsigned long long now = (unsigned long long)atomic_load(&arrivals)
                               + atomic_load(&releases) + atomic_load(&serials)
                               + atomic_load(&cwork);
        stalls = now == last ? stalls + 1u : 0u;
        last   = now;
    }
    printf("FAIL: barrier deadlocked -- arrivals=%u releases=%u serials=%u early=%u\n",
           atomic_load(&arrivals), atomic_load(&releases),
           atomic_load(&serials), atomic_load(&early));
    fflush(stdout);
    _Exit(1);
}


/* Init domain */

// count 0 refused; count 1 and 64 accepted. Count-1 wait returns serial every pass.
static void test_barrier_init_domain(void)
{
    printf("barrier: init domain\n");
    arm_fence();
    CHECK(ano_thread_barrier_init(&fenced.bar, NULL, 0) != 0, "count 0 is refused");
    CHECK(ano_thread_barrier_init(&fenced.bar, NULL, 1) == 0, "count 1 is accepted");
    CHECK(ano_thread_barrier_wait(&fenced.bar) != 0,
          "the lone thread of a count-1 barrier is the serial thread");
    CHECK(ano_thread_barrier_wait(&fenced.bar) != 0, "a count-1 barrier reuses");
    CHECK(ano_thread_barrier_destroy(&fenced.bar) == 0, "destroy");
    CHECK(ano_thread_barrier_init(&fenced.bar, NULL, 64) == 0, "count 64 is accepted");
    CHECK(ano_thread_barrier_destroy(&fenced.bar) == 0, "destroy");
    CHECK(fence_intact(), "init wrote only the barrier object");
}


/* Exactly-count cohorts */

// count threads on a count barrier: each wait is a full memory barrier for the cohort's plain
// writes, exactly one thread per phase gets the serial return, and the barrier reuses forever.

#define CN      4u
#define CROUNDS 4000u

static atomic_int  cbad;
static unsigned    cpayload[CN];   // plain memory: the barrier owes it a happens-before

static void *cohort_thread(void *arg)
{
    unsigned id = (unsigned)(uintptr_t)arg;
    for (unsigned r = 0; r < CROUNDS; ++r) {
        cpayload[id] = r + 1u;
        atomic_fetch_add_explicit(&cwork, 1u, memory_order_seq_cst);
        if (ano_thread_barrier_wait(&fenced.bar) != 0)
            atomic_fetch_add_explicit(&serials, 1u, memory_order_seq_cst);
        // the whole cohort's round-r writes are visible, and none of round r+1
        for (unsigned j = 0; j < CN; ++j)
            if (cpayload[j] != r + 1u) atomic_store_explicit(&cbad, 1, memory_order_seq_cst);
        if (atomic_load_explicit(&cwork, memory_order_seq_cst) != (r + 1u) * CN)
            atomic_store_explicit(&cbad, 1, memory_order_seq_cst);
        if (ano_thread_barrier_wait(&fenced.bar) != 0)
            atomic_fetch_add_explicit(&serials, 1u, memory_order_seq_cst);
    }
    return NULL;
}

static void test_barrier_cohorts(void)
{
    printf("barrier: exactly-count cohorts (%u threads, count %u, %u rounds)\n",
           CN, CN, CROUNDS);
    arm_fence();
    CHECK(ano_thread_barrier_init(&fenced.bar, NULL, CN) == 0, "init accepts count 4");
    atomic_store(&cwork, 0u); atomic_store(&cbad, 0); atomic_store(&serials, 0u);

    anothread_t t[CN];
    for (unsigned i = 0; i < CN; ++i)
        CHECK(ano_thread_create(&t[i], NULL, cohort_thread, (void *)(uintptr_t)i) == 0,
              "cohort thread spawned");
    for (unsigned i = 0; i < CN; ++i) ano_thread_join(t[i], NULL);

    CHECK(atomic_load(&cbad) == 0, "nobody ran ahead of or behind its cohort");
    CHECK(atomic_load(&cwork) == CROUNDS * CN, "every increment landed");
    CHECK(atomic_load(&serials) == CROUNDS * 2u, "exactly one serial return per cohort");
    CHECK(fence_intact(), "nothing wrote outside the barrier object");
    ano_thread_barrier_destroy(&fenced.bar);
}


/* Over-subscribed reuse */

// More threads than count share one barrier. Arrival budget is a multiple of count; total == BUDGET.

#define BUDGET 60000

static atomic_int budget;
static unsigned   tcount;

static void *reuse_thread(void *arg)
{
    (void)arg;
    while (atomic_fetch_sub_explicit(&budget, 1, memory_order_seq_cst) > 0) {
        atomic_fetch_add_explicit(&arrivals, 1u, memory_order_seq_cst);
        int serial = ano_thread_barrier_wait(&fenced.bar);
        unsigned rel = atomic_fetch_add_explicit(&releases, 1u, memory_order_seq_cst) + 1u;
        unsigned arr = atomic_load_explicit(&arrivals, memory_order_seq_cst);
        // releases can never outrun the arrivals of whole cohorts (arr only over-counts)
        if (rel > (arr / tcount) * tcount)
            atomic_fetch_add_explicit(&early, 1u, memory_order_seq_cst);
        if (serial != 0) atomic_fetch_add_explicit(&serials, 1u, memory_order_seq_cst);
    }
    return NULL;
}

static void test_barrier_oversubscribed(unsigned n, unsigned m)
{
    unsigned total = (unsigned)BUDGET - (unsigned)BUDGET % n;
    printf("barrier: %u threads share a count-%u barrier, %u arrivals\n", m, n, total);
    arm_fence();
    CHECK(ano_thread_barrier_init(&fenced.bar, NULL, n) == 0, "init");
    tcount = n;
    atomic_store(&budget, (int)total);
    atomic_store(&arrivals, 0u); atomic_store(&releases, 0u);
    atomic_store(&serials, 0u);  atomic_store(&early, 0u);

    anothread_t *t = malloc(m * sizeof *t);
    CHECK(t != NULL, "thread table");
    if (t == NULL) return;
    for (unsigned i = 0; i < m; ++i)
        CHECK(ano_thread_create(&t[i], NULL, reuse_thread, NULL) == 0, "reuse thread spawned");
    for (unsigned i = 0; i < m; ++i) ano_thread_join(t[i], NULL);
    free(t);

    CHECK(atomic_load(&early) == 0u, "nobody released before its own cohort filled");
    CHECK(atomic_load(&serials) == total / n, "exactly one serial return per cohort");
    CHECK(atomic_load(&releases) == total, "no arrival was erased");
    CHECK(fence_intact(), "nothing wrote outside the barrier object");
    ano_thread_barrier_destroy(&fenced.bar);
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   // progress must survive a deadlocked barrier

    anothread_t wd;
    if (ano_thread_create(&wd, NULL, watchdog, NULL) == 0) ano_thread_detach(wd);

    test_barrier_init_domain();
    test_barrier_cohorts();
    test_barrier_oversubscribed(3u, 9u);
    test_barrier_oversubscribed(5u, 16u);
    test_barrier_oversubscribed(2u, 3u);

    if (failures) {
        printf("anotest_threads: %d FAILURE(S)\n", failures);
        return failures;
    }
    printf("anotest_threads: all checks passed\n");
    return 0;
}
