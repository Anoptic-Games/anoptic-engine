/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private C++ value contracts. Header-only; no ownership or runtime surface.

#ifndef ANO_CPP_TYPES_H
#define ANO_CPP_TYPES_H

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace ano {

template<class T>
concept Data = std::is_standard_layout_v<T>
            && std::is_trivially_copyable_v<T>
            && !std::is_polymorphic_v<T>;

template<class E>
concept Enum = std::is_enum_v<E>;

template<Enum E>
constexpr auto underlying(E value) noexcept
{
    return std::to_underlying(value);
}

constexpr void assume(bool condition) noexcept
{
    [[assume(condition)]];
}

template<class T>
class Option final {
public:
    constexpr Option() noexcept = default;
    constexpr Option(T value) noexcept : value_(value), has_(true) {}

    constexpr explicit operator bool() const noexcept { return has_; }
    constexpr bool has_value() const noexcept { return has_; }

    constexpr const T &operator*() const noexcept
    {
        assume(has_);
        return value_;
    }

    constexpr T &operator*() noexcept
    {
        assume(has_);
        return value_;
    }

    constexpr const T *operator->() const noexcept
    {
        assume(has_);
        return &value_;
    }

    constexpr T *operator->() noexcept
    {
        assume(has_);
        return &value_;
    }

    constexpr T value_or(T fallback) const noexcept
    {
        return has_ ? value_ : fallback;
    }

private:
    T value_{};
    bool has_ = false;
};

template<Enum E, E Count>
inline constexpr std::size_t enum_count = [] consteval {
    constexpr auto count = underlying(Count);
    static_assert(count > 0);
    return static_cast<std::size_t>(count);
}();

// A zero-based dense enum proven inside [0, Count).
template<Enum E, E Count>
class EnumValue final {
public:
    template<std::integral I>
        requires (!std::same_as<std::remove_cv_t<I>, bool>)
    static constexpr Option<EnumValue> from_raw(I raw) noexcept
    {
        if constexpr (std::is_signed_v<I>)
            if (raw < 0)
                return {};
        using U = std::make_unsigned_t<I>;
        if (static_cast<std::uintmax_t>(static_cast<U>(raw)) >= enum_count<E, Count>)
            return {};
        return EnumValue(static_cast<E>(raw));
    }

    static constexpr Option<EnumValue> from(E value) noexcept
    {
        return from_raw(underlying(value));
    }

    template<E Value>
    static consteval EnumValue constant() noexcept
    {
        constexpr auto parsed = from(Value);
        static_assert(parsed.has_value());
        return *parsed;
    }

    constexpr E get() const noexcept { return value_; }
    constexpr std::size_t index() const noexcept
    {
        return static_cast<std::size_t>(underlying(value_));
    }

    friend constexpr bool operator==(EnumValue, EnumValue) noexcept = default;

private:
    template<class>
    friend class Option;

    constexpr EnumValue() noexcept : value_{} {}
    constexpr explicit EnumValue(E value) noexcept : value_(value) {}

    E value_;
};

template<Enum E, class T>
struct EnumEntry final {
    E key;
    T value;
};

template<Enum E, class T>
constexpr auto enum_entry(E key, T &&value)
{
    return EnumEntry<E, std::decay_t<T>>{ key, std::forward<T>(value) };
}

template<Enum E, E Count, class T>
class EnumTable final {
public:
    using Key = EnumValue<E, Count>;

    constexpr const T &operator[](Key key) const noexcept
    {
        return values_.data()[key.index()];
    }
    constexpr T &operator[](Key key) noexcept { return values_.data()[key.index()]; }
    static constexpr std::size_t size() noexcept { return enum_count<E, Count>; }

private:
    template<Enum K, K N, class V, std::size_t Size>
    friend consteval auto make_enum_table(std::array<V, Size>);
    template<Enum K, K N, class V, std::size_t Size>
    friend consteval auto make_enum_map(std::array<EnumEntry<K, V>, Size>);

    consteval explicit EnumTable(std::array<T, enum_count<E, Count>> values)
        : values_(values) {}

    std::array<T, enum_count<E, Count>> values_;
};

template<Enum E, E Count, class T, std::size_t Size>
consteval auto make_enum_table(std::array<T, Size> values)
{
    static_assert(Size == enum_count<E, Count>);
    return EnumTable<E, Count, T>(values);
}

// A dense enum table whose associations are explicit and order-independent.
template<Enum E, E Count, class T, std::size_t Size>
consteval auto make_enum_map(std::array<EnumEntry<E, T>, Size> entries)
{
    static_assert(Size == enum_count<E, Count>);
    std::array<T, enum_count<E, Count>> values{};
    std::array<bool, enum_count<E, Count>> seen{};
    for (const auto &entry : entries) {
        const auto key = EnumValue<E, Count>::from(entry.key);
        if (!key)
            std::unreachable();
        if (seen[key->index()])
            std::unreachable();
        values[key->index()] = entry.value;
        seen[key->index()] = true;
    }
    return EnumTable<E, Count, T>(values);
}

// Invert a one-to-one dense enum map. Duplicate or invalid destinations fail evaluation.
template<Enum From, From FromCount, Enum To, To ToCount>
consteval auto invert_enum_map(const EnumTable<From, FromCount, To> &forward)
{
    std::array<EnumEntry<To, From>, enum_count<From, FromCount>> entries{};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto source = *EnumValue<From, FromCount>::from_raw(i);
        entries[i] = EnumEntry<To, From>{ forward[source], source.get() };
    }
    return make_enum_map<To, ToCount>(entries);
}

// A raw bit field masked to the only bits this boundary understands.
template<Enum E, std::uintmax_t ValidMask>
class EnumFlags final {
public:
    using Underlying = std::make_unsigned_t<std::underlying_type_t<E>>;

    template<std::integral I>
    static constexpr EnumFlags masked(I raw) noexcept
    {
        return EnumFlags(static_cast<Underlying>(
            static_cast<std::uintmax_t>(raw) & ValidMask));
    }

    template<E Flag>
    constexpr bool contains() const noexcept
    {
        constexpr auto flag = static_cast<std::uintmax_t>(underlying(Flag));
        static_assert(flag != 0 && (flag & ~ValidMask) == 0);
        return (bits_ & static_cast<Underlying>(flag)) == static_cast<Underlying>(flag);
    }

    constexpr Underlying bits() const noexcept { return bits_; }

private:
    static_assert((ValidMask & ~static_cast<std::uintmax_t>(
                       static_cast<Underlying>(~Underlying{0}))) == 0);

    constexpr explicit EnumFlags(Underlying bits) noexcept : bits_(bits) {}

    Underlying bits_;
};

} // namespace ano

#endif // ANO_CPP_TYPES_H
