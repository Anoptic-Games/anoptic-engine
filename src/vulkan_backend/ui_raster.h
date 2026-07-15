/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI overlay lane: table buffers on raster-set bindings 4-10. Present when textOverlay up.

#ifndef ANO_UI_RASTER_H
#define ANO_UI_RASTER_H

#include "vulkan_backend/structs.h"

// After ano_vk_text_init. Always true: failure clears uiOverlay; sets fall back to text FB.
bool ano_vk_ui_init(VulkanContext* ctx, RendererState* state);

// Write raster-set bindings 4-10 per slot. Safe after swapchain recreate.
void ano_vk_ui_write_sets(VulkanContext* ctx, RendererState* state);

// Logic UI blocks. block_set ADOPTS blk. Clear idempotent. Dirty -> one recompose + uiVersion bump.
void ano_vk_ui_block_set(RendererState* state, uint32_t ui_id, const RenderUiBlock* blk);
void ano_vk_ui_block_clear(RendererState* state, uint32_t ui_id);

// Mark rescale after uiScale change; applied on next frame refresh.
void ano_vk_ui_rescale(RendererState* state);

// Flush compose + copy pending tables into slot when stale. Call after fence, with text_frame_refresh.
void ano_vk_ui_frame_refresh(RendererState* state, uint32_t frameIndex);

// Build per-tile prim lists over UI bounds (8px tiles). True = tiled dispatch; false = brute scan.
bool ano_vk_ui_build_tiles(RendererState* state, uint32_t frameIndex);

// Frame-independent teardown, handle-guarded. Frees adopted blocks.
void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state);

#endif
