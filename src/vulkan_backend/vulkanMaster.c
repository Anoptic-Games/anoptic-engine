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
#include "vulkan_backend/ui_raster.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/light_registry.h"
#include "vulkan_backend/shadow/shadow.h"
#include "vulkan_backend/scene_buffers.h"
#include "vulkan_backend/bridge/bridge.h"
#include "vulkan_backend/render_api.h"

#define GLFW_INCLUDE_VULKAN

// Variables

// Global, declared extern in backend.h.
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
			rendererState.frames[i].frameSubmitted = false;
		}
    }

	// Drain last signaled async Hi-Z / light-cull / text ordinals before teardown.
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

	// ECS<->render bridge teardown.
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
	// WARNING+ validation messages this run.
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
                // bindless_register_texture handles index, no free yet.
                break;
        }
    }
    q->count = 0;
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

    // ECS->render transitions arrive via the bridge, drained in render_apply_commands.

    if (rendererState.frames[rendererState.frameIndex].frameSubmitted == true)
    {
        vkWaitForFences(ctx.device, 1, &(rendererState.frames[rendererState.frameIndex].frameFence), VK_TRUE, UINT64_MAX);

        // Per-pass timestamps ready to read.
        ano_collect_frame_stats(rendererState.frameIndex);

        // Fence-gated picking readback, id-texel copy retired.
        ano_collect_pick(rendererState.frameIndex);

        // Reclaim streamed-transform ring slices below the oldest in-flight frame's seq.
        uint32_t oldest = (rendererState.frameIndex + 1u) % MAX_FRAMES_IN_FLIGHT;
        uint64_t qmin = rendererState.transformStream.frameSeq[oldest];
        atomic_store_explicit(&rendererState.transformStream.reclaimSeq,
                              qmin ? qmin - 1u : 0u, memory_order_release);
    }

    // Process deferred deletions for this frame.
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

	// Ingest ECS->render transitions for this frame slot.
	render_apply_commands(&rendererState, rendererState.frameIndex);

	// Copy pending on-screen text + UI tables into this slot's buffers (post-fence).
	ano_vk_text_frame_refresh(&rendererState, rendererState.frameIndex);
	ano_vk_ui_frame_refresh(&rendererState, rendererState.frameIndex);

	vkResetCommandBuffer(rendererState.frames[rendererState.frameIndex].commandBuffer, 0);
	if (rendererState.asyncLc)
		vkResetCommandBuffer(rendererState.frames[rendererState.frameIndex].preludeCommandBuffer, 0);
	recordCommandBuffer(imageIndex);

	//updateUniformBuffer(&ctx, &rendererState);

	// Async Hi-Z 1-based ordinal, monotonic across swapchain recreates.
	uint64_t ordinal = rendererState.timelineOrdinal + 1u;
	if (rendererState.asyncHiz)
	{
		// Host-wait this slot's prior compute signal (MAX_FRAMES_IN_FLIGHT ordinals back) before re-recording.
		if (ordinal > (uint64_t)MAX_FRAMES_IN_FLIGHT)
		{
			uint64_t prior = ordinal - (uint64_t)MAX_FRAMES_IN_FLIGHT;
			VkSemaphoreWaitInfo waitInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.semaphoreCount = 1, .pSemaphores = &rendererState.hizTimeline, .pValues = &prior };
			vkWaitSemaphores(ctx.device, &waitInfo, UINT64_MAX);
		}
		recordHiZCompute(rendererState.frameIndex);
	}
	// Async light-cull CB for this frame (see recordLightcullCompute).
	if (rendererState.asyncLc)
		recordLightcullCompute(rendererState.frameIndex);

	// Async text raster, submits first with no waits, overlaps graphics.
	ano_vk_text_submit_async(&ctx, &rendererState, rendererState.frameIndex, ordinal);

	// Graphics submit.
	if (!ano_frame_submit(ordinal)) return;

    // Present before submitting a new frame's commands.

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	
	presentInfo.waitSemaphoreCount = 1;
	// Wait the graphics submit's frame-end signal (renderFinished for this slot).
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

	rendererState.frameIndex += 1; // Advance frame-in-flight index
	if (rendererState.frameIndex == MAX_FRAMES_IN_FLIGHT)
	{
		rendererState.frameIndex = 0;
	}
	rendererState.globalFrame += 1; // Gates slot-quarantine retirement

	ano_frame_mark(); // Wall-clock fps/frametime, presented frames only.
}


bool initVulkan() // Initializes Vulkan
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
		ano_log(ANO_FATAL, "Window initialization failed.");
		unInitVulkan();
		return 0;
	}

	requestPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);

	ctx.enableValidationLayers = true;
	rendererState.frameIndex = 0; // Current frame-in-flight index

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

	// Async Hi-Z gate needs dedicated compute queue, timeline semaphores, depth-max-resolve.
	// ANO_FORCE_NO_ASYNC_HIZ pins the in-frame build.
	rendererState.asyncHiz = ctx.deviceCapabilities.timelineSemaphore
	                      && ctx.deviceCapabilities.depthMaxResolve
	                      && ctx.queueFamilyIndices.computePresent
	                      && ctx.queueFamilyIndices.computeFamily != ctx.queueFamilyIndices.graphicsFamily
	                      && !getenv("ANO_FORCE_NO_ASYNC_HIZ");
	ano_log(ANO_INFO, "Async Hi-Z build: %s", rendererState.asyncHiz ? "on (dedicated compute queue)" : "off (in-frame)");

	// Async light-cull gate rides asyncHiz infrastructure.
	// ANO_FORCE_NO_ASYNC_LC pins the in-frame light-cull.
	rendererState.asyncLc = rendererState.asyncHiz && !getenv("ANO_FORCE_NO_ASYNC_LC");
	ano_log(ANO_INFO, "Async light-cull: %s", rendererState.asyncLc ? "on (split submit, overlaps shadows)" : "off (in-frame)");

	// Task-shader meshlet cull gate.
	// ANO_FORCE_NO_TASK pins direct mesh dispatch.
	rendererState.taskCull = ctx.deviceCapabilities.meshShader
	                      && ctx.deviceCapabilities.taskShader
	                      && !getenv("ANO_FORCE_NO_TASK");
	ano_log(ANO_INFO, "Task meshlet cull: %s", rendererState.taskCull ? "on (frustum+cone, Hi-Z with occlusion toggle)" : "off (direct mesh dispatch)");

	// Text overlay gate, cleared non-fatally on later font/bake failure.
	// ANO_FORCE_NO_TEXT pins it off.
	rendererState.textOverlay = !getenv("ANO_FORCE_NO_TEXT");
	ano_log(ANO_INFO, "Text overlay: %s", rendererState.textOverlay ? "enabled (pending font init)" : "off (forced)");

	// Async text lane gate rides asyncHiz infrastructure.
	// ANO_FORCE_NO_ASYNC_TEXT pins the in-frame raster.
	rendererState.asyncText = rendererState.textOverlay && rendererState.asyncHiz
	                       && !getenv("ANO_FORCE_NO_ASYNC_TEXT");
	ano_log(ANO_INFO, "Async text raster: %s", rendererState.asyncText ? "on (lag-0 compute lane)" : "off (in-frame)");

	// UI overlay gate rides the text lane. ANO_FORCE_NO_UI pins compose off, the table
	// buffers stay resident under textOverlay.
	rendererState.uiOverlay = rendererState.textOverlay && !getenv("ANO_FORCE_NO_UI");
	ano_log(ANO_INFO, "UI overlay: %s", rendererState.uiOverlay ? "enabled" : "off");

    // Mesh-shader entry points, loaded only on the mesh path.
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

	// Async Hi-Z build CBs from their own compute-family pool.
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

	createColorResources(&ctx); // TODO: make bool + check

	if(!createDepthResources(&ctx, &rendererState))
	{
		ano_log(ANO_FATAL, "Quitting init: depth resource creation failure!");
	}

	// Hi-Z occlusion pyramid images, built each frame from depth.
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

	// Text overlay font bake, glyph buffers, raster/blend pipelines, non-fatal.
	ano_vk_text_init(&ctx, &rendererState);

	// UI overlay lane table buffers, non-fatal (rides the text overlay's raster set).
	ano_vk_ui_init(&ctx, &rendererState);

	// Depth-only shadow pipeline + compare sampler.
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

	// Slot-indexed buffers start at INITIAL_ENTITY_CAPACITY and grow on demand.
	uint32_t maxEntities = INITIAL_ENTITY_CAPACITY;
	if (!ano_vk_create_scene_resources())
	{
		ano_log(ANO_FATAL, "Quitting init: buffer creation failure!");
		unInitVulkan();
		return false;
	}

	// Parse + register the scene's glTF assets.
	if (!ano_render_load_scene_assets())
		return false;


	// ECS <-> render bridge, render-owned slot authority + command/event rings.
	rendererState.renderHeap = mi_heap_new();
	if (!rendererState.renderHeap ||
	    !render_slots_init(&rendererState.slots, rendererState.renderHeap, maxEntities, MAX_FRAMES_IN_FLIGHT) ||
	    // Events ring widened to 4096.
	    !ano_render_bridge_init(&rendererState.bridge, rendererState.renderHeap, 4096, 4096))
	{
		ano_log(ANO_FATAL, "Quitting init: render bridge / slot authority failure!");
		unInitVulkan();
		return false;
	}

	// Runtime light registry, static rows [0, ANO_STATIC_LIGHT_COUNT), dynamic remainder to capacity.
	light_registry_init(&rendererState.lightRegistry, ANO_STATIC_LIGHT_COUNT,
	                    rendererState.lightBuffer.capacity - ANO_STATIC_LIGHT_COUNT,
	                    MAX_FRAMES_IN_FLIGHT);

	// Stream render_id ring, CPU-only, parallel to xformRing.
	rendererState.transformStream.idRing = mi_heap_malloc(rendererState.renderHeap,
	    (size_t)rendererState.transformStream.ringSlices * STREAM_CAPACITY * sizeof(uint32_t));
	if (!rendererState.transformStream.idRing)
	{
		ano_log(ANO_FATAL, "Quitting init: stream id ring allocation failure!");
		unInitVulkan();
		return false;
	}
	rendererState.globalFrame = 0;

	// Zero the light palette + shadow config/info device buffers once.
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

