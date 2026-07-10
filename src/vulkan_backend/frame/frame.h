/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Module-private header for the frame/ domain.

#ifndef ANO_FRAME_H
#define ANO_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"

// --- frame/passes.c ----------------------------------------------------------
// The frame pass table (order encodes the depth/EQUAL contract, do not reorder).
extern const RenderPassDef ano_frame_passes[];
extern const uint32_t ano_frame_pass_count;

// --- frame/hiz.c -------------------------------------------------------------
// Async Hi-Z build + light-cull compute CBs (drawFrame records them for the compute queue).
void recordHiZCompute(uint32_t frameIndex);
void recordLightcullCompute(uint32_t frameIndex);
// In-frame Hi-Z pyramid build tail (record path, after the composite).
void ano_record_hiz_tail(VkCommandBuffer cmd);

// --- frame/record_views.c ----------------------------------------------------
// Per-view geometry passes (+ picking on view 0), then the composite/tonemap onto the swapchain.
void ano_record_views(VkCommandBuffer cmd, uint32_t entityCount, uint32_t drawSlotCount);
void ano_record_composite(VkCommandBuffer cmd, uint32_t imageIndex);

// --- frame/record.c ----------------------------------------------------------
// Record one frame's command buffer. Called by drawFrame.
void recordCommandBuffer(uint32_t imageIndex);

// --- frame/submit.c ----------------------------------------------------------
// Submit the frame's command buffers in order. ordinal = 1-based timeline value, false on submit failure.
bool ano_frame_submit(uint64_t ordinal);

// --- frame/update.c ----------------------------------------------------------
// Build each view's camera matrices into its mapped uniform (drawFrame, per frame).
bool updateUniformBuffer(VulkanContext* ctx, RendererState* state);
// Fill the CullUBO (per-view viewProj/planes + knobs), publish the snapshot, refresh the mesh SSBO.
void updateCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);
// Deprecated. Publish transformBuffer.count = entityCount (device-local transforms).
void updateTransformBuffer(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);

// --- frame/profiling.c -------------------------------------------------------
// Stamp a section-boundary timestamp (BOTTOM_OF_PIPE). No-op without queue timestamp support.
void ano_ts(VkCommandBuffer cmd, uint32_t query);
// Read this frame slot's fence-complete timestamps / picking readback (drawFrame, post-fence).
void ano_collect_frame_stats(uint32_t frameIndex);
void ano_collect_pick(uint32_t frameIndex);
// Discard the in-progress timing window (lighting-mode change).
void ano_profile_reset_window(void);
// Mark one presented frame. Logs wall-clock fps + frametime once per second.
void ano_frame_mark(void);
// Shadow-frustum renders per frame. Defined in profiling.c.
extern uint64_t g_shadowRenderAccum;
extern uint32_t g_shadowRenderFrames;

#endif // ANO_FRAME_H
