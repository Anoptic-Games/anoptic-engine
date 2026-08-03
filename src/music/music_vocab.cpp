/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Composer vocabulary. Names derive from the enum declarations at compile time.

#include "music_vocab.h"

#include <anoptic_music.h>

#include <anoptic_meta.h>

namespace {

using Layer = ano::EnumValue<AnoMusicLayer>;
using Patch = ano::EnumValue<AnoPatchName>;

inline constexpr auto kLayerNames = ano::reflect_enum_names<AnoMusicLayer>(
    "ANO_MUSIC_", ano::EnumNameCase::lower);
inline constexpr auto kPatchNames = ano::reflect_enum_names<AnoPatchName>(
    "ANO_PATCH_", ano::EnumNameCase::lower, ANO_PATCH_NONE);

consteval bool names_valid()
{
    for (std::size_t i = 0; i < kLayerNames.size(); ++i) {
        if (kLayerNames.values[i] == nullptr || kLayerNames.values[i][0] == '\0')
            return false;
    }
    if (kPatchNames.values[ANO_PATCH_NONE][0] != '\0')
        return false;
    for (std::size_t i = 1; i < kPatchNames.size(); ++i) {
        if (kPatchNames.values[i] == nullptr || kPatchNames.values[i][0] == '\0')
            return false;
    }
    return true;
}

static_assert(names_valid());
static_assert(ano::Data<Layer>);
static_assert(ano::Data<Patch>);
static_assert(ano::Data<decltype(kLayerNames)>);
static_assert(ano::Data<decltype(kPatchNames)>);

} // namespace

const char *ano_music_layer_name(uint32_t layer)
{
    const auto parsed = Layer::from_raw(layer);
    return parsed ? kLayerNames.values[parsed->index()] : "unknown";
}

uint32_t ano_music_patch_id(const char *name)
{
    return static_cast<uint32_t>(kPatchNames.find(name, ANO_PATCH_NONE));
}

const char *ano_music_patch_name(uint32_t id)
{
    const Patch patch = Patch::from_raw(id).value_or(
        Patch::constant<ANO_PATCH_NONE>());
    return kPatchNames.values[patch.index()];
}
