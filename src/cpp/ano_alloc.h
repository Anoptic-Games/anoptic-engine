/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private typed allocation for plain engine data. No ownership or runtime surface.

#ifndef ANO_CPP_ALLOC_H
#define ANO_CPP_ALLOC_H

#include "cpp/ano_types.h"

#include <anoptic_memory.h>
#include <cstddef>
#include <cstdint>

namespace ano {

template<Data T>
[[nodiscard]] constexpr bool allocation_fits(std::size_t count) noexcept
{
    return count <= SIZE_MAX / sizeof(T);
}

template<Data T>
[[nodiscard]] T* allocate(std::size_t count) noexcept
{
    return allocation_fits<T>(count)
        ? static_cast<T*>(malloc(count * sizeof(T)))
        : nullptr;
}

template<Data T>
[[nodiscard]] T* allocate_zero(std::size_t count) noexcept
{
    return allocation_fits<T>(count)
        ? static_cast<T*>(calloc(count, sizeof(T)))
        : nullptr;
}

template<Data T>
[[nodiscard]] T* reallocate(T* data, std::size_t count) noexcept
{
    return allocation_fits<T>(count)
        ? static_cast<T*>(realloc(data, count * sizeof(T)))
        : nullptr;
}

template<Data T>
[[nodiscard]] T* heap_allocate(mi_heap_t* heap, std::size_t count) noexcept
{
    return allocation_fits<T>(count)
        ? static_cast<T*>(mi_heap_malloc(heap, count * sizeof(T)))
        : nullptr;
}

template<Data T>
[[nodiscard]] T* heap_allocate_zero(mi_heap_t* heap, std::size_t count) noexcept
{
    return allocation_fits<T>(count)
        ? static_cast<T*>(mi_heap_calloc(heap, count, sizeof(T)))
        : nullptr;
}

template<Data T>
[[nodiscard]] T* heap_reallocate(mi_heap_t* heap, T* data, std::size_t count) noexcept
{
    return allocation_fits<T>(count)
        ? static_cast<T*>(mi_heap_realloc(heap, data, count * sizeof(T)))
        : nullptr;
}

} // namespace ano

#endif // ANO_CPP_ALLOC_H
