/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANO_DRAW_PROFILES_H
#define ANO_DRAW_PROFILES_H

#include <meta>
#include <stddef.h>
#include <stdint.h>

enum class AnoDrawGeometry : uint8_t {
    vertex,
    mesh,
    task,
    count,
};

enum class AnoDrawSubmission : uint8_t {
    uncounted,
    counted,
    count,
};

struct AnoDrawProfileSpec final {
    AnoDrawGeometry geometry;
    AnoDrawSubmission submission;
};

struct AnoDrawProfileSentinel final {};

enum class AnoDrawProfile : uint8_t {
    vertex_uncounted [[=AnoDrawProfileSpec{
        AnoDrawGeometry::vertex, AnoDrawSubmission::uncounted}]],
    vertex_counted [[=AnoDrawProfileSpec{
        AnoDrawGeometry::vertex, AnoDrawSubmission::counted}]],
    mesh_uncounted [[=AnoDrawProfileSpec{
        AnoDrawGeometry::mesh, AnoDrawSubmission::uncounted}]],
    mesh_counted [[=AnoDrawProfileSpec{
        AnoDrawGeometry::mesh, AnoDrawSubmission::counted}]],
    task_uncounted [[=AnoDrawProfileSpec{
        AnoDrawGeometry::task, AnoDrawSubmission::uncounted}]],
    task_counted [[=AnoDrawProfileSpec{
        AnoDrawGeometry::task, AnoDrawSubmission::counted}]],
    count [[=AnoDrawProfileSentinel{}]],
};

inline constexpr size_t ANO_DRAW_GEOMETRY_COUNT =
    static_cast<size_t>(AnoDrawGeometry::count);
inline constexpr size_t ANO_DRAW_SUBMISSION_COUNT =
    static_cast<size_t>(AnoDrawSubmission::count);
inline constexpr size_t ANO_DRAW_PROFILE_COUNT =
    ANO_DRAW_GEOMETRY_COUNT * ANO_DRAW_SUBMISSION_COUNT;
static_assert(ANO_DRAW_GEOMETRY_COUNT == 3u);
static_assert(ANO_DRAW_SUBMISSION_COUNT == 2u);
static_assert(static_cast<size_t>(AnoDrawProfile::count) == ANO_DRAW_PROFILE_COUNT);

template<std::meta::info Enumerator>
consteval AnoDrawProfileSpec ano_draw_profile_spec()
{
    static_assert(std::meta::parent_of(Enumerator) == ^^AnoDrawProfile);
    constexpr auto specs = std::define_static_array(
        std::meta::annotations_of_with_type(Enumerator, ^^AnoDrawProfileSpec));
    static_assert(specs.size() == 1u);
    return std::meta::extract<AnoDrawProfileSpec>(specs[0]);
}

consteval bool ano_validate_draw_profiles()
{
    bool seen[ANO_DRAW_GEOMETRY_COUNT][ANO_DRAW_SUBMISSION_COUNT] = {};
    static constexpr auto profiles =
        std::define_static_array(std::meta::enumerators_of(^^AnoDrawProfile));
    static_assert(profiles.size() == ANO_DRAW_PROFILE_COUNT + 1u);

    template for (constexpr auto profile : profiles) {
        constexpr auto specs = std::define_static_array(
            std::meta::annotations_of_with_type(profile, ^^AnoDrawProfileSpec));
        constexpr auto sentinels = std::define_static_array(
            std::meta::annotations_of_with_type(profile, ^^AnoDrawProfileSentinel));
        static_assert(specs.size() + sentinels.size() == 1u);
        if constexpr (!specs.empty()) {
            static_assert([:profile:] != AnoDrawProfile::count);
            constexpr AnoDrawProfileSpec spec =
                std::meta::extract<AnoDrawProfileSpec>(specs[0]);
            constexpr size_t geometry = static_cast<size_t>(spec.geometry);
            constexpr size_t submission = static_cast<size_t>(spec.submission);
            static_assert(geometry < ANO_DRAW_GEOMETRY_COUNT);
            static_assert(submission < ANO_DRAW_SUBMISSION_COUNT);
            if (seen[geometry][submission])
                __builtin_abort();
            seen[geometry][submission] = true;
        } else {
            static_assert([:profile:] == AnoDrawProfile::count);
        }
    }

    for (const auto& geometry : seen)
        for (bool submission : geometry)
            if (!submission)
                return false;
    return true;
}

static_assert(ano_validate_draw_profiles());

#endif // ANO_DRAW_PROFILES_H
