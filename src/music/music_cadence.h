/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// One reflected policy schema for harmony, melody, and verification.

#ifndef ANO_MUSIC_CADENCE_H
#define ANO_MUSIC_CADENCE_H

#include <anoptic_music.h>

#include "cpp/ano_types.h"

namespace ano::music {

using Cadence = EnumValue<AnoCadencePolicy>;

inline constexpr auto CADENCE_CONTRACTS =
    reflect_dense_enum_contracts<AnoCadencePolicy, AnoCadenceContract>();
inline constexpr auto CADENCE_NAMES = reflect_enum_names<AnoCadencePolicy>(
    "ANO_CADENCE_", EnumNameCase::lower);

constexpr const AnoCadenceContract& cadence_contract(AnoCadencePolicy policy)
{
    const Cadence value = Cadence::from(policy).value_or(
        Cadence::constant<ANO_CADENCE_AUTHENTIC>());
    return CADENCE_CONTRACTS.values[value.index()];
}

constexpr const char* cadence_name(AnoCadencePolicy policy)
{
    const auto value = Cadence::from(policy);
    return value ? CADENCE_NAMES.values[value->index()] : "invalid";
}

static_assert(Data<AnoCadenceContract>);
static_assert(Data<decltype(CADENCE_CONTRACTS)>);
static_assert(Data<decltype(CADENCE_NAMES)>);

} // namespace ano::music

#endif // ANO_MUSIC_CADENCE_H
