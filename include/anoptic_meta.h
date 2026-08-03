/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// C++26 structural reflection and value contracts. Header-only; no runtime surface.

#ifndef ANOPTICENGINE_ANOPTIC_META_H
#define ANOPTICENGINE_ANOPTIC_META_H

#include <meta>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <type_traits>

namespace ano {

constexpr bool enum_identifier_ends_with(std::string_view identifier,
                                         std::string_view suffix)
{
    return identifier.size() >= suffix.size()
        && identifier.substr(identifier.size() - suffix.size()) == suffix;
}

struct ReflectedEnumDomain final {
    size_t count;
    bool valid;
};

// Dense values occupy [0, *_COUNT). Negative policy sentinels may follow the count.
template<class Enum>
consteval ReflectedEnumDomain reflect_enum_domain()
{
    static_assert(std::is_enum_v<Enum>);
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Enum));
    size_t count = 0;
    size_t sentinels = 0;
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto identifier = std::meta::identifier_of(enumerator);
        if constexpr (enum_identifier_ends_with(identifier, "_COUNT")) {
            constexpr auto raw = static_cast<int64_t>([:enumerator:]);
            static_assert(raw > 0);
            count = static_cast<size_t>(raw);
            ++sentinels;
        }
    }
    if (sentinels != 1 || count > enumerators.size())
        return { count, false };

    bool seen[enumerators.size()] = {};
    bool valid = true;
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto identifier = std::meta::identifier_of(enumerator);
        constexpr auto raw = static_cast<int64_t>([:enumerator:]);
        if constexpr (!enum_identifier_ends_with(identifier, "_COUNT") && raw >= 0) {
            if (static_cast<size_t>(raw) >= count || seen[static_cast<size_t>(raw)])
                valid = false;
            else
                seen[static_cast<size_t>(raw)] = true;
        }
    }
    for (size_t i = 0; i < count; ++i)
        valid = valid && seen[i];
    return { count, valid };
}

template<class Enum>
inline constexpr ReflectedEnumDomain reflected_enum_domain =
    reflect_enum_domain<Enum>();

template<class Enum>
inline constexpr size_t reflected_enum_count = reflected_enum_domain<Enum>.count;

template<class Enum>
inline constexpr size_t reflected_enumerator_count = [] consteval {
    return std::define_static_array(std::meta::enumerators_of(^^Enum)).size();
}();

enum class EnumNameCase : uint8_t { preserve, lower, upper };

constexpr char enum_name_case(char value, EnumNameCase nameCase)
{
    if (nameCase == EnumNameCase::lower && value >= 'A' && value <= 'Z')
        return static_cast<char>(value + ('a' - 'A'));
    if (nameCase == EnumNameCase::upper && value >= 'a' && value <= 'z')
        return static_cast<char>(value - ('a' - 'A'));
    return value;
}

constexpr bool enum_name_equal(const char* lhs, const char* rhs)
{
    if (lhs == nullptr || rhs == nullptr)
        return false;
    while (*lhs != '\0' && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return *lhs == *rhs;
}

template<class Value, size_t Count>
struct EnumRegistry final {
    static constexpr size_t count = Count;
    Value values[Count];

    static constexpr size_t size() { return Count; }

    constexpr const Value* find(size_t index) const
    {
        return index < Count ? &values[index] : nullptr;
    }

    constexpr int64_t find(const char* name, int64_t fallback) const
        requires std::is_same_v<Value, const char*>
    {
        if (name != nullptr)
            for (size_t i = 0; i < Count; ++i)
                if (enum_name_equal(name, values[i]))
                    return static_cast<int64_t>(i);
        return fallback;
    }
};

template<class Enum, class Value, size_t Count, size_t FirstValue,
         class Projection>
consteval auto reflect_enum_values(Projection projection)
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Enum));
    EnumRegistry<Value, Count> result{};
    bool seen[Count] = {};
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto raw = static_cast<int64_t>([:enumerator:]);
        if constexpr (raw >= static_cast<int64_t>(FirstValue)
                      && raw < static_cast<int64_t>(FirstValue + Count)) {
            constexpr size_t index = static_cast<size_t>(raw) - FirstValue;
            if (seen[index])
                __builtin_abort();
            seen[index] = true;
            result.values[index] =
                projection.template operator()<enumerator>();
        }
    }
    for (bool present : seen)
        if (!present)
            __builtin_abort();
    return result;
}

template<class Enum>
consteval auto reflect_enum_names(std::string_view prefix, EnumNameCase nameCase,
                                  int64_t emptyValue = -1)
{
    static_assert(reflected_enum_domain<Enum>.valid,
                  "reflected names require one dense enum domain and a *_COUNT sentinel");
    return reflect_enum_values<Enum, const char*,
        reflected_enum_count<Enum>, 0>(
        [=]<auto enumerator>() consteval {
            constexpr auto raw = static_cast<int64_t>([:enumerator:]);
            constexpr auto identifier = std::meta::identifier_of(enumerator);
            if (!identifier.starts_with(prefix))
                __builtin_abort();
            char transformed[identifier.size() + 1] = {};
            size_t length = 0;
            if (raw != emptyValue)
                for (size_t i = prefix.size(); i < identifier.size(); ++i)
                    transformed[length++] = enum_name_case(identifier[i], nameCase);
            return std::define_static_string(
                std::string_view(transformed, length));
        });
}

template<class Enum, size_t FirstValue = 0>
consteval auto reflect_enum_identifiers()
{
    return reflect_enum_values<Enum, const char*,
        reflected_enumerator_count<Enum>, FirstValue>(
        []<auto enumerator>() consteval {
            return std::define_static_string(
                std::meta::identifier_of(enumerator));
        });
}

template<class Enum>
consteval std::string_view reflected_enum_identifier(size_t index)
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Enum));
    std::string_view result{};
    template for (constexpr auto enumerator : enumerators) {
        constexpr auto raw = static_cast<int64_t>([:enumerator:]);
        if (raw >= 0 && static_cast<size_t>(raw) == index)
            result = std::meta::identifier_of(enumerator);
    }
    return result;
}

template<class From, class To>
consteval bool reflected_enum_suffixes_equal(std::string_view fromPrefix,
                                             std::string_view toPrefix,
                                             size_t first = 0)
{
    if (!reflected_enum_domain<From>.valid || !reflected_enum_domain<To>.valid
        || reflected_enum_count<From> != reflected_enum_count<To>)
        return false;
    for (size_t i = first; i < reflected_enum_count<From>; ++i) {
        const auto from = reflected_enum_identifier<From>(i);
        const auto to = reflected_enum_identifier<To>(i);
        if (!from.starts_with(fromPrefix) || !to.starts_with(toPrefix)
            || from.substr(fromPrefix.size()) != to.substr(toPrefix.size()))
            return false;
    }
    return true;
}

template<class Enum, class Contract>
consteval auto reflect_dense_enum_contracts()
{
    static_assert(reflected_enum_domain<Enum>.valid,
                  "reflected contracts require one dense enum domain and a *_COUNT sentinel");
    return reflect_enum_values<Enum, Contract,
        reflected_enum_count<Enum>, 0>(
        []<auto enumerator>() consteval {
            constexpr auto annotations = std::define_static_array(
                std::meta::annotations_of_with_type(enumerator, ^^Contract));
            static_assert(annotations.size() == 1);
            return std::meta::extract<Contract>(annotations[0]);
        });
}

template<class Enum, class Contract, size_t FirstValue = 0>
consteval auto reflect_enum_contracts()
{
    return reflect_enum_values<Enum, Contract,
        reflected_enumerator_count<Enum>, FirstValue>(
        []<auto enumerator>() consteval {
            constexpr auto annotations = std::define_static_array(
                std::meta::annotations_of_with_type(enumerator, ^^Contract));
            static_assert(annotations.size() == 1);
            return std::meta::extract<Contract>(annotations[0]);
        });
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
        if (count[i] != static_cast<uint8_t>(hasPointer(registry.values[i])))
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
        if (registry.values[index].payload != link.payload)
            return false;
        ++count[index];
    }
    for (size_t i = 0; i < Registry::count; ++i)
        if (count[i] != static_cast<uint8_t>(requiresPayload(registry.values[i])))
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

#include <anoptic_meta_types.h>

#endif // ANOPTICENGINE_ANOPTIC_META_H
