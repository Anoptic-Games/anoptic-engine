/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text overlay plumbing: glyph-curve GPU buffers, per-frame
// overlay raster targets, the PIPELINE_COMPUTE_TEXTRASTER pass, and the composite
// blend draw. Gated on rendererState.textOverlay.

#ifndef ANO_TEXT_RASTER_H
#define ANO_TEXT_RASTER_H

#include "vulkan_backend/structs.h"

// One-time init on the render thread: FreeType up, fonts loaded + baked, GPU buffers
// and pipelines built. Always returns true: failure logs and clears state->textOverlay.
bool ano_vk_text_init(VulkanContext* ctx, RendererState* state);

// Size-dependent overlay images (one per frame in flight, swapchain extent). Called
// from createColorResources.
void ano_vk_text_create_overlay(VulkanContext* ctx, RendererState* state);

// Destroys the overlay images/views (handle-guarded).
void ano_vk_text_destroy_overlay(VulkanContext* ctx, RendererState* state);

// Allocates and writes the per-frame descriptor sets from the global pool.
// Failure logs and clears state->textOverlay.
void ano_vk_text_create_sets(VulkanContext* ctx, RendererState* state);

// (Re)writes the per-frame sets. Rerun after a swapchain resize.
void ano_vk_text_update_sets(VulkanContext* ctx, RendererState* state);

// Replaces the on-screen text: shapes the string into the pending canonical array and
// bumps textVersion. Render thread only. No-op when the overlay is off or pinned.
void ano_vk_text_set(RendererState* state, anostr_t text, float sizePx,
                     const float origin[2], const float color[4]);

// ano_vk_text_set over a string literal.
#define ano_vk_text_set_lit(state, textlit, sizePx, origin, color) \
    ano_vk_text_set((state), anostr_lit(textlit), (sizePx), (origin), (color))

// ano_vk_text_set with per-glyph style runs (see AnoTextRun). Same semantics and gating.
void ano_vk_text_set_runs(RendererState* state, anostr_t text, const AnoTextRun* runs,
                          uint32_t runCount, const float origin[2]);

// Logic text blocks (the v0 bridge path). block_set ADOPTS blk, replacing text_id's
// contents. block_clear is idempotent. Both recompose the pending canonical and bump
// textVersion.
void ano_vk_text_block_set(RendererState* state, uint32_t text_id, const RenderTextBlock* blk);
void ano_vk_text_block_clear(RendererState* state, uint32_t text_id);

// Copies pending text into this slot's mapped frame buffer when stale. Call after the
// slot's fence wait and before its record/submit.
void ano_vk_text_frame_refresh(RendererState* state, uint32_t frameIndex);

// In-frame raster record (the async lane's fallback): clear, dispatch, hand the image
// to the fragment stage. No-op when asyncText.
void ano_vk_text_record(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex);

// Async lane (step 7): records this frame's raster CB and submits it to the compute
// queue with no waits, signaling textTimeline == ordinal. A failed submit host-signals
// the ordinal. No-op unless asyncText.
void ano_vk_text_submit_async(VulkanContext* ctx, RendererState* state, uint32_t frameIndex,
                              uint64_t ordinal);

// The composite-side overlay draw: one fullscreen premultiplied blend, recorded after
// the PiP insets.
void ano_vk_text_record_composite(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex);

// The world-space text panel: one bufferless quad draw, recorded inside a view's
// additive pass. No-op unless textWorld.
void ano_vk_text_record_world(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex,
                              uint32_t view);

// Frame-independent teardown: frame-data + curve/directory buffers, the CPU bake heap,
// and the FreeType backend. Pipelines die in ano_vk_cleanup_pipelines, the overlay
// images with the swapchain.
void ano_vk_text_destroy(VulkanContext* ctx, RendererState* state);

#endif
