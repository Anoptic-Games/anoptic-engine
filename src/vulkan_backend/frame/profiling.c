/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdio.h>
#include <anoptic_logging.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/frame/frame.h"

// --- Profiling harness ---------------------------------------------------------------------
// Per-pass GPU timestamps as fence-posts: region time = consecutive delta * timestampPeriod.
static double   g_tsAccumMs[ANO_TS_COUNT - 1]; // accumulated per-region ms
static uint32_t g_tsFrames = 0;             // frames since last print
#define ANO_PROFILE_PRINT_INTERVAL 120u     // frames per print
// Shadow-frustum renders per frame, averaged into the profile line.
uint64_t g_shadowRenderAccum = 0;
uint32_t g_shadowRenderFrames = 0;

// Live VRAM use of a bump allocator: sum of each block's high-water offset.
static VkDeviceSize allocator_used_bytes(const GpuAllocator* a) {
    VkDeviceSize used = 0;
    for (uint32_t i = 0; i < a->blockCount; i++) used += a->blocks[i].offset;
    return used;
}

// Stamp a section-boundary timestamp. No-op when the queue has no timestamp support.
void ano_ts(VkCommandBuffer cmd, uint32_t query) {
    if (rendererState.timestampValidBits)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            rendererState.frames[rendererState.frameIndex].timestampPool, query);
}

// Print averaged per-pass GPU times + per-allocator resident VRAM. shadowAtlas reported separately.
static void ano_print_profiling(void) {
    static const char* const modeNames[ANO_LIGHTING_MODE_COUNT] = { "SHADOWMAP", "HYBRID", "RC" };
    uint32_t m = rendererState.lightingMode;
    const char* mn = (m < (uint32_t)ANO_LIGHTING_MODE_COUNT) ? modeNames[m] : "?";
    double inv = g_tsFrames ? 1.0 / (double)g_tsFrames : 0.0;
    double up = g_tsAccumMs[ANO_TS_FRAME_BEGIN]    * inv;
    double cp = g_tsAccumMs[ANO_TS_AFTER_UPLOAD]   * inv;
    double sh = g_tsAccumMs[ANO_TS_AFTER_COMPUTE]  * inv;
    double li = g_tsAccumMs[ANO_TS_AFTER_SHADOW]   * inv;
    double co = g_tsAccumMs[ANO_TS_AFTER_LIGHTING] * inv;
    double total = up + cp + sh + li + co;

    const double MiB = 1024.0 * 1024.0;
    double gpu  = (double)allocator_used_bytes(&gpuAllocator)       / MiB;
    double tex  = (double)allocator_used_bytes(&textureAllocator)   / MiB;
    double swap = (double)allocator_used_bytes(&swapchainAllocator) / MiB;
    double stg  = (double)allocator_used_bytes(&stagingAllocator)   / MiB;
    // CDF stats atlas + blur temp (RGBA16, 2 sublayers/frustum) + transient depth array (D32), one shared instance each.
    double atlas = (double)((VkDeviceSize)ANO_SHADOW_ATLAS_LAYERS * ANO_SHADOW_DIM * ANO_SHADOW_DIM * 8u * 2u
                            + (VkDeviceSize)ANO_SHADOW_FRUSTUM_COUNT * ANO_SHADOW_DIM * ANO_SHADOW_DIM * 4u) / MiB;

    double frusta = g_shadowRenderFrames ? (double)g_shadowRenderAccum / (double)g_shadowRenderFrames : 0.0;
    ano_debug_log(ANO_INFO, "[profile mode=%s] GPU ms: upload=%.3f compute=%.3f shadow=%.3f (frusta %.1f/%u) lighting=%.3f composite=%.3f total=%.3f"
           " | VRAM MiB: gpu=%.1f tex=%.1f swap=%.1f staging=%.1f | shadowAtlas(resident)=%.1f",
           mn, up, cp, sh, frusta, ANO_SHADOW_FRUSTUM_COUNT, li, co, total, gpu, tex, swap, stg, atlas);
    g_shadowRenderAccum = 0;
    g_shadowRenderFrames = 0;

    // Mirror the readout on-screen.
    // Three style runs: white stats, total colored by frame budget, VRAM line dimmed.
    char osd[512];
    int head = snprintf(osd, sizeof osd,
        "[%s] GPU ms  upload %.3f  compute %.3f  shadow %.3f (frusta %.1f/%u)\n"
        "lighting %.3f  composite %.3f  total ",
        mn, up, cp, sh, frusta, ANO_SHADOW_FRUSTUM_COUNT, li, co);
    int mid = head > 0 ? snprintf(osd + head, sizeof osd - (size_t)head, "%.3f", total) : -1;
    int tail = mid > 0 ? snprintf(osd + head + mid, sizeof osd - (size_t)(head + mid),
        "\nVRAM MiB  gpu %.1f  tex %.1f  swap %.1f  staging %.1f  shadowAtlas %.1f",
        gpu, tex, swap, stg, atlas) : -1;
    if (tail > 0 && head + mid + tail < (int)sizeof osd)
    {
        static const float osdOrigin[2] = { 24.0f, 40.0f };
        const float* health = total < 4.0 ? (const float[4]){ 0.35f, 1.00f, 0.45f, 1.0f }
                            : total < 8.0 ? (const float[4]){ 1.00f, 0.78f, 0.32f, 1.0f }
                                          : (const float[4]){ 1.00f, 0.30f, 0.25f, 1.0f };
        AnoTextRun runs[3] = {
            { (uint32_t)head, 22.0f, { 1.0f, 1.0f, 1.0f, 1.0f } },
            { (uint32_t)mid, 22.0f, { health[0], health[1], health[2], health[3] } },
            { (uint32_t)tail, 22.0f, { 0.62f, 0.62f, 0.62f, 1.0f } },
        };
        ano_vk_text_set_runs(&rendererState, anostr_view(osd, (size_t)(head + mid + tail)),
                             runs, 3, osdOrigin);
    }
}

// Fold this frame slot's per-pass timestamp deltas into the running average.
// Print every ANO_PROFILE_PRINT_INTERVAL frames. No-op when timestamps are unsupported.
void ano_collect_frame_stats(uint32_t frameIndex) {
    if (!rendererState.timestampValidBits) return;
    VkQueryPool pool = rendererState.frames[frameIndex].timestampPool;
    if (pool == VK_NULL_HANDLE) return;
    uint64_t ts[ANO_TS_COUNT];
    if (vkGetQueryPoolResults(ctx.device, pool, 0, ANO_TS_COUNT, sizeof(ts), ts,
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return;
    uint64_t mask = (rendererState.timestampValidBits >= 64) ? ~0ull : ((1ull << rendererState.timestampValidBits) - 1ull);
    for (int r = 0; r < ANO_TS_COUNT - 1; r++) {
        uint64_t a = ts[r] & mask, b = ts[r + 1] & mask;
        uint64_t d = (b >= a) ? (b - a) : (mask - a + b + 1u); // wrap-safe
        g_tsAccumMs[r] += (double)d * (double)rendererState.timestampPeriodNs * 1e-6;
    }
    if (++g_tsFrames >= ANO_PROFILE_PRINT_INTERVAL) {
        ano_print_profiling();
        for (int r = 0; r < ANO_TS_COUNT - 1; r++) g_tsAccumMs[r] = 0.0;
        g_tsFrames = 0;
    }
}

// Discard the in-progress timing window so the next printed average is pure for the new regime.
void ano_profile_reset_window(void) {
    for (int r = 0; r < ANO_TS_COUNT - 1; r++) g_tsAccumMs[r] = 0.0;
    g_tsFrames = 0;
}

// Map this frame slot's picking readback to a render_id and emit REVENT_PICK_RESULT when the hit changes.
// A cleared/unmapped slot collapses to ANO_RENDER_NO_PICK. On a full event ring, skip latching.
void ano_collect_pick(uint32_t frameIndex) {
    uint32_t slot = *rendererState.frames[frameIndex].pickReadbackMapped;
    uint32_t rid  = (slot == 0xFFFFFFFFu) ? ANO_RENDER_NO_PICK
                                          : render_slots_render_id_of(&rendererState.slots, slot);
    if (rid == ANO_RENDER_SLOT_UNMAPPED) rid = ANO_RENDER_NO_PICK; // slot retired between draw and read
    if (rid != rendererState.lastPickRenderId) {
        RenderEvent ev = { .kind = REVENT_PICK_RESULT, .u.pick_render_id = rid };
        if (ano_render_emit_event(&rendererState.bridge, &ev))
            rendererState.lastPickRenderId = rid;
    }
}
