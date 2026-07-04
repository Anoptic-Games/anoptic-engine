/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text overlay plumbing (FONT_RENDER.md step 5): glyph-curve GPU buffers, the per-frame
// overlay raster targets, the PIPELINE_COMPUTE_TEXTRASTER pass, and the composite blend
// draw. Everything is gated on rendererState.textOverlay -- creation paths check the
// toggle, destruction paths check handles, so a mid-init disable (missing font) is safe.

#ifndef ANO_TEXT_RASTER_H
#define ANO_TEXT_RASTER_H

#include "vulkan_backend/structs.h"

// One-time init on the render thread: FreeType up, Geist loaded + ASCII baked (blobs on
// state->textHeap), curve/directory blobs uploaded device-local, per-frame frame-data
// buffers created, raster + overlay-blend pipelines built. Returns true always: failure
// logs and clears state->textOverlay instead of failing engine init.
bool ano_vk_text_init(VulkanContext* ctx, RendererState* state);

// Size-dependent overlay images (one per frame in flight, swapchain extent). Called from
// createColorResources so the resize path recreates them with the other targets.
void ano_vk_text_create_overlay(VulkanContext* ctx, RendererState* state);

// Destroys the overlay images/views (handles-guarded; memory is the swapchain arena,
// reset by cleanupSwapChain after this returns).
void ano_vk_text_destroy_overlay(VulkanContext* ctx, RendererState* state);

// Allocates the per-frame descriptor sets from the global pool (raster set + overlay
// sample set) and writes them. Failure logs and clears state->textOverlay.
void ano_vk_text_create_sets(VulkanContext* ctx, RendererState* state);

// (Re)writes the per-frame sets; rerun after a swapchain resize (overlay views change).
void ano_vk_text_update_sets(VulkanContext* ctx, RendererState* state);

// Replaces the on-screen text: shapes utf8 into the pending canonical array and bumps
// textVersion (frame slots pick it up via ano_vk_text_frame_refresh). Render thread
// only; no-op when the overlay is off or ANO_TEXT_DEMO pinned the harness text.
void ano_vk_text_set(RendererState* state, const char* utf8, uint32_t len, float sizePx,
                     const float origin[2], const float color[4]);

// Copies pending text into this slot's mapped frame buffer when stale, and points the
// push-constant instance count at the slot's contents. Call after the slot's fence wait
// (prior GPU read retired) and before its record/submit.
void ano_vk_text_frame_refresh(RendererState* state, uint32_t frameIndex);

// In-frame raster record (the async lane's mandatory fallback): clear the overlay,
// dispatch the raster pass, hand the image to the fragment stage. No-op when asyncText.
void ano_vk_text_record(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex);

// Async lane (step 7, lag-0): records this frame's raster CB and submits it to
// ctx->computeQueue with no waits, signaling textTimeline == ordinal (the main graphics
// submit waits it at FRAGMENT_SHADER). A failed submit host-signals the ordinal so the
// timeline stays monotonic. No-op unless asyncText.
void ano_vk_text_submit_async(VulkanContext* ctx, RendererState* state, uint32_t frameIndex,
                              uint64_t ordinal);

// The composite-side overlay draw: one fullscreen premultiplied blend, recorded inside
// the composite's dynamic-rendering block after the PiP insets.
void ano_vk_text_record_composite(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex);

// Frame-independent teardown: frame-data + curve/directory buffers, the CPU bake heap,
// and the FreeType backend. The compute prototype and the bespoke pipeline die in
// ano_vk_cleanup_pipelines; the overlay images die with the swapchain.
void ano_vk_text_destroy(VulkanContext* ctx, RendererState* state);

#endif
