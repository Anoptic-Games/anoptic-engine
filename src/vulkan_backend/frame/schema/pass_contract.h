#ifndef ANO_FRAME_PASS_CONTRACT_H
#define ANO_FRAME_PASS_CONTRACT_H

#include <meta>
#include <stdint.h>

#include <anoptic_meta.h>
#include "vulkan_backend/components.h"

enum class AnoFrameAttachment : uint8_t {
    depth,
    color,
    pick,
    count,
};

enum class AnoFrameAccess : uint8_t {
    read,
    write,
    read_write,
};

enum class AnoFrameRenderBatch : uint8_t {
    depth,
    opaque,
    translucent,
    count,
};

struct AnoFrameAttachmentUse final {
    AnoFrameAttachment attachment;
    AnoFrameAccess access;
    VkImageLayout layout;
};

struct AnoFrameRenderBatchUse final {
    AnoFrameRenderBatch batch;
};

enum class AnoFrameComputeBarrierMode : uint8_t {
    emit,
    coalesce_with_next,
};

struct AnoFrameComputeBarrier final {
    AnoFrameComputeBarrierMode mode = AnoFrameComputeBarrierMode::emit;
    VkPipelineStageFlags fixedDstStages = 0;
    VkAccessFlags dstAccess = 0;
    bool geometryStages = false;
};

// Declaration order is execution order; each annotation is the complete pass contract.
typedef enum AnoFramePass : uint8_t
{
    ANO_FRAME_PASS_UPDATE [[=RenderPassDef{
        .type = PASS_COMPUTE,
        .prototype = PIPELINE_COMPUTE_UPDATE,
    }]]
    [[=AnoFrameComputeBarrier{
        .mode = AnoFrameComputeBarrierMode::emit,
        .fixedDstStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    }]] = 0,
    ANO_FRAME_PASS_SCATTER [[=RenderPassDef{
        .type = PASS_COMPUTE,
        .prototype = PIPELINE_COMPUTE_SCATTER,
    }]]
    [[=AnoFrameComputeBarrier{
        .mode = AnoFrameComputeBarrierMode::emit,
        .fixedDstStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    }]],
    ANO_FRAME_PASS_LIGHT_SETUP [[=RenderPassDef{
        .type = PASS_COMPUTE,
        .prototype = PIPELINE_COMPUTE_LIGHTSETUP,
    }]]
    [[=AnoFrameComputeBarrier{
        .mode = AnoFrameComputeBarrierMode::coalesce_with_next,
    }]],
    ANO_FRAME_PASS_SHADOW_SETUP [[=RenderPassDef{
        .type = PASS_COMPUTE,
        .prototype = PIPELINE_COMPUTE_SHADOWSETUP,
    }]]
    [[=AnoFrameComputeBarrier{
        .mode = AnoFrameComputeBarrierMode::emit,
        .fixedDstStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT,
        .geometryStages = true,
    }]],
    ANO_FRAME_PASS_CULL [[=RenderPassDef{
        .type = PASS_COMPUTE,
        .prototype = PIPELINE_COMPUTE_CULL,
    }]]
    [[=AnoFrameComputeBarrier{
        .mode = AnoFrameComputeBarrierMode::emit,
        .fixedDstStages = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .dstAccess = VK_ACCESS_INDIRECT_COMMAND_READ_BIT
            | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .geometryStages = true,
    }]],
    ANO_FRAME_PASS_LIGHT_CULL [[=RenderPassDef{
        .type = PASS_COMPUTE,
        .prototype = PIPELINE_COMPUTE_LIGHTCULL,
        .perView = true,
    }]]
    [[=AnoFrameComputeBarrier{
        .mode = AnoFrameComputeBarrierMode::emit,
        .fixedDstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dstAccess = VK_ACCESS_SHADER_READ_BIT,
    }]],
    ANO_FRAME_PASS_DEPTH_OPAQUE [[=RenderPassDef{
        .type = PASS_GRAPHICS,
        .prototype = PIPELINE_FLAT,
        .implementationIndex = 2,
        .perView = true,
        .colorAttachmentCount = 0,
        .depthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
    }]]
    [[=AnoFrameRenderBatchUse{ AnoFrameRenderBatch::depth }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::depth, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    }]],
    ANO_FRAME_PASS_DEPTH_OPAQUE_TWOSIDED [[=RenderPassDef{
        .type = PASS_GRAPHICS,
        .prototype = PIPELINE_FLAT_TWOSIDED,
        .implementationIndex = 2,
        .perView = true,
        .colorAttachmentCount = 0,
        .depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
    }]]
    [[=AnoFrameRenderBatchUse{ AnoFrameRenderBatch::depth }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::depth, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    }]],
    ANO_FRAME_PASS_OPAQUE [[=RenderPassDef{
        .type = PASS_GRAPHICS,
        .prototype = PIPELINE_FLAT,
        .implementationIndex = 0,
        .perView = true,
        .colorAttachmentCount = 2,
        .colorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .resolveMode = VK_RESOLVE_MODE_NONE,
    }]]
    [[=AnoFrameRenderBatchUse{ AnoFrameRenderBatch::opaque }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::depth, AnoFrameAccess::read,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::color, AnoFrameAccess::write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::pick, AnoFrameAccess::write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]],
    ANO_FRAME_PASS_OPAQUE_TWOSIDED [[=RenderPassDef{
        .type = PASS_GRAPHICS,
        .prototype = PIPELINE_FLAT_TWOSIDED,
        .implementationIndex = 0,
        .perView = true,
        .colorAttachmentCount = 2,
        .colorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .resolveMode = VK_RESOLVE_MODE_NONE,
    }]]
    [[=AnoFrameRenderBatchUse{ AnoFrameRenderBatch::opaque }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::depth, AnoFrameAccess::read,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::color, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::pick, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]],
    ANO_FRAME_PASS_MASKED [[=RenderPassDef{
        .type = PASS_GRAPHICS,
        .prototype = PIPELINE_FLAT_MASKED,
        .implementationIndex = 0,
        .perView = true,
        .colorAttachmentCount = 2,
        .colorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .resolveMode = VK_RESOLVE_MODE_NONE,
    }]]
    [[=AnoFrameRenderBatchUse{ AnoFrameRenderBatch::opaque }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::depth, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::color, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::pick, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]],
    ANO_FRAME_PASS_TRANSMISSION [[=RenderPassDef{
        .type = PASS_GRAPHICS,
        .prototype = PIPELINE_TRANSMISSION,
        .implementationIndex = 1,
        .perView = true,
        .colorAttachmentCount = 1,
        .colorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .resolveMode = VK_RESOLVE_MODE_NONE,
    }]]
    [[=AnoFrameRenderBatchUse{ AnoFrameRenderBatch::translucent }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::depth, AnoFrameAccess::read,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::color, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]],
    ANO_FRAME_PASS_ADDITIVE [[=RenderPassDef{
        .type = PASS_GRAPHICS,
        .prototype = PIPELINE_ADDITIVE,
        .implementationIndex = 0,
        .perView = true,
        .colorAttachmentCount = 1,
        .colorLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
    }]]
    [[=AnoFrameRenderBatchUse{ AnoFrameRenderBatch::translucent }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::depth, AnoFrameAccess::read,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    }]]
    [[=AnoFrameAttachmentUse{
        AnoFrameAttachment::color, AnoFrameAccess::read_write,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    }]],
    ANO_FRAME_PASS_COUNT,
} AnoFramePass;

static_assert(std::meta::is_structural_type(^^RenderPassDef));
static_assert(ano::reflected_enum_domain<AnoFramePass>.valid);

inline constexpr auto ANO_FRAME_PASS_REGISTRY =
    ano::reflect_dense_enum_contracts<AnoFramePass, RenderPassDef>();

template<AnoFramePass Pass>
consteval RenderPassDef ano_frame_pass()
{
    static_assert(Pass >= 0 && Pass < ANO_FRAME_PASS_COUNT);
    return ANO_FRAME_PASS_REGISTRY.values[static_cast<size_t>(Pass)];
}

#endif
