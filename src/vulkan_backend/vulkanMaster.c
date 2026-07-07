/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include <stdio.h>
#include <math.h>
#include <vulkan/vulkan.h>
#include <anoptic_memory.h>
#include <anoptic_logging.h>
#include <anoptic_time.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/light_registry.h"
#include "vulkan_backend/shadow/shadow.h"
#include "vulkan_backend/scene_buffers.h"
#include "vulkan_backend/bridge/bridge.h"
#include "vulkan_backend/render_api.h"

#define GLFW_INCLUDE_VULKAN

// Variables

// Promoted from file-static to a plain global (declared extern in backend.h) so the
// split backend TUs can reach it. See backend.h for the shadowing note on instanceInit.c.
VulkanContext ctx;
PFN_vkCmdDrawMeshTasksEXT pfnVkCmdDrawMeshTasksEXT = NULL;
PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT = NULL;
PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT = NULL;
RendererState rendererState;
GpuAllocator gpuAllocator;
GpuAllocator stagingAllocator;
GpuAllocator swapchainAllocator;
GpuAllocator textureAllocator;

struct VulkanGarbage vulkanGarbage = { NULL, NULL, NULL}; // THROW OUT WHEN YOU'RE DONE WITH IT

static GLFWwindow* window;

static Monitors monitors =
{
	.monitorInfos = NULL,	// Array of MonitorInfo for each monitor
	.monitorCount = 0		// Total number of monitors
};


// Assorted utility functions

void unInitVulkan() // A celebration
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		if (rendererState.frames[i].frameSubmitted)
		{
			vkWaitForFences(ctx.device, 1, &(rendererState.frames[i].frameFence), VK_TRUE, UINT64_MAX);
			rendererState.frames[i].frameSubmitted = false; // reset the status
		}
    }

	// Async Hi-Z / light-cull / text: drain last signaled ordinals
	// before teardown destroys the pool/semaphores/images.
	if (rendererState.asyncHiz && rendererState.hizTimeline != VK_NULL_HANDLE
		&& ctx.device != VK_NULL_HANDLE && rendererState.timelineOrdinal > 0)
	{
		uint64_t last[3] = { rendererState.timelineOrdinal, rendererState.timelineOrdinal,
			rendererState.timelineOrdinal };
		VkSemaphore sems[3] = { rendererState.hizTimeline, VK_NULL_HANDLE, VK_NULL_HANDLE };
		uint32_t n = 1u;
		if (rendererState.asyncLc && rendererState.lcTimeline != VK_NULL_HANDLE)
			sems[n++] = rendererState.lcTimeline;
		if (rendererState.asyncText && rendererState.textTimeline != VK_NULL_HANDLE)
			sems[n++] = rendererState.textTimeline;
		VkSemaphoreWaitInfo waitInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = n, .pSemaphores = sems, .pValues = last };
		vkWaitSemaphores(ctx.device, &waitInfo, UINT64_MAX);
	}

	// ECS<->render bridge teardown. CPU-only; safe on a zeroed state (early-init
	// failure) since the destroys guard NULL and the heap free is gated below.
	ano_render_bridge_destroy(&rendererState.bridge);
	render_slots_destroy(&rendererState.slots);
	light_registry_destroy(&rendererState.lightRegistry);
	if (rendererState.renderHeap)
	{
		mi_heap_destroy(rendererState.renderHeap);
		rendererState.renderHeap = NULL;
	}

	if (vulkanGarbage.ctx)
	{
		cleanupVulkan(vulkanGarbage.ctx);
	}
	
	if (vulkanGarbage.window)
	{
		glfwDestroyWindow(vulkanGarbage.window);
		glfwTerminate();
	}

	if (vulkanGarbage.monitors)
	{
		cleanupMonitors(vulkanGarbage.monitors);
	}

	#ifdef DEBUG_BUILD
	// Validation summary: g_ValidationErrors counts every WARNING+ message routed to
	// debugCallback over the run. 0 == validation-clean; nonzero is a regression to chase.
	ano_log(ANO_INFO, "Validation messages (warning+) this run: %u", g_ValidationErrors);
	#endif
}

bool anoShouldClose()
{
	return glfwWindowShouldClose(window);
}

void deferred_delete_resource(RendererState* state, DeletionResourceType type, uint32_t handle)
{
    uint32_t frameIdx = rendererState.frameIndex;
    DeletionQueue* q = &state->frames[frameIdx].deletionQueue;

    if (q->count >= q->capacity) {
        q->capacity = q->capacity == 0 ? 64 : q->capacity * 2;
        q->tasks = realloc(q->tasks, q->capacity * sizeof(DeletionTask));
    }

    q->tasks[q->count++] = (DeletionTask){ .type = type, .handle = handle };
}

void flush_deletion_queue(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
    DeletionQueue* q = &state->frames[frameIndex].deletionQueue;

    for (uint32_t i = 0; i < q->count; i++) {
        DeletionTask task = q->tasks[i];
        switch (task.type) {
            case RESOURCE_TYPE_GEOMETRY_MESH:
                geometry_pool_free(&state->globalGeometryPool, task.handle);
                break;
            case RESOURCE_TYPE_BINDLESS_TEXTURE:
                // For now bindless_register_texture handles index. 
                // We'd add a bindless_free_texture(ctx, &state->bindlessTextures, task.handle) eventually
                break;
        }
    }
    q->count = 0; // Clear queue for next time we hit this frame
}

// Graphics operations







void drawFrame()
{
	if (rendererState.framebufferResized)
	{
		rendererState.framebufferResized = false;
		recreateSwapChain(&ctx, window);
		return;
	}

    // Discrete ECS->render transitions arrive via the bridge from the logic
    // thread; they are drained in render_apply_commands (below). No producer runs
    // on this thread anymore.

    if (rendererState.frames[rendererState.frameIndex].frameSubmitted == true)
    {
        vkWaitForFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence), VK_TRUE, UINT64_MAX);

        // This slot's prior submission is complete: its per-pass timestamps are ready to read.
        ano_collect_frame_stats(rendererState.frameIndex);

        // Same fence-gated readback for picking: this slot's id-texel copy has retired.
        ano_collect_pick(rendererState.frameIndex);

        // Reclaim streamed-transform ring slices the GPU is finished reading. Hold-last-
        // value means several in-flight frames can share one seq (one slice), so this
        // slot completing does NOT free its slice — a sibling may still read it. The safe
        // bound is the OLDEST still-in-flight frame's seq: after we wait slot frameIndex,
        // that is slot (frameIndex+1) % N (the next to be reused). Everything strictly
        // below its seq is unreferenced.
        uint32_t oldest = (rendererState.frameIndex + 1u) % MAX_FRAMES_IN_FLIGHT;
        uint64_t qmin = rendererState.transformStream.frameSeq[oldest];
        atomic_store_explicit(&rendererState.transformStream.reclaimSeq,
                              qmin ? qmin - 1u : 0u, memory_order_release);
    }

    // Process any deferred deletions that were waiting for this frame's previous commands to finish
    flush_deletion_queue(&ctx, &rendererState, rendererState.frameIndex);
    
	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(ctx.device, rendererState.swapChain, UINT64_MAX, rendererState.frames[rendererState.frameIndex].imageAvailable, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) 
	{
		recreateSwapChain(&ctx, window);
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) 
	{
		ano_log(ANO_ERROR, "Failed to acquire swap chain image!");
		return;
	}

	updateUniformBuffer(&ctx, &rendererState);

	// Update entity transforms
	// float moveOffsets[3] = {2.0f, -2.0f, 0.0f};
	// for (uint32_t i = 0; i < rendererState.entityCount && i < 3; i++) {
		//updateMeshTransforms(&ctx, &rendererState.entities[i], moveOffsets[i]);
	// }

	updateTransformBuffer(&ctx, &rendererState, rendererState.frameIndex);
	updateCullingBuffers(&ctx, &rendererState, rendererState.frameIndex);

	// Ingest discrete ECS->render state transitions for this frame slot.
	render_apply_commands(&rendererState, rendererState.frameIndex);

	// Copy pending on-screen text into this slot's frame buffer (post-fence).
	// Must precede record and async submit.
	ano_vk_text_frame_refresh(&rendererState, rendererState.frameIndex);

	vkResetCommandBuffer(rendererState.frames[rendererState.frameIndex].commandBuffer, 0);
	if (rendererState.asyncLc)
		vkResetCommandBuffer(rendererState.frames[rendererState.frameIndex].preludeCommandBuffer, 0);
	recordCommandBuffer(imageIndex);

	//updateUniformBuffer(&ctx, &rendererState);

	// Async Hi-Z (review finding 2): 1-based ordinal of THIS submit. Monotonic across swapchain
	// recreates (unlike frame slots/fences), so timeline signal values never repeat.
	uint64_t ordinal = rendererState.timelineOrdinal + 1u;
	if (rendererState.asyncHiz)
	{
		// This slot's compute CB trails its graphics submit and is not covered by frameFence:
		// host-wait its own prior signal (MAX_FRAMES_IN_FLIGHT ordinals back) before re-recording.
		if (ordinal > (uint64_t)MAX_FRAMES_IN_FLIGHT)
		{
			uint64_t prior = ordinal - (uint64_t)MAX_FRAMES_IN_FLIGHT;
			VkSemaphoreWaitInfo waitInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.semaphoreCount = 1, .pSemaphores = &rendererState.hizTimeline, .pValues = &prior };
			vkWaitSemaphores(ctx.device, &waitInfo, UINT64_MAX);
		}
		recordHiZCompute(rendererState.frameIndex);
	}
	// Async light-cull CB for this frame (reuse is fence-safe — see recordLightcullCompute).
	if (rendererState.asyncLc)
		recordLightcullCompute(rendererState.frameIndex);

	// Async text raster: submits first with no waits, overlaps the
	// graphics frame. The main submit waits textTimeline == ordinal at FRAGMENT_SHADER.
	ano_vk_text_submit_async(&ctx, &rendererState, rendererState.frameIndex, ordinal);

	// Graphics submit. Async Hi-Z adds a second wait — hizTimeline >= ordinal-2 at the cull's
	// COMPUTE stage (the lag-2 pyramid it samples) | EARLY_FRAGMENT_TESTS (chain anchor for the
	// depth-resolve WAR flip in recordCommandBuffer) — and a second signal, gfxTimeline = ordinal
	// (waited by this frame's compute build). Values on binary semaphores are ignored per spec.
	// renderFinished/gfxTimeline are signaled by whichever submit ends the frame (present waits
	// signalSemaphores[0]).
	if (!ano_frame_submit(ordinal)) return;

    // Presentation should happen *before* submitting commands for a new frame, so we're actually taking advantage of buffering

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	
	presentInfo.waitSemaphoreCount = 1;
	// Wait the graphics submit's frame-end signal (== the former signalSemaphores[0], now internal
	// to ano_frame_submit): renderFinished for this frame slot.
	presentInfo.pWaitSemaphores = &rendererState.frames[rendererState.frameIndex].renderFinished;
	VkSwapchainKHR swapChains[] = {rendererState.swapChain};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = NULL; // Optional

	VkResult presentResult = vkQueuePresentKHR(ctx.presentQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
	{
		recreateSwapChain(&ctx, window);
		return;
	} else if (presentResult != VK_SUCCESS)
	{
		ano_log(ANO_ERROR, "Failed to present swap chain image!");
		return;
	}

	rendererState.frames[rendererState.frameIndex].frameSubmitted = true;

	//printUniformTransferState();

	rendererState.frameIndex += 1; // Iterate and reset the frame-in-flight index
	if (rendererState.frameIndex == MAX_FRAMES_IN_FLIGHT)
	{
		rendererState.frameIndex = 0;
	}
	rendererState.globalFrame += 1; // monotonic; gates slot-quarantine retirement
}


bool initVulkan() // Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
{

	// Window initialization
	Dimensions2D initDimensions = {800, 600};
	setResolution(initDimensions);
	setMonitor(-1);
	setBorderless(0);

    vulkanGarbage.monitors = &monitors;
    cleanupMonitors(&monitors);
    enumerateMonitors(&monitors);

	window = initWindow(&ctx, &monitors);

	if (window == NULL)
	{
		// Handle error
		ano_log(ANO_FATAL, "Window initialization failed.");
		unInitVulkan();
		return 0;
	}

	requestPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);

	ctx.enableValidationLayers = true;
	rendererState.frameIndex = 0; // Tracks which frame is being processed

	// Initialize Vulkan
	if (createInstance(&ctx) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create Vulkan instance!");
		unInitVulkan();
		return false;
	}
	vulkanGarbage.ctx = &ctx;

	// Create a window surface
	if (createSurface(ctx.instance, window, &(ctx.surface)) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Failed to create window surface!");
		unInitVulkan();
		return false;
	}

	// Pick physical device
	DeviceCapabilities capabilities;
	ctx.physicalDevice = VK_NULL_HANDLE;

	char* preferredDevice = getChosenDevice();
	if (!pickPhysicalDevice(&ctx, &capabilities, &(ctx.queueFamilyIndices), preferredDevice))
	{
		ano_log(ANO_FATAL, "Quitting init: physical device failure!");
		unInitVulkan();
		return false;
	}
	

	if (createLogicalDevice(ctx.physicalDevice, &(ctx.device), &(ctx.graphicsQueue), &(ctx.computeQueue), &(ctx.transferQueue), &(ctx.presentQueue), &(ctx.queueFamilyIndices)) != VK_SUCCESS)
	{
		ano_log(ANO_FATAL, "Quitting init: logical device failure!");
		unInitVulkan();
		return false;
	}

	// Async Hi-Z gate (review finding 2). Needs a compute queue on a family DISTINCT from graphics
	// (findQueueFamilies prefers a dedicated one; same-family means the very same VkQueue — no
	// overlap possible), timeline semaphores for the cross-queue ordering, and the single-sample MAX
	// depth resolve (the raster resolve stays on graphics; only the pyramid reduce/downsample moves,
	// so the compute CB never needs graphics-only stages). ANO_FORCE_NO_ASYNC_HIZ pins the in-frame
	// build for A/B. Must be decided before depth/Hi-Z resources (sharing mode + rest layouts) and
	// createSyncObjects (timelines) run.
	rendererState.asyncHiz = ctx.deviceCapabilities.timelineSemaphore
	                      && ctx.deviceCapabilities.depthMaxResolve
	                      && ctx.queueFamilyIndices.computePresent
	                      && ctx.queueFamilyIndices.computeFamily != ctx.queueFamilyIndices.graphicsFamily
	                      && !getenv("ANO_FORCE_NO_ASYNC_HIZ");
	ano_log(ANO_INFO, "Async Hi-Z build: %s", rendererState.asyncHiz ? "on (dedicated compute queue)" : "off (in-frame)");

	// Async light-cull gate (review finding 2 remainder): rides asyncHiz's infrastructure (dedicated
	// compute queue, compute command pool, timeline support), so forcing the Hi-Z build in-frame also
	// falls this back. ANO_FORCE_NO_ASYNC_LC pins the in-frame light-cull alone for A/B. Must be
	// decided before buffer creation (CONCURRENT sharing) and createSyncObjects (timelines).
	rendererState.asyncLc = rendererState.asyncHiz && !getenv("ANO_FORCE_NO_ASYNC_LC");
	ano_log(ANO_INFO, "Async light-cull: %s", rendererState.asyncLc ? "on (split submit, overlaps shadows)" : "off (in-frame)");

	// Task-shader meshlet cull gate (review priority 10). Flips together: the TASK stage in every
	// mesh-drawing pipeline + its layouts/pushes/barriers, and cull.comp's indirect groupCountX
	// convention (CullUBO.taskParams). ANO_FORCE_NO_TASK pins direct mesh dispatch for A/B. Must be
	// decided before the descriptor layouts and pipelines are built.
	rendererState.taskCull = ctx.deviceCapabilities.meshShader
	                      && ctx.deviceCapabilities.taskShader
	                      && !getenv("ANO_FORCE_NO_TASK");
	ano_log(ANO_INFO, "Task meshlet cull: %s", rendererState.taskCull ? "on (frustum+cone, Hi-Z with occlusion toggle)" : "off (direct mesh dispatch)");

	// Text overlay gate. A later font/bake init failure clears it non-fatally.
	// ANO_FORCE_NO_TEXT pins it off.
	rendererState.textOverlay = !getenv("ANO_FORCE_NO_TEXT");
	ano_log(ANO_INFO, "Text overlay: %s", rendererState.textOverlay ? "enabled (pending font init)" : "off (forced)");

	// Async text lane gate: rides asyncHiz's infrastructure.
	// ano_vk_text_init downgrades it non-fatally if the lane's objects fail.
	// ANO_FORCE_NO_ASYNC_TEXT pins the in-frame raster.
	rendererState.asyncText = rendererState.textOverlay && rendererState.asyncHiz
	                       && !getenv("ANO_FORCE_NO_ASYNC_TEXT");
	ano_log(ANO_INFO, "Async text raster: %s", rendererState.asyncText ? "on (lag-0 compute lane)" : "off (in-frame)");

    // Mesh-shader entry points only exist on the mesh path. The fallback path draws
    // via core vkCmdDrawIndexedIndirect[Count] and needs none of these.
    if (ctx.deviceCapabilities.meshShader) {
        pfnVkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksEXT");
        pfnVkCmdDrawMeshTasksIndirectEXT = (PFN_vkCmdDrawMeshTasksIndirectEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksIndirectEXT");
        pfnVkCmdDrawMeshTasksIndirectCountEXT = (PFN_vkCmdDrawMeshTasksIndirectCountEXT)vkGetDeviceProcAddr(ctx.device, "vkCmdDrawMeshTasksIndirectCountEXT");

        if (!pfnVkCmdDrawMeshTasksEXT || !pfnVkCmdDrawMeshTasksIndirectEXT || !pfnVkCmdDrawMeshTasksIndirectCountEXT) {
            ano_log(ANO_FATAL, "Failed to load mesh shader extension function pointers!");
            unInitVulkan();
            return false;
        }
    }

	gpuAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &gpuAllocator.memProps);
	gpuAllocator.blocks = NULL;
	gpuAllocator.blockCount = 0;
	stagingAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &stagingAllocator.memProps);
	stagingAllocator.blocks = NULL;
	stagingAllocator.blockCount = 0;

	swapchainAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &swapchainAllocator.memProps);
	swapchainAllocator.blocks = NULL;
	swapchainAllocator.blockCount = 0;

	textureAllocator.device = ctx.device;
	vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &textureAllocator.memProps);
	textureAllocator.blocks = NULL;
	textureAllocator.blockCount = 0;

	if (!ano_vk_init_geometry_pool(&rendererState.globalGeometryPool, &gpuAllocator, ctx.device, ctx.queueFamilyIndices.graphicsFamily, ctx.queueFamilyIndices.transferFamily)) {
		ano_log(ANO_FATAL, "Quitting init: geometry pool creation failure!");
		unInitVulkan();
		return false;
	}

	initSwapChain(&ctx, window, getChosenPresentMode(), VK_NULL_HANDLE, &rendererState); // Initialize a swap chain
	if (rendererState.swapChain == NULL)
	{
		ano_log(ANO_FATAL, "Quitting init: swap chain failure.");
		unInitVulkan();
		return false;
	}
	
	createImageViews(&ctx, &rendererState);
	if (rendererState.views == NULL)
	{
		ano_log(ANO_FATAL, "Quitting init: image view failure.");
		unInitVulkan();
		return false;
	}

	if (!createCommandPool(ctx.device, ctx.physicalDevice,
						   ctx.surface, &(rendererState.commandPool)))
	{
		ano_log(ANO_FATAL, "Quitting init: command pool failure!");
		unInitVulkan();
		return false;
	}

	// Async Hi-Z build CBs (review finding 2): the graphics-family pool is invalid on the compute
	// queue, so the per-frame build CBs come from their own compute-family pool.
	if (rendererState.asyncHiz)
	{
		VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = ctx.queueFamilyIndices.computeFamily };
		if (vkCreateCommandPool(ctx.device, &cpi, NULL, &rendererState.computeCommandPool) != VK_SUCCESS)
		{
			ano_log(ANO_FATAL, "Quitting init: compute command pool failure!");
			unInitVulkan();
			return false;
		}
		VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = rendererState.computeCommandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			if (vkAllocateCommandBuffers(ctx.device, &cai, &rendererState.frames[i].computeCommandBuffer) != VK_SUCCESS
				|| (rendererState.asyncLc
					&& vkAllocateCommandBuffers(ctx.device, &cai, &rendererState.frames[i].lightcullCommandBuffer) != VK_SUCCESS))
			{
				ano_log(ANO_FATAL, "Quitting init: compute command buffer allocation failure!");
				unInitVulkan();
				return false;
			}
		}
	}

	createColorResources(&ctx); // Make this a bool and add check

	if(!createDepthResources(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: depth resource creation failure!");
	}

	// Hi-Z occlusion pyramid images (review 4.9 step 3). Built each frame from depth; the per-mip
	// descriptor sets are allocated/written after the layout + pool exist (below).
	if(!createHiZResources(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: Hi-Z resource creation failure!");
	}



	if (!ano_vk_init_global_layout(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: global layout failure!");
		unInitVulkan();
		return false;
	}
	if (!ano_vk_init_cull_layout(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: cull layout failure!");
		unInitVulkan();
		return false;
	}
	if (!ano_vk_init_material_layouts(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: material layouts failure!");
		unInitVulkan();
		return false;
	}

	if (!createBindlessTextureArray(&ctx, &rendererState))
	{
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_pipelines(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: pipeline failure!");
		unInitVulkan();
		return false;
	}

	if (!ano_vk_init_tonemap(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: tonemap pipeline failure!");
		unInitVulkan();
		return false;
	}

	// Text overlay: font bake + glyph buffers + raster/blend
	// pipelines. Non-fatal: failure logs and turns the overlay off.
	ano_vk_text_init(&ctx, &rendererState);

	// Depth-only shadow pipeline + compare sampler (reuses the flat pipeline layout, so after pipelines).
	if (!ano_vk_init_shadow(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: shadow pipeline failure!");
		unInitVulkan();
		return false;
	}

	if(!createTextureSampler(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: texture sampler failure!");
		unInitVulkan();
		return false;
	}

	if (!createFallbackResources(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: fallback resources failure.");
		unInitVulkan();
		return false;
	}

	// Slot-indexed buffers start at INITIAL_ENTITY_CAPACITY and grow on demand
	// (ensureEntityCapacity); material/light are distinct-element palettes on their
	// own capacity axis. maxEntities is the starting slot count, no longer a ceiling.
	// The buffer creation itself is hoisted into ano_vk_create_scene_resources (scene_buffers.c).
	uint32_t maxEntities = INITIAL_ENTITY_CAPACITY;
	if (!ano_vk_create_scene_resources())
	{
		ano_log(ANO_FATAL, "Quitting init: buffer creation failure!");
		unInitVulkan();
		return false;
	}

	// Parse + register the scene's glTF assets (hoisted into ano_render_load_scene_assets, render_api.c).
	// On a fatal parse failure it has already run unInitVulkan, so just propagate the failure.
	if (!ano_render_load_scene_assets())
		return false;


	// ECS <-> render bridge: render-owned slot authority + command/event rings.
	// The logic master now composes the whole scene and emits its creates through this command ring —
	// the same path a runtime spawn takes — so nothing is written to the per-slot GPU buffers here.
	rendererState.renderHeap = mi_heap_new();
	if (!rendererState.renderHeap ||
	    !render_slots_init(&rendererState.slots, rendererState.renderHeap, maxEntities, MAX_FRAMES_IN_FLIGHT) ||
	    // Events ring widened to 4096: it now also carries forwarded input (key REPEAT bursts), and
	    // render must never block emitting it, so the ring absorbs the spikes (audit 4.11).
	    !ano_render_bridge_init(&rendererState.bridge, rendererState.renderHeap, 4096, 4096))
	{
		ano_log(ANO_FATAL, "Quitting init: render bridge / slot authority failure!");
		unInitVulkan();
		return false;
	}

	// Runtime light registry (audit 4.7 Phase 3). Palette rows [0, ANO_STATIC_LIGHT_COUNT) are the
	// STATIC region the logic master fills with scene light-entities (create-with-light, static shadow
	// budget); the registry owns the dynamic remainder [ANO_STATIC_LIGHT_COUNT, capacity) for runtime
	// attach/detach. A FIXED base (not the live scene-light count) lets logic own the static light_index
	// namespace independently of when/whether it has spawned the scene lights.
	light_registry_init(&rendererState.lightRegistry, ANO_STATIC_LIGHT_COUNT,
	                    rendererState.lightBuffer.capacity - ANO_STATIC_LIGHT_COUNT,
	                    MAX_FRAMES_IN_FLIGHT);

	// Stream render_id ring now that renderHeap exists: CPU-only, parallel to xformRing,
	// producer-written and render-resolved. Freed wholesale on mi_heap_destroy at teardown.
	rendererState.transformStream.idRing = mi_heap_malloc(rendererState.renderHeap,
	    (size_t)rendererState.transformStream.ringSlices * STREAM_CAPACITY * sizeof(uint32_t));
	if (!rendererState.transformStream.idRing)
	{
		ano_log(ANO_FATAL, "Quitting init: stream id ring allocation failure!");
		unInitVulkan();
		return false;
	}
	rendererState.globalFrame = 0;

	// Zero the light palette + shadow config/info device buffers once (hardening): an unwritten row
	// then reads enabled=0 / active=0 / castsShadow=0 (inert) instead of uninitialized memory, which
	// the cull/fragment would treat as a phantom light/caster. The logic master spawns the scene
	// asynchronously; each create is staged + uploaded by the normal per-frame path (render_apply_commands
	// + recordCommandBuffer) as it arrives, so there is no init scene seed beyond this zero-fill.
	{
		VkCommandBuffer up = beginSingleTimeCommands(&ctx);
		vkCmdFillBuffer(up, rendererState.lightBuffer.device, 0,
		                (VkDeviceSize)sizeof(LightData) * rendererState.lightBuffer.capacity, 0u);
		vkCmdFillBuffer(up, rendererState.shadowConfig.device, 0,
		                (VkDeviceSize)sizeof(ShadowFrustumConfig) * rendererState.shadowConfig.capacity, 0u);
		vkCmdFillBuffer(up, rendererState.shadowInfo.device, 0,
		                (VkDeviceSize)sizeof(ShadowLightInfo) * rendererState.shadowInfo.capacity, 0u);
		endSingleTimeCommands(&ctx, up);
	}

	if (!createUniformBuffers(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: uniform buffer creation failure!");
		unInitVulkan();
		return false;
	}



	// HERE
	if (!createDescriptorPool(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: UBO descriptor pool creation failure!");
		unInitVulkan();
		return false;
	}


	if (!createDescriptorSets(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: UBO descriptor sets creation failure!");
		unInitVulkan();
		return false;
	}


	updateUboDescriptorSets(&ctx, &rendererState);
	updateTonemapDescriptorSets(&ctx, &rendererState);
	updateHiZDescriptorSets(&ctx, &rendererState);
	updateClusterDescriptorSets(&ctx, &rendererState);
	updateShadowDescriptorSets(&ctx, &rendererState);
	ano_vk_text_create_sets(&ctx, &rendererState);


	if (!createCommandBuffer(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: command buffer failure!");
		unInitVulkan();
		return false;
	}
	

	if (!createSyncObjects(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: sync failure!");
		unInitVulkan();
		return false;
	}

	ano_log(ANO_INFO, "Instance creation complete!");

	return true;
}

