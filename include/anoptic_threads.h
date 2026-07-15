/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTIC_THREADS_H
#define ANOPTIC_THREADS_H

#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>     // struct timespec for ano_thread_cond_timedwait

typedef pthread_t anothread_t;

typedef pthread_mutex_t anothread_mutex_t;
typedef pthread_mutexattr_t anothread_mutexattr_t;

typedef pthread_attr_t anothread_attr_t;

typedef pthread_cond_t anothread_cond_t;
typedef pthread_condattr_t anothread_condattr_t;

#if defined(__APPLE__)
// Darwin: atomic_int spin (0 free / 1 held). pshared ignored.
typedef atomic_int pthread_spinlock_t;
#endif
typedef pthread_spinlock_t anothread_spinlock_t;

typedef pthread_rwlock_t anothread_rwlock_t;
typedef pthread_rwlockattr_t anothread_rwlockattr_t;

#if defined(__APPLE__)
// Darwin: POSIX barrier stand-in (C23 atomics). attr ignored.
typedef struct {
    unsigned int count;       // required arrivals, set at init
    atomic_uint  arrived;     // arrivals this round
    atomic_uint  generation;  // phase counter
} pthread_barrier_t;
typedef struct {
    int pshared;              // accepted, unused
} pthread_barrierattr_t;
#endif
typedef pthread_barrier_t anothread_barrier_t;
typedef pthread_barrierattr_t anothread_barrierattr_t;

typedef pthread_key_t anothread_key_t;


//typedef void *(*ano_thread_func)(void *arg);


/* Thread Management */

// Stack reserve for engine threads (NULL attr). Win64: PE --stack instead.
#define ANO_THREAD_STACK_SIZE ((size_t)8 << 20)

int ano_thread_create(anothread_t *thread, const anothread_attr_t *attr, void *(* func)(void *), void *arg);

// Initial-thread stack budget. POSIX soft RLIMIT_STACK (SIZE_MAX if unlimited), win64 PE reserve, else 0.
size_t ano_thread_main_stack(void);

int ano_thread_join(anothread_t thread, void **res);

void ano_thread_exit(void *res);

int ano_thread_detach(anothread_t thread);

anothread_t ano_thread_self(void);

int ano_thread_equal(anothread_t a, anothread_t b);


/* Mutexes */

int ano_mutex_init(anothread_mutex_t *mutex, const anothread_mutexattr_t *attr);

int ano_mutex_lock(anothread_mutex_t *mutex);

int ano_mutex_unlock(anothread_mutex_t *mutex);

int ano_mutex_destroy(anothread_mutex_t *mutex);


/* Condition Variables */

int ano_thread_cond_init(anothread_cond_t *conditionVariable, const anothread_condattr_t *attr);

int ano_thread_cond_wait(anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex);

// Absolute CLOCK_REALTIME deadline. 0 / ETIMEDOUT / errno. Build with timespec_get(TIME_UTC).
int ano_thread_cond_timedwait(anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex,
                              const struct timespec *abstime);

int  ano_thread_cond_signal(anothread_cond_t *conditionVariable);

int ano_thread_cond_broadcast(anothread_cond_t *conditionVariable);

int ano_thread_cond_destroy(anothread_cond_t *conditionVariable);


/* Spinlocks */

int ano_thread_spin_init(anothread_spinlock_t *lock, int pshared);

int ano_thread_spin_destroy(anothread_spinlock_t *lock);

int ano_thread_spin_lock(anothread_spinlock_t *lock);

int ano_thread_spin_trylock(anothread_spinlock_t *lock);

int ano_thread_spin_unlock(anothread_spinlock_t *lock);


/* Read-Write Locks */

int ano_thread_rwlock_init(anothread_rwlock_t *rwlock, const anothread_rwlockattr_t *attr);

int ano_thread_rwlock_rdlock(anothread_rwlock_t *rwlock);

int ano_thread_rwlock_wrlock(anothread_rwlock_t *rwlock);

int ano_thread_rwlock_unlock(anothread_rwlock_t *rwlock);

int ano_thread_rwlock_destroy(anothread_rwlock_t *rwlock);


/* Thread Attributes */

int ano_thread_attr_init(anothread_attr_t *attr);

int ano_thread_attr_setdetachstate(anothread_attr_t *attr, int flag);

int  ano_thread_attr_getstacksize(const anothread_attr_t *attr, size_t *size);

int ano_thread_attr_setstacksize(anothread_attr_t *attr, size_t size);

int ano_thread_attr_destroy(anothread_attr_t *attr);


/* Thread-Data */

int ano_thread_key_create(anothread_key_t *key, void (*dest)(void *));

int ano_thread_key_delete(anothread_key_t key);

int ano_thread_setspecific(anothread_key_t key, const void *value);

void* ano_thread_getspecific(anothread_key_t key);


/* Synchronization Barriers */

int ano_thread_barrier_init(anothread_barrier_t *barrier, const anothread_barrierattr_t *attr, unsigned int count);

int ano_thread_barrier_wait(anothread_barrier_t *barrier);

int ano_thread_barrier_destroy(anothread_barrier_t *barrier);


#endif // ANOPTIC_THREADS_H
