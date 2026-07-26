/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* render_bridge transport (private render_bridge.h; public protocol in anoptic_render.h):
 *  - single-threaded SPSC: FIFO, full/empty edges, index wrap;
 *  - bidirectional stress (TSan): one producer, one consumer, main drains events;
 *  - submission results: the four AnoRenderSubmitResult codes across the six owned-payload
 *    endpoints, the packed block's self-containment, the two CLEAR delegations, and a producer
 *    parked on a full ring observing a shutdown flag.
 * Each ring has exactly one producer and one consumer. Exit 0 == pass. */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <mimalloc.h>
#include "render_bridge/render_bridge.h" // private transport: SPSC ring + bridge + endpoints
#include "anoptic_memory.h" // ANO_CACHE_LINE / ANO_THREAD_LINE
#include "anoptic_threads.h"
#include "anoptic_time.h" // ano_sleep
#include "anoptic_log.h"  // route the bridge's drop warnings; this suite runs no logger

_Static_assert(offsetof(AnoSpscRing, head) - offsetof(AnoSpscRing, tail) >= ANO_CACHE_LINE,
               "SPSC head/tail must live on separate cache lines");
// Alignas floor: lesser of ANO_CACHE_LINE and ANO_THREAD_LINE.
#define ANO_MIN_LINE (ANO_CACHE_LINE < ANO_THREAD_LINE ? ANO_CACHE_LINE : ANO_THREAD_LINE)
_Static_assert(_Alignof(AnoSpscRing) >= ANO_MIN_LINE,
               "SPSC ring must be cache-line aligned");

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define ITEMS 100000u


/* Bidirectional Stress */

// payload encodings so a misaligned/torn copy is caught
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
        RenderEvent e = { .kind = REVENT_SLOT_RETIRED, .u.render_id = c.render_id };
        while (!ano_render_emit_event(ctx->b, &e)) { /* event ring full: spin */ }
    }
    ctx->received = next;
    return NULL;
}


/* Single-Threaded SPSC */

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

    // head/tail at 2/2; next push wraps through the mask
    x = 40u; CHECK(ano_spsc_push(&r, &x), "push after drain (wraps index)");
    CHECK(ano_spsc_pop(&r, &x) && x == 40u, "pop wrapped == 40");

    ano_spsc_destroy(&r);
}


/* Submission Results */

// The six owned-payload endpoints answer AnoRenderSubmitResult, so every case here reads .code
// (ACCEPTED is 0 〜 a truth test on the result would be backwards) AND the ring: an ACCEPTED lands
// exactly one command whose block is self-contained, a BACKPRESSURE or an INVALID leaves the ring
// exactly as it found it. Single-threaded, so this suite is both producer and consumer.

// in:  b (bridge)
// out: commands currently enqueued
static uint32_t cmd_depth(AnoRenderBridge *b)
{
    return atomic_load(&b->commands.tail) - atomic_load(&b->commands.head);
}

// in:  p (an interior pointer of a packed block), blk (block base), hdr (header bytes), bytes (packed size)
// out: true when p addresses that block's own payload region rather than a caller array
// inv: an empty table sits at the block's end, so the upper bound is inclusive.
static bool packed_at(const void *p, const void *blk, size_t hdr, size_t bytes)
{
    uintptr_t a = (uintptr_t)p, base = (uintptr_t)blk;
    return a >= base + hdr && a <= base + bytes;
}

// in:  out (>= n instances), n
// out: n instances stamped with distinguishable glyphIDs, so a lost or aliased copy shows
static void fill_glyphs(AnoGlyphInstance *out, uint32_t n)
{
    memset(out, 0, (size_t)n * sizeof *out);
    for (uint32_t i = 0; i < n; i++) out[i].glyphID = 0x5A00u + i;
}

#define UI_GLYPHS_N 2u

// Caller-owned backing for a hand-filled builder. AnoUiBuilder is POD, so ui_prim_valid is
// reachable without the builder API.
typedef struct
{
    AnoUiPrim    prims[3];
    AnoUiClip    clips[1];
    AnoUiPaint   paints[1];
    AnoUiStop    stops[2];
    uint32_t     curves[3];
    AnoUiBuilder b;
} UiFixture;

// in:  f (uninitialized)
// out: f->b bound to f's own arrays: an RRECT naming clip 0 + paint 0, a PATH walking one monotone
//      quad of the three-word curve stream, and a GLYPHS prim naming the whole [0, UI_GLYPHS_N) window
// inv: every reference is in range, so all three prims pass ui_prim_valid. The refusal cases below
//      invalidate exactly one field of a freshly initialized fixture.
static void ui_fixture_init(UiFixture *f)
{
    memset(f, 0, sizeof *f);
    f->prims[0].kind = ANO_UI_RRECT;
    f->prims[0].paintRef = 0u;               f->prims[0].clipRef = 0u;
    f->prims[1].kind = ANO_UI_PATH;
    f->prims[1].paintRef = ANO_UI_REF_NONE;  f->prims[1].clipRef = ANO_UI_REF_NONE;
    f->prims[1].aux0 = 0u;                   f->prims[1].aux1 = 1u;
    f->prims[2].kind = ANO_UI_GLYPHS;
    f->prims[2].paintRef = ANO_UI_REF_NONE;  f->prims[2].clipRef = ANO_UI_REF_NONE;
    f->prims[2].aux0 = 0u;                   f->prims[2].aux1 = UI_GLYPHS_N;
    f->clips[0].rrHalf[0] = -1.0f;           // rect-only clip
    f->paints[0].kind = ANO_UI_GRAD_LINEAR;
    f->paints[0].stopFirst = 0u;             f->paints[0].stopCount = 2u;
    f->stops[1].t = 1.0f;
    f->curves[0] = 0x00000000u;              // start point, control, end: no sentinel in the walk
    f->curves[1] = 0x3C003C00u;
    f->curves[2] = 0x40004000u;
    f->b = (AnoUiBuilder){ .prims  = f->prims,  .primCap  = 3u, .primCount  = 3u,
                           .clips  = f->clips,  .clipCap  = 1u, .clipCount  = 1u,
                           .paints = f->paints, .paintCap = 1u, .paintCount = 1u,
                           .stops  = f->stops,  .stopCap  = 2u, .stopCount  = 2u,
                           .curves = f->curves, .curveCap = 3u, .curveCount = 3u };
}

#define BULK_N 3u

typedef struct
{
    uint32_t            ids[BULK_N];
    mat4                xforms[BULK_N];
    AnoMotionDescriptor motion[BULK_N];
    uint32_t            mesh[BULK_N];
    uint32_t            material[BULK_N];
    AnoInstanceData     inst[BULK_N];
    RenderUpdateBatch   batch;
} BulkFixture;

// in:  f (uninitialized)
// out: f->batch over BULK_N rows naming every bulk field, each array present and stamped
static void bulk_fixture_init(BulkFixture *f)
{
    memset(f, 0, sizeof *f);
    for (uint32_t i = 0; i < BULK_N; i++) {
        f->ids[i]            = 100u + i;
        f->xforms[i][0][0]   = (float)(1u + i);
        f->motion[i].type    = ANO_MOTION_SPIN;
        f->mesh[i]           = 200u + i;
        f->material[i]       = 300u + i;
        f->inst[i].packed[0] = 400u + i;
    }
    f->batch = (RenderUpdateBatch){
        .count = BULK_N, .fields = RFIELD_TRANSFORM | RFIELD_ANIM | RFIELD_MESH_MAT | RFIELD_USERDATA,
        .render_ids = f->ids, .transforms = (const mat4 *)f->xforms, .motion = f->motion,
        .mesh = f->mesh, .material = f->material, .instance_data = f->inst };
}

// Packed size of the block bulk_fixture_init's batch produces (the packer's own accumulation,
// including the round-up that puts the over-aligned sub-arrays on a 16-byte offset).
#define BULK_IDS_END (sizeof(RenderUpdateBatch) + (size_t)BULK_N * sizeof(uint32_t))
#define BULK_UPDATE_BYTES (((BULK_IDS_END + 15u) & ~(size_t)15u)                           \
    + (size_t)BULK_N * (sizeof(mat4) + sizeof(AnoMotionDescriptor)                         \
                        + sizeof(AnoInstanceData) + 2u * sizeof(uint32_t)))

// Every endpoint's ACCEPTED, verified at the ring: right kind and handle, ownership flagged where a
// block rides along, and each interior pointer addressing that block instead of the caller's arrays.
static void test_accepted(mi_heap_t *heap)
{
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, heap, 16, 2), "results: bridge init (cap 16)");
    RenderCommand c;
    AnoRenderSubmitResult r;

    AnoGlyphInstance inst[4];
    fill_glyphs(inst, 4u);
    r = ano_render_text_set(&b, 7u, inst, 4u);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "text_set: ACCEPTED");
    CHECK(cmd_depth(&b) == 1u, "text_set: exactly one command enqueued");
    if (ano_render_next_command(&b, &c) && c.text != NULL) {
        const RenderTextBlock *t = c.text;
        size_t bytes = sizeof(RenderTextBlock) + 4u * sizeof(AnoGlyphInstance);
        CHECK(c.kind == RCMD_TEXT_SET && c.text_id == 7u, "text_set: RCMD_TEXT_SET + text_id");
        CHECK(c.bulk_owned, "text_set: block ownership crossed into the bridge");
        CHECK(t->count == 4u, "text_set: count packed");
        CHECK(t->instances != inst, "text_set: instances copied, not borrowed");
        CHECK(packed_at(t->instances, t, sizeof(RenderTextBlock), bytes),
              "text_set: instances inside the block");
        CHECK(memcmp(t->instances, inst, sizeof inst) == 0, "text_set: instance bytes intact");
        ano_render_command_release(&c);
    } else CHECK(false, "text_set: command with block popped");

    r = ano_render_text_clear(&b, 7u);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "text_clear: ACCEPTED");
    CHECK(ano_render_next_command(&b, &c) && c.kind == RCMD_TEXT_CLEAR && c.text_id == 7u
          && !c.bulk_owned, "text_clear: RCMD_TEXT_CLEAR, nothing owned");

    UiFixture uf;
    ui_fixture_init(&uf);
    AnoGlyphInstance uglyph[UI_GLYPHS_N];
    fill_glyphs(uglyph, UI_GLYPHS_N);
    r = ano_render_ui_set(&b, 3u, 9u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "ui_set: ACCEPTED");
    CHECK(cmd_depth(&b) == 1u, "ui_set: exactly one command enqueued");
    if (ano_render_next_command(&b, &c) && c.ui != NULL) {
        const RenderUiBlock *u = c.ui;
        const size_t hdr = sizeof(RenderUiBlock);
        size_t bytes = hdr + 3u * sizeof(AnoUiPrim) + sizeof(AnoUiClip) + sizeof(AnoUiPaint)
                     + 2u * sizeof(AnoUiStop) + 3u * sizeof(uint32_t)
                     + (size_t)UI_GLYPHS_N * sizeof(AnoGlyphInstance);
        CHECK(c.kind == RCMD_UI_SET && c.ui_id == 3u, "ui_set: RCMD_UI_SET + ui_id");
        CHECK(c.bulk_owned, "ui_set: block ownership crossed into the bridge");
        CHECK(u->layer == 9u && u->surface == ANO_UI_SURFACE_OVERLAY, "ui_set: layer + surface");
        CHECK(u->primCount == 3u && u->clipCount == 1u && u->paintCount == 1u && u->stopCount == 2u
              && u->curveCount == 3u && u->glyphCount == UI_GLYPHS_N, "ui_set: table counts packed");
        CHECK(packed_at(u->prims, u, hdr, bytes) && packed_at(u->clips, u, hdr, bytes)
              && packed_at(u->paints, u, hdr, bytes) && packed_at(u->stops, u, hdr, bytes)
              && packed_at(u->curves, u, hdr, bytes) && packed_at(u->glyphs, u, hdr, bytes),
              "ui_set: every table inside the block");
        CHECK(u->prims != uf.prims && u->clips != uf.clips && u->paints != uf.paints
              && u->stops != uf.stops && u->curves != uf.curves && u->glyphs != uglyph,
              "ui_set: tables copied, not borrowed");
        CHECK(memcmp(u->prims, uf.prims, sizeof uf.prims) == 0
              && memcmp(u->clips, uf.clips, sizeof uf.clips) == 0
              && memcmp(u->paints, uf.paints, sizeof uf.paints) == 0
              && memcmp(u->stops, uf.stops, sizeof uf.stops) == 0
              && memcmp(u->curves, uf.curves, sizeof uf.curves) == 0
              && memcmp(u->glyphs, uglyph, sizeof uglyph) == 0, "ui_set: table bytes intact");
        ano_render_command_release(&c);
    } else CHECK(false, "ui_set: command with block popped");

    r = ano_render_ui_clear(&b, 3u);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "ui_clear: ACCEPTED");
    CHECK(ano_render_next_command(&b, &c) && c.kind == RCMD_UI_CLEAR && c.ui_id == 3u
          && !c.bulk_owned, "ui_clear: RCMD_UI_CLEAR, nothing owned");

    BulkFixture bf;
    bulk_fixture_init(&bf);
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "bulk_update: ACCEPTED");
    CHECK(cmd_depth(&b) == 1u, "bulk_update: exactly one command enqueued");
    if (ano_render_next_command(&b, &c) && c.update != NULL) {
        const RenderUpdateBatch *u = c.update;
        const size_t hdr = sizeof(RenderUpdateBatch), bytes = BULK_UPDATE_BYTES;
        CHECK(c.kind == RCMD_BULK_UPDATE && c.bulk_owned, "bulk_update: RCMD_BULK_UPDATE + ownership");
        CHECK(u->count == BULK_N && u->fields == bf.batch.fields, "bulk_update: count + fields packed");
        CHECK(packed_at(u->render_ids, u, hdr, bytes) && packed_at(u->transforms, u, hdr, bytes)
              && packed_at(u->motion, u, hdr, bytes) && packed_at(u->mesh, u, hdr, bytes)
              && packed_at(u->material, u, hdr, bytes) && packed_at(u->instance_data, u, hdr, bytes),
              "bulk_update: every array inside the block");
        CHECK(u->render_ids != bf.ids && u->transforms != (const mat4 *)bf.xforms
              && u->motion != bf.motion && u->mesh != bf.mesh && u->material != bf.material
              && u->instance_data != bf.inst, "bulk_update: arrays copied, not borrowed");
        CHECK(memcmp(u->render_ids, bf.ids, sizeof bf.ids) == 0
              && memcmp(u->transforms, bf.xforms, sizeof bf.xforms) == 0
              && memcmp(u->motion, bf.motion, sizeof bf.motion) == 0
              && memcmp(u->mesh, bf.mesh, sizeof bf.mesh) == 0
              && memcmp(u->material, bf.material, sizeof bf.material) == 0
              && memcmp(u->instance_data, bf.inst, sizeof bf.inst) == 0,
              "bulk_update: array bytes intact");
        ano_render_command_release(&c);
    } else CHECK(false, "bulk_update: command with block popped");

    r = ano_render_submit_bulk_destroy(&b, bf.ids, BULK_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "bulk_destroy: ACCEPTED");
    CHECK(cmd_depth(&b) == 1u, "bulk_destroy: exactly one command enqueued");
    if (ano_render_next_command(&b, &c) && c.destroy != NULL) {
        const RenderDestroyBatch *d = c.destroy;
        size_t bytes = sizeof(RenderDestroyBatch) + (size_t)BULK_N * sizeof(uint32_t);
        CHECK(c.kind == RCMD_BULK_DESTROY && c.bulk_owned, "bulk_destroy: RCMD_BULK_DESTROY + ownership");
        CHECK(d->count == BULK_N, "bulk_destroy: count packed");
        CHECK(d->render_ids != bf.ids, "bulk_destroy: ids copied, not borrowed");
        CHECK(packed_at(d->render_ids, d, sizeof(RenderDestroyBatch), bytes),
              "bulk_destroy: ids inside the block");
        CHECK(memcmp(d->render_ids, bf.ids, sizeof bf.ids) == 0, "bulk_destroy: id bytes intact");
        ano_render_command_release(&c);
    } else CHECK(false, "bulk_destroy: command with block popped");

    CHECK(cmd_depth(&b) == 0u, "results: ring drained, nothing leaked");
    ano_render_bridge_destroy(&b);
}

// A ring at capacity refuses all six, and the refusal enqueues nothing: the packed block is released
// on the way out, so the depth is exactly what the bare submits left.
static void test_backpressure(mi_heap_t *heap)
{
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, heap, 2, 2), "backpressure: bridge init (cap 2)");
    RenderCommand fill = { .kind = RCMD_UPDATE, .render_id = 1u };
    CHECK(ano_render_submit(&b, &fill), "backpressure: fill 1");
    fill.render_id = 2u;
    CHECK(ano_render_submit(&b, &fill), "backpressure: fill 2");
    CHECK(!ano_render_submit(&b, &fill), "backpressure: ring is full");

    AnoGlyphInstance inst[2];
    fill_glyphs(inst, 2u);
    AnoGlyphInstance uglyph[UI_GLYPHS_N];
    fill_glyphs(uglyph, UI_GLYPHS_N);
    UiFixture uf;   ui_fixture_init(&uf);
    BulkFixture bf; bulk_fixture_init(&bf);
    AnoRenderSubmitResult r;

    r = ano_render_text_set(&b, 1u, inst, 2u);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "text_set: BACKPRESSURE on a full ring");
    r = ano_render_text_clear(&b, 1u);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "text_clear: BACKPRESSURE on a full ring");
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "ui_set: BACKPRESSURE on a full ring");
    r = ano_render_ui_clear(&b, 1u);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "ui_clear: BACKPRESSURE on a full ring");
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "bulk_update: BACKPRESSURE on a full ring");
    r = ano_render_submit_bulk_destroy(&b, bf.ids, BULK_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "bulk_destroy: BACKPRESSURE on a full ring");
    CHECK(cmd_depth(&b) == 2u, "backpressure: ring depth unchanged");

    // What is on the ring is the two bare fills, in order, and nothing else.
    RenderCommand c;
    uint32_t drained = 0;
    while (ano_render_next_command(&b, &c)) {
        CHECK(c.kind == RCMD_UPDATE && c.render_id == ++drained, "backpressure: only the fills survive");
        ano_render_command_release(&c);
    }
    CHECK(drained == 2u, "backpressure: exactly two commands drained");
    ano_render_bridge_destroy(&b);
}

// One case per refusal arm. INVALID is deterministic, so it must also enqueue nothing 〜 the depth
// is asserted after each, and the run ends with the ring still empty.
static void test_invalid(mi_heap_t *heap)
{
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, heap, 16, 2), "invalid: bridge init");
    AnoGlyphInstance uglyph[UI_GLYPHS_N];
    fill_glyphs(uglyph, UI_GLYPHS_N);
    UiFixture uf;
    BulkFixture bf;
    AnoRenderSubmitResult r;

    r = ano_render_text_set(&b, 1u, NULL, 3u);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "text_set: a count without instances is INVALID, nothing enqueued");

    r = ano_render_ui_set(&b, 1u, 0u, NULL, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: a NULL builder is INVALID, never a silent clear");

    ui_fixture_init(&uf); uf.b.primCount = ANO_RENDER_UI_MAX_PRIMS + 1u;
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: primCount over its per-block cap is INVALID, nothing enqueued");

    ui_fixture_init(&uf);
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, NULL, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: a glyphCount without glyphs is INVALID, nothing enqueued");

    ui_fixture_init(&uf); uf.b.clipCount = 0u; // prim 0 still names clip 0
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: an out-of-range clipRef is INVALID, nothing enqueued");

    ui_fixture_init(&uf); uf.b.paintCount = 0u; // prim 0 still names paint 0
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: an out-of-range paintRef is INVALID, nothing enqueued");

    ui_fixture_init(&uf); uf.paints[0].stopFirst = 1u; // window leaves the two-entry stop table
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: a paint whose stop window leaves the table is INVALID, nothing enqueued");

    ui_fixture_init(&uf); uf.prims[2].aux1 = UI_GLYPHS_N + 1u;
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: a GLYPHS window past the array is INVALID, nothing enqueued");

    ui_fixture_init(&uf); uf.b.curveCount = 2u; // the quad's end word is off the stream
    r = ano_render_ui_set(&b, 1u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "ui_set: a PATH walk overrunning the curve stream is INVALID, nothing enqueued");

    r = ano_render_submit_bulk_update(&b, NULL);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_update: a NULL batch is INVALID, nothing enqueued");

    bulk_fixture_init(&bf); bf.batch.render_ids = NULL;
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_update: NULL render_ids is INVALID, nothing enqueued");

    bulk_fixture_init(&bf); bf.batch.transforms = NULL;
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_update: RFIELD_TRANSFORM without transforms is INVALID, nothing enqueued");

    bulk_fixture_init(&bf); bf.batch.motion = NULL;
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_update: RFIELD_ANIM without motion is INVALID, nothing enqueued");

    bulk_fixture_init(&bf); bf.batch.mesh = NULL;
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_update: RFIELD_MESH_MAT without mesh is INVALID, nothing enqueued");

    bulk_fixture_init(&bf); bf.batch.material = NULL;
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_update: RFIELD_MESH_MAT without material is INVALID, nothing enqueued");

    bulk_fixture_init(&bf); bf.batch.instance_data = NULL;
    r = ano_render_submit_bulk_update(&b, &bf.batch);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_update: RFIELD_USERDATA without instance_data is INVALID, nothing enqueued");

    r = ano_render_submit_bulk_destroy(&b, NULL, BULK_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_INVALID && cmd_depth(&b) == 0u,
          "bulk_destroy: a count without render_ids is INVALID, nothing enqueued");

    // The remaining INVALID arm, an unrepresentable packed size, has no case here: the counts are
    // uint32_t and bulk_update's widest row costs 156 bytes, so the largest packed size the public
    // signature can ask for is about 2^39 〜 representable on every 64-bit size_t. Its guard is
    // covered directly at ano_size_add_array below rather than by faking reachability.
    CHECK(cmd_depth(&b) == 0u, "invalid: the ring stayed empty throughout");
    ano_render_bridge_destroy(&b);
}

// count 0 and an empty builder tail-forward to the matching CLEAR and answer ITS result. The kind on
// the ring proves the forward; a full ring proves it forwards rather than forcing success.
static void test_delegation(mi_heap_t *heap)
{
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, heap, 16, 2), "delegation: bridge init");
    RenderCommand c;
    UiFixture uf; ui_fixture_init(&uf); uf.b.primCount = 0u;
    AnoGlyphInstance uglyph[UI_GLYPHS_N];
    fill_glyphs(uglyph, UI_GLYPHS_N);

    AnoRenderSubmitResult r = ano_render_text_set(&b, 5u, NULL, 0u);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "text_set(count 0): ACCEPTED through clear");
    CHECK(ano_render_next_command(&b, &c) && c.kind == RCMD_TEXT_CLEAR && c.text_id == 5u
          && !c.bulk_owned, "text_set(count 0): enqueues RCMD_TEXT_CLEAR, not TEXT_SET");

    r = ano_render_ui_set(&b, 6u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "ui_set(primCount 0): ACCEPTED through clear");
    CHECK(ano_render_next_command(&b, &c) && c.kind == RCMD_UI_CLEAR && c.ui_id == 6u
          && !c.bulk_owned, "ui_set(primCount 0): enqueues RCMD_UI_CLEAR, not UI_SET");
    CHECK(cmd_depth(&b) == 0u, "delegation: ring drained");
    ano_render_bridge_destroy(&b);

    // The forward carries the clear's refusal back out; it never launders a full ring into success.
    AnoRenderBridge full;
    CHECK(ano_render_bridge_init(&full, heap, 2, 2), "delegation: bridge init (cap 2)");
    RenderCommand fill = { .kind = RCMD_UPDATE };
    CHECK(ano_render_submit(&full, &fill) && ano_render_submit(&full, &fill), "delegation: ring filled");
    r = ano_render_text_set(&full, 5u, NULL, 0u);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE,
          "text_set(count 0) on a full ring: BACKPRESSURE, not ACCEPTED");
    r = ano_render_ui_set(&full, 6u, 0u, &uf.b, uglyph, UI_GLYPHS_N);
    CHECK(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE,
          "ui_set(primCount 0) on a full ring: BACKPRESSURE, not ACCEPTED");
    CHECK(cmd_depth(&full) == 2u, "delegation: full ring unchanged");
    ano_render_bridge_destroy(&full);
}

// The documented zero-count no-op is ACCEPTED with NOTHING enqueued 〜 both halves.
static void test_bulk_zero(mi_heap_t *heap)
{
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, heap, 16, 2), "bulk zero: bridge init");
    RenderUpdateBatch empty = { .count = 0u, .fields = RFIELD_TRANSFORM }; // every array absent
    AnoRenderSubmitResult r = ano_render_submit_bulk_update(&b, &empty);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "bulk_update(count 0): ACCEPTED");
    CHECK(cmd_depth(&b) == 0u, "bulk_update(count 0): nothing enqueued");
    r = ano_render_submit_bulk_destroy(&b, NULL, 0u);
    CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "bulk_destroy(count 0): ACCEPTED");
    CHECK(cmd_depth(&b) == 0u, "bulk_destroy(count 0): nothing enqueued");
    ano_render_bridge_destroy(&b);
}

// mat4 is aligned(16) and both payload structs embed an alignas(16) Vector4, but they are packed
// behind a 4-aligned id array whose length moves with the row count 〜 so the packer must round the
// offset up, and the residue that exposes a missing round-up depends on the count. Sweeping 1..8
// covers every residue class: three counts in four misalign if the pad is dropped.
static void test_bulk_alignment(mi_heap_t *heap)
{
    enum { MAXROWS = 8u };
    uint32_t            ids[MAXROWS] = {0};
    mat4                xforms[MAXROWS] = {0};
    AnoMotionDescriptor motion[MAXROWS] = {0};
    AnoInstanceData     inst[MAXROWS] = {0};

    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, heap, 32, 2), "bulk align: bridge init");
    for (uint32_t n = 1u; n <= MAXROWS; n++) {
        RenderUpdateBatch batch = {
            .count = n, .fields = RFIELD_TRANSFORM | RFIELD_ANIM | RFIELD_USERDATA,
            .render_ids = ids, .transforms = (const mat4 *)xforms,
            .motion = motion, .instance_data = inst };
        AnoRenderSubmitResult r = ano_render_submit_bulk_update(&b, &batch);
        CHECK(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "bulk align: ACCEPTED");
        RenderCommand c;
        if (!ano_spsc_pop(&b.commands, &c)) { CHECK(false, "bulk align: command popped"); continue; }
        const RenderUpdateBatch *u = c.update;
        // The block base is mi_malloc's, so an aligned offset is an aligned address. Forming these
        // pointers at a misaligned offset is undefined, and the render side reads them through the
        // 16-aligned type, which entitles the compiler to aligned loads.
        CHECK(((uintptr_t)u->transforms    % _Alignof(mat4))                == 0u
              && ((uintptr_t)u->motion        % _Alignof(AnoMotionDescriptor)) == 0u
              && ((uintptr_t)u->instance_data % _Alignof(AnoInstanceData))      == 0u,
              "bulk align: over-aligned sub-arrays sit on their own alignment");
        ano_render_command_release(&c);
    }
    ano_render_bridge_destroy(&b);
}

// ano_size_add_array is the packers' only guard against an unrepresentable packed size, and the only
// place that guard is reachable: both of its arms sit far above what a uint32_t count can ask for.
static void test_size_add_array(void)
{
    const size_t huge = SIZE_MAX / 4u; // SIZE_MAX / huge == 4
    size_t bytes;

    bytes = 0u;
    CHECK(ano_size_add_array(&bytes, 4u, huge) && bytes == 4u * huge,
          "size_add: count == SIZE_MAX/stride is representable");
    bytes = 0u;
    CHECK(!ano_size_add_array(&bytes, 5u, huge) && bytes == 0u,
          "size_add: one past SIZE_MAX/stride refuses, *bytes untouched");

    bytes = SIZE_MAX - 8u;
    CHECK(ano_size_add_array(&bytes, 8u, 1u) && bytes == SIZE_MAX,
          "size_add: a running sum landing exactly on SIZE_MAX is representable");
    bytes = SIZE_MAX - 8u;
    CHECK(!ano_size_add_array(&bytes, 9u, 1u) && bytes == SIZE_MAX - 8u,
          "size_add: one past SIZE_MAX - *bytes refuses, *bytes untouched");

    bytes = 16u;
    CHECK(ano_size_add_array(&bytes, 4u, 8u) && bytes == 48u, "size_add: advances by count*stride");
}


/* Shutdown During Backpressure */

// A producer parked on a full ring must observe the stop flag BETWEEN attempts, or a window close
// during startup wedges main's join forever. This runs hud_text_spin's loop shape against a full
// ring and asks main to end it; the ctest TIMEOUT is the watchdog.

typedef struct
{
    AnoRenderBridge          *b;
    _Atomic bool              stop;
    AnoGlyphInstance          inst[2];
    uint32_t                  attempts;   // thread-owned; read after the join
    AnoRenderSubmitResultCode code;
} SpinCtx;

static void *spin_fn(void *arg)
{
    SpinCtx *ctx = arg;
    for (;;) {
        AnoRenderSubmitResult r = ano_render_text_set(ctx->b, 1u, ctx->inst, 2u);
        ctx->attempts++;
        switch (r.code) {
        case ANO_RENDER_SUBMIT_ACCEPTED:
        case ANO_RENDER_SUBMIT_OOM:
        case ANO_RENDER_SUBMIT_INVALID:
            ctx->code = r.code;
            return NULL; // the block landed, or it never can
        case ANO_RENDER_SUBMIT_BACKPRESSURE:
            if (atomic_load(&ctx->stop)) { ctx->code = r.code; return NULL; }
            ano_sleep(1000);
            break;
        }
    }
}

static void test_shutdown_during_backpressure(mi_heap_t *heap)
{
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, heap, 2, 2), "shutdown: bridge init (cap 2)");
    RenderCommand fill = { .kind = RCMD_UPDATE };
    CHECK(ano_render_submit(&b, &fill) && ano_render_submit(&b, &fill), "shutdown: ring filled");

    SpinCtx ctx = { .b = &b };
    fill_glyphs(ctx.inst, 2u);
    anothread_t t;
    CHECK(ano_thread_create(&t, NULL, spin_fn, &ctx) == 0, "shutdown: spawn the parked producer");
    ano_sleep(10000); // ~10 ms of refused attempts
    atomic_store(&ctx.stop, true);
    ano_thread_join(t, NULL);

    CHECK(ctx.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "shutdown: the wait ends on BACKPRESSURE");
    CHECK(ctx.attempts >= 2u, "shutdown: the producer retried before the flag was set");
    CHECK(cmd_depth(&b) == 2u, "shutdown: nothing landed while the ring was full");
    ano_render_bridge_destroy(&b);
}

int main(void)
{
    mi_heap_t *heap = mi_heap_new();
    CHECK(heap != NULL, "heap creation");

    test_single_threaded(heap);

    // ui_set warns once per drop. This suite never brings the logger up, so route WARN through the
    // NOW path, which is a plain stderr write before ano_log_init 〜 nothing touches the dead ring.
    ano_log_set_route(ANO_WARN, ANO_TERM | ANO_NOW);

    test_accepted(heap);
    test_backpressure(heap);
    test_invalid(heap);
    test_delegation(heap);
    test_bulk_zero(heap);
    test_bulk_alignment(heap);
    test_size_add_array();
    test_shutdown_during_backpressure(heap);

    // Cap 16: frequent full/empty + wrap over ITEMS (TSan-interesting).
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
        if (e.u.render_id != next) evt_order_err++;
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
