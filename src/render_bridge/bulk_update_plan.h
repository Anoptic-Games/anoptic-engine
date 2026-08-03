/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Reflected bulk inventory and 16 legal field-mask plans.

#ifndef ANO_RENDER_BULK_UPDATE_PLAN_H
#define ANO_RENDER_BULK_UPDATE_PLAN_H

#include "render_command_contract.h"

#include <anoptic_meta.h>

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace ano::render_bulk {

inline constexpr uint32_t commandBit = 1u << RCMD_BULK_UPDATE;
inline constexpr uint32_t allowedFields = render_contract::allowed_fields(RCMD_BULK_UPDATE);

template<std::meta::info Member>
const void* segment_source(const RenderUpdateBatch& batch) noexcept
{
    return batch.[:Member:];
}

template<std::meta::info Member>
consteval size_t segment_alignment()
{
    using Indexed = decltype(((RenderUpdateBatch*)nullptr)->[:Member:][0]);
    return alignof(Indexed);
}

template<std::meta::info Member>
void segment_copy(RenderUpdateBatch& packed, void* destination,
                  const void* source, uint32_t count) noexcept
{
    using Pointer = [:std::meta::type_of(Member):];
    using Element = std::remove_cv_t<std::remove_pointer_t<Pointer>>;
    packed.[:Member:] = static_cast<Pointer>(destination);
    __builtin_memcpy(destination, source, static_cast<size_t>(count) * sizeof(Element));
}

using SegmentSource = const void* (*)(const RenderUpdateBatch&) noexcept;
using SegmentCopy = void (*)(RenderUpdateBatch&, void*, const void*, uint32_t) noexcept;

struct BulkSegment final {
    uint32_t fields;
    uint16_t stride;
    uint16_t alignment;
    SegmentSource source;
    SegmentCopy copy;
};

consteval size_t reflect_segment_count()
{
    size_t count = 0u;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^RenderUpdateBatch, std::meta::access_context::current()));
    template for (constexpr auto member : members)
        if constexpr (std::meta::is_pointer_type(std::meta::type_of(member)))
            ++count;
    return count;
}

inline constexpr size_t segmentCount = reflect_segment_count();
static_assert(allowedFields != 0u && segmentCount > 0u && segmentCount <= UINT8_MAX);
inline constexpr size_t planCount = size_t{1} << __builtin_popcount(allowedFields);
inline constexpr size_t maskDomain = size_t{1} << (32u - __builtin_clz(allowedFields));
inline constexpr uint8_t invalidPlan = UINT8_MAX;
static_assert(planCount == 16u && planCount < invalidPlan && maskDomain <= UINT8_MAX);

struct BulkPackPlan final {
    uint8_t count;
    uint8_t segments[segmentCount];
};

struct BulkPlanRegistry final {
    BulkSegment segments[segmentCount];
    BulkPackPlan plans[planCount];
    uint8_t selection[maskDomain];
};

consteval BulkPlanRegistry reflect_plans()
{
    BulkPlanRegistry result{};
    uint32_t usedFields = 0u;
    size_t alwaysCount = 0u;
    size_t segmentIndex = 0u;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^RenderUpdateBatch, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_pointer_type(std::meta::type_of(member))) {
            constexpr auto uses = std::define_static_array(
                std::meta::annotations_of_with_type(member, ^^AnoRenderFieldUse));
            constexpr auto required = std::define_static_array(
                std::meta::annotations_of_with_type(member, ^^AnoRenderBulkRequired));
            static_assert(uses.size() + required.size() == 1u,
                          "every bulk pointer is required or field-selected");

            using Pointer = [:std::meta::type_of(member):];
            constexpr auto pointee = std::meta::remove_pointer(std::meta::type_of(member));
            using Pointee = [:pointee:];
            using Element = std::remove_cv_t<Pointee>;
            constexpr size_t stride = std::meta::size_of(pointee);
            constexpr size_t alignment = segment_alignment<member>();
            static_assert(!std::is_volatile_v<Pointee> && Data<Element>);
            static_assert(stride <= UINT16_MAX && alignment <= UINT16_MAX);
            static_assert(alignment <= alignof(max_align_t),
                          "bulk arrays cannot exceed the allocator base alignment");

            uint32_t fields = 0u;
            if constexpr (uses.size() == 1u) {
                constexpr AnoRenderFieldUse use =
                    std::meta::extract<AnoRenderFieldUse>(uses[0]);
                static_assert(use.fields != 0u && (use.fields & ~allowedFields) == 0u);
                static_assert((use.fields & (use.fields - 1u)) == 0u,
                              "a bulk pointer belongs to exactly one field bit");
                static_assert(use.commands == commandBit);
                fields = use.fields;
                usedFields |= use.fields;
            } else {
                ++alwaysCount;
            }
            result.segments[segmentIndex++] = {
                .fields = fields,
                .stride = static_cast<uint16_t>(stride),
                .alignment = static_cast<uint16_t>(alignment),
                .source = &segment_source<member>,
                .copy = &segment_copy<member>,
            };
        }
    }
    if (segmentIndex != segmentCount || alwaysCount != 1u || usedFields != allowedFields)
        __builtin_abort();

    // Required first, then descending alignment.
    for (size_t i = 1u; i < segmentCount; ++i) {
        const BulkSegment key = result.segments[i];
        size_t position = i;
        while (position > 0u) {
            const BulkSegment& previous = result.segments[position - 1u];
            const bool keyAlways = key.fields == 0u;
            const bool previousAlways = previous.fields == 0u;
            const bool before = keyAlways != previousAlways
                              ? keyAlways : key.alignment > previous.alignment;
            if (!before)
                break;
            result.segments[position] = previous;
            --position;
        }
        result.segments[position] = key;
    }

    for (size_t i = 0u; i < maskDomain; ++i)
        result.selection[i] = invalidPlan;
    size_t planIndex = 0u;
    for (uint32_t fields = 0u; fields < maskDomain; ++fields) {
        if ((fields & ~allowedFields) != 0u)
            continue;
        BulkPackPlan& plan = result.plans[planIndex];
        for (size_t segment = 0u; segment < segmentCount; ++segment) {
            const uint32_t selectedBy = result.segments[segment].fields;
            if (selectedBy == 0u || (selectedBy & fields) != 0u)
                plan.segments[plan.count++] = static_cast<uint8_t>(segment);
        }
        result.selection[fields] = static_cast<uint8_t>(planIndex++);
    }
    if (planIndex != planCount)
        __builtin_abort();
    return result;
}

inline constexpr BulkPlanRegistry registry = reflect_plans();
static_assert(registry.selection[RFIELD_LIGHT] == invalidPlan);
static_assert(registry.selection[0u] != invalidPlan
              && registry.plans[registry.selection[0u]].count == 1u);
static_assert(registry.selection[allowedFields] != invalidPlan
              && registry.plans[registry.selection[allowedFields]].count == segmentCount);

inline const BulkPackPlan* find_plan(uint32_t fields) noexcept
{
    if (fields >= maskDomain)
        return nullptr;
    const uint8_t index = registry.selection[fields];
    return index != invalidPlan ? &registry.plans[index] : nullptr;
}

inline const BulkSegment& segment(const BulkPackPlan& plan, size_t index) noexcept
{
    return registry.segments[plan.segments[index]];
}

} // namespace ano::render_bulk

#endif // ANO_RENDER_BULK_UPDATE_PLAN_H
