/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Load-bearing mode metadata. Every enum key appears exactly once at compile time.

#include "music_modes.h"

#include <array>

#include "cpp/ano_types.h"

namespace {

using Mode = ano::EnumValue<AnoMode, ANO_MODE_COUNT>;

struct ModeInfo final {
    const char *name;
    std::array<uint8_t, 7> intervals;
    int brightness;
};

constexpr auto kModes = ano::make_enum_map<AnoMode, ANO_MODE_COUNT>(
    std::array{
        ano::enum_entry(ANO_MODE_IONIAN,
                        ModeInfo{ "ionian", { 0, 2, 4, 5, 7, 9, 11 }, 2 }),
        ano::enum_entry(ANO_MODE_DORIAN,
                        ModeInfo{ "dorian", { 0, 2, 3, 5, 7, 9, 10 }, 0 }),
        ano::enum_entry(ANO_MODE_PHRYGIAN,
                        ModeInfo{ "phrygian", { 0, 1, 3, 5, 7, 8, 10 }, -2 }),
        ano::enum_entry(ANO_MODE_LYDIAN,
                        ModeInfo{ "lydian", { 0, 2, 4, 6, 7, 9, 11 }, 3 }),
        ano::enum_entry(ANO_MODE_MIXOLYDIAN,
                        ModeInfo{ "mixolydian", { 0, 2, 4, 5, 7, 9, 10 }, 1 }),
        ano::enum_entry(ANO_MODE_AEOLIAN,
                        ModeInfo{ "aeolian", { 0, 2, 3, 5, 7, 8, 10 }, -1 }),
        ano::enum_entry(ANO_MODE_LOCRIAN,
                        ModeInfo{ "locrian", { 0, 1, 3, 5, 6, 8, 10 }, -1 }),
    });

consteval bool mode_table_valid()
{
    for (std::size_t i = 0; i < kModes.size(); ++i) {
        const Mode mode = *Mode::from_raw(i);
        const ModeInfo &info = kModes[mode];
        if (info.name == nullptr || info.name[0] == '\0' || info.intervals[0] != 0)
            return false;
        for (std::size_t degree = 1; degree < info.intervals.size(); ++degree)
            if (info.intervals[degree] <= info.intervals[degree - 1]
                || info.intervals[degree] >= 12)
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
static_assert(ano::Data<ModeInfo>);
static_assert(ano::Data<decltype(kModes)>);

} // namespace

const char *ano_mode_name(AnoMode mode)
{
    return kModes[mode_or_ionian(mode)].name;
}

int ano_mode_brightness(AnoMode mode)
{
    const auto parsed = Mode::from(mode);
    return parsed ? kModes[*parsed].brightness : -1;
}

const uint8_t *ano_mode_intervals(AnoMode mode)
{
    return kModes[mode_or_ionian(mode)].intervals.data();
}
