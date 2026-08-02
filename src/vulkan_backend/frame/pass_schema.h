#ifndef ANO_FRAME_PASS_SCHEMA_H
#define ANO_FRAME_PASS_SCHEMA_H

#include <meta>
#include <stdint.h>

#include "cpp/ano_reflect.h"
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

struct AnoFrameComputeBarrierRegistry final {
    AnoFrameComputeBarrier values[ANO_FRAME_PASS_COUNT];
};

template<AnoFramePass Pass, size_t ContractCount>
consteval void ano_validate_frame_compute_barrier_count()
{
    constexpr RenderPassDef pass = ano_frame_pass<Pass>();
    if constexpr (pass.type == PASS_COMPUTE)
        static_assert(ContractCount == 1,
            "each compute frame pass needs exactly one synchronization contract");
    else
        static_assert(ContractCount == 0,
            "graphics frame passes cannot carry compute synchronization contracts");
}

template<AnoFramePass Pass, AnoFrameComputeBarrier Contract>
consteval void ano_validate_frame_compute_barrier()
{
    static_assert(ano_frame_pass<Pass>().type == PASS_COMPUTE);
    if constexpr (Contract.mode == AnoFrameComputeBarrierMode::emit) {
        static_assert(Contract.fixedDstStages != 0 || Contract.geometryStages,
            "an emitted compute barrier needs a destination stage");
        static_assert(Contract.dstAccess != 0,
            "an emitted compute barrier needs destination access");
    } else {
        static_assert(Contract.mode
                == AnoFrameComputeBarrierMode::coalesce_with_next,
            "unknown compute barrier mode");
        static_assert(Contract.fixedDstStages == 0
                && Contract.dstAccess == 0 && !Contract.geometryStages,
            "a coalesced barrier delegates its complete destination scope");
    }
}

consteval AnoFrameComputeBarrierRegistry ano_reflect_frame_compute_barriers()
{
    static constexpr auto passes =
        std::define_static_array(std::meta::enumerators_of(^^AnoFramePass));
    AnoFrameComputeBarrierRegistry result{};
    template for (constexpr auto reflectedPass : passes) {
        constexpr int64_t raw = static_cast<int64_t>([:reflectedPass:]);
        if constexpr (raw >= 0 && raw < ANO_FRAME_PASS_COUNT) {
            constexpr AnoFramePass passId = [:reflectedPass:];
            constexpr RenderPassDef pass = ano_frame_pass<passId>();
            static constexpr auto contracts = std::define_static_array(
                std::meta::annotations_of_with_type(
                    reflectedPass, ^^AnoFrameComputeBarrier));
            ano_validate_frame_compute_barrier_count<
                passId, contracts.size()>();
            if constexpr (pass.type == PASS_COMPUTE) {
                constexpr AnoFrameComputeBarrier contract =
                    std::meta::extract<AnoFrameComputeBarrier>(contracts[0]);
                ano_validate_frame_compute_barrier<passId, contract>();
                result.values[static_cast<size_t>(passId)] = contract;
            }
        }
    }
    return result;
}

static_assert(std::meta::is_structural_type(^^AnoFrameComputeBarrier));

inline constexpr auto ANO_FRAME_COMPUTE_BARRIER_REGISTRY =
    ano_reflect_frame_compute_barriers();

template<AnoFramePass Pass>
consteval AnoFrameComputeBarrier ano_frame_compute_barrier()
{
    static_assert(ano_frame_pass<Pass>().type == PASS_COMPUTE);
    return ANO_FRAME_COMPUTE_BARRIER_REGISTRY.values[
        static_cast<size_t>(Pass)];
}

template<AnoFramePass Pass>
consteval void ano_validate_frame_compute_coalescing()
{
    constexpr AnoFrameComputeBarrier contract =
        ano_frame_compute_barrier<Pass>();
    if constexpr (contract.mode
                  == AnoFrameComputeBarrierMode::coalesce_with_next) {
        constexpr size_t nextIndex = static_cast<size_t>(Pass) + 1;
        static_assert(nextIndex < static_cast<size_t>(ANO_FRAME_PASS_COUNT),
            "a coalesced compute barrier needs a following frame pass");
        if constexpr (nextIndex < static_cast<size_t>(ANO_FRAME_PASS_COUNT)) {
            constexpr AnoFramePass nextPass =
                static_cast<AnoFramePass>(nextIndex);
            constexpr RenderPassDef current = ano_frame_pass<Pass>();
            constexpr RenderPassDef next = ano_frame_pass<nextPass>();
            static_assert(next.type == PASS_COMPUTE,
                "a coalesced compute barrier must be followed by compute");
            static_assert(current.perView == next.perView,
                "coalesced compute barriers cannot cross execution lanes");
            static_assert(ano_frame_compute_barrier<nextPass>().mode
                    == AnoFrameComputeBarrierMode::emit,
                "the next compute pass must emit the coalesced barrier");
        }
    }
}

consteval bool ano_validate_frame_compute_barrier_schedule()
{
    static constexpr auto passes =
        std::define_static_array(std::meta::enumerators_of(^^AnoFramePass));
    template for (constexpr auto reflectedPass : passes) {
        constexpr int64_t raw = static_cast<int64_t>([:reflectedPass:]);
        if constexpr (raw >= 0 && raw < ANO_FRAME_PASS_COUNT) {
            constexpr AnoFramePass passId = [:reflectedPass:];
            if constexpr (ano_frame_pass<passId>().type == PASS_COMPUTE)
                ano_validate_frame_compute_coalescing<passId>();
        }
    }
    return true;
}

static_assert(ano_validate_frame_compute_barrier_schedule());

template<AnoFramePass Pass>
static inline void ano_record_frame_compute_barrier(
    VkCommandBuffer cmd, bool meshShader, bool taskCull)
{
    constexpr AnoFrameComputeBarrier contract =
        ano_frame_compute_barrier<Pass>();
    if constexpr (contract.mode == AnoFrameComputeBarrierMode::emit) {
        VkPipelineStageFlags dstStages = contract.fixedDstStages;
        if constexpr (contract.geometryStages) {
            dstStages |= meshShader ? VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT
                                    : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            if (taskCull)
                dstStages |= VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT;
        }
        VkMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = contract.dstAccess,
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            dstStages, 0, 1, &barrier, 0, NULL, 0, NULL);
    }
}

struct AnoFrameAttachmentBarrier final {
    bool required;
    VkPipelineStageFlags srcStages;
    VkPipelineStageFlags dstStages;
    VkAccessFlags srcAccess;
    VkAccessFlags dstAccess;
    VkImageLayout oldLayout;
    VkImageLayout newLayout;
};

struct AnoFrameAttachmentBarrierPlan final {
    AnoFrameAttachmentBarrier barriers[static_cast<size_t>(AnoFrameAttachment::count)];
    VkPipelineStageFlags srcStages;
    VkPipelineStageFlags dstStages;
    uint32_t count;
    AnoFrameRenderBatch batch;
    AnoFramePass firstPass;
    AnoFramePass lastPass;
    bool beginsRendering;
    bool endsRendering;
};

enum class AnoFrameAttachmentContractError : uint8_t {
    none,
    rendering_batch_reentered,
    color_attachment_count_changed,
    duplicate_attachment,
    layout_changed_inside_batch,
    attachment_shape_mismatch,
    depth_load_without_read,
    color_access_mismatch,
    pick_access_mismatch,
    missing_rendering_batch,
    continuation_must_load,
    resolve_before_batch_end,
};

struct AnoFrameAttachmentBarrierRegistry final {
    AnoFrameAttachmentBarrierPlan values[ANO_FRAME_PASS_COUNT];
    AnoFrameAttachmentContractError error;
    AnoFramePass errorPass;
    AnoFrameRenderBatch errorBatch;
};

struct AnoFrameAttachmentState final {
    bool live;
    AnoFrameAccess access;
    AnoFrameRenderBatch batch;
    VkImageLayout layout;
    VkPipelineStageFlags stages;
    VkAccessFlags mask;
};

constexpr void ano_frame_attachment_contract_error(
    AnoFrameAttachmentBarrierRegistry& registry,
    AnoFrameAttachmentContractError error, AnoFramePass pass,
    AnoFrameRenderBatch batch)
{
    if (registry.error == AnoFrameAttachmentContractError::none) {
        registry.error = error;
        registry.errorPass = pass;
        registry.errorBatch = batch;
    }
}

constexpr bool ano_frame_access_reads(AnoFrameAccess access)
{
    return access != AnoFrameAccess::write;
}

constexpr bool ano_frame_access_writes(AnoFrameAccess access)
{
    return access != AnoFrameAccess::read;
}

constexpr VkPipelineStageFlags ano_frame_attachment_stages(AnoFrameAttachment attachment)
{
    if (attachment == AnoFrameAttachment::depth)
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
}

constexpr VkAccessFlags ano_frame_attachment_access(
    AnoFrameAttachment attachment, AnoFrameAccess access)
{
    VkAccessFlags mask = 0;
    if (attachment == AnoFrameAttachment::depth) {
        if (ano_frame_access_reads(access))
            mask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        if (ano_frame_access_writes(access))
            mask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else {
        if (ano_frame_access_reads(access))
            mask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        if (ano_frame_access_writes(access))
            mask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    return mask;
}

constexpr bool ano_frame_attachment_layout_valid(
    AnoFrameAttachment attachment, VkImageLayout layout)
{
    return attachment == AnoFrameAttachment::depth
        ? layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        : layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

consteval AnoFrameAttachmentBarrierRegistry ano_reflect_frame_attachment_barriers()
{
    constexpr size_t attachmentCount = static_cast<size_t>(AnoFrameAttachment::count);
    constexpr size_t batchCount = static_cast<size_t>(AnoFrameRenderBatch::count);
    static constexpr auto passes =
        std::define_static_array(std::meta::enumerators_of(^^AnoFramePass));
    AnoFrameAttachmentBarrierRegistry result{};
    AnoFrameAttachmentState previous[attachmentCount] = {};
    AnoFramePass firstPass[batchCount] = {};
    AnoFramePass lastPass[batchCount] = {};
    uint32_t colorAttachmentCount[batchCount] = {};
    bool batchSeen[batchCount] = {};
    AnoFrameRenderBatch activeBatch = AnoFrameRenderBatch::count;
    for (size_t i = 0; i < batchCount; ++i) {
        firstPass[i] = ANO_FRAME_PASS_COUNT;
        lastPass[i] = ANO_FRAME_PASS_COUNT;
    }
    result.errorPass = ANO_FRAME_PASS_COUNT;
    result.errorBatch = AnoFrameRenderBatch::count;

    template for (constexpr auto reflectedPass : passes) {
        constexpr int64_t raw = static_cast<int64_t>([:reflectedPass:]);
        if constexpr (raw >= 0 && raw < ANO_FRAME_PASS_COUNT) {
            constexpr AnoFramePass passId = [:reflectedPass:];
            constexpr RenderPassDef pass = ano_frame_pass<passId>();
            static constexpr auto uses = std::define_static_array(
                std::meta::annotations_of_with_type(
                    reflectedPass, ^^AnoFrameAttachmentUse));
            static constexpr auto batches = std::define_static_array(
                std::meta::annotations_of_with_type(
                    reflectedPass, ^^AnoFrameRenderBatchUse));
            AnoFrameAttachmentBarrierPlan& plan =
                result.values[static_cast<size_t>(passId)];
            if constexpr (pass.type == PASS_COMPUTE) {
                static_assert(uses.empty(),
                    "compute passes cannot declare graphics attachments");
                static_assert(batches.empty(),
                    "compute passes cannot enter a rendering batch");
                plan.batch = AnoFrameRenderBatch::count;
                plan.firstPass = ANO_FRAME_PASS_COUNT;
                plan.lastPass = ANO_FRAME_PASS_COUNT;
                activeBatch = AnoFrameRenderBatch::count;
            } else {
                static_assert(batches.size() == 1,
                    "graphics passes enter exactly one rendering batch");
                constexpr AnoFrameRenderBatchUse batchUse =
                    std::meta::extract<AnoFrameRenderBatchUse>(batches[0]);
                constexpr AnoFrameRenderBatch batch = batchUse.batch;
                constexpr size_t batchIndex = static_cast<size_t>(batch);
                static_assert(batchIndex < batchCount,
                    "graphics pass references an unknown rendering batch");
                static_assert(!uses.empty() && uses.size() <= attachmentCount,
                    "graphics passes declare one use per attachment");
                plan.batch = batch;
                if (activeBatch != batch) {
                    if (batchSeen[batchIndex]) {
                        ano_frame_attachment_contract_error(result,
                            AnoFrameAttachmentContractError::rendering_batch_reentered,
                            passId, batch);
                    } else {
                        batchSeen[batchIndex] = true;
                        firstPass[batchIndex] = passId;
                        colorAttachmentCount[batchIndex] =
                            pass.colorAttachmentCount;
                    }
                    activeBatch = batch;
                }
                if (colorAttachmentCount[batchIndex]
                    != pass.colorAttachmentCount)
                    ano_frame_attachment_contract_error(result,
                        AnoFrameAttachmentContractError::color_attachment_count_changed,
                        passId, batch);
                lastPass[batchIndex] = passId;

                bool seen[attachmentCount] = {};
                AnoFrameAccess access[attachmentCount] = {};
                template for (constexpr auto reflectedUse : uses) {
                    constexpr AnoFrameAttachmentUse use =
                        std::meta::extract<AnoFrameAttachmentUse>(reflectedUse);
                    constexpr size_t attachment =
                        static_cast<size_t>(use.attachment);
                    static_assert(attachment < attachmentCount,
                        "frame pass references an unknown attachment");
                    static_assert(ano_frame_attachment_layout_valid(
                        use.attachment, use.layout),
                        "frame pass uses an illegal attachment layout");

                    if (seen[attachment]) {
                        ano_frame_attachment_contract_error(result,
                            AnoFrameAttachmentContractError::duplicate_attachment,
                            passId, batch);
                    } else {
                        seen[attachment] = true;
                        access[attachment] = use.access;
                        constexpr VkPipelineStageFlags stages =
                            ano_frame_attachment_stages(use.attachment);
                        constexpr VkAccessFlags mask =
                            ano_frame_attachment_access(
                                use.attachment, use.access);
                        AnoFrameAttachmentState& prior = previous[attachment];
                        const bool sameRenderingScope =
                            prior.live && prior.batch == batch;
                        if (sameRenderingScope && prior.layout != use.layout)
                            ano_frame_attachment_contract_error(result,
                                AnoFrameAttachmentContractError::layout_changed_inside_batch,
                                passId, batch);
                        const bool hazard = prior.live && !sameRenderingScope
                            && (ano_frame_access_writes(prior.access)
                                || ano_frame_access_writes(use.access)
                                || prior.layout != use.layout);
                        if (hazard) {
                            AnoFrameAttachmentBarrier& barrier =
                                plan.barriers[attachment];
                            barrier = {
                                .required = true,
                                .srcStages = prior.stages,
                                .dstStages = stages,
                                .srcAccess = prior.mask,
                                .dstAccess = mask,
                                .oldLayout = prior.layout,
                                .newLayout = use.layout,
                            };
                            plan.srcStages |= prior.stages;
                            plan.dstStages |= stages;
                            ++plan.count;
                        }
                        prior = {
                            .live = true,
                            .access = use.access,
                            .batch = batch,
                            .layout = use.layout,
                            .stages = stages,
                            .mask = mask,
                        };
                    }
                }

                const bool hasDepth =
                    seen[static_cast<size_t>(AnoFrameAttachment::depth)];
                const bool hasColor =
                    seen[static_cast<size_t>(AnoFrameAttachment::color)];
                const bool hasPick =
                    seen[static_cast<size_t>(AnoFrameAttachment::pick)];
                if (!hasDepth
                    || hasColor != (pass.colorAttachmentCount > 0)
                    || hasPick != (pass.colorAttachmentCount == 2))
                    ano_frame_attachment_contract_error(result,
                        AnoFrameAttachmentContractError::attachment_shape_mismatch,
                        passId, batch);

                const AnoFrameAccess depthAccess =
                    access[static_cast<size_t>(AnoFrameAttachment::depth)];
                if (hasDepth && pass.depthLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD
                    && !ano_frame_access_reads(depthAccess))
                    ano_frame_attachment_contract_error(result,
                        AnoFrameAttachmentContractError::depth_load_without_read,
                        passId, batch);

                const AnoFrameAccess colorAccess =
                    access[static_cast<size_t>(AnoFrameAttachment::color)];
                if (hasColor
                    && (!ano_frame_access_writes(colorAccess)
                        || (pass.colorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD
                            && !ano_frame_access_reads(colorAccess))))
                    ano_frame_attachment_contract_error(result,
                        AnoFrameAttachmentContractError::color_access_mismatch,
                        passId, batch);

                const AnoFrameAccess pickAccess =
                    access[static_cast<size_t>(AnoFrameAttachment::pick)];
                if (hasPick
                    && (!ano_frame_access_writes(pickAccess)
                        || (pass.colorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD
                            && !ano_frame_access_reads(pickAccess))))
                    ano_frame_attachment_contract_error(result,
                        AnoFrameAttachmentContractError::pick_access_mismatch,
                        passId, batch);
            }
        }
    }

    for (size_t i = 0; i < batchCount; ++i)
        if (!batchSeen[i])
            ano_frame_attachment_contract_error(result,
                AnoFrameAttachmentContractError::missing_rendering_batch,
                ANO_FRAME_PASS_COUNT,
                static_cast<AnoFrameRenderBatch>(i));
    for (size_t i = 0; i < static_cast<size_t>(ANO_FRAME_PASS_COUNT); ++i) {
        const RenderPassDef& pass = ANO_FRAME_PASS_REGISTRY.values[i];
        if (pass.type != PASS_GRAPHICS)
            continue;
        AnoFrameAttachmentBarrierPlan& plan = result.values[i];
        const size_t batch = static_cast<size_t>(plan.batch);
        plan.firstPass = firstPass[batch];
        plan.lastPass = lastPass[batch];
        plan.beginsRendering = i == static_cast<size_t>(plan.firstPass);
        plan.endsRendering = i == static_cast<size_t>(plan.lastPass);
        if (!plan.beginsRendering
            && (pass.depthLoadOp != VK_ATTACHMENT_LOAD_OP_LOAD
                || (pass.colorAttachmentCount > 0
                    && pass.colorLoadOp != VK_ATTACHMENT_LOAD_OP_LOAD)))
            ano_frame_attachment_contract_error(result,
                AnoFrameAttachmentContractError::continuation_must_load,
                static_cast<AnoFramePass>(i), plan.batch);
        if (!plan.endsRendering && pass.resolveMode != VK_RESOLVE_MODE_NONE)
            ano_frame_attachment_contract_error(result,
                AnoFrameAttachmentContractError::resolve_before_batch_end,
                static_cast<AnoFramePass>(i), plan.batch);
    }
    return result;
}

static_assert(std::meta::is_structural_type(^^AnoFrameAttachmentUse));
static_assert(std::meta::is_structural_type(^^AnoFrameRenderBatchUse));

inline constexpr auto ANO_FRAME_ATTACHMENT_BARRIER_REGISTRY =
    ano_reflect_frame_attachment_barriers();

template<AnoFrameAttachmentContractError Error, AnoFramePass Pass,
         AnoFrameRenderBatch Batch>
consteval bool ano_validate_frame_attachment_contract()
{
    static_assert(Error == AnoFrameAttachmentContractError::none,
        "invalid reflected attachment contract; template arguments identify error, pass, and batch");
    return true;
}

static_assert(ano_validate_frame_attachment_contract<
    ANO_FRAME_ATTACHMENT_BARRIER_REGISTRY.error,
    ANO_FRAME_ATTACHMENT_BARRIER_REGISTRY.errorPass,
    ANO_FRAME_ATTACHMENT_BARRIER_REGISTRY.errorBatch>());

template<AnoFramePass Pass>
consteval AnoFrameAttachmentBarrierPlan ano_frame_attachment_barriers()
{
    static_assert(Pass >= 0 && Pass < ANO_FRAME_PASS_COUNT);
    return ANO_FRAME_ATTACHMENT_BARRIER_REGISTRY.values[
        static_cast<size_t>(Pass)];
}

#endif
