/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Load-bearing mode metadata. Reflection projects each enum annotation exactly once.

#include "music_modes.h"

#include <anoptic_meta.h>

namespace {

using Mode = ano::EnumValue<AnoMode>;

inline constexpr auto kModeNames = ano::reflect_enum_names<AnoMode>(
    "ANO_MODE_", ano::EnumNameCase::lower);
inline constexpr auto kModeContracts =
    ano::reflect_dense_enum_contracts<AnoMode, AnoModeContract>();

consteval bool mode_table_valid()
{
    for (std::size_t i = 0; i < kModeNames.size(); ++i) {
        const AnoModeContract &contract = kModeContracts.values[i];
        if (kModeNames.values[i] == nullptr || kModeNames.values[i][0] == '\0'
            || contract.intervals[0] != 0)
            return false;
        for (std::size_t degree = 1; degree < 7; ++degree)
            if (contract.intervals[degree] <= contract.intervals[degree - 1]
                || contract.intervals[degree] >= 12)
                return false;
    }
    return true;
}

constexpr Mode mode_or_ionian(AnoMode raw)
{
    return Mode::from(raw).value_or(Mode::constant<ANO_MODE_IONIAN>());
}

static_assert(mode_table_valid());
static_assert(ano::Data<Mode>);
static_assert(ano::Data<AnoModeContract>);
static_assert(ano::Data<decltype(kModeContracts)>);
static_assert(ano::Data<decltype(kModeNames)>);

} // namespace

const char *ano_mode_name(AnoMode mode)
{
    return kModeNames.values[mode_or_ionian(mode).index()];
}

int ano_mode_brightness(AnoMode mode)
{
    const auto parsed = Mode::from(mode);
    return parsed ? kModeContracts.values[parsed->index()].brightness : -1;
}

const uint8_t *ano_mode_intervals(AnoMode mode)
{
    return kModeContracts.values[mode_or_ionian(mode).index()].intervals;
}
