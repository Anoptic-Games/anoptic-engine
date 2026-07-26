/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the CUSTODY half of AnoRenderSubmitResult. The owned-payload endpoints in
// ano_render_bridge.c allocate a render-owned block BEFORE they push, so every answer is also a
// custody claim: OOM means the allocator refused and nothing was packed, BACKPRESSURE means a block
// WAS packed and ano_render_command_release retired it again, INVALID means the arm answered before
// the allocator was ever asked, and ACCEPTED means exactly one block crossed into the bridge 〜 which
// the bridge then discharges, at drain or at teardown. None of that is observable through a real
// allocator, so this guard drives the endpoints over a ledgering arena that refuses on command. The
// distinction the typed result exists for is pinned first: an allocator refusal must never be spelled
// BACKPRESSURE, because a retry loop cannot tell those apart and spins forever on one.
//
// Harness: compiles the REAL ano_render_bridge.c TU and links NOTHING 〜 anoptic_core drags
// mimalloc-static in PUBLIC, which would make a locally defined mi_malloc a duplicate symbol (and an
// ODR hazard under Release LTO besides). This TU therefore DEFINES the four seams the production TU
// leaves open: mi_malloc, mi_free, mi_heap_calloc and ano_log_write. ano_spsc_*, ano_seqpub_* and
// ano_size_add_array are static inline in the private header and close on their own.
//
// HAZARD, read this before touching the allocator: anoptic_memory.h pulls <mimalloc-override.h>,
// which #defines malloc -> mi_malloc and free -> mi_free in every TU that sees it 〜 and this one
// sees it, transitively, through render_bridge.h. A fake mi_malloc written over malloc therefore
// calls ITSELF and recurses until the stack dies. The allocator below is backed by a static byte
// arena for exactly that reason. If a later edit really does want libc backing, #undef malloc and
// #undef free first.
//
// No GPU, no threads, no logger. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "render_bridge/render_bridge.h" // private transport: SPSC ring + bridge + the endpoints
#include <anoptic_log.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)
// Same, for a claim that must name the endpoint or arm it is about.
#define CHECKF(cond, ...) do { \
    if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf(" (%s:%d)\n", __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledgering Arena */

// Bump-only over a static arena: an address is never reused, so no two ledger entries can ever be
// confused and a stale pointer is always recognisable as a double free.

#define ARENA_BYTES (512u * 1024u)
#define MAX_SLOTS   256u

typedef enum BlockKind
{
    BLK_PAYLOAD = 0, // mi_malloc: a packed render-owned block
    BLK_RING,        // mi_heap_calloc: an SPSC ring buffer
} BlockKind;

typedef struct Slot
{
    void     *ptr;
    size_t    size;
    BlockKind kind;
    bool      live;
} Slot;

static _Alignas(16) unsigned char g_arena[ARENA_BYTES];
static size_t   g_arenaUsed;
static Slot     g_slot[MAX_SLOTS];
static uint32_t g_slots;

static uint32_t g_payloadAllocs;   // grants through mi_malloc, whole run
static uint32_t g_payloadRefusals; // refusals through mi_malloc, whole run
static uint32_t g_doubleFrees;     // whole-run invariant
static uint32_t g_unknownFrees;    // whole-run invariant
static uint32_t g_arenaExhausted;  // a run that outgrows the arena must FAIL, never pass quietly
static uint32_t g_slotsExhausted;

static uint32_t g_failNextAlloc;   // refuse the next N payload allocations; 0 = healthy
static size_t   g_failAllocAbove;  // refuse any payload allocation larger than this; 0 = off

// in:  bytes (requested), kind (which ledger class the block belongs to)
// out: a 16-aligned arena block with a live ledger entry, or NULL when the arena or the slot table
//      is spent (both counted, both asserted zero at exit)
// inv: bump only. mi_malloc's 16-byte guarantee is reproduced here because the packers place mat4 and
//      Vector4-bearing sub-arrays inside the block they are handed.
static void *arena_take(size_t bytes, BlockKind kind)
{
    size_t need = (bytes + 15u) & ~(size_t)15u;
    if (need == 0u) need = 16u;
    if (g_slots >= MAX_SLOTS) { g_slotsExhausted++; return NULL; }
    if (need > ARENA_BYTES - g_arenaUsed) { g_arenaExhausted++; return NULL; }
    void *p = g_arena + g_arenaUsed;
    g_arenaUsed += need;
    g_slot[g_slots++] = (Slot){ .ptr = p, .size = bytes, .kind = kind, .live = true };
    return p;
}

// in:  kind
// out: how many blocks of that class the ledger still holds live
static uint32_t live_of(BlockKind kind)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_slots; i++)
        if (g_slot[i].live && g_slot[i].kind == kind) n++;
    return n;
}

static uint32_t live_blocks(void) { return live_of(BLK_PAYLOAD); }
static uint32_t live_rings(void)  { return live_of(BLK_RING); }

// in:  size
// out: an arena block, or NULL when the refusal switch is armed
// inv: NEVER back this with malloc 〜 malloc IS mi_malloc in this TU (see the banner).
void *mi_malloc(size_t size)
{
    if (g_failNextAlloc > 0u) { g_failNextAlloc--; g_payloadRefusals++; return NULL; }
    if (g_failAllocAbove > 0u && size > g_failAllocAbove) { g_payloadRefusals++; return NULL; }
    void *p = arena_take(size, BLK_PAYLOAD);
    if (p) g_payloadAllocs++;
    return p;
}

// in:  heap (ignored 〜 the bridge only needs a non-NULL token), count, size
// out: a zeroed arena block, or NULL on overflow / exhaustion
// inv: the ring lane deliberately ignores the refusal switch, so arming an OOM for a packing endpoint
//      can never fail a bridge init by accident.
void *mi_heap_calloc(mi_heap_t *heap, size_t count, size_t size)
{
    (void)heap;
    if (size != 0u && count > SIZE_MAX / size) return NULL;
    void *p = arena_take(count * size, BLK_RING);
    if (p) memset(p, 0, count * size);
    return p;
}

// in:  p (NULL, an arena block, or something nobody minted)
// out: nothing; discharges the ledger entry and counts the two ways a free can be wrong
void mi_free(void *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < g_slots; i++) {
        if (g_slot[i].ptr != p) continue;
        if (!g_slot[i].live) g_doubleFrees++;
        g_slot[i].live = false;
        return;
    }
    g_unknownFrees++;
}


/* Logger Seam */

#define LOG_LEVELS ((size_t)ANO_FATAL + 1u)
static uint32_t g_logs[LOG_LEVELS];

// in:  level, route, sourceFile, lineNumber, printFormat, ... 〜 ano_log's non-macro half
// out: 0; records nothing but the count, per level, so "the bridge may warn" is assertable and a
//      warning storm in a per-submit path is visible as a count that outruns the calls
int ano_log_write(ano_loglevel_t level, ano_logroute_t route,
                  const char *sourceFile, int lineNumber,
                  const char *printFormat, ...)
{
    (void)route; (void)sourceFile; (void)lineNumber; (void)printFormat;
    if ((size_t)level < LOG_LEVELS) g_logs[level]++;
    return 0;
}

static uint32_t warns(void) { return g_logs[ANO_WARN]; }


/* Bridge Probes */

// in:  r (a ring nobody else is touching)
// out: how many elements it holds. Single-threaded here, so the relaxed pair is exact.
static uint32_t ring_depth(const AnoSpscRing *r)
{
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    return tail - head;
}

static int        g_heapToken;                                  // the bridge only checks heap != NULL
static mi_heap_t *const g_heap = (mi_heap_t *)&g_heapToken;


/* Fixtures */

#define TEXT_GLYPHS 5u
#define BULK_COUNT  4u
#define UI_PRIMS    3u
#define UI_CURVES   4u

static AnoGlyphInstance g_glyphs[TEXT_GLYPHS];

static uint32_t            g_ids[BULK_COUNT];
static mat4                g_xforms[BULK_COUNT];
static AnoMotionDescriptor g_motion[BULK_COUNT];
static uint32_t            g_mesh[BULK_COUNT];
static uint32_t            g_material[BULK_COUNT];
static AnoInstanceData     g_instdata[BULK_COUNT];
static RenderUpdateBatch   g_batch;

static AnoUiPrim  g_prims[UI_PRIMS];
static AnoUiClip  g_clips[1];
static AnoUiPaint g_paints[1];
static AnoUiStop  g_stops[2];
static uint32_t   g_curves[UI_CURVES];
static AnoUiBuilder g_ui;      // valid: RRECT with both refs, GLYPHS over the whole array, PATH
static AnoUiBuilder g_uiEmpty; // primCount 0: tail-forwards to clear

// out: nothing; every fixture holds a distinct recognisable value so a packer that copies the wrong
//      array, the wrong count, or nothing at all cannot pass the control's memcmp.
// inv: the UI builder is hand-built. The builder verbs live in src/ui, which this target does not
//      link, and the packer reads only counts plus array pointers anyway.
static void fixtures_init(void)
{
    for (uint32_t i = 0; i < TEXT_GLYPHS; i++) {
        g_glyphs[i].inv[0] = 1.0f + (float)i;
        g_glyphs[i].color[3] = 0.5f + (float)i;
        g_glyphs[i].origin[0] = (float)(10u + i);
        g_glyphs[i].origin[1] = (float)(20u + i);
        g_glyphs[i].glyphID = 300u + i;
        g_glyphs[i].flags = i;
    }

    for (uint32_t i = 0; i < BULK_COUNT; i++) {
        g_ids[i] = 1000u + i;
        for (uint32_t r = 0; r < 4u; r++)
            for (uint32_t c = 0; c < 4u; c++)
                g_xforms[i][r][c] = (float)(i * 100u + r * 10u + c);
        g_motion[i].type = ANO_MOTION_SPIN;
        g_motion[i].epoch = (float)i;
        g_motion[i].p0.v[0] = (float)i + 0.25f;
        g_mesh[i] = 40u + i;
        g_material[i] = 50u + i;
        g_instdata[i].packed[0] = 0xFF00u + i;
        g_instdata[i].params.v[1] = (float)i - 3.5f;
    }
    g_batch = (RenderUpdateBatch){
        .count = BULK_COUNT,
        .fields = RFIELD_TRANSFORM | RFIELD_ANIM | RFIELD_MESH_MAT | RFIELD_USERDATA,
        .render_ids = g_ids, .transforms = g_xforms, .motion = g_motion,
        .mesh = g_mesh, .material = g_material, .instance_data = g_instdata,
    };

    g_clips[0] = (AnoUiClip){ .rect = { 1.0f, 2.0f, 3.0f, 4.0f }, .rrHalf = { -1.0f, -1.0f } };
    g_paints[0] = (AnoUiPaint){ .kind = ANO_UI_GRAD_LINEAR, .stopFirst = 0u, .stopCount = 2u };
    g_stops[0] = (AnoUiStop){ .color = { 1.0f, 0.0f, 0.0f, 1.0f }, .t = 0.0f };
    g_stops[1] = (AnoUiStop){ .color = { 0.0f, 0.0f, 1.0f, 1.0f }, .t = 1.0f };
    for (uint32_t i = 0; i < UI_CURVES; i++) g_curves[i] = 0x1234u + i; // never the SENTINEL word

    g_prims[0] = (AnoUiPrim){ .kind = ANO_UI_RRECT, .half = { 8.0f, 4.0f },
                              .color = { 1.0f, 1.0f, 1.0f, 1.0f }, .paintRef = 0u, .clipRef = 0u };
    g_prims[1] = (AnoUiPrim){ .kind = ANO_UI_GLYPHS, .paintRef = ANO_UI_REF_NONE,
                              .clipRef = ANO_UI_REF_NONE, .aux0 = 0u, .aux1 = TEXT_GLYPHS };
    g_prims[2] = (AnoUiPrim){ .kind = ANO_UI_PATH, .paintRef = ANO_UI_REF_NONE,
                              .clipRef = ANO_UI_REF_NONE, .aux0 = 0u, .aux1 = 1u };

    g_ui = (AnoUiBuilder){
        .prims = g_prims,   .primCap = UI_PRIMS,  .primCount = UI_PRIMS,
        .clips = g_clips,   .clipCap = 1u,        .clipCount = 1u,
        .paints = g_paints, .paintCap = 1u,       .paintCount = 1u,
        .stops = g_stops,   .stopCap = 2u,        .stopCount = 2u,
        .curves = g_curves, .curveCap = UI_CURVES, .curveCount = UI_CURVES,
    };
    g_uiEmpty = g_ui;
    g_uiEmpty.primCount = 0u;
}


/* Packing Endpoints */

// The four endpoints that allocate a block before they push. text_clear / ui_clear allocate nothing
// and so have no custody to claim; they serve here only as ring ballast.
typedef enum Endpoint
{
    EP_TEXT_SET = 0,
    EP_UI_SET,
    EP_BULK_UPDATE,
    EP_BULK_DESTROY,
    EP_COUNT
} Endpoint;

static const char *const kEndpoint[EP_COUNT] = { "text_set", "ui_set", "bulk_update", "bulk_destroy" };

// in:  bridge, ep
// out: that endpoint's result over the shared valid fixtures
// inv: every arm here is contract-valid input, so the only thing that can move the answer off
//      ACCEPTED is the allocator or the ring.
static AnoRenderSubmitResult drive(AnoRenderBridge *bridge, Endpoint ep)
{
    switch (ep) {
    case EP_TEXT_SET:     return ano_render_text_set(bridge, 11u, g_glyphs, TEXT_GLYPHS);
    case EP_UI_SET:       return ano_render_ui_set(bridge, 12u, 3u, &g_ui, g_glyphs, TEXT_GLYPHS);
    case EP_BULK_UPDATE:  return ano_render_submit_bulk_update(bridge, &g_batch);
    case EP_BULK_DESTROY: return ano_render_submit_bulk_destroy(bridge, g_ids, BULK_COUNT);
    case EP_COUNT:        break;
    }
    return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_INVALID);
}


/* Case 1 〜 OOM */

// in:  nothing; builds and tears down its own bridge
// out: nothing; every failure lands in `failures`
// inv: the four packing endpoints ask the allocator before they touch the ring, so a refusal must
//      answer OOM with the ledger and the ring exactly where they were. OOM spelled BACKPRESSURE is
//      the defect this case exists to catch: the two codes drive opposite caller policies.
static void case_oom(void)
{
    printf("case 1: the allocator refuses under each packing endpoint\n");
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, g_heap, 8u, 4u), "oom: bridge init");

    for (Endpoint ep = 0; ep < EP_COUNT; ep++) {
        const uint32_t depth = ring_depth(&b.commands);
        const uint32_t allocs = g_payloadAllocs, live = live_blocks(), warn = warns();
        g_failNextAlloc = 1u;

        AnoRenderSubmitResult r = drive(&b, ep);

        CHECKF(r.code == ANO_RENDER_SUBMIT_OOM, "oom: %s answers OOM (got %d)", kEndpoint[ep], (int)r.code);
        CHECKF(r.code != ANO_RENDER_SUBMIT_BACKPRESSURE,
               "oom: %s must NOT spell an allocator refusal as BACKPRESSURE", kEndpoint[ep]);
        CHECKF(g_failNextAlloc == 0u, "oom: %s consumed the armed refusal", kEndpoint[ep]);
        CHECKF(g_payloadAllocs == allocs, "oom: %s granted no block", kEndpoint[ep]);
        CHECKF(live_blocks() == live, "oom: %s left the ledger untouched", kEndpoint[ep]);
        CHECKF(ring_depth(&b.commands) == depth, "oom: %s enqueued nothing", kEndpoint[ep]);
        CHECKF(warns() - warn <= 1u, "oom: %s does not storm the log", kEndpoint[ep]);
    }

    ano_render_bridge_destroy(&b);
    CHECK(live_blocks() == 0u, "oom: no payload block outlives the bridge");
    CHECK(live_rings() == 0u, "oom: both ring buffers released");
}


/* Case 2 〜 Release After Ring Refusal */

// inv: the ring refuses AFTER the endpoint has packed, so BACKPRESSURE is honest only if
//      ano_render_command_release has already retired that block: exactly one grant, and a ledger
//      back at zero. A refused push that skipped the release would leak one block per retry 〜 which
//      is the shape a retry loop repeats forever.
static void case_backpressure_release(void)
{
    printf("case 2: the ring refuses after the block is already packed\n");
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, g_heap, 2u, 2u), "backpressure: bridge init");

    // Ballast: clear commands own no payload, so only the endpoint under test can move the ledger.
    AnoRenderSubmitResult fill;
    fill = ano_render_text_clear(&b, 7u);
    CHECK(fill.code == ANO_RENDER_SUBMIT_ACCEPTED, "backpressure: ring ballast 1 accepted");
    fill = ano_render_ui_clear(&b, 8u);
    CHECK(fill.code == ANO_RENDER_SUBMIT_ACCEPTED, "backpressure: ring ballast 2 accepted");
    fill = ano_render_text_clear(&b, 9u);
    CHECK(fill.code == ANO_RENDER_SUBMIT_BACKPRESSURE, "backpressure: the ring is now full");
    CHECK(live_blocks() == 0u, "backpressure: the ballast allocated nothing");

    const uint32_t full = ring_depth(&b.commands);
    for (Endpoint ep = 0; ep < EP_COUNT; ep++) {
        const uint32_t allocs = g_payloadAllocs, warn = warns();

        AnoRenderSubmitResult r = drive(&b, ep);

        CHECKF(r.code == ANO_RENDER_SUBMIT_BACKPRESSURE,
               "backpressure: %s answers BACKPRESSURE (got %d)", kEndpoint[ep], (int)r.code);
        CHECKF(g_payloadAllocs == allocs + 1u,
               "backpressure: %s really packed a block before the push was refused", kEndpoint[ep]);
        CHECKF(live_blocks() == 0u,
               "backpressure: %s released the block the refused push left it holding", kEndpoint[ep]);
        CHECKF(ring_depth(&b.commands) == full, "backpressure: %s enqueued nothing", kEndpoint[ep]);
        CHECKF(warns() == warn, "backpressure: %s is silent (ordinary backpressure carries no warning)",
               kEndpoint[ep]);
    }

    ano_render_bridge_destroy(&b);
    CHECK(live_blocks() == 0u, "backpressure: nothing outlives the bridge");
    CHECK(live_rings() == 0u, "backpressure: both ring buffers released");
}


/* Case 3 〜 Teardown With Owned Blocks Queued */

// inv: ACCEPTED transfers ownership, so a bridge torn down with undelivered commands must discharge
//      their payloads itself. The drain in ano_render_bridge_destroy is the only thing standing
//      between a shutdown and one leaked block per unread owned command.
static void case_destroy_discharges(void)
{
    printf("case 3: teardown with four accepted, undrained, owned blocks\n");
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, g_heap, 8u, 4u), "teardown: bridge init");

    for (Endpoint ep = 0; ep < EP_COUNT; ep++) {
        AnoRenderSubmitResult r = drive(&b, ep);
        CHECKF(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "teardown: %s accepted", kEndpoint[ep]);
    }
    CHECK(live_blocks() == (uint32_t)EP_COUNT, "teardown: four owned blocks are live");
    CHECK(ring_depth(&b.commands) == (uint32_t)EP_COUNT, "teardown: four commands wait undrained");

    ano_render_bridge_destroy(&b);

    CHECK(live_blocks() == 0u, "teardown: destroy discharges every undelivered payload");
    CHECK(live_rings() == 0u, "teardown: both ring buffers released");
}


/* Case 4 〜 INVALID Allocates Nothing */

typedef enum InvalidArm
{
    INV_TEXT_NULL = 0,     // count > 0 with a NULL instances pointer
    INV_UI_NULL_BUILDER,   // a NULL builder is INVALID, never a silent clear
    INV_UI_CAP,            // primCount past the per-block cap
    INV_UI_GLYPH_PAIR,     // glyphCount > 0 with NULL glyphs
    INV_UI_PAINT_REF,      // paintRef past the block's paint table
    INV_UI_STOP_WINDOW,    // a paint whose stop window leaves the block
    INV_UI_PATH_WALK,      // a PATH whose curve walk would read past the stream
    INV_UPDATE_NULL_BATCH, // a NULL batch cannot even be probed for a count
    INV_UPDATE_NULL_IDS,   // nonzero count with no id array
    INV_UPDATE_NO_ARRAY,   // a field the mask names whose array is absent
    INV_DESTROY_NULL_IDS,
    INV_COUNT
} InvalidArm;

// warns: the exact warning count the arm owes, or -1 where the contract names no warning and only
// the "no storm" bound applies. The UI drops the public header documents as warned are the ones
// pinned at 1: caps, the glyph pair, out-of-range refs, and the path walk.
static const struct { const char *name; int warns; } kInvalid[INV_COUNT] = {
    { "text_set(count>0, NULL instances)", -1 },
    { "ui_set(NULL builder)",              -1 },
    { "ui_set(primCount past cap)",         1 },
    { "ui_set(glyphCount>0, NULL glyphs)",  1 },
    { "ui_set(paintRef out of range)",      1 },
    { "ui_set(paint stop window escapes)",  1 },
    { "ui_set(PATH walk past the stream)",  1 },
    { "bulk_update(NULL batch)",           -1 },
    { "bulk_update(NULL render_ids)",      -1 },
    { "bulk_update(field named, array absent)", -1 },
    { "bulk_destroy(NULL render_ids)",     -1 },
};

// in:  bridge, arm
// out: that arm's result
// inv: each arm mutates a LOCAL copy of a fixture, so the shared valid fixtures stay valid for the
//      control. The cap arm leaves primCount pointing past its own array on purpose: the cap is
//      refused before any prim is read, which is precisely the ordering it pins.
static AnoRenderSubmitResult drive_invalid(AnoRenderBridge *bridge, InvalidArm arm)
{
    AnoUiBuilder ui = g_ui;
    AnoUiPrim prims[UI_PRIMS];
    AnoUiPaint paints[1];
    RenderUpdateBatch batch = g_batch;

    memcpy(prims, g_prims, sizeof prims);
    memcpy(paints, g_paints, sizeof paints);
    ui.prims = prims;
    ui.paints = paints;

    switch (arm) {
    case INV_TEXT_NULL:
        return ano_render_text_set(bridge, 21u, NULL, TEXT_GLYPHS);
    case INV_UI_NULL_BUILDER:
        return ano_render_ui_set(bridge, 22u, 0u, NULL, g_glyphs, TEXT_GLYPHS);
    case INV_UI_CAP:
        ui.primCount = ANO_RENDER_UI_MAX_PRIMS + 1u;
        return ano_render_ui_set(bridge, 23u, 0u, &ui, g_glyphs, TEXT_GLYPHS);
    case INV_UI_GLYPH_PAIR:
        return ano_render_ui_set(bridge, 24u, 0u, &ui, NULL, TEXT_GLYPHS);
    case INV_UI_PAINT_REF:
        prims[0].paintRef = ui.paintCount + 1u;
        return ano_render_ui_set(bridge, 25u, 0u, &ui, g_glyphs, TEXT_GLYPHS);
    case INV_UI_STOP_WINDOW:
        paints[0].stopCount = ui.stopCount + 1u;
        return ano_render_ui_set(bridge, 26u, 0u, &ui, g_glyphs, TEXT_GLYPHS);
    case INV_UI_PATH_WALK:
        prims[2].aux1 = UI_CURVES; // more quads than the stream can hold words for
        return ano_render_ui_set(bridge, 27u, 0u, &ui, g_glyphs, TEXT_GLYPHS);
    case INV_UPDATE_NULL_BATCH:
        return ano_render_submit_bulk_update(bridge, NULL);
    case INV_UPDATE_NULL_IDS:
        batch.render_ids = NULL;
        return ano_render_submit_bulk_update(bridge, &batch);
    case INV_UPDATE_NO_ARRAY:
        batch.transforms = NULL; // RFIELD_TRANSFORM is in the mask
        return ano_render_submit_bulk_update(bridge, &batch);
    case INV_DESTROY_NULL_IDS:
        return ano_render_submit_bulk_destroy(bridge, NULL, BULK_COUNT);
    case INV_COUNT:
        break;
    }
    return ANO_RESULT(AnoRenderSubmitResult, ANO_RENDER_SUBMIT_ACCEPTED); // unreachable: fails the arm
}

// inv: INVALID is deterministic, so the caller retires rather than retries 〜 which is only safe if
//      the arm answered before it ever asked the allocator. Nothing allocated, nothing enqueued,
//      and a warning at most once per drop.
static void case_invalid(void)
{
    printf("case 4: every INVALID arm answers before the allocator is asked\n");
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, g_heap, 8u, 4u), "invalid: bridge init");

    for (InvalidArm arm = 0; arm < INV_COUNT; arm++) {
        const uint32_t depth = ring_depth(&b.commands);
        const uint32_t allocs = g_payloadAllocs, refusals = g_payloadRefusals;
        const uint32_t live = live_blocks(), warn = warns();

        AnoRenderSubmitResult r = drive_invalid(&b, arm);

        CHECKF(r.code == ANO_RENDER_SUBMIT_INVALID, "invalid: %s answers INVALID (got %d)",
               kInvalid[arm].name, (int)r.code);
        CHECKF(g_payloadAllocs == allocs && g_payloadRefusals == refusals,
               "invalid: %s never reached the allocator", kInvalid[arm].name);
        CHECKF(live_blocks() == live, "invalid: %s left the ledger untouched", kInvalid[arm].name);
        CHECKF(ring_depth(&b.commands) == depth, "invalid: %s left the ring untouched", kInvalid[arm].name);
        if (kInvalid[arm].warns >= 0)
            CHECKF(warns() - warn == (uint32_t)kInvalid[arm].warns,
                   "invalid: %s warns exactly %d time(s) (got %" PRIu32 ")",
                   kInvalid[arm].name, kInvalid[arm].warns, warns() - warn);
        else
            CHECKF(warns() - warn <= 1u, "invalid: %s does not storm the log", kInvalid[arm].name);
    }

    ano_render_bridge_destroy(&b);
    CHECK(live_blocks() == 0u, "invalid: nothing outlives the bridge");
    CHECK(live_rings() == 0u, "invalid: both ring buffers released");
}


/* Case 5 〜 Controls */

// in:  cmd (a drained command), ep (which endpoint produced it)
// out: nothing; asserts the packed block carries the fixture verbatim
// inv: every read is byte-wise. The packed sub-arrays land at whatever offset the running byte total
//      reached, so a typed walk over them is the packer's business, not this guard's.
static void check_packed(const RenderCommand *cmd, Endpoint ep)
{
    switch (ep) {
    case EP_TEXT_SET:
        CHECK(cmd->kind == RCMD_TEXT_SET && cmd->text_id == 11u, "control: text_set kind and id survive");
        CHECK(cmd->bulk_owned, "control: text_set marks the block render-owned");
        CHECK(cmd->text != NULL && cmd->text->count == TEXT_GLYPHS, "control: text_set count survives");
        CHECK(cmd->text != NULL && cmd->text->instances != NULL
                  && memcmp(cmd->text->instances, g_glyphs, sizeof g_glyphs) == 0,
              "control: text_set copied every glyph verbatim");
        break;
    case EP_UI_SET:
        CHECK(cmd->kind == RCMD_UI_SET && cmd->ui_id == 12u, "control: ui_set kind and id survive");
        CHECK(cmd->bulk_owned, "control: ui_set marks the block render-owned");
        CHECK(cmd->ui != NULL && cmd->ui->layer == 3u && cmd->ui->surface == ANO_UI_SURFACE_OVERLAY,
              "control: ui_set layer and surface survive");
        CHECK(cmd->ui != NULL && cmd->ui->primCount == UI_PRIMS && cmd->ui->clipCount == 1u
                  && cmd->ui->paintCount == 1u && cmd->ui->stopCount == 2u
                  && cmd->ui->curveCount == UI_CURVES && cmd->ui->glyphCount == TEXT_GLYPHS,
              "control: ui_set table counts survive");
        CHECK(cmd->ui != NULL && memcmp(cmd->ui->prims, g_prims, sizeof g_prims) == 0
                  && memcmp(cmd->ui->clips, g_clips, sizeof g_clips) == 0
                  && memcmp(cmd->ui->paints, g_paints, sizeof g_paints) == 0
                  && memcmp(cmd->ui->stops, g_stops, sizeof g_stops) == 0
                  && memcmp(cmd->ui->curves, g_curves, sizeof g_curves) == 0
                  && memcmp(cmd->ui->glyphs, g_glyphs, sizeof g_glyphs) == 0,
              "control: ui_set copied every table verbatim");
        break;
    case EP_BULK_UPDATE:
        CHECK(cmd->kind == RCMD_BULK_UPDATE && cmd->bulk_owned, "control: bulk_update kind and ownership");
        CHECK(cmd->update != NULL && cmd->update->count == BULK_COUNT
                  && cmd->update->fields == g_batch.fields,
              "control: bulk_update count and field mask survive");
        CHECK(cmd->update != NULL
                  && memcmp(cmd->update->render_ids, g_ids, sizeof g_ids) == 0
                  && memcmp(cmd->update->transforms, g_xforms, sizeof g_xforms) == 0
                  && memcmp(cmd->update->motion, g_motion, sizeof g_motion) == 0
                  && memcmp(cmd->update->mesh, g_mesh, sizeof g_mesh) == 0
                  && memcmp(cmd->update->material, g_material, sizeof g_material) == 0
                  && memcmp(cmd->update->instance_data, g_instdata, sizeof g_instdata) == 0,
              "control: bulk_update copied every named array verbatim");
        break;
    case EP_BULK_DESTROY:
        CHECK(cmd->kind == RCMD_BULK_DESTROY && cmd->bulk_owned, "control: bulk_destroy kind and ownership");
        CHECK(cmd->destroy != NULL && cmd->destroy->count == BULK_COUNT
                  && memcmp(cmd->destroy->render_ids, g_ids, sizeof g_ids) == 0,
              "control: bulk_destroy copied the id array verbatim");
        break;
    case EP_COUNT:
        break;
    }
}

// inv: without this case a reject-everything implementation passes every case above. ACCEPTED must
//      cost exactly one block per call, hand it over whole, and hold it until the consumer drains and
//      releases it. The documented no-ops must cost none.
static void case_controls(void)
{
    printf("case 5: the accepted path allocates exactly one block per call and hands it over\n");
    AnoRenderBridge b;
    CHECK(ano_render_bridge_init(&b, g_heap, 8u, 4u), "control: bridge init");

    // The documented no-ops: ACCEPTED with nothing allocated. The two `set` forms tail-forward to
    // their clear (one command, no block); the two bulk forms answer without enqueueing at all.
    uint32_t allocs = g_payloadAllocs;
    AnoRenderSubmitResult noop;
    noop = ano_render_text_set(&b, 31u, NULL, 0u);
    CHECK(noop.code == ANO_RENDER_SUBMIT_ACCEPTED, "control: text_set(count 0) forwards to clear, ACCEPTED");
    noop = ano_render_ui_set(&b, 32u, 0u, &g_uiEmpty, NULL, 0u);
    CHECK(noop.code == ANO_RENDER_SUBMIT_ACCEPTED, "control: ui_set(no prims) forwards to clear, ACCEPTED");
    CHECK(ring_depth(&b.commands) == 2u, "control: each forwarded clear enqueued one command");
    RenderUpdateBatch empty = { .count = 0u };
    noop = ano_render_submit_bulk_update(&b, &empty);
    CHECK(noop.code == ANO_RENDER_SUBMIT_ACCEPTED, "control: bulk_update(count 0) is an ACCEPTED no-op");
    noop = ano_render_submit_bulk_destroy(&b, NULL, 0u);
    CHECK(noop.code == ANO_RENDER_SUBMIT_ACCEPTED, "control: bulk_destroy(count 0) is an ACCEPTED no-op");
    CHECK(ring_depth(&b.commands) == 2u, "control: a zero-count bulk enqueues nothing");
    CHECK(g_payloadAllocs == allocs && live_blocks() == 0u, "control: no no-op allocated a block");

    const uint32_t base = ring_depth(&b.commands);
    for (Endpoint ep = 0; ep < EP_COUNT; ep++) {
        allocs = g_payloadAllocs;
        AnoRenderSubmitResult r = drive(&b, ep);
        CHECKF(r.code == ANO_RENDER_SUBMIT_ACCEPTED, "control: %s accepted (got %d)", kEndpoint[ep], (int)r.code);
        CHECKF(g_payloadAllocs == allocs + 1u, "control: %s allocates exactly one block", kEndpoint[ep]);
        CHECKF(live_blocks() == (uint32_t)ep + 1u, "control: %s handed its block over (live == queued)",
               kEndpoint[ep]);
        CHECKF(ring_depth(&b.commands) == base + (uint32_t)ep + 1u, "control: %s enqueued one command",
               kEndpoint[ep]);
    }

    // Consumer side: drain in FIFO order, verify the payload, and release what the drain adopted.
    RenderCommand cmd;
    CHECK(ano_render_next_command(&b, &cmd) && cmd.kind == RCMD_TEXT_CLEAR, "control: drain 1 is the forwarded text clear");
    ano_render_command_release(&cmd);
    CHECK(ano_render_next_command(&b, &cmd) && cmd.kind == RCMD_UI_CLEAR, "control: drain 2 is the forwarded ui clear");
    ano_render_command_release(&cmd);
    CHECK(live_blocks() == (uint32_t)EP_COUNT, "control: releasing a clear frees nothing (no payload rides it)");

    for (Endpoint ep = 0; ep < EP_COUNT; ep++) {
        CHECKF(ano_render_next_command(&b, &cmd), "control: %s command drains", kEndpoint[ep]);
        check_packed(&cmd, ep);
        ano_render_command_release(&cmd);
        CHECKF(live_blocks() == (uint32_t)(EP_COUNT - ep) - 1u,
               "control: releasing the %s command frees exactly its block", kEndpoint[ep]);
    }
    CHECK(!ano_render_next_command(&b, &cmd), "control: the ring is empty");
    CHECK(live_blocks() == 0u, "control: the drained ledger is back at zero");

    ano_render_bridge_destroy(&b);
    CHECK(live_rings() == 0u, "control: both ring buffers released");
}


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    fixtures_init();

    case_oom();
    case_backpressure_release();
    case_destroy_discharges();
    case_invalid();
    case_controls();

    // Whole-run ledger invariants. These never reset, so a case that quietly double-discharged or
    // leaked is caught here even when every claim above happened to line up.
    CHECK(live_blocks() == 0u, "whole run: no payload block outlives the suite");
    CHECK(live_rings() == 0u, "whole run: no ring buffer outlives the suite");
    CHECK(g_doubleFrees == 0u, "whole run: no block freed twice");
    CHECK(g_unknownFrees == 0u, "whole run: no unminted pointer freed");
    CHECK(g_arenaExhausted == 0u, "whole run: the arena covered every allocation asked of it");
    CHECK(g_slotsExhausted == 0u, "whole run: the slot table covered every block");
    CHECK(g_payloadAllocs > 0u && g_payloadRefusals > 0u,
          "whole run: the allocator was both exercised and armed (a vacuous run cannot pass)");

    printf("ledger: %" PRIu32 " grants, %" PRIu32 " refusals, %" PRIu32 " warns, %zu/%u arena bytes\n",
           g_payloadAllocs, g_payloadRefusals, warns(), g_arenaUsed, ARENA_BYTES);

    if (failures) {
        printf("anotest_rendersubmitguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_rendersubmitguard: all passed\n");
    return 0;
}
