/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Lock-free rings vs mutex+array baseline at same capacity/stride.
 * Grid: spsc 1x1, mpsc 4x1/8x1, spmc 1x4/1x8, mpmc 4x4/8x8 x stride {16,64}.
 * Plus: FAA ticket vs mutex counter, 8 threads.
 * Metrics: wall Mops/s, producer-0 push latency percentiles.
 * Merge bar: every lock-free flavor >= mutex at >= 4 threads.
 * DISABLED in ctest. Run -O3 by hand (build.sh 8 / build.bat 8). */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "anoptic_collections.h"
#include "anoptic_threads.h"
#include "templates/bench.h"

#define RB_CAP    1024u
#define RB_TOTAL  1000000u
#define RB_SAMPLE 200000u

static uint64_t g_sink;

/* Mutex Baseline */

// Bounded array queue, one lock.

typedef struct mtxq {
    anothread_mutex_t mtx;
    uint32_t head, tail, mask, stride;
    uint8_t *buf;
} mtxq;

static bool mtxq_init(mtxq *q, uint32_t cap, uint32_t stride)
{
    if (ano_mutex_init(&q->mtx, NULL) != 0)
        return false;
    q->head = q->tail = 0;
    q->mask = cap - 1;
    q->stride = stride;
    q->buf = mi_malloc_aligned((size_t)cap * stride, ANO_CACHE_LINE);
    return q->buf != NULL;
}

static void mtxq_destroy(mtxq *q)
{
    mi_free(q->buf);
    ano_mutex_destroy(&q->mtx);
}

static bool mtxq_push(mtxq *q, const void *e)
{
    ano_mutex_lock(&q->mtx);
    if (q->tail - q->head > q->mask) {
        ano_mutex_unlock(&q->mtx);
        return false;
    }
    memcpy(q->buf + (size_t)(q->tail & q->mask) * q->stride, e, q->stride);
    q->tail++;
    ano_mutex_unlock(&q->mtx);
    return true;
}

static bool mtxq_pop(mtxq *q, void *out)
{
    ano_mutex_lock(&q->mtx);
    if (q->head == q->tail) {
        ano_mutex_unlock(&q->mtx);
        return false;
    }
    memcpy(out, q->buf + (size_t)(q->head & q->mask) * q->stride, q->stride);
    q->head++;
    ano_mutex_unlock(&q->mtx);
    return true;
}

/* Run Config */

// One config, one run. kind: 0 spsc, 1 mpsc, 2 mpmc, 3 mutexq, 4 spmc.

typedef struct runcfg {
    int      kind;
    uint32_t producers, consumers, stride;
    const char *label;
} runcfg;

static anoring_spsc r_spsc;
static anoring_mpsc r_mpsc;
static anoring_spmc r_spmc;
static anoring_mpmc r_mpmc;
static mtxq         r_mtxq;
static _Atomic uint32_t g_consumed;
static uint64_t g_lat[RB_SAMPLE];

typedef struct workctx {
    const runcfg *cfg;
    uint32_t      id;
    uint32_t      count;      // items this thread
    bench_lat    *lat;        // producer-0 push samples
} workctx;

static bool push_one(int kind, const void *e)
{
    switch (kind) {
    case 0:  return ano_ring_spsc_push(&r_spsc, e);
    case 1:  return ano_ring_mpsc_push(&r_mpsc, e);
    case 2:  return ano_ring_mpmc_push(&r_mpmc, e);
    case 4:  return ano_ring_spmc_push(&r_spmc, e);
    default: return mtxq_push(&r_mtxq, e);
    }
}

static bool pop_one(int kind, void *out)
{
    switch (kind) {
    case 0:  return ano_ring_spsc_pop(&r_spsc, out);
    case 1:  return ano_ring_mpsc_pop(&r_mpsc, out);
    case 2:  return ano_ring_mpmc_pop(&r_mpmc, out);
    case 4:  return ano_ring_spmc_pop(&r_spmc, out);
    default: return mtxq_pop(&r_mtxq, out);
    }
}

static void *producer_main(void *arg)
{
    workctx *c = arg;
    uint8_t item[64] = {0};
    for (uint32_t i = 0; i < c->count; i++) {
        *(uint64_t *)item = ((uint64_t)c->id << 32) | i;
        if (c->lat != NULL) {
            uint64_t t0 = bench_begin();
            while (!push_one(c->cfg->kind, item)) { }
            bench_lat_add(c->lat, bench_end(t0));
        } else {
            while (!push_one(c->cfg->kind, item)) { }
        }
    }
    return NULL;
}

static void *consumer_main(void *arg)
{
    workctx *c = arg;
    uint8_t out[64];
    uint64_t local = 0;
    uint32_t total = c->cfg->producers * c->count;
    for (;;) {
        uint32_t seen = atomic_load_explicit(&g_consumed, memory_order_acquire);
        if (seen >= total)
            break;
        if (!pop_one(c->cfg->kind, out))
            continue;
        local += *(uint64_t *)out;
        atomic_fetch_add_explicit(&g_consumed, 1u, memory_order_release);
    }
    g_sink += local;
    return NULL;
}

static double run_config(const runcfg *cfg, bool report_lat)
{
    uint32_t per = RB_TOTAL / cfg->producers;
    bool mtx_kind = cfg->kind == 3;
    bool ok = true;
    switch (cfg->kind) {
    case 0:  ok = ano_ring_spsc_init(&r_spsc, ano_mem_parent_default(), RB_CAP, cfg->stride); break;
    case 1:  ok = ano_ring_mpsc_init(&r_mpsc, ano_mem_parent_default(), RB_CAP, cfg->stride); break;
    case 2:  ok = ano_ring_mpmc_init(&r_mpmc, ano_mem_parent_default(), RB_CAP, cfg->stride); break;
    case 4:  ok = ano_ring_spmc_init(&r_spmc, ano_mem_parent_default(), RB_CAP, cfg->stride); break;
    default: ok = mtxq_init(&r_mtxq, RB_CAP, cfg->stride); break;
    }
    if (!ok) {
        printf("%s: init failed\n", cfg->label);
        return 0.0;
    }
    atomic_store(&g_consumed, 0u);

    bench_lat lat;
    bench_lat_init(&lat, g_lat, RB_SAMPLE);
    static anothread_t prod[8], cons[8];
    static workctx pctx[8], cctx[8];

    uint64_t w0 = bench_begin();
    for (uint32_t i = 0; i < cfg->consumers; i++) {
        cctx[i] = (workctx){ .cfg = cfg, .id = i, .count = per };
        ano_thread_create(&cons[i], NULL, consumer_main, &cctx[i]);
    }
    for (uint32_t i = 0; i < cfg->producers; i++) {
        pctx[i] = (workctx){ .cfg = cfg, .id = i, .count = per,
                             .lat = (report_lat && i == 0) ? &lat : NULL };
        ano_thread_create(&prod[i], NULL, producer_main, &pctx[i]);
    }
    for (uint32_t i = 0; i < cfg->producers; i++)
        ano_thread_join(prod[i], NULL);
    for (uint32_t i = 0; i < cfg->consumers; i++)
        ano_thread_join(cons[i], NULL);
    uint64_t wallNs = ano_ticks_to_ns(bench_end(w0));

    uint32_t moved = atomic_load(&g_consumed);
    if (moved != per * cfg->producers)
        printf("%s: LOST ITEMS (%u of %u)\n", cfg->label, moved, per * cfg->producers);

    double mops = bench_ops_per_sec(moved, wallNs) / 1e6;
    if (report_lat) {
        bench_stats st = bench_lat_stats(&lat);
        bench_lat_row(cfg->label, st);
        printf("%-28s   wall %.1f ms, %.2f Mops/s through\n", "",
               (double)wallNs / 1e6, mops);
    }

    switch (cfg->kind) {
    case 0:  ano_ring_spsc_destroy(&r_spsc); break;
    case 1:  ano_ring_mpsc_destroy(&r_mpsc); break;
    case 2:  ano_ring_mpmc_destroy(&r_mpmc); break;
    case 4:  ano_ring_spmc_destroy(&r_spmc); break;
    default: mtxq_destroy(&r_mtxq); break;
    }
    (void)mtx_kind;
    return mops;
}

/* Ticket Bench */

// Ticket vs mutex counter, 8 threads x 500k.

static anoticket_t       tb_ticket;
static anothread_mutex_t tb_mtx;
static uint64_t          tb_counter;

static void *ticket_worker(void *arg)
{
    uint64_t last = 0;
    for (uint32_t i = 0; i < 500000u; i++)
        last = ano_ticket_next(&tb_ticket);
    g_sink += last;
    (void)arg;
    return NULL;
}

static void *mtxcnt_worker(void *arg)
{
    uint64_t last = 0;
    for (uint32_t i = 0; i < 500000u; i++) {
        ano_mutex_lock(&tb_mtx);
        last = tb_counter++;
        ano_mutex_unlock(&tb_mtx);
    }
    g_sink += last;
    (void)arg;
    return NULL;
}

static void run_tickets(void)
{
    anothread_t th[8];
    ano_ticket_init(&tb_ticket, 0);
    uint64_t t0 = bench_begin();
    for (int i = 0; i < 8; i++) ano_thread_create(&th[i], NULL, ticket_worker, NULL);
    for (int i = 0; i < 8; i++) ano_thread_join(th[i], NULL);
    double faaMs = (double)ano_ticks_to_ns(bench_end(t0)) / 1e6;

    ano_mutex_init(&tb_mtx, NULL);
    tb_counter = 0;
    t0 = bench_begin();
    for (int i = 0; i < 8; i++) ano_thread_create(&th[i], NULL, mtxcnt_worker, NULL);
    for (int i = 0; i < 8; i++) ano_thread_join(th[i], NULL);
    double mtxMs = (double)ano_ticks_to_ns(bench_end(t0)) / 1e6;
    ano_mutex_destroy(&tb_mtx);

    printf("ticket 8x500k: FAA %.1f ms (%.1f Mops/s)  vs  mutex %.1f ms (%.1f Mops/s)\n",
           faaMs, 4.0 / (faaMs / 1e3) , mtxMs, 4.0 / (mtxMs / 1e3));
}

int main(void)
{
    printf("ring grid: %u items, cap %u, lock-free vs mutex baseline\n",
           RB_TOTAL, RB_CAP);
    bench_lat_header();

    static const struct { int kind; uint32_t p, c; const char *ring, *mtx; } GRID[] = {
        { 0, 1, 1, "spsc 1x1",  "mutexq 1x1" },
        { 1, 4, 1, "mpsc 4x1",  "mutexq 4x1" },
        { 1, 8, 1, "mpsc 8x1",  "mutexq 8x1" },
        { 4, 1, 4, "spmc 1x4",  "mutexq 1x4" },
        { 4, 1, 8, "spmc 1x8",  "mutexq 1x8" },
        { 2, 4, 4, "mpmc 4x4",  "mutexq 4x4" },
        { 2, 8, 8, "mpmc 8x8",  "mutexq 8x8" },
    };
    uint32_t strides[2] = { 16, 64 };

    int losses = 0;
    for (size_t s = 0; s < 2; s++) {
        for (size_t g = 0; g < sizeof GRID / sizeof GRID[0]; g++) {
            char lbl[64];
            runcfg ring = { GRID[g].kind, GRID[g].p, GRID[g].c, strides[s], lbl };
            snprintf(lbl, sizeof lbl, "%s s%u", GRID[g].ring, strides[s]);
            double ringM = run_config(&ring, true);

            char mlbl[64];
            runcfg mtx = { 3, GRID[g].p, GRID[g].c, strides[s], mlbl };
            snprintf(mlbl, sizeof mlbl, "%s s%u", GRID[g].mtx, strides[s]);
            double mtxM = run_config(&mtx, true);

            uint32_t threads = GRID[g].p + GRID[g].c;
            if (threads >= 4 && ringM < mtxM) {
                printf("BAR MISS: %s %.2f < mutex %.2f Mops/s\n", lbl, ringM, mtxM);
                losses++;
            }
        }
    }
    run_tickets();

    printf("sink %llu\n", (unsigned long long)g_sink);
    if (losses == 0) { printf("anotest_ringbench: bar held on every >=4-thread point\n"); return 0; }
    printf("anotest_ringbench: %d bar miss(es)\n", losses);
    return 1;
}
