#ifndef ANO_FRAME_CULL_UBO_H
#define ANO_FRAME_CULL_UBO_H

#include "vulkan_backend/buffer_types.h"
#include "vulkan_backend/pipeline_registry.h"

struct AnoCullUboInvariantImage final {
    uint32_t viewCount;
    uint32_t drawSlotCount;
    uint32_t drawSlotOf[16];
    uint32_t specialSlots[4];
    uint32_t taskParams[4];
    float hizParamsPad[ANO_VIEW_COUNT];
    int32_t shadowPadding[3];
};

consteval AnoCullUboInvariantImage ano_compile_cull_ubo_invariants(bool taskCull)
{
    // PipelineType annotations are the sole draw-slot source; no runtime registry walk survives.
    static_assert(PIPELINE_FLAT_MASKED < 16);
    AnoCullUboInvariantImage image{};
    image.viewCount = ANO_VIEW_COUNT;
    image.drawSlotCount = ANO_PIPELINE_REGISTRY.drawCount;
    for (uint32_t i = 0; i < 16u; ++i)
        image.drawSlotOf[i] = i < PIPELINE_TYPE_COUNT
            ? ANO_PIPELINE_REGISTRY.values[i].drawSlot : ANO_NO_DRAW_SLOT;

    constexpr PipelineType specialPipelines[] = {
        PIPELINE_ADDITIVE,
        PIPELINE_TRANSMISSION,
        PIPELINE_FLAT_MASKED,
    };
    for (uint32_t i = 0; i < 4u; ++i)
        image.specialSlots[i] = ANO_NO_DRAW_SLOT;
    for (uint32_t i = 0; i < 3u; ++i)
        image.specialSlots[i] =
            ANO_PIPELINE_REGISTRY.values[static_cast<uint32_t>(specialPipelines[i])].drawSlot;

    image.taskParams[0] = taskCull ? 1u : 0u;
    return image;
}

inline constexpr AnoCullUboInvariantImage ANO_CULL_UBO_INVARIANTS[] = {
    ano_compile_cull_ubo_invariants(false),
    ano_compile_cull_ubo_invariants(true),
};

inline constexpr uint32_t ANO_CULL_UBO_REPEATED_INVARIANT_BYTES =
    2u * sizeof(uint32_t)
    + 16u * sizeof(uint32_t)
    + 4u * sizeof(uint32_t)
    + 4u * sizeof(uint32_t)
    + ANO_VIEW_COUNT * sizeof(float);

static_assert(ANO_CULL_UBO_REPEATED_INVARIANT_BYTES >= 96u);
static_assert(ANO_CULL_UBO_INVARIANTS[0].taskParams[0] == 0u);
static_assert(ANO_CULL_UBO_INVARIANTS[1].taskParams[0] == 1u);
static_assert(ANO_CULL_UBO_INVARIANTS[0].specialSlots[3] == ANO_NO_DRAW_SLOT);

inline void ano_initialize_cull_ubo_invariants(CullUBO* ubo, bool taskCull)
{
    const AnoCullUboInvariantImage& image =
        ANO_CULL_UBO_INVARIANTS[taskCull ? 1u : 0u];
    ubo->viewCount = image.viewCount;
    ubo->drawSlotCount = image.drawSlotCount;
    for (uint32_t i = 0; i < 16u; ++i)
        ubo->drawSlotOf[i] = image.drawSlotOf[i];
    for (uint32_t i = 0; i < 4u; ++i) {
        ubo->specialSlots[i] = image.specialSlots[i];
        ubo->taskParams[i] = image.taskParams[i];
    }
    for (uint32_t view = 0; view < ANO_VIEW_COUNT; ++view)
        ubo->hizParams[view][3] = image.hizParamsPad[view];
    ubo->_hizPad0 = image.shadowPadding[0];
    ubo->_hizPad1 = image.shadowPadding[1];
    ubo->_hizPad2 = image.shadowPadding[2];
}

#endif
