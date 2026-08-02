#ifndef ANO_RENDER_COMMAND_CONTRACT_H
#define ANO_RENDER_COMMAND_CONTRACT_H

#include "cpp/ano_reflect.h"

#include <anoptic_render.h>

namespace ano::render_contract {

inline constexpr auto commands =
    ano::reflect_enum_contracts<RenderCommandKind, AnoRenderCommandContract>();
inline constexpr auto events =
    ano::reflect_enum_contracts<RenderEventKind, AnoRenderEventContract>();
inline constexpr auto inputs =
    ano::reflect_enum_contracts<AnoInputKind, AnoInputContract>();

static_assert(commands.count <= 32);

template<class Visitor>
constexpr bool visit_command(RenderCommandKind kind, Visitor&& visitor)
{
    return commands.find(static_cast<size_t>(kind)) && ano::visit_enum(kind, visitor);
}

consteval auto reflect_fields()
{
    ano::EnumFieldRegistry<commands.count> result{};
    result.declared = ano::reflect_bit_flags<RenderFieldBits>();
    uint32_t used = 0;
    ano::reflect_field_uses<RenderCommand, AnoRenderFieldUse>(result, used);
    ano::reflect_field_uses<RenderUpdateBatch, AnoRenderFieldUse>(result, used);
    if (used != result.declared)
        __builtin_abort();
    for (size_t i = 0; i < commands.count; ++i) {
        const AnoRenderCommandPayload payload = commands.values[i].payload;
        const bool fieldsExpected = payload == AnoRenderCommandPayload::create ||
                                    payload == AnoRenderCommandPayload::update ||
                                    payload == AnoRenderCommandPayload::bulk_update;
        if (fieldsExpected != (result.allowed[i] != 0))
            __builtin_abort();
    }
    return result;
}

inline constexpr auto fields = reflect_fields();

constexpr uint32_t allowed_fields(RenderCommandKind kind)
{
    const size_t index = static_cast<size_t>(kind);
    return index < commands.count ? fields.allowed[index] : 0u;
}

static_assert(ano::validate_pointer_payloads<RenderCommand, AnoRenderOwnedPayloadFor>(commands,
    [](AnoRenderCommandContract contract) { return contract.ownership == AnoRenderPayloadOwnership::conditional_owned; }));

constexpr const void* owned_payload(const RenderCommand& commandValue)
{
    return ano::pointer_payload<RenderCommand, AnoRenderOwnedPayloadFor>(commandValue);
}

static_assert(ano::validate_tagged_union<AnoInputPayload, AnoInputPayloadFor>(inputs,
    [](AnoInputContract) { return true; }));
static_assert(ano::validate_tagged_union<AnoRenderEventPayload, AnoRenderEventPayloadFor>(events,
    [](AnoRenderEventContract contract) { return contract.payload != AnoRenderEventPayloadKind::none; }));

} // namespace ano::render_contract

#endif // ANO_RENDER_COMMAND_CONTRACT_H
