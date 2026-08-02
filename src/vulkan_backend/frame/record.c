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
#include "vulkan_backend/frame/pass_schema.h"

static constexpr auto frame_pass_enumerators =
    std::define_static_array(std::meta::enumerators_of(^^AnoFramePass));

template<AnoFramePass Pass>
static inline void record_shared_compute_pass(VkCommandBuffer cmd, uint32_t entityCount,
                                              uint32_t streamCount, uint32_t lightCount)
{
    constexpr RenderPassDef pass = ano_frame_pass<Pass>();
    constexpr PipelineType Prototype = pass.prototype;
    static_assert(pass.type == PASS_COMPUTE && !pass.perView);
    static_assert(pass.dispatchY == 0 && pass.dispatchZ == 0);
    static_assert(Prototype == PIPELINE_COMPUTE_UPDATE
                  || Prototype == PIPELINE_COMPUTE_SCATTER
                  || Prototype == PIPELINE_COMPUTE_LIGHTSETUP
                  || Prototype == PIPELINE_COMPUTE_SHADOWSETUP
                  || Prototype == PIPELINE_COMPUTE_CULL);

    if constexpr (Prototype == PIPELINE_COMPUTE_SCATTER)
        if (streamCount == 0)
            return;

    if constexpr (Prototype == PIPELINE_COMPUTE_CULL) {
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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        rendererState.prototypes[Prototype].implementations[pass.implementationIndex].pipeline);

    VkDescriptorSet set;
    if constexpr (Prototype == PIPELINE_COMPUTE_UPDATE)
        set = rendererState.frames[rendererState.frameIndex].updateSet;
    else if constexpr (Prototype == PIPELINE_COMPUTE_SCATTER)
        set = rendererState.frames[rendererState.frameIndex].scatterSet;
    else if constexpr (Prototype == PIPELINE_COMPUTE_SHADOWSETUP)
        set = rendererState.frames[rendererState.frameIndex].shadow.setupSet;
    else if constexpr (Prototype == PIPELINE_COMPUTE_LIGHTSETUP)
        set = rendererState.frames[rendererState.frameIndex].lightsetupSet;
    else
        set = rendererState.frames[rendererState.frameIndex].cullSet;

    uint32_t dynCount = 0;
    const uint32_t* dynOff = NULL;
    if constexpr (Prototype == PIPELINE_COMPUTE_SCATTER) {
        dynCount = 1;
        dynOff = &rendererState.transformStream.dynOffset[rendererState.frameIndex];
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        rendererState.prototypes[Prototype].layout, 0, 1, &set, dynCount, dynOff);

    if constexpr (Prototype == PIPELINE_COMPUTE_UPDATE)
        vkCmdPushConstants(cmd, rendererState.prototypes[Prototype].layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &entityCount);
    else if constexpr (Prototype == PIPELINE_COMPUTE_SCATTER)
        vkCmdPushConstants(cmd, rendererState.prototypes[Prototype].layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &streamCount);
    else if constexpr (Prototype == PIPELINE_COMPUTE_LIGHTSETUP)
        vkCmdPushConstants(cmd, rendererState.prototypes[Prototype].layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &lightCount);

    uint32_t dispatchX;
    if constexpr (Prototype == PIPELINE_COMPUTE_SHADOWSETUP)
        dispatchX = (ANO_SHADOW_FRUSTUM_COUNT + 63u) / 64u;
    else if constexpr (Prototype == PIPELINE_COMPUTE_LIGHTSETUP)
        dispatchX = (lightCount + 63u) / 64u;
    else {
        const uint32_t workItems = Prototype == PIPELINE_COMPUTE_SCATTER
            ? streamCount : entityCount;
        if constexpr (pass.dispatchX == 0)
            dispatchX = (workItems + 255u) / 256u;
        else
            dispatchX = pass.dispatchX;
    }
    vkCmdDispatch(cmd, dispatchX, 1, 1);

    VkMemoryBarrier memoryBarrier = {};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    if constexpr (Prototype == PIPELINE_COMPUTE_SHADOWSETUP) {
        VkPipelineStageFlags geomStage = (ctx.deviceCapabilities.meshShader
            ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
            | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0);
        memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | geomStage | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 1, &memoryBarrier, 0, NULL, 0, NULL);
    } else if constexpr (Prototype == PIPELINE_COMPUTE_LIGHTSETUP) {
        // Shadow setup carries the shared barrier for these disjoint writes.
    } else if constexpr (Prototype == PIPELINE_COMPUTE_UPDATE
                         || Prototype == PIPELINE_COMPUTE_SCATTER) {
        memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memoryBarrier, 0, NULL, 0, NULL);
    } else {
        VkPipelineStageFlags geomStage = (ctx.deviceCapabilities.meshShader
            ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)
            | (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0);
        memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT
            | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | geomStage | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &memoryBarrier, 0, NULL, 0, NULL);
    }
}

// Record this frame slot's command buffer(s). imageIndex = acquired swapchain image.
// False on begin/end refusal. No vkCmd* without a begun buffer.
bool recordCommandBuffer(uint32_t imageIndex)
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
		return false;
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
    // Cull: single-pass multi-frustum across all views.
    if (entityCount > 0) {
        uint32_t streamCount = rendererState.transformStream.count[rendererState.frameIndex];
        uint32_t lightCount = rendererState.lightBuffer.count; // active light rows
        template for (constexpr auto reflectedPass : frame_pass_enumerators) {
            constexpr AnoFramePass passId = [:reflectedPass:];
            if constexpr (passId != ANO_FRAME_PASS_COUNT) {
                constexpr RenderPassDef pass = ano_frame_pass<passId>();
                if constexpr (pass.type == PASS_COMPUTE && !pass.perView)
                    record_shared_compute_pass<passId>(
                        cmd, entityCount, streamCount, lightCount);
            }
        }
    }

    uint32_t drawSlotCount = ano_draw_pipeline_count();

    ano_ts(cmd, ANO_TS_AFTER_COMPUTE);

    // End prelude CB. Rest records into the main CB.
    if (rendererState.asyncLc) {
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            ano_log(ANO_ERROR, "Failed to record prelude command buffer!");
            return false;
        }
        cmd = rendererState.frames[rendererState.frameIndex].commandBuffer;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            ano_olog(ANO_ERROR, "Failed to begin recording command buffer!");
            return false;
        }
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
    // tpsort: compacted draws + depth keys, one workgroup per view.
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
		return false;
	}

	return true;
}
