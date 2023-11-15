/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// include guard
#ifndef ANOPTIC_THREADS_H
#define ANOPTIC_THREADS_H

#include <stddef.h>
#include <pthread.h>

/// \brief Represents a thread handle.
typedef struct ano_thread_t ano_thread_t;

/// \brief Represents a mutex handle.
typedef struct ano_mutex_t ano_mutex_t;

/// \brief Function signature for thread entry point.
typedef void *(*ano_thread_func)(void *arg);

/* Thread Management */

/// \brief Creates a new thread.
/// \param func The function to be executed by the thread.
/// \param arg The arguments to be passed to the thread function.
/// \param thread A pointer to the thread handle.
/// \return 0 on success, non-zero on failure.
int ano_thread_create(ano_thread_func func, void *arg, ano_thread_t **thread);


/// \brief Joins a thread, waiting for it to finish execution.
/// \param thread The thread handle.
/// \return 0 on success, non-zero on failure.
int ano_thread_join(ano_thread_t *thread);

/// \brief Terminates the calling thread.
/// \param thread The thread handle.
/// \return 0 on success, non-zero on failure.
int ano_thread_exit(ano_thread_t *thread);

int ano_thread_detach(pthread_t thread);

pthread_t ano_thread_self(void);


/* Mutexes */


/// \brief Initializes a mutex.
/// \param mutex A pointer to the mutex handle.
/// \return 0 on success, non-zero on failure.
int ano_mutex_init(ano_mutex_t **mutex);

/// \brief Locks a mutex.
/// \param mutex The mutex handle.
/// \return 0 on success, non-zero on failure.
int ano_mutex_lock(ano_mutex_t *mutex);

/// \brief Unlocks a mutex.
/// \param mutex The mutex handle.
/// \return 0 on success, non-zero on failure.
int ano_mutex_unlock(ano_mutex_t *mutex);

/// \brief Destroys a mutex.
/// \param mutex The mutex handle.
/// \return 0 on success, non-zero on failure.
int ano_mutex_destroy(ano_mutex_t *mutex);


/* Condition Variables */
int ano_thread_cond_init(pthread_cond_t *conditionVariable, const pthread_condattr_t *attribute);

int ano_thread_cond_wait(pthread_cond_t *conditionVariable, pthread_mutex_t *external_mutex);

int  ano_thread_cond_signal(pthread_cond_t *conditionVariable);

int ano_thread_cond_broadcast(pthread_cond_t *conditionVariable);

int ano_thread_cond_destroy(pthread_cond_t *conditionVariable);


/* Spinlocks */
int ano_thread_spin_init(pthread_spinlock_t *lock, int pshared);

int ano_thread_spin_destroy(pthread_spinlock_t *lock);

int ano_thread_spin_lock(pthread_spinlock_t *lock);

int ano_thread_spin_trylock(pthread_spinlock_t *lock);

int ano_thread_spin_unlock(pthread_spinlock_t *lock);


/* Read-Write Locks */

int ano_thread_rwlock_init(pthread_rwlock_t *rwlock);

int ano_thread_rwlock_rdlock(pthread_rwlock_t *rwlock);

int ano_thread_rwlock_wrlock(pthread_rwlock_t *rwlock);

int ano_thread_rwlock_unlock(pthread_rwlock_t *rwlock);

int ano_thread_rwlock_destroy(pthread_rwlock_t *rwlock);


/* Thread Attributes */

int ano_thread_attr_init(pthread_condattr_t *attribute);

int ano_thread_attr_setdetachstate(pthread_attr_t *attribute, int flag);

int ano_thread_attr_setstacksize(pthread_attr_t *attribute, size_t *size);

int ano_thread_attr_destroy(pthread_attr_t *attr);


/* Thread-Data */

int ano_thread_key_create(pthread_key_t *key, void (*destructor)(void *));

int ano_thread_key_delete(pthread_key_t key);

int ano_thread_setspecific(pthread_key_t key, const void *value);

int ano_thread_getspecific(pthread_key_t key);


/* Synchronization Barriers */

int ano_thread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count);

int ano_thread_barrier_wait(pthread_barrier_t *barrier);

int ano_thread_barrier_destroy(pthread_barrier_t *barrier);


/* Thread Cancellation */

int ano_thread_cancel(pthread_t thread);

int ano_thread_setcancelstate(int state, int *oldstate);

int ano_thread_setcanceltype(int type, int *oldtype);


#endif // ANOPTIC_THREADS_H