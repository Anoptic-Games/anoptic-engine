/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/frame/frame.h"

// Hi-Z pyramid chain for one view: mips -> GENERAL, reduce mip 0 from resolved/MSAA depth, MAX-downsample, return to SHADER_READ for the cull.
static void record_hiz_pyramid_chain(VkCommandBuffer cmd, ViewResources* vr, VkExtent2D viewExtent)
{
    // pyramid (all mips) -> GENERAL for the storage writes.
    VkImageMemoryBarrier pyrToGeneral = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    pyrToGeneral.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pyrToGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    pyrToGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToGeneral.image = vr->hizImage;
    pyrToGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pyrToGeneral.subresourceRange.levelCount = vr->hizMipCount;
    pyrToGeneral.subresourceRange.layerCount = 1;
    pyrToGeneral.srcAccessMask = 0;                        // WAR: execution-only
    pyrToGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &pyrToGeneral);

    // mip 0 reduces resolved/MSAA depth, mip k downsamples mip k-1, barrier between mips.
    VkPipelineLayout hizLayout = rendererState.prototypes[PIPELINE_COMPUTE_HIZ].layout;
    for (uint32_t m = 0; m < vr->hizMipCount; m++) {
        uint32_t dstW = vr->hizWidth  >> m; if (dstW < 1u) dstW = 1u;
        uint32_t dstH = vr->hizHeight >> m; if (dstH < 1u) dstH = 1u;
        struct { int32_t srcMip, pad, dstW, dstH, srcW, srcH; } pc;
        pc.srcMip = (int32_t)m - 1; pc.pad = 0;
        pc.dstW = (int32_t)dstW; pc.dstH = (int32_t)dstH;
        if (m == 0u) {
            pc.srcW = (int32_t)viewExtent.width;
            pc.srcH = (int32_t)viewExtent.height;
        } else {
            uint32_t sw = vr->hizWidth  >> (m - 1u); if (sw < 1u) sw = 1u;
            uint32_t sh = vr->hizHeight >> (m - 1u); if (sh < 1u) sh = 1u;
            pc.srcW = (int32_t)sw; pc.srcH = (int32_t)sh;
        }
        uint32_t impl = (m == 0u) ? 0u : 1u;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_HIZ].implementations[impl].pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, hizLayout, 0, 1, &vr->hizSets[m], 0, NULL);
        vkCmdPushConstants(cmd, hizLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (dstW + 7u) / 8u, (dstH + 7u) / 8u, 1u);

        if (m + 1u < vr->hizMipCount) {
            VkMemoryBarrier mipBarrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &mipBarrier, 0, NULL, 0, NULL);
        }
    }

    // pyramid -> SHADER_READ for the cull.
    VkImageMemoryBarrier pyrToRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    pyrToRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    pyrToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pyrToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pyrToRead.image = vr->hizImage;
    pyrToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pyrToRead.subresourceRange.levelCount = vr->hizMipCount;
    pyrToRead.subresourceRange.layerCount = 1;
    pyrToRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    pyrToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &pyrToRead);
}

// Async Hi-Z build CB: both views' pyramid chains for this frame slot on ctx.computeQueue.
void recordHiZCompute(uint32_t frameIndex)
{
    VkCommandBuffer cmd = rendererState.frames[frameIndex].computeCommandBuffer;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &beginInfo);
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
        ViewResources* vr = &rendererState.frames[frameIndex].views[v];
        record_hiz_pyramid_chain(cmd, vr, rendererState.viewExtent[v]);
    }
    vkEndCommandBuffer(cmd);
}

// Async light-cull CB: both views' froxel binning on ctx.computeQueue, recorded every frame even with no entities.
void recordLightcullCompute(uint32_t frameIndex)
{
    VkCommandBuffer cmd = rendererState.frames[frameIndex].lightcullCommandBuffer;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &beginInfo);
    if (rendererState.entityCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            rendererState.prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations[0].pipeline);
        uint32_t lightcullDispatch = (ANO_CLUSTER_COUNT + 63u) / 64u;
        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            ViewResources* vr = &rendererState.frames[frameIndex].views[v];
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                rendererState.prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout, 0, 1, &vr->lightcullSet, 0, NULL);
            vkCmdDispatch(cmd, lightcullDispatch, 1, 1);
        }
    }
    vkEndCommandBuffer(cmd);
}

// In-frame Hi-Z pyramid build tail: reduce every view's depth into its pyramid, restore the depth layout.
void ano_record_hiz_tail(VkCommandBuffer cmd)
{
    // Hi-Z occlusion pyramid build, all views: reduce each view's MSAA depth into mip 0, MAX-downsample the chain.
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
        ViewResources* vr = &rendererState.frames[rendererState.frameIndex].views[v];
        {
            // Reduce source setup: depthMaxResolve MAX-resolves MSAA depth to depthResolveImage (sampler2D), else reads MSAA depth as sampler2DMS.
            if (ctx.deviceCapabilities.depthMaxResolve) {
                // (A) order geometry depth writes before the resolve reads MSAA depth.
                VkImageMemoryBarrier depWaw = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = vr->depthImage, .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 } };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    0, 0, NULL, 0, NULL, 1, &depWaw);

                // Async build: flip the resolve target SHADER_READ -> DEPTH_ATTACHMENT for this frame's resolve write.
                if (rendererState.asyncHiz) {
                    VkImageMemoryBarrier resPre = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    resPre.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    resPre.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    resPre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resPre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resPre.image = vr->depthResolveImage;
                    resPre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    resPre.subresourceRange.levelCount = 1;
                    resPre.subresourceRange.layerCount = 1;
                    resPre.srcAccessMask = 0;              // WAR: execution-only
                    resPre.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        0, 0, NULL, 0, NULL, 1, &resPre);
                }

                // Dedicated depth-resolve pass (no color, no draws) writes depthResolveImage.
                VkRenderingAttachmentInfo rDepth = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                rDepth.imageView = vr->depthView;                       // MSAA source
                rDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                rDepth.resolveMode = VK_RESOLVE_MODE_MAX_BIT;
                rDepth.resolveImageView = vr->depthResolveView;
                rDepth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                rDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                rDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;      // MSAA depth not needed after resolve
                VkRenderingInfo rInfo = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
                rInfo.renderArea.offset = (VkOffset2D){0, 0};
                rInfo.renderArea.extent = rendererState.viewExtent[v];
                rInfo.layerCount = 1;
                rInfo.colorAttachmentCount = 0;
                rInfo.pDepthAttachment = &rDepth;
                vkCmdBeginRendering(cmd, &rInfo);
                vkCmdEndRendering(cmd);

                // (C) resolved depth DEPTH_ATTACHMENT -> SHADER_READ for the reduce.
                VkImageMemoryBarrier resToRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                resToRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                resToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                resToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                resToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                resToRead.image = vr->depthResolveImage;
                resToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                resToRead.subresourceRange.levelCount = 1;
                resToRead.subresourceRange.layerCount = 1;
                resToRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                resToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &resToRead);
            } else {
                // depth DEPTH_ATTACHMENT -> SHADER_READ (reduce reads sampler2DMS).
                VkImageMemoryBarrier depToRead = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                depToRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                depToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depToRead.image = vr->depthImage;
                depToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                depToRead.subresourceRange.levelCount = 1;
                depToRead.subresourceRange.layerCount = 1;
                depToRead.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                depToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &depToRead);
            }

            if (rendererState.asyncHiz) {
                // Async build: pyramid chain recorded into the compute CB, resolved depth stays SHADER_READ.
            } else {
                // In-frame build: pre-chain WAR waits in submission order on a prior frame's cull.
                record_hiz_pyramid_chain(cmd, vr, rendererState.viewExtent[v]);

                // Restore depth to DEPTH_ATTACHMENT for next frame's geometry/resolve pass.
                if (ctx.deviceCapabilities.depthMaxResolve) {
                    // Restore resolved depth SHADER_READ -> DEPTH_ATTACHMENT for next frame's resolve.
                    VkImageMemoryBarrier resRestore = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    resRestore.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    resRestore.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    resRestore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resRestore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    resRestore.image = vr->depthResolveImage;
                    resRestore.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    resRestore.subresourceRange.levelCount = 1;
                    resRestore.subresourceRange.layerCount = 1;
                    resRestore.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    resRestore.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        0, 0, NULL, 0, NULL, 1, &resRestore);
                } else {
                    VkImageMemoryBarrier depRestore = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    depRestore.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    depRestore.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    depRestore.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    depRestore.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    depRestore.image = vr->depthImage;
                    depRestore.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    depRestore.subresourceRange.levelCount = 1;
                    depRestore.subresourceRange.layerCount = 1;
                    depRestore.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    depRestore.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        0, 0, NULL, 0, NULL, 1, &depRestore);
                }
            }
        }
    }
}
