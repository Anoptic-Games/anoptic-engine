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
#include "vulkan_backend/frame/pass_schema.h"
#include "vulkan_backend/frame/schema/draw_profiles.h"
#include "vulkan_backend/pipeline_registry.h"

static constexpr auto view_pass_enumerators =
    std::define_static_array(std::meta::enumerators_of(^^AnoFramePass));

static constexpr auto frame_attachment_enumerators =
    std::define_static_array(std::meta::enumerators_of(^^AnoFrameAttachment));

using AnoDrawKernel = void (*)(VkCommandBuffer, VkPipelineLayout,
    uint32_t, uint32_t, uint32_t) noexcept;

template<AnoDrawGeometry Geometry, AnoDrawSubmission Submission>
static void record_draw_kernel(VkCommandBuffer cmd, VkPipelineLayout layout,
                               uint32_t baseOffset, uint32_t partition,
                               uint32_t entityCount) noexcept
{
    static_assert(static_cast<size_t>(Geometry) < ANO_DRAW_GEOMETRY_COUNT);
    static_assert(static_cast<size_t>(Submission) < ANO_DRAW_SUBMISSION_COUNT);
    constexpr bool mesh = Geometry != AnoDrawGeometry::vertex;
    constexpr bool task = Geometry == AnoDrawGeometry::task;
    constexpr VkShaderStageFlags stages =
        (mesh ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT)
        | VK_SHADER_STAGE_FRAGMENT_BIT
        | (task ? VK_SHADER_STAGE_TASK_BIT_EXT : 0u);
    constexpr uint32_t stride =
        mesh ? sizeof(VkDrawMeshTasksIndirectCommandEXT)
             : sizeof(VkDrawIndexedIndirectCommand);

    vkCmdPushConstants(cmd, layout, stages, 0, sizeof(uint32_t), &baseOffset);

    if constexpr (!mesh)
        vkCmdBindIndexBuffer(cmd, rendererState.globalGeometryPool.indexBuffer,
            0, VK_INDEX_TYPE_UINT32);

    const uint32_t maxDraws = rendererState.indirectBuffer.capacity;
    const VkBuffer indirectBuffer =
        rendererState.indirectBuffer.buffer[rendererState.frameIndex];
    const VkDeviceSize indirectOffset =
        static_cast<VkDeviceSize>(partition) * maxDraws * stride;

    if constexpr (Submission == AnoDrawSubmission::counted) {
        const VkBuffer countBuffer =
            rendererState.culling.drawCountBuffer[rendererState.frameIndex];
        const VkDeviceSize countOffset =
            static_cast<VkDeviceSize>(partition) * sizeof(uint32_t);
        if constexpr (mesh)
            pfnVkCmdDrawMeshTasksIndirectCountEXT(cmd, indirectBuffer,
                indirectOffset, countBuffer, countOffset, maxDraws, stride);
        else
            vkCmdDrawIndexedIndirectCount(cmd, indirectBuffer, indirectOffset,
                countBuffer, countOffset, maxDraws, stride);
    } else {
        if constexpr (mesh)
            pfnVkCmdDrawMeshTasksIndirectEXT(cmd, indirectBuffer,
                indirectOffset, entityCount, stride);
        else
            vkCmdDrawIndexedIndirect(cmd, indirectBuffer, indirectOffset,
                entityCount, stride);
    }
}

struct AnoDrawKernelRegistry final {
    AnoDrawKernel kernels[ANO_DRAW_GEOMETRY_COUNT][ANO_DRAW_SUBMISSION_COUNT];
};

consteval AnoDrawKernelRegistry ano_reflect_draw_kernels()
{
    AnoDrawKernelRegistry result{};
    static constexpr auto profiles =
        std::define_static_array(std::meta::enumerators_of(^^AnoDrawProfile));
    template for (constexpr auto profile : profiles) {
        if constexpr ([:profile:] != AnoDrawProfile::count) {
            constexpr AnoDrawProfileSpec spec = ano_draw_profile_spec<profile>();
            result.kernels[static_cast<size_t>(spec.geometry)]
                          [static_cast<size_t>(spec.submission)] =
                &record_draw_kernel<spec.geometry, spec.submission>;
        }
    }
    for (const auto& geometry : result.kernels)
        for (AnoDrawKernel kernel : geometry)
            if (kernel == nullptr)
                __builtin_abort();
    return result;
}

static constexpr AnoDrawKernelRegistry drawKernelRegistry =
    ano_reflect_draw_kernels();
static AnoDrawKernel selectedDrawKernel;

void ano_select_view_draw_profile(void)
{
    const AnoDrawGeometry geometry = !ctx.deviceCapabilities.meshShader
        ? AnoDrawGeometry::vertex
        : rendererState.taskCull ? AnoDrawGeometry::task
                                 : AnoDrawGeometry::mesh;
    const AnoDrawSubmission submission = ctx.deviceCapabilities.drawIndirectCount
        ? AnoDrawSubmission::counted : AnoDrawSubmission::uncounted;
    selectedDrawKernel =
        drawKernelRegistry.kernels[static_cast<size_t>(geometry)]
                                  [static_cast<size_t>(submission)];
}

template<AnoFramePass Pass>
static inline void record_view_compute_pass(VkCommandBuffer cmd, uint32_t entityCount,
                                            ViewResources* vr)
{
    constexpr RenderPassDef pass = ano_frame_pass<Pass>();
    constexpr PipelineType Prototype = pass.prototype;
    static_assert(pass.type == PASS_COMPUTE && pass.perView);
    static_assert(Prototype == PIPELINE_COMPUTE_LIGHTCULL);
    if (entityCount == 0 || rendererState.asyncLc)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        rendererState.prototypes[Prototype].implementations[pass.implementationIndex].pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        rendererState.prototypes[Prototype].layout, 0, 1, &vr->lightcullSet, 0, NULL);
    vkCmdDispatch(cmd, (ANO_CLUSTER_COUNT + 63u) / 64u, 1, 1);
    ano_record_frame_compute_barrier<Pass>(cmd,
        ctx.deviceCapabilities.meshShader, rendererState.taskCull);
}

template<AnoFramePass Pass>
static inline void record_frame_attachment_barriers(
    VkCommandBuffer cmd, uint32_t v, ViewResources* vr)
{
    constexpr AnoFrameAttachmentBarrierPlan plan =
        ano_frame_attachment_barriers<Pass>();
    if constexpr (plan.count > 0) {
        VkImageMemoryBarrier barriers[plan.count] = {};
        uint32_t barrierCount = 0;
        template for (constexpr auto reflectedAttachment :
                      frame_attachment_enumerators) {
            constexpr AnoFrameAttachment attachment = [:reflectedAttachment:];
            if constexpr (attachment != AnoFrameAttachment::count) {
                constexpr AnoFrameAttachmentBarrier contract =
                    plan.barriers[static_cast<size_t>(attachment)];
                if constexpr (contract.required) {
                    VkImageMemoryBarrier& barrier = barriers[barrierCount++];
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.srcAccessMask = contract.srcAccess;
                    barrier.dstAccessMask = contract.dstAccess;
                    barrier.oldLayout = contract.oldLayout;
                    barrier.newLayout = contract.newLayout;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = 1;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = 1;
                    if constexpr (attachment == AnoFrameAttachment::depth) {
                        barrier.image = vr->depthImage;
                        barrier.subresourceRange.aspectMask =
                            VK_IMAGE_ASPECT_DEPTH_BIT;
                    } else if constexpr (attachment
                                         == AnoFrameAttachment::color) {
                        barrier.image = rendererState.colorImage[v];
                        barrier.subresourceRange.aspectMask =
                            VK_IMAGE_ASPECT_COLOR_BIT;
                    } else {
                        static_assert(attachment == AnoFrameAttachment::pick);
                        barrier.image = rendererState.pickIdImage[v];
                        barrier.subresourceRange.aspectMask =
                            VK_IMAGE_ASPECT_COLOR_BIT;
                    }
                }
            }
        }
        vkCmdPipelineBarrier(cmd, plan.srcStages, plan.dstStages,
            VK_DEPENDENCY_BY_REGION_BIT, 0, NULL, 0, NULL,
            plan.count, barriers);
    }
}

template<AnoFramePass Pass>
static inline void begin_graphics_batch(
    VkCommandBuffer cmd, uint32_t v, ViewResources* vr)
{
    constexpr RenderPassDef pass = ano_frame_pass<Pass>();
    constexpr AnoFrameAttachmentBarrierPlan plan =
        ano_frame_attachment_barriers<Pass>();
    constexpr RenderPassDef lastPass = ano_frame_pass<plan.lastPass>();
    static_assert(pass.type == PASS_GRAPHICS && pass.perView);
    static_assert(pass.colorAttachmentCount <= 2);
    static_assert(plan.beginsRendering);
    static_assert(lastPass.type == PASS_GRAPHICS);

    record_frame_attachment_barriers<Pass>(cmd, v, vr);

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
    VkClearValue clearDepth = {};
    clearDepth.depthStencil.depth = 1.0f;

    VkRenderingAttachmentInfo color[2] = {};
    if constexpr (pass.colorAttachmentCount > 0) {
        color[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color[0].imageView = rendererState.colorView[v];
        color[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color[0].resolveMode = lastPass.resolveMode;
        if constexpr (lastPass.resolveMode != VK_RESOLVE_MODE_NONE) {
            color[0].resolveImageView = vr->hdrColorView;
            color[0].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        color[0].loadOp = pass.colorLoadOp;
        color[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color[0].clearValue = clearColor;
    }

    if constexpr (pass.colorAttachmentCount == 2) {
        color[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color[1].imageView = rendererState.pickIdView[v];
        color[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color[1].loadOp = pass.colorLoadOp;
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
    depthAttachment.loadOp = pass.depthLoadOp;
    depthAttachment.storeOp = lastPass.depthStoreOp;
    depthAttachment.clearValue = clearDepth;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = (VkOffset2D){0, 0};
    renderingInfo.renderArea.extent = rendererState.viewExtent[v];
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = pass.colorAttachmentCount;
    renderingInfo.pColorAttachments = pass.colorAttachmentCount == 0 ? NULL : color;
    renderingInfo.pDepthAttachment = &depthAttachment;
    renderingInfo.pStencilAttachment = NULL;

    vkCmdBeginRendering(cmd, &renderingInfo);
}

template<AnoFramePass Pass>
static inline void record_graphics_pass(VkCommandBuffer cmd, uint32_t v,
                                        uint32_t entityCount, uint32_t drawSlotCount,
                                        ViewResources* vr)
{
    constexpr RenderPassDef pass = ano_frame_pass<Pass>();
    constexpr PipelineType Prototype = pass.prototype;
    constexpr AnoPipelineSpec pipeline = ano_pipeline_spec<Prototype>();
    static_assert(pass.type == PASS_GRAPHICS && pass.perView);
    static_assert(pipeline.kind == AnoPipelineKind::graphics);
    static_assert(pipeline.drawSlot != ANO_NO_DRAW_SLOT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        rendererState.prototypes[Prototype].implementations[pass.implementationIndex].pipeline);

    VkViewport viewport = {};
    viewport.width = (float)rendererState.viewExtent[v].width;
    viewport.height = (float)rendererState.viewExtent[v].height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = rendererState.viewExtent[v];
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        rendererState.prototypes[Prototype].layout, 0, 1, &vr->globalSet, 0, NULL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        rendererState.prototypes[Prototype].layout, 2, 1,
        &rendererState.frames[rendererState.frameIndex].shadow.geomSet, 0, NULL);

    if (entityCount > 0) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            rendererState.prototypes[Prototype].layout, 1, 1,
            &rendererState.bindlessTextures.set, 0, NULL);

        constexpr uint32_t slot = pipeline.drawSlot;
        uint32_t partition = v * drawSlotCount + slot;
        uint32_t baseOffset = partition * rendererState.culling.maxEntities;
        ano::assume(selectedDrawKernel != nullptr);
        selectedDrawKernel(cmd, rendererState.prototypes[Prototype].layout,
            baseOffset, partition, entityCount);
    }

    if constexpr (Prototype == PIPELINE_ADDITIVE)
        ano_vk_text_record_world(&rendererState, cmd, rendererState.frameIndex, v);
}

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

        // The reflected declaration order is light-cull followed by the geometry lanes.
        template for (constexpr auto reflectedPass : view_pass_enumerators) {
            constexpr AnoFramePass passId = [:reflectedPass:];
            if constexpr (passId != ANO_FRAME_PASS_COUNT) {
                constexpr RenderPassDef pass = ano_frame_pass<passId>();
                if constexpr (pass.perView && pass.type == PASS_COMPUTE)
                    record_view_compute_pass<passId>(cmd, entityCount, vr);
                else if constexpr (pass.perView && pass.type == PASS_GRAPHICS) {
                    constexpr AnoFrameAttachmentBarrierPlan plan =
                        ano_frame_attachment_barriers<passId>();
                    if constexpr (plan.beginsRendering)
                        begin_graphics_batch<passId>(cmd, v, vr);
                    record_graphics_pass<passId>(
                        cmd, v, entityCount, drawSlotCount, vr);
                    if constexpr (plan.endsRendering)
                        vkCmdEndRendering(cmd);
                }
            }
        }

        // Copy the cursor texel from view 0's resolved id image into this frame's readback buffer. Skip on a degenerate extent.
        if (v == 0 && rendererState.imageExtent.width > 0 && rendererState.imageExtent.height > 0) {
            VkImageMemoryBarrier toSrc = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            toSrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toSrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSrc.image = vr->pickIdResolveImage;
            toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
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

            VkImageMemoryBarrier toColor = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            toColor.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toColor.image = vr->pickIdResolveImage;
            toColor.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
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

// Composite: tonemap each view HDR -> swapchain + text/UI overlay.
void ano_record_composite(VkCommandBuffer cmd, uint32_t imageIndex)
{
    // --- Composite: tonemap each view's HDR target onto the swapchain ---
    // View 0 fullscreen; aux PiP bottom-right. ACES tonemap per view (viewport+scissor).
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
                // Aux inset: stack up right edge from bottom.
                // Skip unfit; idx+1 vs H/stride (no wrap).
                uint32_t idx = v - 1u;
                uint32_t stride = insetH + margin;      // one stacked row
                if (insetW == 0u || insetH == 0u) continue;
                if (W < insetW + margin) continue;
                if (idx + 1u > H / stride) continue;
                int32_t x = (int32_t)(W - insetW - margin);
                int32_t y = (int32_t)(H - stride * (idx + 1u));
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
