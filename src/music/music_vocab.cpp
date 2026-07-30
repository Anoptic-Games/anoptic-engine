/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Composer vocabulary. Enum/name associations are explicit compile-time data.

#include "music_vocab.h"

#include <array>
#include <string.h>

#include <anoptic_music.h>

#include "cpp/ano_types.h"

namespace {

using Layer = ano::EnumValue<AnoMusicLayer, ANO_MUSIC_LAYER_COUNT>;
using Patch = ano::EnumValue<AnoPatchName, ANO_PATCH_COUNT>;

constexpr auto kLayerNames = ano::make_enum_map<
    AnoMusicLayer, ANO_MUSIC_LAYER_COUNT>(
        std::array{
            ano::enum_entry(ANO_MUSIC_PAD, "pad"),
            ano::enum_entry(ANO_MUSIC_BASS, "bass"),
            ano::enum_entry(ANO_MUSIC_MELODY, "melody"),
            ano::enum_entry(ANO_MUSIC_COUNTER, "counter"),
            ano::enum_entry(ANO_MUSIC_ARP, "arp"),
            ano::enum_entry(ANO_MUSIC_PERC, "perc"),
        });

constexpr auto kPatchNames = ano::make_enum_map<
    AnoPatchName, ANO_PATCH_COUNT>(
        std::array{
            ano::enum_entry(ANO_PATCH_NONE, ""),
            ano::enum_entry(ANO_PATCH_WARM, "warm"),
            ano::enum_entry(ANO_PATCH_BRIGHT, "bright"),
            ano::enum_entry(ANO_PATCH_MORPH, "morph"),
            ano::enum_entry(ANO_PATCH_BREEZE, "breeze"),
            ano::enum_entry(ANO_PATCH_ROUND, "round"),
            ano::enum_entry(ANO_PATCH_DRIVEN, "driven"),
            ano::enum_entry(ANO_PATCH_BAD_GROUND, "bad_ground"),
            ano::enum_entry(ANO_PATCH_SOFT, "soft"),
            ano::enum_entry(ANO_PATCH_HARD, "hard"),
            ano::enum_entry(ANO_PATCH_MELLOW, "mellow"),
            ano::enum_entry(ANO_PATCH_KEYS, "keys"),
            ano::enum_entry(ANO_PATCH_WHISTLE, "whistle"),
            ano::enum_entry(ANO_PATCH_PLUCK, "pluck"),
            ano::enum_entry(ANO_PATCH_GLASS, "glass"),
            ano::enum_entry(ANO_PATCH_CHIMES, "chimes"),
        });

consteval bool names_valid()
{
    for (std::size_t i = 0; i < kLayerNames.size(); ++i) {
        const Layer layer = *Layer::from_raw(i);
        if (kLayerNames[layer] == nullptr || kLayerNames[layer][0] == '\0')
            return false;
    }
    const Patch first = Patch::constant<ANO_PATCH_NONE>();
    if (kPatchNames[first][0] != '\0')
        return false;
    for (std::size_t i = 1; i < kPatchNames.size(); ++i) {
        const Patch patch = *Patch::from_raw(i);
        if (kPatchNames[patch] == nullptr || kPatchNames[patch][0] == '\0')
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
    return parsed ? kLayerNames[*parsed] : "unknown";
}

uint32_t ano_music_patch_id(const char *name)
{
    if (name == nullptr)
        return 0;
    for (uint32_t i = 1; i < kPatchNames.size(); ++i) {
        const Patch patch = *Patch::from_raw(i);
        if (strcmp(name, kPatchNames[patch]) == 0)
            return i;
    }
    return 0;
}

const char *ano_music_patch_name(uint32_t id)
{
    const Patch patch = Patch::from_raw(id).value_or(
        Patch::constant<ANO_PATCH_NONE>());
    return kPatchNames[patch];
}
