#ifndef ANO_AUDIO_COMMAND_CONTRACT_H
#define ANO_AUDIO_COMMAND_CONTRACT_H

#include "cpp/ano_reflect.h"

#include <anoptic_audio.h>

namespace ano::audio_contract {

inline constexpr auto commands =
    ano::reflect_enum_contracts<AnoAudioCommandKind, AnoAudioCommandContract>();
inline constexpr auto events =
    ano::reflect_enum_contracts<AnoAudioEventKind, AnoAudioEventContract>();

static_assert(commands.count <= 32);

template<class Visitor>
constexpr bool visit_command(uint32_t raw, Visitor&& visitor)
{
    return commands.find(raw) && ano::visit_enum(static_cast<AnoAudioCommandKind>(raw), visitor);
}

consteval auto reflect_fields()
{
    ano::EnumFieldRegistry<commands.count> result{};
    result.declared = ano::reflect_bit_flags<AnoAudioFieldBits>();
    uint32_t used = 0;
    ano::reflect_field_uses<AnoAudioCommand, AnoAudioFieldUse>(result, used);
    if (used != result.declared)
        __builtin_abort();
    for (size_t i = 0; i < commands.count; ++i) {
        const AnoAudioCommandPayload payload = commands.values[i].payload;
        const bool partial = payload == AnoAudioCommandPayload::source_update ||
                             payload == AnoAudioCommandPayload::bus_update;
        if (partial != (result.allowed[i] != 0))
            __builtin_abort();
    }
    return result;
}

static_assert(reflect_fields().declared != 0);
static_assert(ano::validate_pointer_payloads<AnoAudioCommand, AnoAudioPointerPayloadUse>(commands,
    [](AnoAudioCommandContract contract) {
        return contract.ownership == AnoAudioPayloadOwnership::adopted || contract.ownership == AnoAudioPayloadOwnership::borrowed;
    }));

constexpr const void* pointer_payload(const AnoAudioCommand& commandValue)
{
    return ano::pointer_payload<AnoAudioCommand, AnoAudioPointerPayloadUse>(commandValue);
}

static_assert(ano::validate_tagged_union<AnoAudioEventPayload, AnoAudioEventPayloadFor>(events,
    [](AnoAudioEventContract contract) { return contract.payload != AnoAudioEventPayloadKind::none; }));
static_assert([] consteval {
    for (size_t i = 0; i < events.count; ++i) {
        if ((events.values[i].ownership == AnoAudioPayloadOwnership::returned) !=
            (events.values[i].payload == AnoAudioEventPayloadKind::buffer))
            return false;
    }
    return true;
}());

} // namespace ano::audio_contract

#endif // ANO_AUDIO_COMMAND_CONTRACT_H
