/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Composer timbres -> synth voices. Separate enum spaces, proven correspondent at compile time.

#include <anoptic_synth.h>

#include "cpp/ano_types.h"

namespace {

using MusicPatch = ano::EnumValue<AnoPatchName>;
using SynthPatch = ano::EnumValue<AnoSynthPatch>;

static_assert(static_cast<uint32_t>(ANO_PATCH_NONE)
              == static_cast<uint32_t>(ANO_SYNTH_PATCH_DEFAULT));
static_assert(ano::reflected_enum_suffixes_equal<AnoPatchName, AnoSynthPatch>(
    "ANO_PATCH_", "ANO_SYNTH_PATCH_", 1));
static_assert(ano::Data<MusicPatch>);
static_assert(ano::Data<SynthPatch>);

} // namespace

uint32_t ano_synth_patch_id(const char *name)
{
    return ano_music_patch_id(name);
}

const char *ano_synth_patch_name(uint32_t id)
{
    const SynthPatch patch = SynthPatch::from_raw(id).value_or(
        SynthPatch::constant<ANO_SYNTH_PATCH_DEFAULT>());
    return ano_music_patch_name(static_cast<uint32_t>(patch.index()));
}

uint32_t ano_synth_patch_of(uint32_t musicPatch)
{
    const MusicPatch music = MusicPatch::from_raw(musicPatch).value_or(
        MusicPatch::constant<ANO_PATCH_NONE>());
    return static_cast<uint32_t>(music.index());
}
