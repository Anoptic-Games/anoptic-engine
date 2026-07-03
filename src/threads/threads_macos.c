/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// macOS libpthread has no spinlocks or barriers.
// Fill the gap with C23 atomics that stay lock-free, no pthread_mutex, per engine rules.
// Darwin counterpart to the !__APPLE__ sections of threads.c.

#if defined(__APPLE__)

#include "threads_macos.h"
#include <errno.h>


/* Spinlocks — POSIX gap-fill */

// in: lock, pshared (ignored, always process-private). out: 0. unlocked state.
int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {

    (void)pshared;
    atomic_store_explicit(lock, 0, memory_order_relaxed);
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock) {

    (void)lock;
    return 0;
}

// in: lock. out: 0. invariant: returns owning the lock (0->1, acquire).
int pthread_spin_lock(pthread_spinlock_t *lock) {

    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(lock, &expected, 1,
                                                  memory_order_acquire,
                                                  memory_order_relaxed)) {
        expected = 0;   // CAS wrote the seen value back on failure
    }
    return 0;
}

// in: lock. out: 0 if acquired, EBUSY if already held.
int pthread_spin_trylock(pthread_spinlock_t *lock) {

    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(lock, &expected, 1,
                                                memory_order_acquire,
                                                memory_order_relaxed))
        return 0;
    return EBUSY;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {

    atomic_store_explicit(lock, 0, memory_order_release);
    return 0;
}


/* Synchronization Barriers — POSIX gap-fill */

// in: barrier, attr (ignored), count (>0). out: 0, or EINVAL if count==0.
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned int count) {

    (void)attr;
    if (count == 0)
        return EINVAL;
    barrier->count = count;
    atomic_store_explicit(&barrier->arrived, 0, memory_order_relaxed);
    atomic_store_explicit(&barrier->generation, 0, memory_order_relaxed);
    return 0;
}

// in: barrier. out: PTHREAD_BARRIER_SERIAL_THREAD to exactly one thread, else 0.
// invariant: no thread returns until all `count` threads have arrived.
int pthread_barrier_wait(pthread_barrier_t *barrier) {

    unsigned int gen = atomic_load_explicit(&barrier->generation,
                                            memory_order_relaxed);
    unsigned int n = atomic_fetch_add_explicit(&barrier->arrived, 1,
                                               memory_order_acq_rel) + 1;
    if (n == barrier->count) {
        atomic_store_explicit(&barrier->arrived, 0, memory_order_relaxed);
        atomic_fetch_add_explicit(&barrier->generation, 1,
                                  memory_order_release);   // open the next round
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (atomic_load_explicit(&barrier->generation,
                               memory_order_acquire) == gen) {
        // spin until the last arrival bumps the generation
    }
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {

    (void)barrier;
    return 0;
}


/* Spinlocks — ano_ wrappers (Darwin) */

int ano_thread_spin_init(anothread_spinlock_t *lock, int pshared) {

    return pthread_spin_init(lock, pshared);
}

int ano_thread_spin_destroy(anothread_spinlock_t *lock) {

    return pthread_spin_destroy(lock);
}

int ano_thread_spin_lock(anothread_spinlock_t *lock) {

    return pthread_spin_lock(lock);
}

int ano_thread_spin_trylock(anothread_spinlock_t *lock) {

    return pthread_spin_trylock(lock);
}

int ano_thread_spin_unlock(anothread_spinlock_t *lock) {

    return pthread_spin_unlock(lock);
}


/* Synchronization Barriers — ano_ wrappers (Darwin) */

int ano_thread_barrier_init(anothread_barrier_t *barrier, const anothread_barrierattr_t *attr, unsigned int count) {

    return pthread_barrier_init(barrier, attr, count);
}

int ano_thread_barrier_wait(anothread_barrier_t *barrier) {

    return pthread_barrier_wait(barrier);
}

int ano_thread_barrier_destroy(anothread_barrier_t *barrier) {

    return pthread_barrier_destroy(barrier);
}

#endif // __APPLE__
