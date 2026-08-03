#ifndef ANO_FRAME_COMPUTE_BARRIERS_H
#define ANO_FRAME_COMPUTE_BARRIERS_H

#include "vulkan_backend/frame/schema/pass_contract.h"

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

#endif
