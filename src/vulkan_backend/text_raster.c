/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text overlay plumbing (FONT_RENDER.md step 5). CPU side: FreeType init + Geist ASCII
// bake at renderer init (render thread == the text module's owner thread). GPU side:
// curve/directory blobs uploaded once to device-local EXCLUSIVE buffers (one consuming
// queue per boot mode), per-frame host-visible frame-data buffers, per-frame overlay
// images, the PIPELINE_COMPUTE_TEXTRASTER prototype (lightcull recipe), and a bespoke
// composite blend pipeline that shares the tonemap set/pipeline layout.

#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/instance/instanceInit.h"
#include "vulkan_backend/instance/pipeline.h"
#include "vulkan_backend/texture/texture.h"

#include <stdio.h>
#include <string.h>

#include <anoptic_filesystem.h>
#include <anoptic_text.h>

#define ANO_TEXT_FONT_REL "resources/fonts/Geist/static/Geist-Regular.ttf"
// Frame-data capacity: 4096 glyph instances (48 B each) leaves headroom for tile lists
// (step 6) in the same buffer. Rewritten wholesale at text-change cadence.
#define ANO_TEXT_FRAME_BYTES (1u << 20)

// Push-constant block shared with textraster.comp (16 B).
typedef struct TextRasterPush {
    uint32_t instanceCount;
    uint32_t flags;
    uint32_t extentW;
    uint32_t extentH;
} TextRasterPush;

// Builds the compute raster prototype: 3 SSBOs (curves, directory, frame data) + 1
// storage image (overlay), 16 B push. Mirrors the lightcull recipe.
static bool text_init_raster_pipeline(VulkanContext* ctx, RendererState* state)
{
    VkDescriptorSetLayoutBinding bindings[4] = {};
    for (uint32_t b = 0; b < 4; ++b)
    {
        bindings[b].binding = b;
        bindings[b].descriptorType = (b == 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                              : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &state->textRasterSetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange push = {};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.size = sizeof(TextRasterPush);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &state->textRasterSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL,
                               &state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].layout) != VK_SUCCESS)
        return false;

    state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].type = PIPELINE_COMPUTE_TEXTRASTER;
    state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].implementationCount = 1;
    state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].implementations = calloc(1, sizeof(PipelineImplementation));
    state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].supportedFeatures = PBR_FEATURE_NONE;
    if (state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].implementations == NULL)
        return false;

    struct Buffer code;
    if (!loadFile("resources/shaders/textraster.comp.spv", &code))
        return false;
    VkShaderModule module = createShaderModule(ctx->device, &code);

    VkPipelineCacheCreateInfo cacheInfo = {};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    vkCreatePipelineCache(ctx->device, &cacheInfo, NULL, &state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].cache);

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].layout;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";

    VkResult r = vkCreateComputePipelines(ctx->device, state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].cache,
                                          1, &pipelineInfo, NULL,
                                          &state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].implementations[0].pipeline);
    state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].implementations[0].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;

    ano_aligned_free(code.data);
    vkDestroyShaderModule(ctx->device, module, NULL);
    return r == VK_SUCCESS;
}

// Builds the composite blend pipeline: the tonemap pipeline's twin (bufferless triangle,
// dynamic viewport/scissor, swapchain format, shared tonemap set/pipeline layout) with
// premultiplied src-over blending and overlay.frag sampling the overlay image.
static bool text_init_overlay_pipeline(VulkanContext* ctx, RendererState* state)
{
    struct Buffer vertCode, fragCode;
    if (!loadFile("resources/shaders/tonemap.vert.spv", &vertCode))
        return false;
    if (!loadFile("resources/shaders/overlay.frag.spv", &fragCode))
    {
        ano_aligned_free(vertCode.data);
        return false;
    }
    VkShaderModule vertModule = createShaderModule(ctx->device, &vertCode);
    VkShaderModule fragModule = createShaderModule(ctx->device, &fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Premultiplied src-over: out = overlay.rgb + (1 - overlay.a) * dst.rgb.
    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkFormat colorFormat = state->imageFormat;
    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = state->tonemapLayout;

    VkResult r = vkCreateGraphicsPipelines(ctx->device, state->tonemapCache, 1, &pipelineInfo,
                                           NULL, &state->textOverlayPipeline);
    ano_aligned_free(vertCode.data);
    ano_aligned_free(fragCode.data);
    vkDestroyShaderModule(ctx->device, vertModule, NULL);
    vkDestroyShaderModule(ctx->device, fragModule, NULL);
    return r == VK_SUCCESS;
}

bool ano_vk_text_init(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
        return true;

    // CPU side: bake blobs live on textHeap (also the step-6 shaping source).
    state->textHeap = mi_heap_new();
    ano_fspath game = ano_fs_gamepath();
    char fontPath[512];
    snprintf(fontPath, sizeof fontPath, "%s/%s", game.str, ANO_TEXT_FONT_REL);
    AnoFontId font = 0;
    if (state->textHeap == NULL || ano_text_init() != 0
        || (font = ano_text_font_load(fontPath)) == 0
        || ano_text_font_bake(font, 32, 126, state->textHeap, &state->textBake) != 0)
    {
        printf("Text overlay disabled: font load/bake failed ('%s').\n", fontPath);
        state->textOverlay = false;
        return true;
    }

    // Static glyph data: one-shot staged upload to device-local.
    VkDeviceSize curveBytes = (VkDeviceSize)state->textBake.pointCount * sizeof(uint32_t);
    VkDeviceSize glyphBytes = (VkDeviceSize)state->textBake.glyphCount * sizeof(AnoGlyphEntry);
    bool ok = createDataBuffer(ctx, &gpuAllocator, curveBytes,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &state->textCurveBuffer, &state->textCurveAlloc)
            && stagingTransfer(ctx, state->textBake.points, state->textCurveBuffer, curveBytes)
            && createDataBuffer(ctx, &gpuAllocator, glyphBytes,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &state->textGlyphBuffer, &state->textGlyphAlloc)
            && stagingTransfer(ctx, state->textBake.glyphs, state->textGlyphBuffer, glyphBytes);

    // Per-frame frame data: host-visible, persistently mapped (uniform-ring pattern).
    for (uint32_t i = 0; ok && i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        GpuAllocation alloc;
        ok = createDataBuffer(ctx, &gpuAllocator, ANO_TEXT_FRAME_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              &state->frames[i].textFrameBuffer, &state->frames[i].textFrameAlloc);
        if (ok)
        {
            alloc = state->frames[i].textFrameAlloc;
            state->frames[i].textFrameMapped = alloc.mapped;
        }
    }

    if (!ok || !text_init_raster_pipeline(ctx, state) || !text_init_overlay_pipeline(ctx, state))
    {
        // Partially-created objects are handle-guarded in the teardown paths.
        printf("Text overlay disabled: GPU resource/pipeline creation failed.\n");
        state->textOverlay = false;
        return true;
    }

    printf("Text overlay: on (%u glyphs, %u curve points, %.1f KiB static)\n",
           state->textBake.glyphCount, state->textBake.pointCount,
           (double)(curveBytes + glyphBytes) / 1024.0);
    return true;
}

void ano_vk_text_create_overlay(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
        return;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        PerFrameResources* fr = &state->frames[i];
        createImage(ctx, &swapchainAllocator, state->imageExtent.width, state->imageExtent.height,
                    1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &fr->textOverlayImage, &fr->textOverlayAlloc, false);
        fr->textOverlayView = createImageView(ctx->device, fr->textOverlayImage,
                                              VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        // Seed the composite's resting layout; the per-frame record rewrites from UNDEFINED.
        if (!transitionImageLayout(ctx, VK_NULL_HANDLE, fr->textOverlayImage, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1))
        {
            printf("Failed to transition text overlay image layout!\n");
        }
    }
}

void ano_vk_text_destroy_overlay(VulkanContext* ctx, RendererState* state)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        PerFrameResources* fr = &state->frames[i];
        if (fr->textOverlayView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx->device, fr->textOverlayView, NULL);
            fr->textOverlayView = VK_NULL_HANDLE;
        }
        if (fr->textOverlayImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx->device, fr->textOverlayImage, NULL);
            fr->textOverlayImage = VK_NULL_HANDLE;
        }
        fr->textOverlayAlloc.memory = VK_NULL_HANDLE;
    }
}

void ano_vk_text_create_sets(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
        return;

    VkDescriptorSetLayout rasterLayouts[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout overlayLayouts[MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        rasterLayouts[i] = state->textRasterSetLayout;
        overlayLayouts[i] = state->tonemapSetLayout;
    }
    VkDescriptorSet rasterSets[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSet overlaySets[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = state->globalDescriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = rasterLayouts;
    if (vkAllocateDescriptorSets(ctx->device, &allocInfo, rasterSets) != VK_SUCCESS)
    {
        printf("Text overlay disabled: raster descriptor set allocation failed.\n");
        state->textOverlay = false;
        return;
    }
    allocInfo.pSetLayouts = overlayLayouts;
    if (vkAllocateDescriptorSets(ctx->device, &allocInfo, overlaySets) != VK_SUCCESS)
    {
        printf("Text overlay disabled: overlay descriptor set allocation failed.\n");
        state->textOverlay = false;
        return;
    }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        state->frames[i].textRasterSet = rasterSets[i];
        state->frames[i].textOverlaySet = overlaySets[i];
    }
    ano_vk_text_update_sets(ctx, state);
}

void ano_vk_text_update_sets(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
        return;
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        PerFrameResources* fr = &state->frames[i];

        VkDescriptorBufferInfo curveInfo = { state->textCurveBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo glyphInfo = { state->textGlyphBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo frameInfo = { fr->textFrameBuffer, 0, VK_WHOLE_SIZE };
        VkDescriptorImageInfo storageInfo = { VK_NULL_HANDLE, fr->textOverlayView, VK_IMAGE_LAYOUT_GENERAL };
        VkDescriptorImageInfo sampleInfo = { state->textureSampler, fr->textOverlayView,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

        VkWriteDescriptorSet writes[5] = {};
        for (uint32_t w = 0; w < 3; ++w)
        {
            writes[w].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[w].dstSet = fr->textRasterSet;
            writes[w].dstBinding = w;
            writes[w].descriptorCount = 1;
            writes[w].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        writes[0].pBufferInfo = &curveInfo;
        writes[1].pBufferInfo = &glyphInfo;
        writes[2].pBufferInfo = &frameInfo;
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = fr->textRasterSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[3].pImageInfo = &storageInfo;
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = fr->textOverlaySet;
        writes[4].dstBinding = 0;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].pImageInfo = &sampleInfo;
        vkUpdateDescriptorSets(ctx->device, 5, writes, 0, NULL);
    }
}

void ano_vk_text_record(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!state->textOverlay)
        return;
    PerFrameResources* fr = &state->frames[frameIndex];

    // Prior readers of this slot's overlay retired with its frame fence; contents are
    // stale by 3 frames either way, so UNDEFINED discards them.
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageMemoryBarrier toClear = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = fr->textOverlayImage,
        .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .subresourceRange = range };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &toClear);

    VkClearColorValue transparent = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } };
    vkCmdClearColorImage(cmd, fr->textOverlayImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &transparent, 1, &range);

    VkImageMemoryBarrier toCompute = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = fr->textOverlayImage,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .subresourceRange = range };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &toCompute);

    // Raster dispatch: 8x8 pixel tiles over the full overlay. Step-5 stub: instanceCount
    // stays 0 (threads early-out), exercising the pipeline/sets for validation.
    TextRasterPush push = { .instanceCount = 0, .flags = 0,
                            .extentW = state->imageExtent.width, .extentH = state->imageExtent.height };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].implementations[0].pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].layout,
                            0, 1, &fr->textRasterSet, 0, NULL);
    vkCmdPushConstants(cmd, state->prototypes[PIPELINE_COMPUTE_TEXTRASTER].layout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof push, &push);
    vkCmdDispatch(cmd, (state->imageExtent.width + 7u) / 8u, (state->imageExtent.height + 7u) / 8u, 1);

    VkImageMemoryBarrier toSample = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = fr->textOverlayImage,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .subresourceRange = range };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &toSample);
}

void ano_vk_text_record_composite(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!state->textOverlay)
        return;
    // Full-screen viewport/scissor: the PiP loop left the last inset's rect bound.
    VkViewport viewport = { 0.0f, 0.0f, (float)state->imageExtent.width,
                            (float)state->imageExtent.height, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, state->imageExtent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state->textOverlayPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state->tonemapLayout,
                            0, 1, &state->frames[frameIndex].textOverlaySet, 0, NULL);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void ano_vk_text_destroy(VulkanContext* ctx, RendererState* state)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (state->frames[i].textFrameBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx->device, state->frames[i].textFrameBuffer, NULL);
            state->frames[i].textFrameBuffer = VK_NULL_HANDLE;
            state->frames[i].textFrameMapped = NULL;
        }
    }
    if (state->textCurveBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx->device, state->textCurveBuffer, NULL);
        state->textCurveBuffer = VK_NULL_HANDLE;
    }
    if (state->textGlyphBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx->device, state->textGlyphBuffer, NULL);
        state->textGlyphBuffer = VK_NULL_HANDLE;
    }
    // CPU side: FreeType down (init thread == this thread), bake blobs die with the heap.
    ano_text_shutdown();
    if (state->textHeap != NULL)
    {
        mi_heap_destroy(state->textHeap);
        state->textHeap = NULL;
    }
}
