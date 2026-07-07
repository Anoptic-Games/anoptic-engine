/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI overlay lane plumbing (docs/ui/ui-render.md). Groundwork skeleton: the prim
// buffers, descriptor sets, and raster dispatch land with build steps 3-4; today
// this TU only owns the gate. Gated on rendererState.uiOverlay.

#ifndef ANO_UI_RASTER_H
#define ANO_UI_RASTER_H

#include "vulkan_backend/structs.h"

// One-time init on the render thread. Always returns true: failure logs and clears
// state->uiOverlay (the text-lane non-fatal pattern). Groundwork stub: no GPU objects.
bool ano_vk_ui_init(VulkanContext* ctx, RendererState* state);

// Frame-independent teardown, handle-guarded. Groundwork stub: nothing to free.
void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state);

#endif
