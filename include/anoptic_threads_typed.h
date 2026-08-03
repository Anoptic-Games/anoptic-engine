/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// C++26 typed lock-free transport. Header-only; runtime thread API stays in anoptic_threads.h.

#ifndef ANOPTIC_THREADS_TYPED_H
#define ANOPTIC_THREADS_TYPED_H

#ifndef __cplusplus
#error "anoptic_threads_typed.h requires C++26"
#endif

#include <stddef.h>
#include <stdint.h>

#include <anoptic_atomic.h>
#include <anoptic_memory.h>
#include <anoptic_meta.h>

namespace ano {

template<class T>
consteval bool reflect_transport_data()
{
    using Value = std::remove_cv_t<std::remove_all_extents_t<T>>;
    static_assert(Data<Value>,
                  "transport values must be standard-layout, trivially copyable data");
    if constexpr (std::is_class_v<Value> || std::is_union_v<Value>) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^Value, std::meta::access_context::unchecked()));
        template for (constexpr auto member : members) {
            using Member = [:std::meta::type_of(member):];
            constexpr auto elementType = std::meta::remove_all_extents(^^Member);
            using Element = [:elementType:];
            static_assert(!std::is_volatile_v<Element>,
                          "transport fields cannot be volatile");
            static_assert(!std::is_member_pointer_v<std::remove_cv_t<Element>>,
                          "transport fields cannot be member pointers");
            static_assert(reflect_transport_data<Element>(),
                          "transport field violates the recursive data contract");
        }
    }
    return true;
}

template<class T>
concept TransportData = Data<T> && reflect_transport_data<T>();

constexpr uint32_t spsc_capacity(uint32_t requested) noexcept
{
    if (requested < 2u) return 2u;
    --requested;
    requested |= requested >> 1;
    requested |= requested >> 2;
    requested |= requested >> 4;
    requested |= requested >> 8;
    requested |= requested >> 16;
    return requested + 1u;
}

template<TransportData T>
struct SpscRing final {
    alignas(ANO_THREAD_LINE) Atomic<uint32_t> tail;
    alignas(ANO_THREAD_LINE) Atomic<uint32_t> head;
    alignas(ANO_THREAD_LINE) uint32_t mask = 0u;
    T* buffer = nullptr;

    template<class Allocate>
    [[nodiscard]] bool initialize(uint32_t requested, Allocate allocate) noexcept
    {
        if (buffer != nullptr) return false;
        const uint32_t capacity = spsc_capacity(requested);
        if (capacity == 0u || static_cast<size_t>(capacity) > SIZE_MAX / sizeof(T))
            return false;
        T* storage = static_cast<T*>(allocate(capacity, sizeof(T)));
        if (storage == nullptr) return false;
        atomic_init(&tail, 0u);
        atomic_init(&head, 0u);
        mask = capacity - 1u;
        buffer = storage;
        return true;
    }

    template<class Release>
    void destroy(Release release) noexcept
    {
        if (buffer != nullptr) {
            release(buffer);
            buffer = nullptr;
        }
        mask = 0u;
        atomic_store_explicit(&head, 0u, memory_order_relaxed);
        atomic_store_explicit(&tail, 0u, memory_order_relaxed);
    }

    [[nodiscard]] bool push(const T& value) noexcept
    {
        const uint32_t write = atomic_load_explicit(&tail, memory_order_relaxed);
        const uint32_t read = atomic_load_explicit(&head, memory_order_acquire);
        if ((write - read) > mask)
            return false;
        __builtin_memcpy(buffer + (write & mask), &value, sizeof(T));
        atomic_store_explicit(&tail, write + 1u, memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(T& value) noexcept
    {
        const uint32_t read = atomic_load_explicit(&head, memory_order_relaxed);
        const uint32_t write = atomic_load_explicit(&tail, memory_order_acquire);
        if (read == write)
            return false;
        __builtin_memcpy(&value, buffer + (read & mask), sizeof(T));
        atomic_store_explicit(&head, read + 1u, memory_order_release);
        return true;
    }

    [[nodiscard]] bool full() const noexcept
    {
        const uint32_t write = atomic_load_explicit(&tail, memory_order_relaxed);
        const uint32_t read = atomic_load_explicit(&head, memory_order_acquire);
        return (write - read) > mask;
    }
};

// Runtime-stride fallback for cooked audio blocks. Fixed command/event transport uses SpscRing<T>.
struct ByteSpscRing final {
    alignas(ANO_THREAD_LINE) Atomic<uint32_t> tail;
    alignas(ANO_THREAD_LINE) Atomic<uint32_t> head;
    alignas(ANO_THREAD_LINE) uint32_t mask = 0u;
    uint32_t stride = 0u;
    uint8_t* buffer = nullptr;

    template<class Allocate>
    [[nodiscard]] bool initialize(uint32_t requested, uint32_t elementStride,
                                  Allocate allocate) noexcept
    {
        if (buffer != nullptr || elementStride == 0u) return false;
        const uint32_t capacity = spsc_capacity(requested);
        if (capacity == 0u || static_cast<size_t>(capacity) > SIZE_MAX / elementStride)
            return false;
        uint8_t* storage = static_cast<uint8_t*>(allocate(capacity, elementStride));
        if (storage == nullptr) return false;
        atomic_init(&tail, 0u);
        atomic_init(&head, 0u);
        mask = capacity - 1u;
        stride = elementStride;
        buffer = storage;
        return true;
    }

    template<class Release>
    void destroy(Release release) noexcept
    {
        if (buffer != nullptr) {
            release(buffer);
            buffer = nullptr;
        }
        mask = 0u;
        stride = 0u;
        atomic_store_explicit(&head, 0u, memory_order_relaxed);
        atomic_store_explicit(&tail, 0u, memory_order_relaxed);
    }

    [[nodiscard]] bool push(const void* value) noexcept
    {
        const uint32_t write = atomic_load_explicit(&tail, memory_order_relaxed);
        const uint32_t read = atomic_load_explicit(&head, memory_order_acquire);
        if ((write - read) > mask)
            return false;
        __builtin_memcpy(buffer + static_cast<size_t>(write & mask) * stride,
                         value, stride);
        atomic_store_explicit(&tail, write + 1u, memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(void* value) noexcept
    {
        const uint32_t read = atomic_load_explicit(&head, memory_order_relaxed);
        const uint32_t write = atomic_load_explicit(&tail, memory_order_acquire);
        if (read == write)
            return false;
        __builtin_memcpy(value,
                         buffer + static_cast<size_t>(read & mask) * stride,
                         stride);
        atomic_store_explicit(&head, read + 1u, memory_order_release);
        return true;
    }

    [[nodiscard]] bool full() const noexcept
    {
        const uint32_t write = atomic_load_explicit(&tail, memory_order_relaxed);
        const uint32_t read = atomic_load_explicit(&head, memory_order_acquire);
        return (write - read) > mask;
    }
};

static_assert(sizeof(Atomic<uint64_t>) == sizeof(uint64_t),
              "seqlock lanes require plain-width atomics");

template<TransportData T>
    requires (sizeof(T) % sizeof(uint64_t) == 0u)
struct SeqPub final {
    static constexpr size_t laneCount = sizeof(T) / sizeof(uint64_t);

    alignas(ANO_THREAD_LINE) Atomic<uint64_t> version;
    Atomic<uint64_t> lanes[laneCount];

    void initialize() noexcept
    {
        atomic_init(&version, 0u);
        for (size_t i = 0; i < laneCount; ++i)
            atomic_init(&lanes[i], 0u);
    }

    void publish(const T& value) noexcept
    {
        const uint64_t sequence =
            atomic_load_explicit(&version, memory_order_relaxed);
        atomic_store_explicit(&version, sequence + 1u, memory_order_relaxed);
        atomic_thread_fence(memory_order_release);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < laneCount; ++i) {
            uint64_t word;
            __builtin_memcpy(&word, bytes + sizeof(uint64_t) * i,
                             sizeof(uint64_t));
            atomic_store_explicit(&lanes[i], word, memory_order_relaxed);
        }
        atomic_store_explicit(&version, sequence + 2u, memory_order_release);
    }

    [[nodiscard]] bool acquire(T& value) const noexcept
    {
        uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);
        for (;;) {
            const uint64_t before =
                atomic_load_explicit(&version, memory_order_acquire);
            if (before == 0u) return false;
            if (before & 1u) continue;
            for (size_t i = 0; i < laneCount; ++i) {
                const uint64_t word =
                    atomic_load_explicit(&lanes[i], memory_order_relaxed);
                __builtin_memcpy(bytes + sizeof(uint64_t) * i, &word,
                                 sizeof(uint64_t));
            }
            atomic_thread_fence(memory_order_acquire);
            const uint64_t after =
                atomic_load_explicit(&version, memory_order_relaxed);
            if (before == after) return true;
        }
    }
};

static_assert(alignof(SpscRing<uint32_t>) == ANO_THREAD_LINE);
static_assert(alignof(ByteSpscRing) == ANO_THREAD_LINE);
static_assert(alignof(SeqPub<uint64_t>) == ANO_THREAD_LINE);

} // namespace ano

#endif // ANOPTIC_THREADS_TYPED_H
