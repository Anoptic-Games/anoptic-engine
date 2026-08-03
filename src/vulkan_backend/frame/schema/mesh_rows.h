#ifndef ANO_FRAME_MESH_ROWS_H
#define ANO_FRAME_MESH_ROWS_H

#include <anoptic_meta.h>

#include "vulkan_backend/structs.h"

template<class Record>
consteval std::meta::info ano_mesh_member_named(std::string_view name)
{
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Record, std::meta::access_context::unchecked()));
    std::meta::info found{};
    template for (constexpr auto member : members)
        if (std::meta::identifier_of(member) == name)
            found = member;
    return found;
}

template<class Destination>
    requires ano::Data<Destination>
constexpr Destination ano_project_mesh_row(const MeshRegion& source)
{
    Destination result{};
    static constexpr auto destinations = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Destination, std::meta::access_context::unchecked()));
    template for (constexpr auto destination : destinations) {
        constexpr auto derived = std::define_static_array(
            std::meta::annotations_of_with_type(
                destination, ^^AnoMeshDerivedField));
        static_assert(derived.size() <= 1,
            "a GPU mesh field may have at most one derivation");
        if constexpr (derived.empty()) {
            constexpr auto sourceMember = ano_mesh_member_named<MeshRegion>(
                std::meta::identifier_of(destination));
            if constexpr (sourceMember == std::meta::info{}) {
                static_assert(ano::dependent_false<destination>,
                    "a GPU mesh field needs a same-named MeshRegion source");
            } else {
                using SourceField = [:std::meta::type_of(sourceMember):];
                using DestinationField = [:std::meta::type_of(destination):];
                static_assert(std::is_same_v<SourceField, DestinationField>,
                    "direct mesh projection fields must have identical types");
                auto& output = result.*(&[:destination:]);
                const auto& input = source.*(&[:sourceMember:]);
                if constexpr (std::is_array_v<DestinationField>) {
                    for (size_t i = 0; i < std::extent_v<DestinationField>; ++i)
                        output[i] = input[i];
                } else {
                    output = input;
                }
            }
        } else {
            constexpr auto spec = std::meta::extract<AnoMeshDerivedField>(
                derived[0]);
            using DestinationField = [:std::meta::type_of(destination):];
            static_assert(std::is_same_v<DestinationField, uint32_t>);
            if constexpr (spec.kind
                          == AnoMeshFieldDerivation::classic_index_bytes_to_elements) {
                constexpr auto sourceMember =
                    ano_mesh_member_named<MeshRegion>("classicIndexOffset");
                if constexpr (sourceMember == std::meta::info{}) {
                    static_assert(ano::dependent_false<destination>,
                        "classic index derivation needs classicIndexOffset");
                } else {
                    using SourceField = [:std::meta::type_of(sourceMember):];
                    static_assert(std::is_same_v<SourceField, uint32_t>);
                    result.*(&[:destination:]) =
                        source.*(&[:sourceMember:]) / sizeof(uint32_t);
                }
            } else {
                static_assert(ano::dependent_false<spec.kind>,
                    "unknown GPU mesh-field derivation");
            }
        }
    }
    return result;
}

static_assert(ano::Data<MeshRegion>);
static_assert(ano::Data<GpuMeshData>);
static_assert(ano::Data<GpuMeshBounds>);
static_assert(std::meta::is_structural_type(^^AnoMeshDerivedField));
static_assert(sizeof(GpuMeshData) == 9u * sizeof(uint32_t));
static_assert(sizeof(GpuMeshBounds) == 4u * sizeof(float));
static_assert(offsetof(GpuMeshBounds, boundingSphereRadius)
              == 3u * sizeof(float));

static_assert([] consteval {
    MeshRegion source{};
    source.vertexOffset = 1u;
    source.meshletOffset = 2u;
    source.meshletCount = 3u;
    source.uniqueVerticesOffset = 4u;
    source.trianglesOffset = 5u;
    source.boundsOffset = 6u;
    source.classicIndexOffset = 28u;
    source.classicIndexCount = 8u;
    source.boundingSphereCenter[0] = 9.0f;
    source.boundingSphereCenter[1] = 10.0f;
    source.boundingSphereCenter[2] = 11.0f;
    source.boundingSphereRadius = 12.0f;
    source.lodCount = 13u;
    constexpr uint32_t expectedFirstIndex = 7u;
    const GpuMeshData data = ano_project_mesh_row<GpuMeshData>(source);
    const GpuMeshBounds bounds = ano_project_mesh_row<GpuMeshBounds>(source);
    return data.vertexOffset == source.vertexOffset
        && data.meshletOffset == source.meshletOffset
        && data.meshletCount == source.meshletCount
        && data.uniqueVerticesOffset == source.uniqueVerticesOffset
        && data.trianglesOffset == source.trianglesOffset
        && data.boundsOffset == source.boundsOffset
        && data.classicIndexCount == source.classicIndexCount
        && data.classicFirstIndex == expectedFirstIndex
        && data.lodCount == source.lodCount
        && bounds.boundingSphereCenter[0] == source.boundingSphereCenter[0]
        && bounds.boundingSphereCenter[1] == source.boundingSphereCenter[1]
        && bounds.boundingSphereCenter[2] == source.boundingSphereCenter[2]
        && bounds.boundingSphereRadius == source.boundingSphereRadius;
}());

#endif
