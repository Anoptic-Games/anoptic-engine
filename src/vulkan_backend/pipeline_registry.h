#ifndef ANOPTIC_PIPELINE_REGISTRY_H
#define ANOPTIC_PIPELINE_REGISTRY_H

#include <meta>
#include <stddef.h>
#include <stdint.h>

#include "vulkan_backend/components.h"

struct AnoPipelineRegistry final {
    AnoPipelineSpec values[PIPELINE_TYPE_COUNT];
    uint32_t drawCount;
};

consteval auto ano_reflect_pipeline_registry()
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^PipelineType));
    static_assert(enumerators.size() == PIPELINE_TYPE_COUNT + 1);

    AnoPipelineRegistry result{};
    bool seen[PIPELINE_TYPE_COUNT] = {};
    bool drawSlots[PIPELINE_TYPE_COUNT] = {};
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto specs =
            std::define_static_array(std::meta::annotations_of_with_type(enumerator, ^^AnoPipelineSpec));
        constexpr auto sentinels =
            std::define_static_array(std::meta::annotations_of_with_type(enumerator, ^^AnoPipelineSentinel));
        static_assert(specs.size() + sentinels.size() == 1);
        if constexpr (!specs.empty()) {
            constexpr PipelineType type = [:enumerator:];
            constexpr uint32_t index = static_cast<uint32_t>(type);
            constexpr AnoPipelineSpec spec = std::meta::extract<AnoPipelineSpec>(specs[0]);
            static_assert(index < PIPELINE_TYPE_COUNT);
            static_assert((spec.kind == AnoPipelineKind::skeleton) == (spec.implementationCount == 0));
            static_assert((spec.kind == AnoPipelineKind::graphics) == (spec.drawSlot != ANO_NO_DRAW_SLOT));
            static_assert(spec.drawSlot == ANO_NO_DRAW_SLOT || spec.drawSlot < 16);
            static_assert(spec.kind == AnoPipelineKind::graphics || spec.supportedFeatures == PBR_FEATURE_NONE);
            static_assert((spec.kind == AnoPipelineKind::skeleton) == (spec.shader == AnoShaderFamily::none));
            static_assert((spec.kind == AnoPipelineKind::skeleton) == (spec.schedule == AnoPipelineSchedule::none));
            if (seen[index])
                __builtin_abort();
            seen[index] = true;
            result.values[index] = spec;
            if constexpr (spec.drawSlot != ANO_NO_DRAW_SLOT) {
                if (drawSlots[spec.drawSlot])
                    __builtin_abort();
                drawSlots[spec.drawSlot] = true;
                ++result.drawCount;
            }
        } else {
            static_assert([:enumerator:] == PIPELINE_TYPE_COUNT);
        }
    }
    for (uint32_t i = 0; i < PIPELINE_TYPE_COUNT; ++i)
        if (!seen[i] || (i < result.drawCount && !drawSlots[i]))
            __builtin_abort();
    return result;
}

inline constexpr auto ANO_PIPELINE_REGISTRY = ano_reflect_pipeline_registry();

constexpr bool ano_pipeline_type_valid(PipelineType type)
{
    return static_cast<uint32_t>(type) < PIPELINE_TYPE_COUNT;
}

constexpr const AnoPipelineSpec* ano_pipeline_spec(PipelineType type)
{
    return ano_pipeline_type_valid(type)
        ? &ANO_PIPELINE_REGISTRY.values[static_cast<uint32_t>(type)] : nullptr;
}

template<PipelineType Type>
consteval AnoPipelineSpec ano_pipeline_spec()
{
    static_assert(static_cast<uint32_t>(Type) < PIPELINE_TYPE_COUNT);
    return ANO_PIPELINE_REGISTRY.values[static_cast<uint32_t>(Type)];
}

template<PipelineType Type>
consteval const char* ano_pipeline_compute_shader_path()
{
    constexpr auto spec = ano_pipeline_spec<Type>();
    static_assert(spec.kind == AnoPipelineKind::compute);
    if constexpr (spec.shader == AnoShaderFamily::cull) return "resources/shaders/cull.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::update) return "resources/shaders/update.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::scatter) return "resources/shaders/scatter.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::tpsort) return "resources/shaders/tpsort.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::lightcull) return "resources/shaders/lightcull.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::shadowsetup) return "resources/shaders/shadowsetup.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::lightsetup) return "resources/shaders/lightsetup.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::hiz) return "resources/shaders/hiz.comp.spv";
    else if constexpr (spec.shader == AnoShaderFamily::textraster) return "resources/shaders/textraster.comp.spv";
    else {
        static_assert(Type != Type, "active compute pipelines require a registered shader family");
        return nullptr;
    }
}

template<PipelineType Type>
constexpr const char* ano_pipeline_geometry_shader_path(bool mesh, bool task, bool depth = false)
{
    constexpr auto spec = ano_pipeline_spec<Type>();
    static_assert(spec.kind == AnoPipelineKind::graphics);
    static_assert(spec.shader == AnoShaderFamily::flat || spec.shader == AnoShaderFamily::flat_masked ||
        spec.shader == AnoShaderFamily::transmission || spec.shader == AnoShaderFamily::additive);
    if (depth)
        return mesh ? (task ? "resources/shaders/flat_depth_task.mesh.spv"
                            : "resources/shaders/flat_depth.mesh.spv")
                    : "resources/shaders/flat_depth.vert.spv";
    return mesh ? (task ? "resources/shaders/flat_task.mesh.spv"
                        : "resources/shaders/flat.mesh.spv")
                : "resources/shaders/flat.vert.spv";
}

template<PipelineType Type>
constexpr const char* ano_pipeline_fragment_shader_path(bool fp16)
{
    constexpr auto family = ano_pipeline_spec<Type>().shader;
    if constexpr (family == AnoShaderFamily::flat)
        return fp16 ? "resources/shaders/flat_fp16.frag.spv" : "resources/shaders/flat.frag.spv";
    else if constexpr (family == AnoShaderFamily::flat_masked)
        return fp16 ? "resources/shaders/flat_masked_fp16.frag.spv" : "resources/shaders/flat_masked.frag.spv";
    else if constexpr (family == AnoShaderFamily::transmission)
        return fp16 ? "resources/shaders/transmission_fp16.frag.spv" : "resources/shaders/transmission.frag.spv";
    else if constexpr (family == AnoShaderFamily::additive)
        return "resources/shaders/additive.frag.spv";
    else {
        static_assert(Type != Type, "active graphics pipelines require a registered fragment shader family");
        return nullptr;
    }
}

template<size_t Count>
consteval bool ano_pipeline_passes_valid(const RenderPassDef (&passes)[Count])
{
    bool covered[PIPELINE_TYPE_COUNT] = {};
    for (const RenderPassDef& pass : passes) {
        const AnoPipelineSpec* spec = ano_pipeline_spec(pass.prototype);
        if (spec == nullptr || spec->schedule != AnoPipelineSchedule::frame ||
            pass.implementationIndex >= spec->implementationCount)
            return false;
        if ((pass.type == PASS_GRAPHICS) != (spec->kind == AnoPipelineKind::graphics) ||
            (pass.type == PASS_COMPUTE) != (spec->kind == AnoPipelineKind::compute))
            return false;
        if (pass.type == PASS_GRAPHICS) {
            if (!pass.perView || spec->drawSlot == ANO_NO_DRAW_SLOT || pass.colorAttachmentCount > 4)
                return false;
        } else if (pass.colorAttachmentCount != 0 || spec->drawSlot != ANO_NO_DRAW_SLOT) {
            return false;
        }
        covered[static_cast<uint32_t>(pass.prototype)] = true;
    }
    for (uint32_t i = 0; i < PIPELINE_TYPE_COUNT; ++i)
        if (ANO_PIPELINE_REGISTRY.values[i].schedule == AnoPipelineSchedule::frame && !covered[i])
            return false;
    return true;
}

#endif
