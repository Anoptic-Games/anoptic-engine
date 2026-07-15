/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>
#include <anoptic_log.h>

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
	
	// Async light-cull records uploads + shared compute prelude into a separate CB submitted ahead of the main one.
	VkCommandBuffer cmd = rendererState.asyncLc
		? rendererState.frames[rendererState.frameIndex].preludeCommandBuffer
		: rendererState.frames[rendererState.frameIndex].commandBuffer;

	if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
	{
		ano_olog(ANO_ERROR, "Failed to begin recording command buffer!");
	}

	// Profiling reset of this frame's query pool plus the frame-begin timestamp.
	if (rendererState.timestampValidBits) {
		vkCmdResetQueryPool(cmd, rendererState.frames[rendererState.frameIndex].timestampPool, 0, ANO_TS_COUNT);
		vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			rendererState.frames[rendererState.frameIndex].timestampPool, ANO_TS_FRAME_BEGIN);
	}

    // Each view's HDR resolve target moves to COLOR_ATTACHMENT inside the per-view loop below.

    // Upload this frame's staged per-slot deltas into the DEVICE_LOCAL buffers before any pass reads them.
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
            // Pre (WAR) and post (visibility) scopes are exactly the shader stages that read these buffers.
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
    // Cull is single-pass multi-frustum, testing each entity against every view, so it runs here not per view.
    if (entityCount > 0) {
        uint32_t streamCount = rendererState.transformStream.count[rendererState.frameIndex];
        uint32_t lightCount = rendererState.lightBuffer.count; // active light rows
        for (int p = 0; p < (int)ano_frame_pass_count; p++) {
            const RenderPassDef* pass = &ano_frame_passes[p];
            if (pass->type != PASS_COMPUTE || pass->perView) continue;
            if (pass->prototype == PIPELINE_COMPUTE_SCATTER && streamCount == 0)
                continue; // nothing streamed this frame

            if (pass->prototype == PIPELINE_COMPUTE_CULL) {
                // Zero the per-partition draw counts, and fill the full indirect buffer only on the fallback path.
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

                // Hi-Z occlusion cull reads the previous frame slot's pyramids, so order that build's writes before these reads.
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

            // Scatter binding 1 is STORAGE_BUFFER_DYNAMIC, bound by per-frame dynamic offset (others have none).
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
                dispatchX = (lightCount + 63u) / 64u; // one invocation per light
            } else {
                uint32_t workItems = pass->prototype == PIPELINE_COMPUTE_SCATTER ? streamCount : entityCount;
                dispatchX = pass->dispatchX == 0 ? (workItems + 255) / 256 : pass->dispatchX;
            }
            vkCmdDispatch(cmd, dispatchX, 1, 1);

            VkMemoryBarrier memoryBarrier = {};
            memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            if (pass->prototype == PIPELINE_COMPUTE_SHADOWSETUP) {
                // One barrier covers lightsetup + shadowsetup (disjoint writes), dst scope is COMPUTE, geom, and FRAGMENT.
                VkPipelineStageFlags geomStage = (ctx.deviceCapabilities.meshShader
                    ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
                    | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0);
                // UNIFORM_READ for fragments reading the packed sampling viewProjs as a UBO.
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | geomStage | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else if (pass->prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
                // No barrier, shadowsetup carries the shared one above.
            } else if (pass->prototype == PIPELINE_COMPUTE_UPDATE || pass->prototype == PIPELINE_COMPUTE_SCATTER) {
                // update -> scatter is a WAW on streamed slots, both -> cull is a read.
                memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 1, &memoryBarrier, 0, NULL, 0, NULL);
            } else {
                // Cull dst: DRAW_INDIRECT + geom + COMPUTE (tpsort).
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

    // Async light-cull ends the prelude CB here, everything below records into the main CB.
    if (rendererState.asyncLc) {
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            ano_log(ANO_ERROR, "Failed to record prelude command buffer!");
        cmd = rendererState.frames[rendererState.frameIndex].commandBuffer;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
            ano_olog(ANO_ERROR, "Failed to begin recording command buffer!");
    }

    // Transition swapchain image to color attachment optimal.
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
    // tpsort rewrites the transmission partition back-to-front from cull's compacted draws + depth keys, one workgroup per view.
    if (entityCount > 0 && ano_draw_slot_of(PIPELINE_TRANSMISSION) != ANO_NO_DRAW_SLOT) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_TPSORT].implementations[0].pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_TPSORT].layout, 0, 1,
            &rendererState.frames[rendererState.frameIndex].cullSet, 0, NULL);
        vkCmdDispatch(cmd, ANO_VIEW_COUNT, 1, 1); // one workgroup per camera view

        // Sort writes -> the geometry stage's indirect + SSBO reads in the per-view transmission pass below.
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

    // Text overlay raster (in-frame path) clears, dispatches, and hands the overlay to the composite.
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
