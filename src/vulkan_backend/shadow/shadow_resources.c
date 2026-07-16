/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <string.h>
#include <stdlib.h>
#include <anoptic_log.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/shadow/shadow.h"

// Dynamic shadow resources.
// Per frame: GPU-written frustum SSBO + RGBA16 CDF atlas + blur temp (2D arrays: per-sublayer render + array sample views).
// Shared: transient caster-depth (one slice/frustum) + shadow config/info SlotUploads.
// in: ctx, state; out: frames[].shadow.*, shadowDepth*, shadow{Config,Info}
bool createShadowResources(VulkanContext* ctx, RendererState* state) {
    VkDeviceSize frustumSize = (VkDeviceSize)sizeof(CullView) * ANO_SHADOW_FRUSTUM_COUNT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        ShadowResources* sh = &state->frames[i].shadow;

        // GPU-written shadow frustum buffer (viewProj + planes per frustum).
        VkBufferCreateInfo binfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = frustumSize, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        if (vkCreateBuffer(ctx->device, &binfo, NULL, &sh->frustumBuffer) != VK_SUCCESS) return false;
        VkMemoryRequirements bmr; vkGetBufferMemoryRequirements(ctx->device, sh->frustumBuffer, &bmr);
        sh->frustumAlloc = gpu_alloc(&gpuAllocator, bmr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (sh->frustumAlloc.memory == VK_NULL_HANDLE) return false;
        vkBindBufferMemory(ctx->device, sh->frustumBuffer, sh->frustumAlloc.memory, sh->frustumAlloc.offset);

        // Packed sampling viewProjs: SSBO write (shadowsetup) / UBO read (lighting), full shader bound mat4[64].
        VkBufferCreateInfo vinfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            // mat4 viewProj + vec4 depthParams per frustum (shadow_sample.glsl / shadowsetup.comp).
            .size = (VkDeviceSize)(sizeof(float) * (16u + 4u)) * ANO_SHADOW_SAMPLE_VP_CAP,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        if (vkCreateBuffer(ctx->device, &vinfo, NULL, &sh->sampleVPBuffer) != VK_SUCCESS) return false;
        VkMemoryRequirements vmr; vkGetBufferMemoryRequirements(ctx->device, sh->sampleVPBuffer, &vmr);
        sh->sampleVPAlloc = gpu_alloc(&gpuAllocator, vmr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (sh->sampleVPAlloc.memory == VK_NULL_HANDLE) return false;
        vkBindBufferMemory(ctx->device, sh->sampleVPBuffer, sh->sampleVPAlloc.memory, sh->sampleVPAlloc.offset);
    }

    // CDF atlas + blur-temp: RGBA16_UNORM 2D arrays, ANO_SHADOW_ATLAS_LAYERS, one instance across FIF. Seeded SHADER_READ.
    // Atlas = final blurred per-band (coverage,M); temp = separable-blur intermediate.
    {
        VkImage* momentImgs[2]     = { &state->shadowAtlasImage, &state->shadowTempImage };
        GpuAllocation* momentAl[2] = { &state->shadowAtlasAlloc, &state->shadowTempAlloc };
        VkImageView* momentArr[2]  = { &state->shadowAtlasArrayView, &state->shadowTempArrayView };
        VkImageView* momentLyr[2]  = { state->shadowAtlasLayerView, state->shadowTempLayerView };
        for (int m = 0; m < 2; m++) {
            VkImageCreateInfo iinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            iinfo.imageType = VK_IMAGE_TYPE_2D;
            iinfo.format = ANO_SHADOW_STATS_FORMAT;
            iinfo.extent = (VkExtent3D){ ANO_SHADOW_DIM, ANO_SHADOW_DIM, 1 };
            iinfo.mipLevels = 1;
            iinfo.arrayLayers = ANO_SHADOW_ATLAS_LAYERS;
            iinfo.samples = VK_SAMPLE_COUNT_1_BIT;
            iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            iinfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(ctx->device, &iinfo, NULL, momentImgs[m]) != VK_SUCCESS) return false;
            VkMemoryRequirements imr; vkGetImageMemoryRequirements(ctx->device, *momentImgs[m], &imr);
            *momentAl[m] = gpu_alloc(&gpuAllocator, imr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (momentAl[m]->memory == VK_NULL_HANDLE) return false;
            vkBindImageMemory(ctx->device, *momentImgs[m], momentAl[m]->memory, momentAl[m]->offset);

            VkImageViewCreateInfo vinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vinfo.image = *momentImgs[m];
            vinfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            vinfo.format = ANO_SHADOW_STATS_FORMAT;
            vinfo.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS };
            if (vkCreateImageView(ctx->device, &vinfo, NULL, momentArr[m]) != VK_SUCCESS) return false;

            for (uint32_t s = 0; s < ANO_SHADOW_ATLAS_LAYERS; s++) {
                VkImageViewCreateInfo lv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                lv.image = *momentImgs[m];
                lv.viewType = VK_IMAGE_VIEW_TYPE_2D;
                lv.format = ANO_SHADOW_STATS_FORMAT;
                lv.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, s, 1 };
                if (vkCreateImageView(ctx->device, &lv, NULL, &momentLyr[m][s]) != VK_SUCCESS) return false;
            }

            // Seed ALL layers to SHADER_READ (transitionImageLayout spans only layer 0).
            VkCommandBuffer seedCmd = beginSingleTimeCommands(ctx);
            VkImageMemoryBarrier seed = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = *momentImgs[m], .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, ANO_SHADOW_ATLAS_LAYERS } };
            vkCmdPipelineBarrier(seedCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &seed);
            endSingleTimeCommands(ctx, seedCmd);
        }
    }

    // Dirty-frustum cache: every layer starts invalid.
    // Mover bookkeeping (slotMotion) allocated by createMotionBuffer.
    state->shadowCacheMode = getenv("ANO_FORCE_NO_SHADOW_CACHE") ? 1u
                           : getenv("ANO_SHADOW_CACHE_FREEZE")   ? 2u : 0u;
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) state->shadowLayerValid[s] = false;
    state->shadowGlobalDirty = false;
    if (state->shadowCacheMode)
        ano_log(ANO_INFO, "Shadow cache: %s", state->shadowCacheMode == 1u ? "OFF (every frame dirty)" : "FREEZE");
    // Swept-bound motion exposure: per-frustum caster volumes start uninstalled.
    state->sweptExposure = getenv("ANO_FORCE_NO_SWEPT") == NULL;
    state->sweptPoisoned = false;
    if (!state->sweptExposure)
        ano_log(ANO_INFO, "Shadow cache: swept motion exposure OFF (any mover dirties every frustum)");
    for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
        state->shadowVolume[s].parentSlot = ANO_RENDER_SLOT_UNMAPPED;
        state->shadowExposed[s] = 0u;
        state->shadowMatrixDirty[s] = false;
        state->shadowLastRendered[s] = 0u;
    }
    // Content-refresh budget (0 = unlimited). Matrix-dirty renders exempt.
    const char* budgetEnv = getenv("ANO_SHADOW_BUDGET");
    state->shadowRenderBudget = budgetEnv ? (uint32_t)atoi(budgetEnv) : 0u;
    if (state->shadowRenderBudget)
        ano_log(ANO_INFO, "Shadow cache: content re-render budget %u/frame (matrix-dirty exempt)",
               state->shadowRenderBudget);

    // Transient nearest-occluder depth (never sampled): one image across FIF, one slice per frustum.
    // Contents frame-transient (loadOp CLEAR each render).
    {
        VkImageCreateInfo dinfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        dinfo.imageType = VK_IMAGE_TYPE_2D;
        dinfo.format = ANO_SHADOW_TRANSIENT_DEPTH_FORMAT;
        dinfo.extent = (VkExtent3D){ ANO_SHADOW_DIM, ANO_SHADOW_DIM, 1 };
        dinfo.mipLevels = 1;
        dinfo.arrayLayers = ANO_SHADOW_FRUSTUM_COUNT;
        dinfo.samples = VK_SAMPLE_COUNT_1_BIT;
        dinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        dinfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        dinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(ctx->device, &dinfo, NULL, &state->shadowDepthImage) != VK_SUCCESS) return false;
        VkMemoryRequirements dmr; vkGetImageMemoryRequirements(ctx->device, state->shadowDepthImage, &dmr);
        state->shadowDepthAlloc = gpu_alloc(&gpuAllocator, dmr, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (state->shadowDepthAlloc.memory == VK_NULL_HANDLE) return false;
        vkBindImageMemory(ctx->device, state->shadowDepthImage, state->shadowDepthAlloc.memory, state->shadowDepthAlloc.offset);
        for (uint32_t s = 0; s < ANO_SHADOW_FRUSTUM_COUNT; s++) {
            VkImageViewCreateInfo dv = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            dv.image = state->shadowDepthImage;
            dv.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dv.format = ANO_SHADOW_TRANSIENT_DEPTH_FORMAT;
            dv.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, s, 1 };
            if (vkCreateImageView(ctx->device, &dv, NULL, &state->shadowDepthSliceView[s]) != VK_SUCCESS) return false;
        }
    }

    // Shadow config + per-light info as SlotUploads (×1 device + delta staging).
    // shadowCfgMirror is the render-thread CPU copy the record loop gates on.
    if (!slot_upload_create(&state->shadowConfig, ANO_SHADOW_FRUSTUM_COUNT, sizeof(ShadowFrustumConfig), SLOT_STAGING_INIT, false)) return false;
    if (!slot_upload_create(&state->shadowInfo, state->lightBuffer.capacity, sizeof(ShadowLightInfo), SLOT_STAGING_INIT, false)) return false;
    state->shadowCfgMirror = (ShadowFrustumConfig*)calloc(ANO_SHADOW_FRUSTUM_COUNT, sizeof(ShadowFrustumConfig)); // active=0
    if (!state->shadowCfgMirror) return false;

    // Static rig allocator (monotonic, fills [0, shadowFrustumNext) within the static region).
    state->shadowFrustumNext = 0u;
    state->shadowTypeUsed[0] = state->shadowTypeUsed[1] = state->shadowTypeUsed[2] = 0u;
    // Runtime pools: push every free single slot + point-block base in the headroom region.
    state->rtSingleFreeCount = 0u;
    for (uint32_t s = 0; s < ANO_SHADOW_RT_SINGLE_COUNT; s++)
        state->rtSingleFree[state->rtSingleFreeCount++] = ANO_SHADOW_RT_SINGLE_BASE + s;
    state->rtPointFreeCount = 0u;
    for (uint32_t b = 0; b < ANO_SHADOW_RT_POINT_COUNT; b++)
        state->rtPointFree[state->rtPointFreeCount++] = ANO_SHADOW_RT_POINT_BASE + b * ANO_SHADOW_CUBE_FACES;

    return true;
}
