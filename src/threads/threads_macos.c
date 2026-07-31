/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Darwin spinlock/barrier gap-fill: C23 atomics. No OS locks.

#if defined(__APPLE__)

#include "threads_macos.h"
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <time.h>

// Spin-wait hint, not a scheduling call: x86 `pause` drops the memory-order-violation flush the
// owner eats on release, arm64 `yield` frees the pipeline slot. Every spin loop below wants it.
#if defined(__aarch64__) || defined(__arm64__)
#  define ANO_CPU_RELAX() __builtin_arm_yield()
#elif defined(__x86_64__) || defined(__i386__)
#  define ANO_CPU_RELAX() __builtin_ia32_pause()
#else
#  define ANO_CPU_RELAX() ((void)0)
#endif


/* Spinlocks: POSIX gap-fill */

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
        ANO_CPU_RELAX();
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


/* Synchronization Barriers: POSIX gap-fill */

// `arrived`: [phase:16][arrivals:16]. One RMW claims arrival+phase; last opens next phase.
#define ANO_BAR_SHIFT 16u
#define ANO_BAR_MASK  ((1u << ANO_BAR_SHIFT) - 1u)   // one half of the state word

// A waiter blocks for an unbounded time by definition, so a bare spin starves the very peers it
// waits on the moment runnable threads outnumber cores: the cohort then costs a whole scheduler
// quantum instead of a handful of loads. Relax, then hand the core over, then park.
#define ANO_BAR_SPINS   1024u
#define ANO_BAR_YIELDS  64u
#define ANO_BAR_PARK_NS 1000L

static_assert(sizeof(unsigned int) * CHAR_BIT >= 2u * ANO_BAR_SHIFT,
               "barrier state word must hold a phase half and an arrival half");
static_assert(__atomic_always_lock_free(sizeof(unsigned int), 0),
               "the barrier spins on atomic_uint; a lock-backed atomic would deadlock it");

// in: spin tally, bumped in place. out: void. relax -> sched_yield -> short park, by tally.
// Both calls are hints: sched_yield cannot fail here, and a park cut short by EINTR just re-tests.
static inline void ano_bar_backoff(unsigned *spins) {

    const unsigned i = (*spins)++;
    if (i < ANO_BAR_SPINS)
        ANO_CPU_RELAX();
    else if (i < ANO_BAR_SPINS + ANO_BAR_YIELDS)
        sched_yield();
    else {
        const struct timespec park = { .tv_sec = 0, .tv_nsec = ANO_BAR_PARK_NS };
        nanosleep(&park, NULL);
    }
}

// in: barrier, attr (ignored), count (1..ANO_BAR_MASK). out: 0, or EINVAL outside that domain.
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
// invariant: no return until all `count` of this phase arrive; each arrival in exactly one cohort.
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

    unsigned spins = 0;
    while ((atomic_load_explicit(&barrier->arrived, memory_order_acquire)
            >> ANO_BAR_SHIFT) == phase) {
        ano_bar_backoff(&spins);   // wait out this phase's last arrival
    }
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {

    (void)barrier;
    return 0;
}


/* Spinlocks: ano_ wrappers (Darwin) */

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


/* Synchronization Barriers: ano_ wrappers (Darwin) */

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
