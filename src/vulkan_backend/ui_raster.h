/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI overlay lane plumbing (docs/ui/ui-render.md): per-frame table buffers bound as
// raster-set bindings 4-10 of the shared text/UI dispatch. Buffers exist whenever
// textOverlay is up (ANO_FORCE_NO_UI pins the composed counts to 0).

#ifndef ANO_UI_RASTER_H
#define ANO_UI_RASTER_H

#include "vulkan_backend/structs.h"

// One-time init on the render thread, after ano_vk_text_init. Creates the per-frame
// table buffers. Always returns true: failure logs, clears state->uiOverlay, and the
// set writes fall back to the text frame buffer.
bool ano_vk_ui_init(VulkanContext* ctx, RendererState* state);

// Writes raster-set bindings 4-10 for every frame slot (region ranges of the slot's
// uiFrameBuffer). Called by ano_vk_text_update_sets after its own writes. Safe after
// a swapchain recreate.
void ano_vk_ui_write_sets(VulkanContext* ctx, RendererState* state);

// Logic UI blocks (the v0 bridge path). block_set ADOPTS blk, replacing ui_id's
// contents. block_clear is idempotent. Both mark the pending tables dirty. The next
// frame refresh recomposes ONCE and bumps uiVersion. Render thread only.
void ano_vk_ui_block_set(RendererState* state, uint32_t ui_id, const RenderUiBlock* blk);
void ano_vk_ui_block_clear(RendererState* state, uint32_t ui_id);

// Re-folds the retained blocks after state->uiScale changed, deferred to the next
// frame refresh. No-op while the overlay is down or the canvas is pinned.
void ano_vk_ui_rescale(RendererState* state);

// Flushes a deferred compose, then copies pending tables into this slot's mapped
// buffers when stale (prims/clips/paints/stops into uiFrameBuffer, glyph labels into
// the text frame buffer's UI region) and publishes the slot-current counts/bounds.
// Call after the slot's fence wait, next to ano_vk_text_frame_refresh.
void ano_vk_ui_frame_refresh(RendererState* state, uint32_t frameIndex);

// Builds this slot's per-tile prim lists (§3.7) over the UI bounds alone, snapped to
// 8px tiles and clamped to the canvas. The built grid lands in the slot's
// uiTile{Ox,Oy,Gx,Gy} (origin px, extent tiles) for the push block. Returns true when
// the slot's tile buffers are valid for a tiled dispatch, false means brute scan.
bool ano_vk_ui_build_tiles(RendererState* state, uint32_t frameIndex);

// Frame-independent teardown, handle-guarded. Frees adopted blocks.
void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state);

#endif
