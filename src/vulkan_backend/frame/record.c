/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>
#include <anoptic_logging.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/shadow/shadow.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/frame/frame.h"

void recordCommandBuffer(uint32_t imageIndex)
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // re-recorded every frame, submitted once
	beginInfo.pInheritanceInfo = NULL;// Optional
	
	// Async light-cull (review finding 2 remainder): the uploads + shared compute prelude record
	// into their own CB, submitted ahead of the main one (its completion signal releases the
	// compute-queue light-cull). Off async, everything records into the one frame CB as before.
	VkCommandBuffer cmd = rendererState.asyncLc
		? rendererState.frames[rendererState.frameIndex].preludeCommandBuffer
		: rendererState.frames[rendererState.frameIndex].commandBuffer;

	if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
	{
		ano_olog(ANO_ERROR, "Failed to begin recording command buffer!");
	}

	// Profiling: reset this frame's query pool and stamp the frame-begin boundary (outside any
	// render pass). The five section boundaries below are stamped unconditionally at top level;
	// split submits execute in submission order on the one graphics queue, so the reset (prelude
	// CB) still precedes every stamp in the main CB.
	if (rendererState.timestampValidBits) {
		vkCmdResetQueryPool(cmd, rendererState.frames[rendererState.frameIndex].timestampPool, 0, ANO_TS_COUNT);
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			rendererState.frames[rendererState.frameIndex].timestampPool, ANO_TS_FRAME_BEGIN);
	}

    // Each view's HDR resolve target is moved to COLOR_ATTACHMENT inside the per-view loop below
    // (UNDEFINED -> COLOR: the geometry clear + resolve overwrite the whole render area).

    // Upload this frame's staged per-slot deltas into the DEVICE_LOCAL authoritative buffers
    // before any pass reads them. One vkCmdCopyBuffer per buffer, bracketed by a single
    // read->transfer / transfer->read barrier: the first scope reaches back across submission
    // order on this (single graphics) queue, so prior frames' shader reads of the shared buffers
    // complete before the copy writes; the second makes the writes visible to this frame's
    // update/cull/geometry reads. Skipped entirely when nothing changed this frame.
    {
        SlotUpload* ups[7] = {
            &rendererState.initialTransformBuffer, &rendererState.motionBuffer,
            &rendererState.instanceDataBuffer, &rendererState.lightBuffer,
            &rendererState.culling.entity,
            &rendererState.shadowConfig, &rendererState.shadowInfo, // runtime shadow caster lifecycle
        };
        uint32_t fi = rendererState.frameIndex;
        bool any = false;
        for (int u = 0; u < 7; u++) if (ups[u]->staged[fi]) { any = true; break; }
        if (any) {
            // These buffers are only ever read by the shader stages below: compute (update/cull/
            // lightcull), the geometry stage (entity buffer; + task when the meshlet cull is on),
            // and fragment (instance data + lights). So both the WAR (pre) and the visibility
            // (post) scopes are exactly that stage set, not ALL_COMMANDS. The pre barrier's first
            // scope still reaches prior submissions on this single queue, so earlier frames'
            // shader reads finish before the copy overwrites.
            VkPipelineStageFlags shaderStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                | (ctx.deviceCapabilities.meshShader ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
                | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0)
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            VkMemoryBarrier pre = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT };
            vkCmdPipelineBarrier(cmd, shaderStages, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 1, &pre, 0, NULL, 0, NULL);
            for (int u = 0; u < 7; u++) slot_upload_flush(cmd, ups[u], fi);
            VkMemoryBarrier post = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, shaderStages,
                0, 1, &post, 0, NULL, 0, NULL);
        }
    }
    ano_ts(cmd, ANO_TS_AFTER_UPLOAD);

    uint32_t entityCount = rendererState.entityCount;

    // === Shared (view-independent) compute: update, scatter, cull ===
    // The cull dispatch is single-pass multi-frustum — it tests each entity against every view's
    // frustum and writes all views' partitions at once, so it runs here, not per view.
    if (entityCount > 0) {
        uint32_t streamCount = rendererState.transformStream.count[rendererState.frameIndex];
        uint32_t lightCount = rendererState.lightBuffer.count; // active light rows (lightsetup dispatch size)
        for (int p = 0; p < (int)ano_frame_pass_count; p++) {
            const RenderPassDef* pass = &ano_frame_passes[p];
            if (pass->type != PASS_COMPUTE || pass->perView) continue;
            if (pass->prototype == PIPELINE_COMPUTE_SCATTER && streamCount == 0)
                continue; // nothing streamed this frame: skip the scatter pass entirely

            if (pass->prototype == PIPELINE_COMPUTE_CULL) {
                // Zero the per-partition draw counts. The full indirect-buffer fill (several MB:
                // stride x capacity x partitions) is only needed on the fallback path, where the
                // fixed-count vkCmdDraw*Indirect reads every slot and unwritten commands must decode
                // as no-op draws. On the indirect-count path nothing reads past drawCount — cull
                // appends [0, count) and tpsort bounds by drawCounts — so the fill is skipped.
                if (!ctx.deviceCapabilities.drawIndirectCount) {
                    VkDeviceSize cmdStride = sizeof(VkDrawIndexedIndirectCommand) > sizeof(VkDrawMeshTasksIndirectCommandEXT)
                        ? sizeof(VkDrawIndexedIndirectCommand) : sizeof(VkDrawMeshTasksIndirectCommandEXT);
                    vkCmdFillBuffer(cmd, rendererState.indirectBuffer.buffer[rendererState.frameIndex], 0,
                        cmdStride * rendererState.indirectBuffer.capacity * ano_draw_partition_count(), 0);
                }
                vkCmdFillBuffer(cmd, rendererState.culling.drawCountBuffer[rendererState.frameIndex], 0,
                    sizeof(uint32_t) * ano_draw_partition_count(), 0);

                VkMemoryBarrier fillBarrier = {};
                fillBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &fillBarrier, 0, NULL, 0, NULL);

                // Hi-Z occlusion (review 4.9 step 3): the cull samples binding 11 = the PREVIOUS frame-
                // in-flight slot's pyramids (built last frame); the task meshlet cull samples the same
                // images per view (global set binding 13), so its stage joins the dst scope. Order that
                // build's writes before those reads (no layout change — they rest in SHADER_READ). First
                // frame: the prev slot was never built but is seeded to SHADER_READ, so the barrier is a
                // harmless no-op. Async build (review finding 2): the writes happened on the compute
                // queue; the submit's hizTimeline wait carries them instead (its stage set gains TASK).
                if (!rendererState.asyncHiz) {
                    uint32_t hizPrevSlot = (rendererState.frameIndex + MAX_FRAMES_IN_FLIGHT - 1u) % MAX_FRAMES_IN_FLIGHT;
                    VkImageMemoryBarrier hizRead[ANO_VIEW_COUNT] = {};
                    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
                        hizRead[v].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        hizRead[v].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        hizRead[v].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        hizRead[v].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        hizRead[v].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        hizRead[v].image = rendererState.frames[hizPrevSlot].views[v].hizImage;
                        hizRead[v].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        hizRead[v].subresourceRange.levelCount = rendererState.frames[hizPrevSlot].views[v].hizMipCount;
                        hizRead[v].subresourceRange.layerCount = 1;
                        hizRead[v].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        hizRead[v].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    }
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                            | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0),
                        0, 0, NULL, 0, NULL, ANO_VIEW_COUNT, hizRead);
                }
            }

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rendererState.prototypes[pass->prototype].implementations[0].pipeline);

            VkDescriptorSet set =
                pass->prototype == PIPELINE_COMPUTE_UPDATE      ? rendererState.frames[rendererState.frameIndex].updateSet :
                pass->prototype == PIPELINE_COMPUTE_SCATTER     ? rendererState.frames[rendererState.frameIndex].scatterSet :
                pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP ? rendererState.frames[rendererState.frameIndex].shadow.setupSet :
                pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP  ? rendererState.frames[rendererState.frameIndex].lightsetupSet :
                                                                  rendererState.frames[rendererState.frameIndex].cullSet;

            // Scatter binding 1 (xform ring) is STORAGE_BUFFER_DYNAMIC: bind the
            // published slice by per-frame dynamic offset; other passes have none.
            uint32_t dynCount = pass->prototype == PIPELINE_COMPUTE_SCATTER ? 1u : 0u;
            const uint32_t* dynOff = pass->prototype == PIPELINE_COMPUTE_SCATTER
                ? &rendererState.transformStream.dynOffset[rendererState.frameIndex] : NULL;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                rendererState.prototypes[pass->prototype].layout, 0, 1, &set, dynCount, dynOff);

            if (pass->prototype == PIPELINE_COMPUTE_UPDATE) {
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &entityCount);
            } else if (pass->prototype == PIPELINE_COMPUTE_SCATTER) {
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &streamCount);
            } else if (pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &lightCount);
            }

            uint32_t dispatchX;
            if (pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP) {
                dispatchX = (ANO_SHADOW_FRUSTUM_COUNT + 63u) / 64u; // one invocation per shadow frustum
            } else if (pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
                dispatchX = (lightCount + 63u) / 64u; // one invocation per light (local_size_x = 64)
            } else {
                uint32_t workItems = pass->prototype == PIPELINE_COMPUTE_SCATTER ? streamCount : entityCount;
                dispatchX = pass->dispatchX == 0 ? (workItems + 255) / 256 : pass->dispatchX;
            }
            vkCmdDispatch(cmd, dispatchX, 1, 1);

            VkMemoryBarrier memoryBarrier = {};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            if (pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP) {
                // ONE barrier for lightsetup + shadowsetup (mutually independent: disjoint writes, no
                // cross-reads, adjacent in the pass table — lightsetup emits none below). Shadow
                // frustums feed the cull (compute), the depth render (mesh/vertex; + the task meshlet
                // cull, which tests shadow draws against the frustum planes), and the fragment
                // sampler; the light runtime feeds the light-cull (compute) and the fragment passes
                // (set 0 binding 12). Union: COMPUTE | geom | FRAGMENT.
                VkPipelineStageFlags geomStage = (ctx.deviceCapabilities.meshShader
                    ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
                    | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0);
                // UNIFORM_READ: the fragments read the packed sampling viewProjs as a UBO.
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | geomStage | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else if (pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
                // No barrier: shadowsetup (next pass, independent) carries the shared one above.
            } else if (pass->prototype == PIPELINE_COMPUTE_UPDATE || pass->prototype == PIPELINE_COMPUTE_SCATTER) {
                // update -> scatter is a WAW on streamed slots (scatter must win); both -> cull is a read.
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else {
                // The cull pass feeds the indirect commands (DRAW_INDIRECT) and the compacted/entity
                // SSBOs read by the geometry stage (mesh or vertex; + the task meshlet cull, which
                // resolves its draw from the compacted indices) AND the transparency-sort compute
                // pass (tpsort), which reads the compacted draws + depth keys and rewrites the
                // transmission partition. So the barrier reaches COMPUTE as well as DRAW_INDIRECT|geom,
                // and dst includes SHADER_WRITE (tpsort overwrites cull's writes — WAW).
                VkPipelineStageFlags geomStage = (ctx.deviceCapabilities.meshShader
                    ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
                    | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0);
                memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | geomStage | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            }
        }
    }

    uint32_t drawSlotCount = ano_draw_pipeline_count();

    ano_ts(cmd, ANO_TS_AFTER_COMPUTE);

    // Async light-cull: the prelude CB ends here; everything below records into the main CB.
    // Between the two submits the compute queue runs both views' light-culls off the prelude's
    // completion signal, overlapping the shadow region below.
    if (rendererState.asyncLc) {
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            ano_log(ANO_ERROR, "Failed to record prelude command buffer!");
        cmd = rendererState.frames[rendererState.frameIndex].commandBuffer;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
            ano_olog(ANO_ERROR, "Failed to begin recording command buffer!");
    }

    // Transition swapchain image to color attachment optimal (consumed by the composite at the
    // frame's end; recorded here so the prelude CB never touches the swapchain).
    {
        VkImageMemoryBarrier swapChainBarrier = {};
        swapChainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapChainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapChainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapChainBarrier.image = rendererState.images[imageIndex];
        swapChainBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapChainBarrier.subresourceRange.baseMipLevel = 0;
        swapChainBarrier.subresourceRange.levelCount = 1;
        swapChainBarrier.subresourceRange.baseArrayLayer = 0;
        swapChainBarrier.subresourceRange.layerCount = 1;
        swapChainBarrier.srcAccessMask = 0;
        swapChainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, NULL, 0, NULL, 1, &swapChainBarrier);
    }

    ano_shadow_record(cmd, entityCount, drawSlotCount);

    ano_ts(cmd, ANO_TS_AFTER_SHADOW);

    // === Transparency sort: reorder each camera view's transmission partition back-to-front ===
    // tpsort.comp reads cull's compacted draws + per-draw depth keys and rewrites the transmission
    // partition in place so the "over" blend in the per-view transmission pass composites farthest-
    // first instead of in cull's arbitrary atomic-append order. One workgroup per camera view. Runs
    // after cull (whose post-barrier now reaches COMPUTE) and before the per-view geometry passes.
    if (entityCount > 0 && ano_draw_slot_of(PIPELINE_TRANSMISSION) != ANO_NO_DRAW_SLOT) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_TPSORT].implementations[0].pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_TPSORT].layout, 0, 1,
            &rendererState.frames[rendererState.frameIndex].cullSet, 0, NULL);
        vkCmdDispatch(cmd, ANO_VIEW_COUNT, 1, 1); // one workgroup per camera view

        // Sort writes (compacted indices + indirect commands) -> the geometry stage's indirect +
        // SSBO reads in the per-view transmission pass below (task stage included: it resolves the
        // reordered compacted indices per draw).
        VkPipelineStageFlags geomStage = (ctx.deviceCapabilities.meshShader
            ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
            | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0);
        VkMemoryBarrier sortBarrier = {};
        sortBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        sortBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sortBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | geomStage,
            0, 1, &sortBarrier, 0, NULL, 0, NULL);
    }

    ano_record_views(cmd, entityCount, drawSlotCount);

    ano_ts(cmd, ANO_TS_AFTER_LIGHTING);

    // Text overlay raster (in-frame path): clear + dispatch + hand the
    // overlay to the composite's fragment stage.
    ano_vk_text_record(&rendererState, cmd, rendererState.frameIndex);

    ano_record_composite(cmd, imageIndex);

    ano_record_hiz_tail(cmd);

    ano_ts(cmd, ANO_TS_AFTER_COMPOSITE);

	// Transition swapchain image to present
	{
		VkImageMemoryBarrier swapChainBarrier = {};
		swapChainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		swapChainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		swapChainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		swapChainBarrier.image = rendererState.images[imageIndex];
		swapChainBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		swapChainBarrier.subresourceRange.baseMipLevel = 0;
		swapChainBarrier.subresourceRange.levelCount = 1;
		swapChainBarrier.subresourceRange.baseArrayLayer = 0;
		swapChainBarrier.subresourceRange.layerCount = 1;
		swapChainBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		swapChainBarrier.dstAccessMask = 0;

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &swapChainBarrier
		);
	}

	if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to record command buffer!");
	}
}
