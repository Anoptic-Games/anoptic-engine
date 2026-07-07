/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>
#include <anoptic_logging.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/frame/frame.h"

// Submit this frame's recorded command buffers in the exact required order: the graphics submit
// (plain, or the async-light-cull split prelude+main two-batch), then the async light-cull compute
// submit, then the async Hi-Z compute submit. ordinal = this submit's 1-based timeline value.
// Extracted verbatim from drawFrame; the graphics-submit failure returns became `return false` so
// drawFrame can skip present (present waits renderFinished == the graphics submit signal).
bool ano_frame_submit(uint64_t ordinal)
{
	VkSemaphore signalSemaphores[2] = {rendererState.frames[rendererState.frameIndex].renderFinished, rendererState.gfxTimeline};
	uint64_t signalValues[2] = {0, ordinal};
	uint64_t hizWaitValue = ordinal > 2u ? ordinal - 2u : 0u;
	vkResetFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence)); // this goes here because multi-threading
	if (!rendererState.asyncLc)
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSemaphore waitSemaphores[3] = {rendererState.frames[rendererState.frameIndex].imageAvailable,
			rendererState.hizTimeline, rendererState.textTimeline};
		// Task meshlet cull samples the async-built pyramids (global set binding 13), joins the
		// hizTimeline wait when active. The async text raster joins at FRAGMENT_SHADER.
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
		// Async light-cull split (finding 2 remainder), one atomic two-batch submit + fence.
		// A (prelude CB): waits hizTimeline >= ordinal-2 at COMPUTE (the cull samples the lag-2
		// pyramids) and lcTimeline >= ordinal-1 at TRANSFER (the lightBuffer copy must not
		// overwrite the PRIOR frame's compute-queue light-cull read — the in-queue reach-back
		// barrier cannot see the other queue); signals preludeTimeline = ordinal.
		// B (main CB): waits imageAvailable at COLOR_OUT, hizTimeline >= ordinal-2 at
		// EARLY_FRAGMENT_TESTS (depth-resolve WAR anchor), and lcTimeline == ordinal at
		// FRAGMENT_SHADER (first froxel-list consumer; timeline waits may be submitted before
		// their signal); signals renderFinished + gfxTimeline = ordinal as before.
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
		// hizTimeline @ EARLY_FRAG (depth-resolve WAR) + TASK when the meshlet cull samples the
		// async-built pyramids in this submit's geometry passes (global set binding 13).
		// textTimeline == ordinal @ FRAGMENT.
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

		// Light-cull compute submit: released by this frame's prelude, consumed by its main submit —
		// it executes DURING the shadow region on the dedicated queue.
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
			// Keep the timeline monotonic so the main submit's wait cannot deadlock (the frame keeps
			// the previous froxel lists); a failed submit here is device-loss territory anyway.
			ano_log(ANO_ERROR, "Failed to submit async light-cull command buffer!");
			VkSemaphoreSignalInfo signalInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
				.semaphore = rendererState.lcTimeline, .value = ordinal };
			vkSignalSemaphore(ctx.device, &signalInfo);
		}
	}
	rendererState.timelineOrdinal = ordinal;

	// Async Hi-Z compute submit (review finding 2): waits this frame's graphics (gfxTimeline ==
	// ordinal: depth resolves done, prior readers of the slots being rewritten retired), signals
	// hizTimeline == ordinal for the ordinal+2 graphics submit. Executes during the NEXT frame's
	// graphics on the dedicated queue — off the critical path.
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
			// Keep the timeline monotonic so the ordinal+2 graphics wait cannot deadlock; a failed
			// submit here is device-loss territory anyway.
			ano_log(ANO_ERROR, "Failed to submit async Hi-Z command buffer!");
			VkSemaphoreSignalInfo signalInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
				.semaphore = rendererState.hizTimeline, .value = ordinal };
			vkSignalSemaphore(ctx.device, &signalInfo);
		}
	}

	return true;
}
