#ifndef ANO_CPP_REFLECT_H
#define ANO_CPP_REFLECT_H

#include <meta>
#include <stddef.h>
#include <stdint.h>

namespace ano {

template<class Contract, size_t Count>
struct EnumContractRegistry final {
    static constexpr size_t count = Count;
    Contract contracts[Count];
    constexpr const Contract* find(size_t index) const { return index < Count ? &contracts[index] : nullptr; }
};

template<class Enum, class Contract>
consteval auto reflect_enum_contracts()
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Enum));
    EnumContractRegistry<Contract, enumerators.size()> result{};
    bool seen[enumerators.size()] = {};
    template for (constexpr auto enumerator : enumerators) {
        constexpr Enum value = [:enumerator:];
        constexpr size_t index = static_cast<size_t>(value);
        static_assert(index < enumerators.size());
        if (seen[index])
            __builtin_abort();
        seen[index] = true;
        constexpr auto annotations = std::define_static_array(
            std::meta::annotations_of_with_type(enumerator, ^^Contract));
        static_assert(annotations.size() == 1);
        result.contracts[index] = std::meta::extract<Contract>(annotations[0]);
    }
    for (bool present : seen)
        if (!present)
            __builtin_abort();
    return result;
}

template<size_t Count>
struct EnumFieldRegistry final {
    uint32_t allowed[Count];
    uint32_t declared;
};

template<class Bits>
consteval uint32_t reflect_bit_flags()
{
    uint32_t result = 0;
    static constexpr auto bits = std::define_static_array(std::meta::enumerators_of(^^Bits));
    template for (constexpr auto bit : bits) {
        constexpr uint32_t value = static_cast<uint32_t>([:bit:]);
        static_assert(value != 0 && (value & (value - 1u)) == 0);
        if (result & value)
            __builtin_abort();
        result |= value;
    }
    return result;
}

template<class Record, class Use, size_t Count>
consteval void reflect_field_uses(EnumFieldRegistry<Count>& result, uint32_t& used)
{
    constexpr uint32_t commandMask = static_cast<uint32_t>((uint64_t{1} << Count) - 1u);
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(^^Record, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(std::meta::annotations_of_with_type(member, ^^Use));
        static_assert(annotations.size() <= 1);
        if constexpr (annotations.size() == 1) {
            constexpr Use use = std::meta::extract<Use>(annotations[0]);
            if (use.fields == 0 || (use.fields & ~result.declared) != 0 || use.commands == 0 || (use.commands & ~commandMask) != 0)
                __builtin_abort();
            used |= use.fields;
            for (size_t i = 0; i < Count; ++i)
                if (use.commands & (1u << i))
                    result.allowed[i] |= use.fields;
        }
    }
}

template<class Command, class Use, class Registry, class HasPointer>
consteval bool validate_pointer_payloads(const Registry& registry, HasPointer hasPointer)
{
    static_assert(Registry::count <= 32);
    uint8_t count[Registry::count] = {};
    constexpr uint32_t commandMask = static_cast<uint32_t>((uint64_t{1} << Registry::count) - 1u);
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(^^Command, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(std::meta::annotations_of_with_type(member, ^^Use));
        static_assert(annotations.size() <= 1);
        if constexpr (annotations.size() == 1) {
            static_assert(std::meta::is_pointer_type(std::meta::type_of(member)));
            constexpr Use use = std::meta::extract<Use>(annotations[0]);
            if (use.commands == 0 || (use.commands & ~commandMask) != 0)
                return false;
            for (size_t i = 0; i < Registry::count; ++i)
                if (use.commands & (1u << i))
                    ++count[i];
        }
    }
    for (size_t i = 0; i < Registry::count; ++i)
        if (count[i] != static_cast<uint8_t>(hasPointer(registry.contracts[i])))
            return false;
    return true;
}

template<class Command, class Use>
constexpr const void* pointer_payload(const Command& command)
{
    const size_t index = static_cast<size_t>(command.kind);
    if (index >= 32)
        return nullptr;
    const uint32_t commandBit = 1u << index;
    const void* result = nullptr;
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(^^Command, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(std::meta::annotations_of_with_type(member, ^^Use));
        if constexpr (annotations.size() == 1) {
            constexpr Use use = std::meta::extract<Use>(annotations[0]);
            if (use.commands & commandBit)
                result = command.[:member:];
        }
    }
    return result;
}

template<class Union, class Link, class Registry, class RequiresPayload>
consteval bool validate_tagged_union(const Registry& registry, RequiresPayload requiresPayload)
{
    uint8_t count[Registry::count] = {};
    static constexpr auto members = std::define_static_array(std::meta::nonstatic_data_members_of(^^Union, std::meta::access_context::current()));
    template for (constexpr auto member : members) {
        constexpr auto annotations = std::define_static_array(std::meta::annotations_of_with_type(member, ^^Link));
        static_assert(annotations.size() == 1);
        constexpr Link link = std::meta::extract<Link>(annotations[0]);
        constexpr size_t index = static_cast<size_t>(link.kind);
        static_assert(index < Registry::count);
        if (registry.contracts[index].payload != link.payload)
            return false;
        ++count[index];
    }
    for (size_t i = 0; i < Registry::count; ++i)
        if (count[i] != static_cast<uint8_t>(requiresPayload(registry.contracts[i])))
            return false;
    return true;
}

template<class Enum, class Visitor>
constexpr bool visit_enum(Enum value, Visitor&& visitor)
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Enum));
    template for (constexpr auto enumerator : enumerators) {
        if (value == [:enumerator:]) {
            visitor.template operator()<([:enumerator:])>();
            return true;
        }
    }
    return false;
}

template<auto>
inline constexpr bool dependent_false = false;

} // namespace ano

#endif // ANO_CPP_REFLECT_H
