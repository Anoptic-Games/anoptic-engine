/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Included by anoptic_meta.h. These value contracts share its reflected enum proofs.

#ifndef ANOPTICENGINE_ANOPTIC_META_H
#error "Include <anoptic_meta.h> instead of <anoptic_meta_types.h>"
#endif

#ifndef ANOPTICENGINE_ANOPTIC_META_TYPES_H
#define ANOPTICENGINE_ANOPTIC_META_TYPES_H

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

template<Enum E>
inline constexpr std::size_t enum_count = reflected_enum_count<E>;

// A reflected zero-based dense enum proven inside [0, *_COUNT).
template<Enum E>
class EnumValue final {
public:
    static_assert(reflected_enum_domain<E>.valid,
                  "EnumValue requires one dense enum domain and a *_COUNT sentinel");

    template<std::integral I>
        requires (!std::same_as<std::remove_cv_t<I>, bool>)
    static constexpr Option<EnumValue> from_raw(I raw) noexcept
    {
        if constexpr (std::is_signed_v<I>)
            if (raw < 0)
                return {};
        using U = std::make_unsigned_t<I>;
        if (static_cast<std::uintmax_t>(static_cast<U>(raw)) >= enum_count<E>)
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

#endif // ANOPTICENGINE_ANOPTIC_META_TYPES_H
