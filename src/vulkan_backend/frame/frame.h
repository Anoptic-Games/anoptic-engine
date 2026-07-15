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

#include <anoptic_time.h>

#include "vulkan_backend/structs.h"

/* frame/passes.c */

// The frame pass table (order encodes the depth/EQUAL contract, do not reorder).
extern const RenderPassDef ano_frame_passes[];
extern const uint32_t ano_frame_pass_count;

/* frame/hiz.c */

// Async Hi-Z build + light-cull compute CBs (drawFrame records them for the compute queue).
void recordHiZCompute(uint32_t frameIndex);
void recordLightcullCompute(uint32_t frameIndex);
// In-frame Hi-Z pyramid build tail (record path, after the composite).
void ano_record_hiz_tail(VkCommandBuffer cmd);

/* frame/record_views.c */

// Per-view geometry passes (+ picking on view 0), then the composite/tonemap onto the swapchain.
void ano_record_views(VkCommandBuffer cmd, uint32_t entityCount, uint32_t drawSlotCount);
void ano_record_composite(VkCommandBuffer cmd, uint32_t imageIndex);

/* frame/record.c */

// Record one frame's command buffer. Called by drawFrame.
void recordCommandBuffer(uint32_t imageIndex);

/* frame/submit.c */

// Submit the frame's command buffers in order. ordinal = 1-based timeline value, false on submit failure.
bool ano_frame_submit(uint64_t ordinal);

/* frame/update.c */

// Build each view's camera matrices into its mapped uniform (drawFrame, per frame).
bool updateUniformBuffer(VulkanContext* ctx, RendererState* state);
// Fill the CullUBO (per-view viewProj/planes + knobs), publish the snapshot, refresh the mesh SSBO.
void updateCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);
// Deprecated. Publish transformBuffer.count = entityCount (device-local transforms).
void updateTransformBuffer(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);

/* frame/profiling.c */

// Stamp a section-boundary timestamp (BOTTOM_OF_PIPE). No-op without queue timestamp support.
void ano_ts(VkCommandBuffer cmd, uint32_t query);
// Read this frame slot's fence-complete timestamps / picking readback (drawFrame, post-fence).
void ano_collect_frame_stats(uint32_t frameIndex);
void ano_collect_pick(uint32_t frameIndex);
// Discard the in-progress timing window (lighting-mode change).
void ano_profile_reset_window(void);

// Frames per profiling flush window, shared by the [frame]/[frametime] pair and the [profile] line.
// 2^7: exactly 1 s of frames at 128 fps, ~2 s at a 60 fps target, ~1 s at 120.
#define ANO_PERF_WINDOW_FRAMES 128u

// Wall-clock frame-timing tally for one flush window. Zero-init = unseeded (prevUs == 0).
// Invariants: count < ANO_PERF_WINDOW_FRAMES between marks; prevUs - startUs == sum of dtUs.
typedef struct {
    uint64_t prevUs;                        // stamp of the last marked frame (dt base)
    uint64_t startUs;                       // stamp the current window opened at
    uint32_t count;                         // dts tallied this window
    uint32_t dtUs[ANO_PERF_WINDOW_FRAMES];  // per-frame wall dt (us), unordered until flush
} anoperf_accumulator_t;

extern anoperf_accumulator_t g_perfAcc;

// Drain one full window: sort, log the [frame] + [frametime] pair, reset. Cold, defined in profiling.c.
void anoperf_flush(anoperf_accumulator_t* acc);

// Mark one presented frame (drawFrame, presented path only). Hot path: one clock read, one dt
// store, one flush per ANO_PERF_WINDOW_FRAMES frames. First call seeds the stamps, uncounted.
static inline void ano_frame_mark(void) {
    anoperf_accumulator_t* acc = &g_perfAcc;
    uint64_t now = ano_timestamp_us();
    if (acc->prevUs == 0) { acc->prevUs = acc->startUs = now; return; }
    uint64_t dt = now - acc->prevUs;
    acc->prevUs = now;
    acc->dtUs[acc->count++] = (dt > UINT32_MAX) ? UINT32_MAX : (uint32_t)dt;
    if (acc->count == ANO_PERF_WINDOW_FRAMES) anoperf_flush(acc);
}

// Shadow-frustum renders per frame. Defined in profiling.c.
extern uint64_t g_shadowRenderAccum;
extern uint32_t g_shadowRenderFrames;

#endif // ANO_FRAME_H
