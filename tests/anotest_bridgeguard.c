/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the render-owned payload lifetime across bridge teardown. The submit helpers
// copy each text/UI/bulk batch into one mi_malloc block whose ownership rides the command
// ring (anoptic_render.h: "released render-side"; RenderCommand.bulk_owned), and every
// live path honors it: a full-ring reject frees the copy at submit, a popped command's
// block is freed by the consumer after adoption. But ano_render_bridge_destroy tears the
// commands ring down through ano_spsc_destroy, which frees only ring->buffer 〜 commands
// still enqueued are discarded with their owned blocks unreachable (docs/BUGS.md,
// Render / Vulkan backend / Interlink, render_bridge/ano_render_bridge.c:92). In-tree,
// main.c stops draining before it stops the producer, so the final ticks' TEXT_SET/UI_SET
// blocks sit exactly in that window every run. Leak observed precisely: the blocks land
// in a dedicated mimalloc heap installed as the thread default around each submit, and
// mi_heap_visit_blocks counts what survives destroy. Controls pin the two live paths so
// a fix cannot pass by rejecting submissions. Headless, no device, deterministic.
// Fails until bridge destroy (or a teardown drain above it) releases the enqueued blocks.
// Exit 0 == pass.

#include <stdio.h>
#include <string.h>
#include <mimalloc.h>
#include "render_bridge/render_bridge.h" // private transport: SPSC ring + bridge + endpoints

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// mi_heap_visit_blocks visitor: count live allocated blocks.
static bool count_block(const mi_heap_t *heap, const mi_heap_area_t *area,
                        void *block, size_t block_size, void *arg)
{
    (void)heap; (void)area; (void)block_size;
    if (block != NULL)
        (*(size_t *)arg)++;
    return true;
}

// Live allocated blocks in `heap`.
static size_t live_blocks(mi_heap_t *heap)
{
    size_t n = 0;
    mi_heap_visit_blocks(heap, true, count_block, &n);
    return n;
}

// One recognizable shaped glyph.
static AnoGlyphInstance glyph_of(uint32_t i)
{
    AnoGlyphInstance g = { 0 };
    g.origin[0] = (float)i * 8.0f;
    g.origin[1] = 42.0f;
    g.glyphID   = 0xB00u + i;
    return g;
}

int main(void)
{
    mi_heap_t *ringHeap = mi_heap_new();
    mi_heap_t *watch    = mi_heap_new(); // every render-owned payload block lands here
    CHECK(ringHeap != NULL && watch != NULL, "heap creation");
    CHECK(live_blocks(watch) == 0, "watch heap starts empty");

    AnoRenderBridge bridge; // stack: _Alignas on the ring propagates
    CHECK(ano_render_bridge_init(&bridge, ringHeap, 8, 8), "bridge init (cap 8)");

    AnoGlyphInstance glyphs[4];
    for (uint32_t i = 0; i < 4; i++)
        glyphs[i] = glyph_of(i);

    // control: backpressure reject frees the copy at submit (the pre-push drop path).
    {
        RenderCommand fill = { .kind = RCMD_TEXT_CLEAR, .text_id = 999 };
        for (uint32_t i = 0; i < 8; i++)
            CHECK(ano_render_submit(&bridge, &fill), "fill push");
        CHECK(!ano_render_submit(&bridge, &fill), "ring full at capacity");

        mi_heap_t *old = mi_heap_set_default(watch);
        bool r = ano_render_text_set(&bridge, 7, glyphs, 4);
        mi_heap_set_default(old);
        CHECK(!r, "text_set reports backpressure on a full ring");
        CHECK(live_blocks(watch) == 0, "rejected submit freed its block copy");

        RenderCommand out;
        for (uint32_t i = 0; i < 8; i++)
            CHECK(ano_render_next_command(&bridge, &out), "drain fill");
        CHECK(!ano_render_next_command(&bridge, &out), "ring empty after drain");
    }

    // control: ownership rides the ring; the consumer that pops the command frees the block.
    {
        mi_heap_t *old = mi_heap_set_default(watch);
        bool r = ano_render_text_set(&bridge, 11, glyphs, 4);
        mi_heap_set_default(old);
        CHECK(r, "text_set enqueues");
        CHECK(live_blocks(watch) == 1, "one render-owned block in flight");

        RenderCommand out = { 0 };
        CHECK(ano_render_next_command(&bridge, &out), "pop TEXT_SET");
        CHECK(out.kind == RCMD_TEXT_SET && out.text_id == 11 && out.bulk_owned,
              "popped command carries the owned block");
        CHECK(out.text != NULL && out.text->count == 4
                  && memcmp(out.text->instances, glyphs, sizeof glyphs) == 0,
              "block packs the shaped instances intact");
        mi_free((void *)out.text); // consumer's adoption contract
        CHECK(live_blocks(watch) == 0, "consumed block released");
    }

    // trigger: commands still enqueued at destroy must not leak their owned blocks.
    // Four TEXT_SET blocks through the module's own endpoint, plus one hand-built
    // BULK_DESTROY block shaped exactly as ano_render_submit_bulk_destroy packs it.
    {
        mi_heap_t *old = mi_heap_set_default(watch);
        for (uint32_t id = 0; id < 4; id++)
            CHECK(ano_render_text_set(&bridge, id, glyphs, 4), "enqueue TEXT_SET");
        mi_heap_set_default(old);

        size_t bytes = sizeof(RenderDestroyBatch) + 3u * sizeof(uint32_t);
        char *blk = mi_heap_malloc(watch, bytes);
        CHECK(blk != NULL, "bulk block alloc");
        RenderDestroyBatch *d = (RenderDestroyBatch *)blk;
        uint32_t *ids = (uint32_t *)(blk + sizeof(RenderDestroyBatch));
        ids[0] = 1u; ids[1] = 2u; ids[2] = 3u;
        d->count = 3u;
        d->render_ids = ids;
        RenderCommand cmd = { .kind = RCMD_BULK_DESTROY, .destroy = d, .bulk_owned = true };
        CHECK(ano_render_submit(&bridge, &cmd), "enqueue BULK_DESTROY");

        CHECK(live_blocks(watch) == 5, "five render-owned blocks in flight pre-destroy");

        ano_render_bridge_destroy(&bridge);

        // The blocks' ownership transferred to the ring at push; after destroy no
        // reference exists anywhere else, so anything still live here is leaked.
        size_t leaked = live_blocks(watch);
        if (leaked != 0)
            printf("  destroy dropped %zu enqueued command(s) without releasing their blocks\n",
                   leaked);
        CHECK(leaked == 0, "bridge destroy releases render-owned payloads still enqueued");
    }

    mi_heap_destroy(watch); // reclaims any leaked blocks so sanitizers stay quiet
    mi_heap_destroy(ringHeap);

    if (failures == 0) { printf("anotest_bridgeguard: all checks passed\n"); return 0; }
    printf("anotest_bridgeguard: %d check(s) failed\n", failures);
    return 1;
}
