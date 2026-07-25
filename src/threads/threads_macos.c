/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Darwin spinlock/barrier gap-fill: C23 atomics. No OS locks.

#if defined(__APPLE__)

#include "threads_macos.h"
#include <errno.h>
#include <limits.h>


/* Spinlocks 〜 POSIX gap-fill */

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


/* Synchronization Barriers 〜 POSIX gap-fill */

// The whole round rides one word, `arrived`: [phase : 16][arrivals : 16]. One RMW claims the
// arrival AND the phase it belongs to, and the RMW that admits the last arrival opens the next
// phase - so reuse by more than `count` threads can neither erase an arrival nor hand a waiter
// a phase it never joined. A phase wrap (65536 rounds inside one waiter's spin) can only delay
// a release, never advance one. The phase rides the same word because a two-word round cannot be
// sampled atomically.
#define ANO_BAR_SHIFT 16u
#define ANO_BAR_MASK  ((1u << ANO_BAR_SHIFT) - 1u)   // one half of the state word

_Static_assert(sizeof(unsigned int) * CHAR_BIT >= 2u * ANO_BAR_SHIFT,
               "barrier state word must hold a phase half and an arrival half");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "the barrier spins on atomic_uint; a lock-backed atomic would deadlock it");

// in: barrier, attr (ignored), count (1..ANO_BAR_MASK). out: 0, or EINVAL outside that domain.
// Sole gate on the packing: past ANO_BAR_MASK an arrival tally would run into the phase half.
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned int count) {

    (void)attr;
    if (count == 0 || count > ANO_BAR_MASK)
        return EINVAL;
    barrier->count = count;
    atomic_store_explicit(&barrier->arrived, 0, memory_order_relaxed);
    return 0;
}

// in: barrier. out: PTHREAD_BARRIER_SERIAL_THREAD to exactly one thread per cohort, else 0.
// invariant: no thread returns before all `count` threads of ITS OWN phase have arrived, and
// every arrival is served by exactly one cohort - both hold when more than `count` threads
// share the barrier cohort by cohort.
int pthread_barrier_wait(pthread_barrier_t *barrier) {

    const unsigned int count = barrier->count;
    unsigned int state = atomic_load_explicit(&barrier->arrived, memory_order_relaxed);
    unsigned int phase, n;

    for (;;) {
        phase = state >> ANO_BAR_SHIFT;
        n     = (state & ANO_BAR_MASK) + 1u;
        // the cohort's last arrival opens the next phase in the same RMW that admits it
        unsigned int next = n == count ? ((phase + 1u) & ANO_BAR_MASK) << ANO_BAR_SHIFT
                                       : (phase << ANO_BAR_SHIFT) | n;
        if (atomic_compare_exchange_weak_explicit(&barrier->arrived, &state, next,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire))
            break;
    }

    if (n == count)
        return PTHREAD_BARRIER_SERIAL_THREAD;

    while ((atomic_load_explicit(&barrier->arrived, memory_order_acquire)
            >> ANO_BAR_SHIFT) == phase) {
        // spin until this phase's last arrival closes it
    }
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {

    (void)barrier;
    return 0;
}


/* Spinlocks 〜 ano_ wrappers (Darwin) */

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


/* Synchronization Barriers 〜 ano_ wrappers (Darwin) */

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
