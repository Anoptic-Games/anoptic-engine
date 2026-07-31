/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Composer timbres -> synth voices. Separate enum spaces, proven at compile time.

#include <array>

#include <anoptic_synth.h>

#include "cpp/ano_types.h"

namespace {

using MusicPatch = ano::EnumValue<AnoPatchName, ANO_PATCH_COUNT>;
using SynthPatch = ano::EnumValue<AnoSynthPatch, ANO_SYNTH_PATCH_COUNT>;

constexpr auto kPatchOfMusic = ano::make_enum_map<
    AnoPatchName, ANO_PATCH_COUNT>(
        std::array{
            ano::enum_entry(ANO_PATCH_NONE, ANO_SYNTH_PATCH_DEFAULT),
            ano::enum_entry(ANO_PATCH_WARM, ANO_SYNTH_PATCH_WARM),
            ano::enum_entry(ANO_PATCH_BRIGHT, ANO_SYNTH_PATCH_BRIGHT),
            ano::enum_entry(ANO_PATCH_MORPH, ANO_SYNTH_PATCH_MORPH),
            ano::enum_entry(ANO_PATCH_BREEZE, ANO_SYNTH_PATCH_BREEZE),
            ano::enum_entry(ANO_PATCH_ROUND, ANO_SYNTH_PATCH_ROUND),
            ano::enum_entry(ANO_PATCH_DRIVEN, ANO_SYNTH_PATCH_DRIVEN),
            ano::enum_entry(ANO_PATCH_BAD_GROUND, ANO_SYNTH_PATCH_BAD_GROUND),
            ano::enum_entry(ANO_PATCH_SOFT, ANO_SYNTH_PATCH_SOFT),
            ano::enum_entry(ANO_PATCH_HARD, ANO_SYNTH_PATCH_HARD),
            ano::enum_entry(ANO_PATCH_MELLOW, ANO_SYNTH_PATCH_MELLOW),
            ano::enum_entry(ANO_PATCH_KEYS, ANO_SYNTH_PATCH_KEYS),
            ano::enum_entry(ANO_PATCH_WHISTLE, ANO_SYNTH_PATCH_WHISTLE),
            ano::enum_entry(ANO_PATCH_PLUCK, ANO_SYNTH_PATCH_PLUCK),
            ano::enum_entry(ANO_PATCH_GLASS, ANO_SYNTH_PATCH_GLASS),
            ano::enum_entry(ANO_PATCH_CHIMES, ANO_SYNTH_PATCH_CHIMES),
        });

constexpr auto kMusicOfPatch = ano::invert_enum_map<
    AnoPatchName, ANO_PATCH_COUNT, AnoSynthPatch, ANO_SYNTH_PATCH_COUNT>(
        kPatchOfMusic);

consteval bool patch_map_valid()
{
    for (std::size_t i = 0; i < kPatchOfMusic.size(); ++i) {
        const MusicPatch music = *MusicPatch::from_raw(i);
        if (!SynthPatch::from(kPatchOfMusic[music]))
            return false;
    }
    return true;
}

static_assert(patch_map_valid());
static_assert(ano::Data<MusicPatch>);
static_assert(ano::Data<SynthPatch>);
static_assert(ano::Data<decltype(kPatchOfMusic)>);
static_assert(ano::Data<decltype(kMusicOfPatch)>);

} // namespace

uint32_t ano_synth_patch_id(const char *name)
{
    const MusicPatch music = *MusicPatch::from_raw(ano_music_patch_id(name));
    const SynthPatch synth = *SynthPatch::from(kPatchOfMusic[music]);
    return static_cast<uint32_t>(ano::underlying(synth.get()));
}

const char *ano_synth_patch_name(uint32_t id)
{
    const SynthPatch patch = SynthPatch::from_raw(id).value_or(
        SynthPatch::constant<ANO_SYNTH_PATCH_DEFAULT>());
    const MusicPatch music = *MusicPatch::from(kMusicOfPatch[patch]);
    return ano_music_patch_name(
        static_cast<uint32_t>(ano::underlying(music.get())));
}

uint32_t ano_synth_patch_of(uint32_t musicPatch)
{
    const MusicPatch music = MusicPatch::from_raw(musicPatch).value_or(
        MusicPatch::constant<ANO_PATCH_NONE>());
    const SynthPatch synth = *SynthPatch::from(kPatchOfMusic[music]);
    return static_cast<uint32_t>(ano::underlying(synth.get()));
}
