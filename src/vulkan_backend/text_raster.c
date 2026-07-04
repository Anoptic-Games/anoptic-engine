/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Text overlay plumbing (FONT_RENDER.md step 5). CPU side: FreeType init + font bake
// at renderer init. GPU side: static glyph buffers, per-frame frame-data buffers and
// overlay images, the PIPELINE_COMPUTE_TEXTRASTER prototype, and a composite blend
// pipeline sharing the tonemap set/pipeline layout.

#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/instance/instanceInit.h"
#include "vulkan_backend/instance/pipeline.h"
#include "vulkan_backend/texture/texture.h"
#include "vulkan_backend/vertex/vertex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anoptic_filesystem.h>
#include <anoptic_strings.h>
#include <anoptic_text.h>

#define ANO_TEXT_FONT_REL      "resources/fonts/Geist/static/Geist-Regular.ttf"
#define ANO_TEXT_RUNE_FONT_REL "resources/fonts/NotoSansRunic/NotoSansRunic-Regular.ttf"
// Frame-data capacity: ~21k glyph instances, rewritten wholesale on text change.
#define ANO_TEXT_FRAME_BYTES (1u << 20)

// Push-constant block shared with textraster.comp (16 B).
typedef struct TextRasterPush {
    uint32_t instanceCount;
    uint32_t flags;
    uint32_t extentW;
    uint32_t extentH;
} TextRasterPush;

// TextRasterPush.flags bits (mirrored in textraster.comp).
#define ANO_TEXT_RASTER_OPAQUE 0x1u // opaque black backdrop for the screenshot self-test

// Region split: the OSD/pending path owns instance indices [0, ANO_TEXT_WORLD_FIRST),
// the world panel's static instances sit from there up.
#define ANO_TEXT_WORLD_FIRST 8192u
static_assert(ANO_TEXT_WORLD_FIRST == ANO_RENDER_TEXT_MAX,
              "the public screen-text capacity is the pending region size");

// Push-constant block shared with textworld.vert/.frag (88 B).
typedef struct TextWorldPush {
    float    mvp[4][4]; // proj * view * model, this view
    float    panel[4];  // panel pixel W,H | panel world W,H
    uint32_t first;     // == ANO_TEXT_WORLD_FIRST
    uint32_t count;
} TextWorldPush;

// World panel: text shaped in a virtual pixel space, mapped onto a quad of the given
// world dimensions. Same shaper conventions as the overlay.
#define ANO_TEXT_PANEL_PX_W   768.0f
#define ANO_TEXT_PANEL_PX_H   352.0f
#define ANO_TEXT_PANEL_WORLD_W  6.00f
#define ANO_TEXT_PANEL_WORLD_H  2.75f
// Line 4 is the full 24-rune Elder Futhark, the world lane's unicode proof.
static const char g_worldText[] =
    "Scanline Sweeper\n"
    "world-space lane\n"
    "AV LT To Wa \"kerned\"\n"
    "\u16A0\u16A2\u16A6\u16A8\u16B1\u16B2\u16B7\u16B9\u16EB"
    "\u16BA\u16BE\u16C1\u16C3\u16C7\u16C8\u16C9\u16CA\u16EB"
    "\u16CF\u16D2\u16D6\u16D7\u16DA\u16DC\u16DE\u16DF";

// Static torture text, pinned by ANO_TEXT_DEMO. Covers every baked codepoint,
// the stable target for the offline pixel-compare harness.
static const char g_demoText[] =
    "Anoptic Engine :: Scanline Sweeper v0\n"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz\n"
    "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
#define ANO_TEXT_DEMO_SIZE_PX 36.0f
static const float g_demoOrigin[2] = { 48.0f, 96.0f };

// On-screen readout placement (step 8 stats mirror + the boot line).
static const float g_osdOrigin[2] = { 24.0f, 40.0f };
#define ANO_TEXT_OSD_SIZE_PX 22.0f

static bool g_textPinned; // ANO_TEXT_DEMO: hold the harness text, ignore later sets

// Recomposes the pending canonical after any source changed. The OSD region
// [0, textOsdCount) is already in place, logic blocks append after it in registry
// order, truncating at the region cap. The demo pin keeps the canvas OSD-only.
static void text_blocks_append(RendererState* state)
{
    uint32_t cap = ANO_TEXT_WORLD_FIRST;
    uint32_t total = state->textOsdCount < cap ? state->textOsdCount : cap;
    if (!g_textPinned)
    {
        for (uint32_t i = 0; i < state->textBlockCount && total < cap; i++)
        {
            const RenderTextBlock* b = state->textBlocks[i].blk;
            uint32_t n = b->count < cap - total ? b->count : cap - total;
            memcpy(state->textPending + total, b->instances, (size_t)n * sizeof(AnoGlyphInstance));
            total += n;
        }
    }
    state->textPendingCount = total;
    state->textVersion++;
}

void ano_vk_text_set(RendererState* state, anostr_t text, float sizePx,
                     const float origin[2], const float color[4])
{
    if (!state->textOverlay || state->textPending == NULL || g_textPinned)
        return;
    uint32_t cap = ANO_TEXT_WORLD_FIRST; // the world panel owns the region above
    uint32_t count = ano_text_shape(&state->textBake, text, sizePx, origin, color,
                                    state->textPending, cap, NULL);
    state->textOsdCount = count < cap ? count : cap;
    text_blocks_append(state);
}

void ano_vk_text_set_runs(RendererState* state, anostr_t text, const AnoTextRun* runs,
                          uint32_t runCount, const float origin[2])
{
    if (!state->textOverlay || state->textPending == NULL || g_textPinned)
        return;
    uint32_t cap = ANO_TEXT_WORLD_FIRST;
    uint32_t count = ano_text_shape_runs(&state->textBake, text, runs, runCount, origin,
                                         state->textPending, cap, NULL);
    state->textOsdCount = count < cap ? count : cap;
    text_blocks_append(state);
}

// Adopts a logic-submitted block (RCMD_TEXT_SET), replacing text_id's contents.
// Ownership of blk (one mi allocation) transfers here unconditionally. Freed on
// replace/clear/teardown, or immediately when the overlay is off or the registry is
// full. Render thread only.
void ano_vk_text_block_set(RendererState* state, uint32_t text_id, const RenderTextBlock* blk)
{
    if (blk == NULL)
        return;
    if (!state->textOverlay || state->textPending == NULL)
    {
        mi_free((void*)blk);
        return;
    }
    for (uint32_t i = 0; i < state->textBlockCount; i++)
    {
        if (state->textBlocks[i].id == text_id)
        {
            mi_free((void*)state->textBlocks[i].blk);
            state->textBlocks[i].blk = blk;
            text_blocks_append(state);
            return;
        }
    }
    if (state->textBlockCount >= ANO_TEXT_MAX_BLOCKS)
    {
        printf("Text bridge: block registry full (%u); text_id %u dropped.\n",
               ANO_TEXT_MAX_BLOCKS, text_id);
        mi_free((void*)blk);
        return;
    }
    state->textBlocks[state->textBlockCount].id = text_id;
    state->textBlocks[state->textBlockCount].blk = blk;
    state->textBlockCount++;
    text_blocks_append(state);
}

// Removes a block (RCMD_TEXT_CLEAR), idempotent. Order-preserving compaction keeps
// the composite in creation order.
void ano_vk_text_block_clear(RendererState* state, uint32_t text_id)
{
    for (uint32_t i = 0; i < state->textBlockCount; i++)
    {
        if (state->textBlocks[i].id != text_id)
            continue;
        mi_free((void*)state->textBlocks[i].blk);
        for (uint32_t j = i + 1; j < state->textBlockCount; j++)
            state->textBlocks[j - 1] = state->textBlocks[j];
        state->textBlockCount--;
        if (state->textOverlay && state->textPending != NULL)
            text_blocks_append(state);
        return;
    }
}

void ano_vk_text_frame_refresh(RendererState* state, uint32_t frameIndex)
{
    if (!state->textOverlay || state->textPending == NULL)
        return;
    PerFrameResources* fr = &state->frames[frameIndex];
    if (fr->textSlotVersion != state->textVersion)
    {
        memcpy(fr->textFrameMapped, state->textPending,
               (size_t)state->textPendingCount * sizeof(AnoGlyphInstance));
        fr->textSlotVersion = state->textVersion;
    }
    state->textInstanceCount = state->textPendingCount;
}

// createDataBuffer with optional CONCURRENT graphics+compute sharing, which sidesteps
// the queue-family ownership transfer for buffers the async lane's compute queue
// reads. Exclusive when shared is false.
static bool text_create_buffer(VulkanContext* ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags props, bool shared,
                               VkBuffer* buffer, GpuAllocation* alloc)
{
    uint32_t fams[2] = { ctx->queueFamilyIndices.graphicsFamily, ctx->queueFamilyIndices.computeFamily };
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = usage,
        .sharingMode = shared ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE };
    if (shared)
    {
        bi.queueFamilyIndexCount = 2;
        bi.pQueueFamilyIndices = fams;
    }
    if (vkCreateBuffer(ctx->device, &bi, NULL, buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(ctx->device, *buffer, &req);
    *alloc = gpu_alloc(&gpuAllocator, req, props);
    if (alloc->memory == VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(ctx->device, *buffer, alloc->memory, alloc->offset);
    return true;
}

// Builds the compute raster prototype: 3 SSBOs + 1 storage image, 16 B push.
// Mirrors the lightcull recipe.
static bool text_init_raster_pipeline(VulkanContext* ctx, RendererState* state)
{
    VkDescriptorSetLayoutBinding bindings[4] = {};
    for (uint32_t b = 0; b < 4; ++b)
    {
        bindings[b].binding = b;
        bindings[b].descriptorType = (b == 3) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                              : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[b].descriptorCount = 1;
        // The world lane's fragment shader reads the three glyph buffers through the
        // same set. The overlay storage image stays compute-only.
        bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
                               | ((b < 3) ? VK_SHADER_STAGE_FRAGMENT_BIT : 0u);
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

// Builds the composite blend pipeline: the tonemap pipeline's twin with premultiplied
// src-over blending and overlay.frag sampling the overlay image.
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

// Builds the world-space text pipeline: the additive lane's raster recipe with
// premultiplied src-over blending, a bufferless quad, and the raster set layout for
// the glyph buffers. Its own layout carries the 88 B push.
static bool text_init_world_pipeline(VulkanContext* ctx, RendererState* state)
{
    VkPushConstantRange push = {};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(TextWorldPush);
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &state->textRasterSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(ctx->device, &layoutInfo, NULL, &state->textWorldLayout) != VK_SUCCESS)
        return false;

    struct Buffer vertCode, fragCode;
    if (!loadFile("resources/shaders/textworld.vert.spv", &vertCode))
        return false;
    if (!loadFile("resources/shaders/textworld.frag.spv", &fragCode))
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
    rasterizer.cullMode = VK_CULL_MODE_NONE; // sign readable from both sides
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = ctx->msaaSamples;

    // Premultiplied src-over onto the lit HDR scene.
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

    // Depth-tested against the scene, no write: a blend lane.
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkFormat colorFormat = ANO_HDR_COLOR_FORMAT;
    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = state->depthFormat;

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
    pipelineInfo.layout = state->textWorldLayout;

    VkResult r = vkCreateGraphicsPipelines(ctx->device, state->tonemapCache, 1, &pipelineInfo,
                                           NULL, &state->textWorldPipeline);
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

    // CPU side: bake blobs live on textHeap.
    state->textHeap = mi_heap_new();
    ano_fspath game = ano_fs_gamepath();
    char fontPath[512];
    snprintf(fontPath, sizeof fontPath, "%s/%s", game.str, ANO_TEXT_FONT_REL);
    AnoFontId font = 0;
    if (state->textHeap == NULL || ano_text_init() != 0
        || (font = ano_text_font_load(anostr_view(fontPath, strlen(fontPath)))) == 0)
    {
        printf("Text overlay disabled: font load failed ('%s').\n", fontPath);
        state->textOverlay = false;
        state->asyncText = false;
        return true;
    }

    // Bake coverage: ASCII, Latin-1, and core Cyrillic from Geist, plus the Runic
    // block from Noto Sans Runic. A missing rune font degrades to the Geist ranges
    // and runes render as gaps, not tofu.
    char runePath[512];
    snprintf(runePath, sizeof runePath, "%s/%s", game.str, ANO_TEXT_RUNE_FONT_REL);
    AnoFontId runeFont = ano_text_font_load(anostr_view(runePath, strlen(runePath)));
    if (runeFont == 0)
        printf("Text overlay: rune font missing ('%s'); Runic will not render.\n", runePath);
    AnoBakeRange ranges[4] = {
        { .font = font, .first = 0x0020, .last = 0x007E },     // ASCII
        { .font = font, .first = 0x00A0, .last = 0x00FF },     // Latin-1 supplement
        { .font = font, .first = 0x0400, .last = 0x045F },     // Cyrillic core + Ё/ё
        { .font = runeFont, .first = 0x16A0, .last = 0x16F8 }, // Runic (Elder Futhark+)
    };
    uint32_t rangeCount = runeFont != 0 ? 4u : 3u;
    if (ano_text_font_bake_ranges(ranges, rangeCount, state->textHeap, &state->textBake) != 0)
    {
        printf("Text overlay disabled: font bake failed.\n");
        state->textOverlay = false;
        state->asyncText = false;
        return true;
    }

    // Static glyph data: one-shot staged upload to device-local, CONCURRENT-shared
    // with the compute family when the async lane will read them.
    VkDeviceSize curveBytes = (VkDeviceSize)state->textBake.pointCount * sizeof(uint32_t);
    VkDeviceSize glyphBytes = (VkDeviceSize)state->textBake.glyphCount * sizeof(AnoGlyphEntry);
    bool ok = text_create_buffer(ctx, curveBytes,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, state->asyncText,
                   &state->textCurveBuffer, &state->textCurveAlloc)
            && stagingTransfer(ctx, state->textBake.points, state->textCurveBuffer, curveBytes)
            && text_create_buffer(ctx, glyphBytes,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, state->asyncText,
                   &state->textGlyphBuffer, &state->textGlyphAlloc)
            && stagingTransfer(ctx, state->textBake.glyphs, state->textGlyphBuffer, glyphBytes);

    // Per-frame frame data: host-visible, persistently mapped.
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
        // Partially-created objects are handle-guarded in teardown.
        printf("Text overlay disabled: GPU resource/pipeline creation failed.\n");
        state->textOverlay = false;
        state->asyncText = false;
        state->textWorld = false;
        return true;
    }

    // World-space lane: its pipeline, plus the demo panel shaped ONCE into the upper
    // region of every frame slot (no slot is in flight during init, so direct writes
    // are safe). Failure drops just this lane.
    state->textWorld = getenv("ANO_FORCE_NO_TEXT_WORLD") == NULL;
    if (state->textWorld && !text_init_world_pipeline(ctx, state))
    {
        printf("Text overlay: world lane pipeline failed, disabled.\n");
        state->textWorld = false;
    }
    if (state->textWorld)
    {
        // Standing styled-runs demo: mixed sizes across lines, and line 3's color
        // splits land INSIDE the kern pairs (A|V, L|T, T|o, W|a) to prove same-size
        // runs stay kerned. The futhark run owns its leading '\n' so the line step
        // takes the runes' size.
        static const AnoTextRun worldRuns[] = {
            { 17, 72.0f, { 1.00f, 0.78f, 0.32f, 1.0f } }, // "Scanline Sweeper\n"
            { 17, 48.0f, { 0.90f, 0.90f, 0.90f, 1.0f } }, // "world-space lane\n"
            {  1, 60.0f, { 1.00f, 0.42f, 0.35f, 1.0f } }, // "A
            {  3, 60.0f, { 0.45f, 0.75f, 1.00f, 1.0f } }, //  V L
            {  3, 60.0f, { 0.45f, 0.95f, 0.60f, 1.0f } }, //  T T
            {  3, 60.0f, { 1.00f, 0.85f, 0.30f, 1.0f } }, //  o W
            { 10, 60.0f, { 0.80f, 0.60f, 1.00f, 1.0f } }, //  a "kerned""
            { 79, 40.0f, { 0.55f, 0.85f, 1.00f, 1.0f } }, // "\n" + the Elder Futhark
        };
        static_assert(17 + 17 + 1 + 3 + 3 + 3 + 10 + 79 == sizeof g_worldText - 1,
                      "world runs cover the panel text exactly");
        const float worldOrigin[2] = { 24.0f, 76.0f };
        uint32_t worldCap = ANO_TEXT_FRAME_BYTES / (uint32_t)sizeof(AnoGlyphInstance)
                          - ANO_TEXT_WORLD_FIRST;
        anostr_t worldText = anostr_view(g_worldText, sizeof g_worldText - 1);
        uint32_t count = 0;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            AnoGlyphInstance* dst = (AnoGlyphInstance*)state->frames[i].textFrameMapped
                                  + ANO_TEXT_WORLD_FIRST;
            count = ano_text_shape_runs(&state->textBake, worldText, worldRuns,
                                        (uint32_t)(sizeof worldRuns / sizeof worldRuns[0]),
                                        worldOrigin, dst, worldCap, NULL);
        }
        state->textWorldCount = count < worldCap ? count : worldCap;
    }

    // Async lane objects (step 7): the timeline the main submit waits on, plus a
    // per-frame raster CB on the compute-family pool. Failure downgrades to the
    // in-frame record, the overlay stays on.
    if (state->asyncText)
    {
        VkSemaphoreTypeCreateInfo timelineInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0 };
        VkSemaphoreCreateInfo timelineSem = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &timelineInfo };
        VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = state->computeCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
        bool asyncOk = vkCreateSemaphore(ctx->device, &timelineSem, NULL, &state->textTimeline) == VK_SUCCESS;
        for (uint32_t i = 0; asyncOk && i < MAX_FRAMES_IN_FLIGHT; i++)
            asyncOk = vkAllocateCommandBuffers(ctx->device, &cai, &state->frames[i].textCommandBuffer) == VK_SUCCESS;
        if (!asyncOk)
        {
            printf("Text overlay: async lane objects failed, falling back in-frame.\n");
            state->asyncText = false;
        }
    }

    // Pending canonical text (step 8): full frame-buffer capacity, dies with textHeap.
    // ANO_TEXT_DEMO pins the torture text for the offline pixel-compare harness.
    // The default boot line is replaced by the profiling mirror at its first print.
    uint32_t cap = ANO_TEXT_FRAME_BYTES / (uint32_t)sizeof(AnoGlyphInstance);
    state->textPending = mi_heap_malloc(state->textHeap, (size_t)cap * sizeof(AnoGlyphInstance));
    if (state->textPending == NULL)
    {
        printf("Text overlay disabled: pending-text allocation failed.\n");
        state->textOverlay = false;
        state->asyncText = false;
        return true;
    }
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (getenv("ANO_TEXT_DEMO") != NULL)
    {
        ano_vk_text_set(state, anostr_view(g_demoText, sizeof g_demoText - 1),
                        ANO_TEXT_DEMO_SIZE_PX, g_demoOrigin, white);
        g_textPinned = true;
    }
    else
    {
        ano_vk_text_set_lit(state, "Anoptic Engine :: Scanline Sweeper",
                            ANO_TEXT_OSD_SIZE_PX, g_osdOrigin, white);
    }
    state->textFlags = (getenv("ANO_TEXT_OPAQUE") != NULL) ? ANO_TEXT_RASTER_OPAQUE : 0u;

    printf("Text overlay: on (%u glyphs, %u curve points, %.1f KiB static, %u instances, %s%s)\n",
           state->textBake.glyphCount, state->textBake.pointCount,
           (double)(curveBytes + glyphBytes) / 1024.0, state->textPendingCount,
           state->asyncText ? "async lane" : "in-frame",
           state->textWorld ? ", world panel" : "");
    return true;
}

void ano_vk_text_record_world(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex,
                              uint32_t view)
{
    if (!state->textWorld || state->textWorldCount == 0)
        return;
    // Spinning sign: yaw from the UBO clock, model = T(position) * R(yaw).
    // MVP composes CPU-side (proj * view * model, column vectors).
    mat4 T, R, pv, model;
    TextWorldPush push;
    translate(T, 0.0f, 2.6f, 0.0f);
    translate(R, 0.0f, 0.0f, 0.0f);
    rotateMatrix(R, 'Y', state->uboData[0].time * 0.7f);
    multiplyMat4(model, T, R);
    multiplyMat4(pv, state->uboData[view].proj, state->uboData[view].view);
    multiplyMat4(push.mvp, pv, model);
    push.panel[0] = ANO_TEXT_PANEL_PX_W;
    push.panel[1] = ANO_TEXT_PANEL_PX_H;
    push.panel[2] = ANO_TEXT_PANEL_WORLD_W;
    push.panel[3] = ANO_TEXT_PANEL_WORLD_H;
    push.first = ANO_TEXT_WORLD_FIRST;
    push.count = state->textWorldCount;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state->textWorldPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state->textWorldLayout,
                            0, 1, &state->frames[frameIndex].textRasterSet, 0, NULL);
    vkCmdPushConstants(cmd, state->textWorldLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof push, &push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

void ano_vk_text_create_overlay(VulkanContext* ctx, RendererState* state)
{
    if (!state->textOverlay)
        return;
    // Async lane: written on compute, sampled on graphics, so CONCURRENT skips the
    // ownership transfer. Exclusive when in-frame.
    uint32_t shareFamilies[2] = { ctx->queueFamilyIndices.graphicsFamily, ctx->queueFamilyIndices.computeFamily };
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        PerFrameResources* fr = &state->frames[i];
        createImageShared(ctx, &swapchainAllocator, state->imageExtent.width, state->imageExtent.height,
                    1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &fr->textOverlayImage, &fr->textOverlayAlloc, false,
                    shareFamilies, state->asyncText ? 2u : 0u);
        fr->textOverlayView = createImageView(ctx->device, fr->textOverlayImage,
                                              VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        // Seed the composite's resting layout. The per-frame record rewrites from UNDEFINED.
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

// The raster pass body, queue-agnostic: clear, dispatch, hand off to the composite's
// sampled read. On the graphics queue the final barrier targets FRAGMENT_SHADER.
// A compute-only family has no such stage, so the barrier only transitions the layout
// and the textTimeline wait carries the cross-queue dependency.
static void text_record_raster(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex,
                               bool asyncQueue)
{
    PerFrameResources* fr = &state->frames[frameIndex];

    // Prior readers of this slot's overlay retired with its frame fence.
    // Contents are stale anyway, so UNDEFINED discards them.
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

    // Raster dispatch: 8x8 pixel tiles over the full overlay.
    TextRasterPush push = { .instanceCount = state->textInstanceCount, .flags = state->textFlags,
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
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = asyncQueue ? 0 : VK_ACCESS_SHADER_READ_BIT,
        .subresourceRange = range };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         asyncQueue ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &toSample);
}

void ano_vk_text_record(RendererState* state, VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!state->textOverlay || state->asyncText)
        return;
    text_record_raster(state, cmd, frameIndex, false);
}

void ano_vk_text_submit_async(VulkanContext* ctx, RendererState* state, uint32_t frameIndex,
                              uint64_t ordinal)
{
    if (!state->asyncText)
        return;
    // CB reuse is fence-safe: the frame fence retiring implies the prior text CB
    // retired (the main submit waited on textTimeline). The submit needs NO waits.
    // Frame data is CPU-written before this call and the overlay slot's prior reader
    // retired with the same fence.
    VkCommandBuffer cmd = state->frames[frameIndex].textCommandBuffer;
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &beginInfo);
    text_record_raster(state, cmd, frameIndex, true);
    vkEndCommandBuffer(cmd);

    VkTimelineSemaphoreSubmitInfo timelineInfo = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = 1, .pSignalSemaphoreValues = &ordinal };
    VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &timelineInfo,
        .commandBufferCount = 1, .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &state->textTimeline };
    if (vkQueueSubmit(ctx->computeQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        // Keep the timeline monotonic so the main submit's wait cannot deadlock.
        // A failed submit here is device-loss territory.
        printf("Failed to submit async text raster command buffer!\n");
        VkSemaphoreSignalInfo signalInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = state->textTimeline, .value = ordinal };
        vkSignalSemaphore(ctx->device, &signalInfo);
    }
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
    // Async lane: the timeline was drained in unInitVulkan, the CBs die with the pool.
    if (state->textTimeline != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(ctx->device, state->textTimeline, NULL);
        state->textTimeline = VK_NULL_HANDLE;
    }
    // Logic text blocks: adopted copies on the default mi heap, not textHeap.
    for (uint32_t i = 0; i < state->textBlockCount; i++)
        mi_free((void*)state->textBlocks[i].blk);
    state->textBlockCount = 0;
    // CPU side: FreeType down, bake blobs die with the heap.
    ano_text_shutdown();
    if (state->textHeap != NULL)
    {
        mi_heap_destroy(state->textHeap);
        state->textHeap = NULL;
    }
}
