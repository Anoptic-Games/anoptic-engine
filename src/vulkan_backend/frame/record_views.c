/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdint.h>
#include <math.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/frame/frame.h"

// Per view light-cull then geometry into this view's HDR target + depth, reading its cull partition. Picking readback on view 0.
void ano_record_views(VkCommandBuffer cmd, uint32_t entityCount, uint32_t drawSlotCount)
{
    // === Per view: light-cull (this view's froxel lists) then geometry into this view's
    // HDR target + depth, reading this view's cull partition. ===
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
        ViewResources* vr = &rendererState.frames[rendererState.frameIndex].views[v];

        // This view's HDR resolve target: UNDEFINED -> COLOR_ATTACHMENT.
        {
            VkImageMemoryBarrier hdrToColor = {};
            hdrToColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            hdrToColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            hdrToColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrToColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToColor.image = vr->hdrColorImage;
            hdrToColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            hdrToColor.subresourceRange.levelCount = 1;
            hdrToColor.subresourceRange.layerCount = 1;
            hdrToColor.srcAccessMask = 0;
            hdrToColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, NULL, 0, NULL, 1, &hdrToColor);
        }

        // Light-cull bins lights into this view's froxel grid. Async mode records dispatches into the compute-queue CB instead.
        if (entityCount > 0 && !rendererState.asyncLc) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                rendererState.prototypes[PIPELINE_COMPUTE_LIGHTCULL].implementations[0].pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                rendererState.prototypes[PIPELINE_COMPUTE_LIGHTCULL].layout, 0, 1, &vr->lightcullSet, 0, NULL);
            uint32_t lightcullDispatch = (ANO_CLUSTER_COUNT + 63u) / 64u;
            vkCmdDispatch(cmd, lightcullDispatch, 1, 1);

            VkMemoryBarrier lcBarrier = {};
            lcBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            lcBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            lcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 1, &lcBarrier, 0, NULL, 0, NULL);
        }

        // MSAA color + id targets are per view. No inter-view reuse barrier.
        for (int p = 0; p < (int)ano_frame_pass_count; p++) {
            const RenderPassDef* pass = &ano_frame_passes[p];
            if (pass->type != PASS_GRAPHICS) continue;

            // Depth write->read hazard. Order the pre-pass's writes before this pass's reads/tests.
            if (pass->depthBarrierBefore) {
                VkImageMemoryBarrier depthWaw = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = vr->depthImage, .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 } };
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    0, 0, NULL, 0, NULL, 1, &depthWaw);
            }

            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
            VkClearValue clearDepth = {};
            clearDepth.depthStencil.depth = 1.0f;
            clearDepth.depthStencil.stencil = 0;

            // color[0] = HDR, color[1] = R32_UINT picking id (only the opaque pass declares 2).
            VkRenderingAttachmentInfo color[2] = {};
            color[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            color[0].imageView = rendererState.colorView[v]; // this view's MSAA color
            color[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color[0].resolveMode = pass->resolveMode;
            if (pass->resolveMode != VK_RESOLVE_MODE_NONE) {
                color[0].resolveImageView = vr->hdrColorView; // resolve into this view's HDR target
                color[0].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            color[0].loadOp = pass->colorLoadOp;
            color[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color[0].clearValue = clearColor;

            if (pass->colorAttachmentCount == 2) {
                // Picking id into the shared MSAA id image, cleared to the no-hit sentinel. Integer formats MUST resolve SAMPLE_ZERO. Only view 0 resolves to a readable target.
                color[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                color[1].imageView = rendererState.pickIdView[v];
                color[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                // CLEAR only with the first opaque pass. The two-sided lane LOADs its ids.
                color[1].loadOp = pass->colorLoadOp;
                color[1].clearValue.color.uint32[0] = 0xFFFFFFFFu;
                if (v == 0) {
                    color[1].resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                    color[1].resolveImageView = vr->pickIdResolveView;
                    color[1].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    color[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                } else {
                    color[1].resolveMode = VK_RESOLVE_MODE_NONE;
                    color[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                }
            }

            VkRenderingAttachmentInfo depthAttachment = {};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = vr->depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
            depthAttachment.loadOp = pass->depthLoadOp;
            depthAttachment.storeOp = pass->depthStoreOp;
            depthAttachment.clearValue = clearDepth;

            VkRenderingInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = (VkOffset2D){0, 0};
            renderingInfo.renderArea.extent = rendererState.viewExtent[v];
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = pass->colorAttachmentCount;
            renderingInfo.pColorAttachments = color;
            renderingInfo.pDepthAttachment = &depthAttachment;
            renderingInfo.pStencilAttachment = NULL;

            vkCmdBeginRendering(cmd, &renderingInfo);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.prototypes[pass->prototype].implementations[pass->implementationIndex].pipeline);

            VkViewport viewport = {};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)(rendererState.viewExtent[v].width);
            viewport.height = (float)(rendererState.viewExtent[v].height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor = {};
            scissor.offset = (VkOffset2D){0, 0};
            scissor.extent = rendererState.viewExtent[v];
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // This view's global set selects its camera UBO + froxel light lists.
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                rendererState.prototypes[pass->prototype].layout, 0, 1, &vr->globalSet, 0, NULL);
            // Set 2: shadow frustums + atlas + per-light info (fragment PCF-samples shadows).
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                rendererState.prototypes[pass->prototype].layout, 2, 1,
                &rendererState.frames[rendererState.frameIndex].shadow.geomSet, 0, NULL);

            if (entityCount > 0) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    rendererState.prototypes[pass->prototype].layout, 1, 1, &rendererState.bindlessTextures.set, 0, NULL);

                // Compacted draws in this (view, draw slot) partition = view*drawSlotCount + slot.
                uint32_t slot = ano_draw_slot_of(pass->prototype);
                uint32_t partition = v * drawSlotCount + slot;
                uint32_t baseOffset = partition * rendererState.culling.maxEntities;
                bool useMesh = ctx.deviceCapabilities.meshShader;
                // Must equal the layout's push range flags exactly.
                VkShaderStageFlags pcStage = (useMesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
                    | VK_SHADER_STAGE_FRAGMENT_BIT // widened push range
                    | (rendererState.taskCull ? VK_SHADER_STAGE_TASK_BIT_EXT : 0);
                vkCmdPushConstants(cmd, rendererState.prototypes[pass->prototype].layout, pcStage, 0, sizeof(uint32_t), &baseOffset);

                VkBuffer indirectBuf = rendererState.indirectBuffer.buffer[rendererState.frameIndex];
                VkBuffer drawCountBuf = rendererState.culling.drawCountBuffer[rendererState.frameIndex];
                VkDeviceSize countOffset = (VkDeviceSize)partition * sizeof(uint32_t);
                uint32_t maxDraws = rendererState.indirectBuffer.capacity;

                if (useMesh) {
                    VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDraws * sizeof(VkDrawMeshTasksIndirectCommandEXT);
                    if (ctx.deviceCapabilities.drawIndirectCount) {
                        pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBuf, indirectOffset,
                            drawCountBuf, countOffset, maxDraws, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    } else {
                        pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBuf, indirectOffset,
                            entityCount, sizeof(VkDrawMeshTasksIndirectCommandEXT));
                    }
                } else {
                    vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    VkDeviceSize indirectOffset = (VkDeviceSize)partition * maxDraws * sizeof(VkDrawIndexedIndirectCommand);
                    if (ctx.deviceCapabilities.drawIndirectCount) {
                        vkCmdDrawIndexedIndirectCount(cmd, indirectBuf, indirectOffset,
                            drawCountBuf, countOffset, maxDraws, sizeof(VkDrawIndexedIndirectCommand));
                    } else {
                        vkCmdDrawIndexedIndirect(cmd, indirectBuf, indirectOffset,
                            entityCount, sizeof(VkDrawIndexedIndirectCommand));
                    }
                }
            }

            // World-space text panel drawn in the additive pass, resolves with the scene.
            if (pass->prototype == PIPELINE_ADDITIVE)
                ano_vk_text_record_world(&rendererState, cmd, rendererState.frameIndex, v);

            vkCmdEndRendering(cmd);
        }

        // Copy the cursor texel from view 0's resolved id image into this frame's readback buffer. Skip on a degenerate extent.
        if (v == 0 && rendererState.imageExtent.width > 0 && rendererState.imageExtent.height > 0) {
            VkImageMemoryBarrier toSrc = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vr->pickIdResolveImage,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &toSrc);

            float fx = rendererState.cursorX < 0.0f ? 0.0f : rendererState.cursorX;
            float fy = rendererState.cursorY < 0.0f ? 0.0f : rendererState.cursorY;
            uint32_t cx = (uint32_t)fx, cy = (uint32_t)fy;
            if (cx >= rendererState.imageExtent.width)  cx = rendererState.imageExtent.width - 1u;
            if (cy >= rendererState.imageExtent.height) cy = rendererState.imageExtent.height - 1u;
            VkBufferImageCopy region = { .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageOffset = { (int32_t)cx, (int32_t)cy, 0 }, .imageExtent = { 1, 1, 1 } };
            vkCmdCopyImageToBuffer(cmd, vr->pickIdResolveImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                rendererState.frames[rendererState.frameIndex].pickReadback, 1, &region);

            VkImageMemoryBarrier toColor = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vr->pickIdResolveImage,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, NULL, 0, NULL, 1, &toColor);
        }

        // This view's HDR target -> SHADER_READ for the composite below.
        {
            VkImageMemoryBarrier hdrToRead = {};
            hdrToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            hdrToRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            hdrToRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            hdrToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hdrToRead.image = vr->hdrColorImage;
            hdrToRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            hdrToRead.subresourceRange.levelCount = 1;
            hdrToRead.subresourceRange.layerCount = 1;
            hdrToRead.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            hdrToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &hdrToRead);
        }
    }
}

// Composite: tonemap each view's HDR target onto the swapchain (view 0 fullscreen, aux views as PiP insets) + the text/UI overlay.
void ano_record_composite(VkCommandBuffer cmd, uint32_t imageIndex)
{
    // --- Composite: tonemap each view's HDR target onto the swapchain ---
    // View 0 fills the screen, aux views as PiP insets along the bottom-right. Same ACES tonemap fullscreen triangle per view, scoped by viewport+scissor.
    {
        VkRenderingAttachmentInfo tmColor = {};
        tmColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        tmColor.imageView = rendererState.views[imageIndex];
        tmColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        tmColor.resolveMode = VK_RESOLVE_MODE_NONE;
        tmColor.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // view 0 covers every pixel
        tmColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo tmInfo = {};
        tmInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        tmInfo.renderArea.offset = (VkOffset2D){0, 0};
        tmInfo.renderArea.extent = rendererState.imageExtent;
        tmInfo.layerCount = 1;
        tmInfo.colorAttachmentCount = 1;
        tmInfo.pColorAttachments = &tmColor;

        vkCmdBeginRendering(cmd, &tmInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.tonemapPipeline);

        uint32_t W = rendererState.imageExtent.width, H = rendererState.imageExtent.height;
        uint32_t insetW = W / 3u, insetH = H / 3u, margin = 16u;

        for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++) {
            ViewResources* vr = &rendererState.frames[rendererState.frameIndex].views[v];

            VkViewport tmViewport = {};
            VkRect2D tmScissor = {};
            if (v == 0) {
                // Main view: full screen.
                tmViewport.x = 0.0f; tmViewport.y = 0.0f;
                tmViewport.width = (float)W; tmViewport.height = (float)H;
                tmScissor.offset = (VkOffset2D){0, 0};
                tmScissor.extent = rendererState.imageExtent;
            } else {
                // Inset: stack auxiliary views up the right edge from the bottom corner.
                uint32_t idx = v - 1u;
                int32_t x = (int32_t)(W - insetW - margin);
                int32_t y = (int32_t)(H - margin - (insetH + margin) * (idx + 1u) + margin);
                tmViewport.x = (float)x; tmViewport.y = (float)y;
                tmViewport.width = (float)insetW; tmViewport.height = (float)insetH;
                tmScissor.offset = (VkOffset2D){x, y};
                tmScissor.extent = (VkExtent2D){insetW, insetH};
            }
            tmViewport.minDepth = 0.0f; tmViewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &tmViewport);
            vkCmdSetScissor(cmd, 0, 1, &tmScissor);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rendererState.tonemapLayout,
                0, 1, &vr->tonemapSet, 0, NULL);
            vkCmdDraw(cmd, 3, 1, 0, 0); // fullscreen triangle, scoped by viewport+scissor
        }

        // Text/UI overlay over everything, one fullscreen premultiplied blend.
        ano_vk_text_record_composite(&rendererState, cmd, rendererState.frameIndex);

        vkCmdEndRendering(cmd);
    }
}
