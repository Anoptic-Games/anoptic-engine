/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI overlay lane plumbing (docs/ui/ui-render.md). Step 3 state: per-frame table
// buffers (prim/clip/paint/stop regions) bound as raster-set bindings 4-7 of the
// shared text/UI dispatch; the shader consumes them from step 4 on. Buffers exist
// whenever textOverlay is up so the bindings stay valid even with uiOverlay off
// (ANO_FORCE_NO_UI just pins the composed counts to 0).

#ifndef ANO_UI_RASTER_H
#define ANO_UI_RASTER_H

#include "vulkan_backend/structs.h"

// One-time init on the render thread, after ano_vk_text_init. Creates the per-frame
// table buffers. Always returns true: failure logs, clears state->uiOverlay, and the
// set writes fall back to the text frame buffer so bindings 4-7 stay valid.
bool ano_vk_ui_init(VulkanContext* ctx, RendererState* state);

// Writes raster-set bindings 4-7 for every frame slot (region ranges of the slot's
// uiFrameBuffer). Called by ano_vk_text_update_sets after its own writes; safe to
// call again after a swapchain recreate.
void ano_vk_ui_write_sets(VulkanContext* ctx, RendererState* state);

// Logic UI blocks (the v0 bridge path). block_set ADOPTS blk, replacing ui_id's
// contents. block_clear is idempotent. Both recompose the pending tables (blocks
// ascending by layer, creation order breaking ties; block-local refs rebased) and
// bump uiVersion. Render thread only.
void ano_vk_ui_block_set(RendererState* state, uint32_t ui_id, const RenderUiBlock* blk);
void ano_vk_ui_block_clear(RendererState* state, uint32_t ui_id);

// Copies pending tables into this slot's mapped buffers when stale (prims/clips/
// paints/stops into uiFrameBuffer, glyph labels into the text frame buffer's UI
// region) and publishes the slot-current counts/bounds. Call after the slot's fence
// wait, next to ano_vk_text_frame_refresh.
void ano_vk_ui_frame_refresh(RendererState* state, uint32_t frameIndex);

// Builds this slot's per-tile prim lists for the dispatch grid (§3.7): origin (ox,oy) in
// overlay px, gx*gy tiles of 8px. Rebuilds only when the version or grid changed for the
// slot. Returns true when the slot's tile buffers are valid for a tiled dispatch; false
// (tiles disabled, grid too large, or entry overflow) means fall back to the brute scan.
bool ano_vk_ui_build_tiles(RendererState* state, uint32_t frameIndex,
                           int32_t ox, int32_t oy, uint32_t gx, uint32_t gy);

// Frame-independent teardown, handle-guarded; frees adopted blocks.
void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state);

#endif
