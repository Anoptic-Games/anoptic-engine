/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_render_bridge.h:
 *  - single-threaded SPSC ring: FIFO order, full/empty edges, index wraparound;
 *  - concurrent bidirectional stress (TSan target): a producer thread feeds
 *    commands, a consumer thread drains them and echoes events, the main thread
 *    drains events. Each ring has exactly one producer and one consumer, so this
 *    exercises the real SPSC contract under contention with small (wrapping) rings.
 * Exit 0 == pass. */

#include <stdio.h>
#include <stddef.h>
#include <mimalloc.h>
#include "anoptic_render_bridge.h"
#include "anoptic_threads.h"

// The false-sharing avoidance must be real, not aspirational.
_Static_assert(offsetof(AnoSpscRing, head) - offsetof(AnoSpscRing, tail) >= ANO_CACHE_LINE,
               "SPSC head/tail must live on separate cache lines");
_Static_assert(_Alignof(AnoSpscRing) == ANO_CACHE_LINE,
               "SPSC ring must be cache-line aligned");

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define ITEMS 100000u

// payload encodings, chosen so a misaligned/torn copy is caught
static inline uint32_t mesh_of(uint32_t i)     { return i ^ 0xABCDu; }
static inline uint32_t material_of(uint32_t i) { return i + 7u; }

static void *producer_fn(void *arg)
{
    AnoRenderBridge *b = arg;
    for (uint32_t i = 0; i < ITEMS; i++) {
        RenderCommand c = {0};
        c.kind           = RCMD_UPDATE;
        c.render_id      = i;
        c.fields         = RFIELD_MESH_MAT;
        c.mesh_index     = mesh_of(i);
        c.material_index = material_of(i);
        while (!ano_render_submit(b, &c)) { /* ring full: spin */ }
    }
    return NULL;
}

typedef struct
{
    AnoRenderBridge *b;
    uint32_t         order_err;
    uint32_t         payload_err;
    uint32_t         received;
} ConsumerCtx;

static void *consumer_fn(void *arg)
{
    ConsumerCtx *ctx = arg;
    uint32_t next = 0;
    while (next < ITEMS) {
        RenderCommand c;
        if (!ano_render_next_command(ctx->b, &c)) continue; // empty: spin
        if (c.render_id != next) ctx->order_err++;          // SPSC is FIFO
        if (c.mesh_index != mesh_of(next) || c.material_index != material_of(next))
            ctx->payload_err++;
        next++;
        RenderEvent e = { .kind = REVENT_SLOT_RETIRED, .render_id = c.render_id };
        while (!ano_render_emit_event(ctx->b, &e)) { /* event ring full: spin */ }
    }
    ctx->received = next;
    return NULL;
}

static void test_single_threaded(mi_heap_t *heap)
{
    AnoSpscRing r;
    CHECK(ano_spsc_init(&r, heap, 2, sizeof(uint32_t)), "spsc init (cap 2)");

    uint32_t x;
    x = 10u; CHECK(ano_spsc_push(&r, &x), "push 1");
    x = 20u; CHECK(ano_spsc_push(&r, &x), "push 2");
    x = 30u; CHECK(!ano_spsc_push(&r, &x), "push 3 rejected (full)");

    CHECK(ano_spsc_pop(&r, &x) && x == 10u, "pop 1 == 10 (FIFO)");
    CHECK(ano_spsc_pop(&r, &x) && x == 20u, "pop 2 == 20");
    CHECK(!ano_spsc_pop(&r, &x), "pop 3 rejected (empty)");

    // head/tail are now at 2/2; pushing again forces an index wrap through the mask
    x = 40u; CHECK(ano_spsc_push(&r, &x), "push after drain (wraps index)");
    CHECK(ano_spsc_pop(&r, &x) && x == 40u, "pop wrapped == 40");

    ano_spsc_destroy(&r);
}

int main(void)
{
    mi_heap_t *heap = mi_heap_new();
    CHECK(heap != NULL, "heap creation");

    test_single_threaded(heap);

    // Small rings (capacity 16) force frequent full/empty transitions and
    // wraparound over ITEMS — the interesting case for the race detector.
    AnoRenderBridge bridge; // stack: _Alignas on the ring propagates -> 64-aligned
    CHECK(ano_render_bridge_init(&bridge, heap, 16, 16), "bridge init");

    ConsumerCtx ctx = { .b = &bridge };
    anothread_t tp, tc;
    CHECK(ano_thread_create(&tp, NULL, producer_fn, &bridge) == 0, "spawn producer");
    CHECK(ano_thread_create(&tc, NULL, consumer_fn, &ctx) == 0, "spawn consumer");

    // Main thread is the sole consumer of the events ring.
    uint32_t evt_order_err = 0;
    for (uint32_t next = 0; next < ITEMS; ) {
        RenderEvent e;
        if (!ano_render_poll_event(&bridge, &e)) continue;
        if (e.render_id != next) evt_order_err++;
        next++;
    }

    ano_thread_join(tp, NULL);
    ano_thread_join(tc, NULL);

    CHECK(ctx.received == ITEMS, "consumer received every command");
    CHECK(ctx.order_err == 0, "commands arrived in FIFO order");
    CHECK(ctx.payload_err == 0, "command payloads intact (no tearing)");
    CHECK(evt_order_err == 0, "events arrived in FIFO order");

    ano_render_bridge_destroy(&bridge);
    mi_heap_destroy(heap);

    if (failures == 0) { printf("anotest_render_bridge: all checks passed\n"); return 0; }
    printf("anotest_render_bridge: %d check(s) failed\n", failures);
    return 1;
}
