/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>
#include <anoptic_log.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/frame/frame.h"

// Submit this frame's command buffers in order, graphics (plain or async-light-cull split
// prelude+main two-batch), async light-cull compute, async Hi-Z compute. ordinal = 1-based timeline value.
bool ano_frame_submit(uint64_t ordinal)
{
	VkSemaphore signalSemaphores[2] = {rendererState.frames[rendererState.frameIndex].renderFinished, rendererState.gfxTimeline};
	uint64_t signalValues[2] = {0, ordinal};
	uint64_t hizWaitValue = ordinal > 2u ? ordinal - 2u : 0u;
	vkResetFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence));
	if (!rendererState.asyncLc)
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSemaphore waitSemaphores[3] = {rendererState.frames[rendererState.frameIndex].imageAvailable,
			rendererState.hizTimeline, rendererState.textTimeline};
		// Task cull joins hizTimeline wait when active. Async text @ FRAGMENT_SHADER.
		VkPipelineStageFlags waitStages[3] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
				| (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0),
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
		uint64_t waitValues[3] = {0, hizWaitValue, ordinal};
		uint32_t waitCount = rendererState.asyncText ? 3u : (rendererState.asyncHiz ? 2u : 1u);
		VkTimelineSemaphoreSubmitInfo timelineValues = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
			.waitSemaphoreValueCount = waitCount, .pWaitSemaphoreValues = waitValues,
			.signalSemaphoreValueCount = 2, .pSignalSemaphoreValues = signalValues };
		if (rendererState.asyncHiz)
			submitInfo.pNext = &timelineValues;
		submitInfo.waitSemaphoreCount = waitCount;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &(rendererState.frames[rendererState.frameIndex].commandBuffer);
		submitInfo.signalSemaphoreCount = rendererState.asyncHiz ? 2 : 1;
		submitInfo.pSignalSemaphores = signalSemaphores;
		if (vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, rendererState.frames[rendererState.frameIndex].frameFence) != VK_SUCCESS)
		{
			ano_log(ANO_ERROR, "Failed to submit draw command buffer!");
			return false;
		}
	}
	else
	{
		// Async LC split, one two-batch submit + fence.
		// A prelude: wait hiz>=ord-2 @COMPUTE, lc>=ord-1 @TRANSFER; signal prelude=ord.
		// B main: wait imageAvail @COLOR_OUT, hiz>=ord-2 @EARLY_FRAG, lc==ord @FRAGMENT; signal renderFinished+gfx.
		uint64_t lcPrevValue = ordinal > 1u ? ordinal - 1u : 0u;
		VkSemaphore aWaitSems[2] = { rendererState.hizTimeline, rendererState.lcTimeline };
		VkPipelineStageFlags aWaitStages[2] = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
		uint64_t aWaitValues[2] = { hizWaitValue, lcPrevValue };
		VkTimelineSemaphoreSubmitInfo aTimeline = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
			.waitSemaphoreValueCount = 2, .pWaitSemaphoreValues = aWaitValues,
			.signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &ordinal };
		VkSubmitInfo submits[2] = {};
		submits[0].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submits[0].pNext = &aTimeline;
		submits[0].waitSemaphoreCount = 2;
		submits[0].pWaitSemaphores = aWaitSems;
		submits[0].pWaitDstStageMask = aWaitStages;
		submits[0].commandBufferCount = 1;
		submits[0].pCommandBuffers = &(rendererState.frames[rendererState.frameIndex].preludeCommandBuffer);
		submits[0].signalSemaphoreCount = 1;
		submits[0].pSignalSemaphores = &rendererState.preludeTimeline;

		VkSemaphore bWaitSems[4] = { rendererState.frames[rendererState.frameIndex].imageAvailable,
			rendererState.hizTimeline, rendererState.lcTimeline, rendererState.textTimeline };
		// hizTimeline @ EARLY_FRAG + TASK for meshlet cull. textTimeline @ FRAGMENT.
		VkPipelineStageFlags bWaitStages[4] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
				| (rendererState.taskCull ? VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT : 0),
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };
		uint64_t bWaitValues[4] = { 0, hizWaitValue, ordinal, ordinal };
		uint32_t bWaitCount = rendererState.asyncText ? 4u : 3u;
		VkTimelineSemaphoreSubmitInfo bTimeline = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
			.waitSemaphoreValueCount = bWaitCount, .pWaitSemaphoreValues = bWaitValues,
			.signalSemaphoreValueCount = 2, .pSignalSemaphoreValues = signalValues };
		submits[1].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submits[1].pNext = &bTimeline;
		submits[1].waitSemaphoreCount = bWaitCount;
		submits[1].pWaitSemaphores = bWaitSems;
		submits[1].pWaitDstStageMask = bWaitStages;
		submits[1].commandBufferCount = 1;
		submits[1].pCommandBuffers = &(rendererState.frames[rendererState.frameIndex].commandBuffer);
		submits[1].signalSemaphoreCount = 2;
		submits[1].pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(ctx.graphicsQueue, 2, submits, rendererState.frames[rendererState.frameIndex].frameFence) != VK_SUCCESS)
		{
			ano_log(ANO_ERROR, "Failed to submit draw command buffers!");
			return false;
		}

		// Light-cull compute submit, released by this frame's prelude, consumed by its main submit.
		VkPipelineStageFlags lcWaitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		VkTimelineSemaphoreSubmitInfo lcTimelineInfo = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
			.waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &ordinal,
			.signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &ordinal };
		VkSubmitInfo lcSubmit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &lcTimelineInfo,
			.waitSemaphoreCount = 1, .pWaitSemaphores = &rendererState.preludeTimeline, .pWaitDstStageMask = &lcWaitStage,
			.commandBufferCount = 1, .pCommandBuffers = &rendererState.frames[rendererState.frameIndex].lightcullCommandBuffer,
			.signalSemaphoreCount = 1, .pSignalSemaphores = &rendererState.lcTimeline };
		if (vkQueueSubmit(ctx.computeQueue, 1, &lcSubmit, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			// Force-signal to keep the timeline monotonic.
			ano_log(ANO_ERROR, "Failed to submit async light-cull command buffer!");
			VkSemaphoreSignalInfo signalInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
				.semaphore = rendererState.lcTimeline, .value = ordinal };
			vkSignalSemaphore(ctx.device, &signalInfo);
		}
	}
	rendererState.timelineOrdinal = ordinal;

	// Async Hi-Z compute submit, waits gfxTimeline == ordinal, signals hizTimeline == ordinal for the ordinal+2 graphics submit.
	if (rendererState.asyncHiz)
	{
		VkPipelineStageFlags hizWaitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		VkTimelineSemaphoreSubmitInfo hizValues = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
			.waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &ordinal,
			.signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &ordinal };
		VkSubmitInfo hizSubmit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &hizValues,
			.waitSemaphoreCount = 1, .pWaitSemaphores = &rendererState.gfxTimeline, .pWaitDstStageMask = &hizWaitStage,
			.commandBufferCount = 1, .pCommandBuffers = &rendererState.frames[rendererState.frameIndex].computeCommandBuffer,
			.signalSemaphoreCount = 1, .pSignalSemaphores = &rendererState.hizTimeline };
		if (vkQueueSubmit(ctx.computeQueue, 1, &hizSubmit, VK_NULL_HANDLE) != VK_SUCCESS)
		{
			// Force-signal to keep the timeline monotonic.
			ano_log(ANO_ERROR, "Failed to submit async Hi-Z command buffer!");
			VkSemaphoreSignalInfo signalInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
				.semaphore = rendererState.hizTimeline, .value = ordinal };
			vkSignalSemaphore(ctx.device, &signalInfo);
		}
	}

	return true;
}
