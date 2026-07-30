/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// C atomic surface over compiler builtins in C++26. No standard-library runtime.

#ifndef ANOPTIC_ATOMIC_H
#define ANOPTIC_ATOMIC_H

#ifdef __cplusplus

namespace ano {

enum memory_order : int
{
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST,
};

template<class T>
struct Atomic final
{
    using value_type = T;

    alignas(T) T value{};

    constexpr Atomic() noexcept = default;
    constexpr Atomic(T desired) noexcept : value(desired) {}
    Atomic(const Atomic &) = delete;
    Atomic &operator=(const Atomic &) = delete;
};

using atomic_bool = Atomic<bool>;
using atomic_int = Atomic<int>;
using atomic_uint = Atomic<unsigned>;

template<class T>
inline void atomic_init(Atomic<T> *object, typename Atomic<T>::value_type desired) noexcept
{
    __atomic_store_n(&object->value, desired, __ATOMIC_RELAXED);
}

template<class T>
[[nodiscard]] inline T atomic_load_explicit(const volatile Atomic<T> *object,
                                            memory_order order) noexcept
{
    return __atomic_load_n(&object->value, static_cast<int>(order));
}

template<class T>
[[nodiscard]] inline T atomic_load(const volatile Atomic<T> *object) noexcept
{
    return atomic_load_explicit(object, memory_order_seq_cst);
}

template<class T>
inline void atomic_store_explicit(volatile Atomic<T> *object,
                                  typename Atomic<T>::value_type desired,
                                  memory_order order) noexcept
{
    __atomic_store_n(&object->value, desired, static_cast<int>(order));
}

template<class T>
inline void atomic_store(volatile Atomic<T> *object,
                         typename Atomic<T>::value_type desired) noexcept
{
    atomic_store_explicit(object, desired, memory_order_seq_cst);
}

template<class T>
inline T atomic_exchange(volatile Atomic<T> *object,
                         typename Atomic<T>::value_type desired) noexcept
{
    return __atomic_exchange_n(&object->value, desired, __ATOMIC_SEQ_CST);
}

template<class T>
inline T atomic_fetch_add_explicit(volatile Atomic<T> *object,
                                   typename Atomic<T>::value_type operand,
                                   memory_order order) noexcept
{
    return __atomic_fetch_add(&object->value, operand, static_cast<int>(order));
}

template<class T>
inline T atomic_fetch_add(volatile Atomic<T> *object,
                          typename Atomic<T>::value_type operand) noexcept
{
    return atomic_fetch_add_explicit(object, operand, memory_order_seq_cst);
}

template<class T>
inline T atomic_fetch_sub_explicit(volatile Atomic<T> *object,
                                   typename Atomic<T>::value_type operand,
                                   memory_order order) noexcept
{
    return __atomic_fetch_sub(&object->value, operand, static_cast<int>(order));
}

template<class T>
[[nodiscard]] inline bool atomic_compare_exchange_strong_explicit(
    volatile Atomic<T> *object, T *expected, typename Atomic<T>::value_type desired,
    memory_order success, memory_order failure) noexcept
{
    return __atomic_compare_exchange_n(&object->value, expected, desired, false,
                                       static_cast<int>(success), static_cast<int>(failure));
}

template<class T>
[[nodiscard]] inline bool atomic_compare_exchange_weak_explicit(
    volatile Atomic<T> *object, T *expected, typename Atomic<T>::value_type desired,
    memory_order success, memory_order failure) noexcept
{
    return __atomic_compare_exchange_n(&object->value, expected, desired, true,
                                       static_cast<int>(success), static_cast<int>(failure));
}

template<class T>
[[nodiscard]] inline bool atomic_compare_exchange_strong(
    volatile Atomic<T> *object, T *expected, typename Atomic<T>::value_type desired) noexcept
{
    return atomic_compare_exchange_strong_explicit(
        object, expected, desired, memory_order_seq_cst, memory_order_seq_cst);
}

inline void atomic_thread_fence(memory_order order) noexcept
{
    __atomic_thread_fence(static_cast<int>(order));
}

static_assert(__is_standard_layout(Atomic<unsigned>));
static_assert(__is_trivially_copyable(Atomic<unsigned>));
static_assert(sizeof(Atomic<unsigned>) == sizeof(unsigned));
static_assert(alignof(Atomic<unsigned>) == alignof(unsigned));

} // namespace ano

#define ANO_ATOMIC(T) ::ano::Atomic<T>

using ano::atomic_bool;
using ano::atomic_compare_exchange_strong;
using ano::atomic_compare_exchange_strong_explicit;
using ano::atomic_compare_exchange_weak_explicit;
using ano::atomic_exchange;
using ano::atomic_fetch_add;
using ano::atomic_fetch_add_explicit;
using ano::atomic_fetch_sub_explicit;
using ano::atomic_init;
using ano::atomic_int;
using ano::atomic_load;
using ano::atomic_load_explicit;
using ano::atomic_store;
using ano::atomic_store_explicit;
using ano::atomic_thread_fence;
using ano::atomic_uint;
using ano::memory_order;
using ano::memory_order_acq_rel;
using ano::memory_order_acquire;
using ano::memory_order_consume;
using ano::memory_order_relaxed;
using ano::memory_order_release;
using ano::memory_order_seq_cst;

#else

#include <stdatomic.h>

#define ANO_ATOMIC(T) _Atomic(T)

#endif

#endif // ANOPTIC_ATOMIC_H
