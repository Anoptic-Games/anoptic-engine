/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <anoptic_memory.h>
#include <anoptic_logging.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/ui_raster.h"



void cleanupVulkan(VulkanContext* ctx) // Frees the initialized Vulkan parameters
{
	// !TODO ADD INTERMEDIARY FUNCTION TO DESTROY ENTITY STRUCT ASSETS, CALL HERE
	// !TODO Also texture samplers, image views, etc
	if (ctx == NULL)
	{
		return;
	}

	vkDeviceWaitIdle(ctx->device);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        flush_deletion_queue(ctx, &rendererState, i);
        if (rendererState.frames[i].deletionQueue.tasks) {
            free(rendererState.frames[i].deletionQueue.tasks);
            rendererState.frames[i].deletionQueue.tasks = NULL;
            rendererState.frames[i].deletionQueue.capacity = 0;
        }
    }

	cleanupSwapChain(ctx, &rendererState);
	if (rendererState.swapChain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(ctx->device, rendererState.swapChain, NULL);
	}

	if(rendererState.entities != NULL)
	{
        free(rendererState.entities);
        rendererState.entities = NULL;
	}

    for (uint32_t i = 0; i < rendererState.primitives.textureCount; i++)
    {
        if (rendererState.primitives.textureBuffers[i].textureImageView)
            vkDestroyImageView(ctx->device, rendererState.primitives.textureBuffers[i].textureImageView, NULL);
        if (rendererState.primitives.textureBuffers[i].textureImage)
            vkDestroyImage(ctx->device, rendererState.primitives.textureBuffers[i].textureImage, NULL);

    }
    ano_vk_cleanup_primitives(&rendererState.primitives);

    if (rendererState.fallbackImageView) vkDestroyImageView(ctx->device, rendererState.fallbackImageView, NULL);
    if (rendererState.fallbackImage) vkDestroyImage(ctx->device, rendererState.fallbackImage, NULL);

	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if(rendererState.transformBuffer.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.transformBuffer.buffer[i], NULL);
		if(rendererState.lightRuntimeBuffer.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.lightRuntimeBuffer.buffer[i], NULL);
		// initialTransform/motion/instanceData are SlotUploads, destroyed below
		if(rendererState.transformStream.slotBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.transformStream.slotBuffer[i], NULL);
		if(rendererState.materialBuffer.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.materialBuffer.buffer[i], NULL);
		// lightBuffer is a SlotUpload, destroyed below
		if(rendererState.indirectBuffer.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.indirectBuffer.buffer[i], NULL);
		// culling.entity is a SlotUpload, destroyed below
		if(rendererState.culling.meshDataBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.meshDataBuffer[i], NULL);
		if(rendererState.culling.meshBoundsBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.meshBoundsBuffer[i], NULL);
		if(rendererState.culling.drawCountBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.drawCountBuffer[i], NULL);
		if(rendererState.culling.compactedEntityIndicesBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.compactedEntityIndicesBuffer[i], NULL);
		if(rendererState.culling.sortKeysBuffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.sortKeysBuffer[i], NULL);
		if(rendererState.culling.ubo.buffer[i])
			vkDestroyBuffer(ctx->device, rendererState.culling.ubo.buffer[i], NULL);
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
			if(rendererState.frames[i].views[v].clusterLightCountBuffer)
				vkDestroyBuffer(ctx->device, rendererState.frames[i].views[v].clusterLightCountBuffer, NULL);
			if(rendererState.frames[i].views[v].clusterLightIndexBuffer)
				vkDestroyBuffer(ctx->device, rendererState.frames[i].views[v].clusterLightIndexBuffer, NULL);
		}
		// Per-frame shadow frustum + sampleVP buffers
		ShadowResources* sh = &rendererState.frames[i].shadow;
		if (sh->frustumBuffer) vkDestroyBuffer(ctx->device, sh->frustumBuffer, NULL);
		if (sh->sampleVPBuffer) vkDestroyBuffer(ctx->device, sh->sampleVPBuffer, NULL);
	}
	// Text overlay: frame-data, glyph buffers, bake heap, FreeType backend
	ano_vk_text_destroy(ctx, &rendererState);
	// UI overlay lane (groundwork stub: handle-guarded no-op)
	ano_vk_ui_destroy(ctx, &rendererState);

	// Shared shadow images: atlas, blur temp, transient caster depth
	if (rendererState.shadowAtlasArrayView) vkDestroyImageView(ctx->device, rendererState.shadowAtlasArrayView, NULL);
	if (rendererState.shadowTempArrayView) vkDestroyImageView(ctx->device, rendererState.shadowTempArrayView, NULL);
	for (uint32_t s = 0; s < ANO_SHADOW_ATLAS_LAYERS; s++) {
		if (rendererState.shadowAtlasLayerView[s]) vkDestroyImageView(ctx->device, rendererState.shadowAtlasLayerView[s], NULL);
		if (rendererState.shadowTempLayerView[s]) vkDestroyImageView(ctx->device, rendererState.shadowTempLayerView[s], NULL);
	}
	if (rendererState.shadowAtlasImage) vkDestroyImage(ctx->device, rendererState.shadowAtlasImage, NULL);
	if (rendererState.shadowTempImage) vkDestroyImage(ctx->device, rendererState.shadowTempImage, NULL);
	for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
		if (rendererState.shadowDepthSliceView[s]) vkDestroyImageView(ctx->device, rendererState.shadowDepthSliceView[s], NULL);
	}
	if (rendererState.shadowDepthImage) vkDestroyImage(ctx->device, rendererState.shadowDepthImage, NULL);
	// Mover bookkeeping + swept-exposure mirrors
	if (rendererState.slotMotion)   { free(rendererState.slotMotion);   rendererState.slotMotion = NULL; }
	if (rendererState.slotBasePose) { free(rendererState.slotBasePose); rendererState.slotBasePose = NULL; }
	if (rendererState.slotMeshIdx)  { free(rendererState.slotMeshIdx);  rendererState.slotMeshIdx = NULL; }
	if (rendererState.slotMoverIdx) { free(rendererState.slotMoverIdx); rendererState.slotMoverIdx = NULL; }
	if (rendererState.movers)       { free(rendererState.movers);       rendererState.movers = NULL; }
	// Shadow config mirror (render-thread CPU copy)
	if (rendererState.shadowCfgMirror) { free(rendererState.shadowCfgMirror); rendererState.shadowCfgMirror = NULL; }

	// SlotUpload buffers: device handles + per-frame staging + region arrays
	{
		SlotUpload* ups[7] = { &rendererState.initialTransformBuffer, &rendererState.motionBuffer,
			&rendererState.instanceDataBuffer, &rendererState.lightBuffer, &rendererState.culling.entity,
			&rendererState.shadowConfig, &rendererState.shadowInfo };
		for (uint32_t u = 0; u < 7; u++) {
			if (ups[u]->device) vkDestroyBuffer(ctx->device, ups[u]->device, NULL);
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
				if (ups[u]->staging[i]) vkDestroyBuffer(ctx->device, ups[u]->staging[i], NULL);
				if (ups[u]->regions[i]) free(ups[u]->regions[i]);
			}
		}
	}

	// Streamed-transform ring
	if(rendererState.transformStream.xformRing)
		vkDestroyBuffer(ctx->device, rendererState.transformStream.xformRing, NULL);

	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
		{
			if(rendererState.frames[i].views[v].uniformBuffer)
			{
				vkDestroyBuffer(ctx->device, rendererState.frames[i].views[v].uniformBuffer, NULL);
			}
		}
	}

    ano_vk_cleanup_pipelines(ctx, &rendererState);

	#ifdef DEBUG_BUILD
	DestroyDebugUtilsMessengerEXT(ctx->instance, ctx->debugMessenger, NULL);
	#endif
	for (uint32_t i = 0; i<MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (rendererState.frames[i].imageAvailable)
		{
			vkDestroySemaphore(ctx->device, rendererState.frames[i].imageAvailable, NULL);
		}
		if (rendererState.frames[i].renderFinished)
		{
			vkDestroySemaphore(ctx->device, rendererState.frames[i].renderFinished, NULL);
		}
		if (rendererState.frames[i].frameFence)
		{
			vkDestroyFence(ctx->device, rendererState.frames[i].frameFence, NULL);
		}
		if (rendererState.frames[i].timestampPool)
		{
			vkDestroyQueryPool(ctx->device, rendererState.frames[i].timestampPool, NULL);
		}
		if (rendererState.frames[i].pickReadback)
		{
			// Handle only; memory owned by gpuAllocator
			vkDestroyBuffer(ctx->device, rendererState.frames[i].pickReadback, NULL);
			rendererState.frames[i].pickReadback = VK_NULL_HANDLE;
		}

	}

	
	if (rendererState.commandPool != NULL)
	{
		vkDestroyCommandPool(ctx->device, rendererState.commandPool, NULL);
	}

	// Async Hi-Z objects; NULL when asyncHiz off
	if (rendererState.computeCommandPool != NULL)
	{
		vkDestroyCommandPool(ctx->device, rendererState.computeCommandPool, NULL);
	}
	if (rendererState.gfxTimeline != NULL)
	{
		vkDestroySemaphore(ctx->device, rendererState.gfxTimeline, NULL);
	}
	if (rendererState.hizTimeline != NULL)
	{
		vkDestroySemaphore(ctx->device, rendererState.hizTimeline, NULL);
	}
	// Async light-cull timelines; NULL when asyncLc off
	if (rendererState.preludeTimeline != NULL)
	{
		vkDestroySemaphore(ctx->device, rendererState.preludeTimeline, NULL);
	}
	if (rendererState.lcTimeline != NULL)
	{
		vkDestroySemaphore(ctx->device, rendererState.lcTimeline, NULL);
	}



	if (rendererState.textureSampler != NULL)
	{
		vkDestroySampler(ctx->device, rendererState.textureSampler, NULL);
	}
	
	if (ctx->device != VK_NULL_HANDLE) 
	{
		vkDeviceWaitIdle(ctx->device);

		ano_vk_cleanup_geometry_pool(&rendererState.globalGeometryPool, ctx->device);
		gpu_alloc_destroy(&gpuAllocator);
		gpu_alloc_destroy(&swapchainAllocator);
		gpu_alloc_destroy(&stagingAllocator);
		gpu_alloc_destroy(&textureAllocator);

		vkDestroyDevice(ctx->device, NULL);
	}

	if (ctx->availableDevices != NULL)
	{
		for (uint32_t i = 0; i < ctx->deviceCount; i++)
		{
			free(ctx->availableDevices[i]);
			ctx->availableDevices[i] = NULL;
		}
		free(ctx->availableDevices);
		ctx->availableDevices = NULL;
	}

	if (ctx->instance != VK_NULL_HANDLE) 
	{
		if (ctx->surface != VK_NULL_HANDLE) 
		{
			vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
		}
		vkDestroyInstance(ctx->instance, NULL);
	}

}

