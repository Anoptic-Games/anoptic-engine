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

// Frame-independent teardown, handle-guarded.
void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state);

#endif
