/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// include guard
#ifndef ANOPTIC_THREADS_H
#define ANOPTIC_THREADS_H

#include <stddef.h>

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

int ano_thread_detach(); // TODO: fill

int ano_thread_self(); //TODO: fill


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

int ano_thread_cond_init();

int ano_thread_cond_wait();

int  ano_thread_cond_signal();

int ano_thread_cond_broadcast();

int ano_thread_cond_destroy();


/* Read-Write Locks */

int ano_thread_rwlock_init();

int ano_thread_rwlock_rdlock();

int ano_thread_rwlock_wrlock();

int ano_thread_rwlock_unlock();

int ano_thread_rwlock_destroy();


/* Thread Attributes */

int ano_thread_attr_init();

int ano_thread_attr_setdetachstate();

int ano_thread_attr_setstacksize();

int ano_thread_attr_destroy();


/* Thread-Data */

int ano_thread_key_create();

int ano_thread_key_delete();

int ano_thread_setspecific();

int ano_thread_getspecific();


/* Synchronization Barriers */

int ano_thread_barrier_init();

int ano_thread_barrier_wait();

int ano_thread_barrier_destroy();


/* Thread Cancellation */

int ano_thread_cancel();

int ano_thread_setcancelstate();

int ano_thread_setcanceltype();


#endif // ANOPTIC_THREADS_H