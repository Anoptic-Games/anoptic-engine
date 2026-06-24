/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private to threads_macos.c. macOS libpthread declares no spinlock/barrier
// primitives; these are the POSIX functions we supply ourselves on Darwin.
// NOT a public interface — never include from outside src/threads.

#ifndef ANOPTIC_THREADS_MACOS_H
#define ANOPTIC_THREADS_MACOS_H

#if defined(__APPLE__)

#include <anoptic_threads.h>   // pthread_spinlock_t / pthread_barrier_t (Darwin types)

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_wait(pthread_barrier_t *barrier);
int pthread_barrier_destroy(pthread_barrier_t *barrier);

#endif // __APPLE__

#endif // ANOPTIC_THREADS_MACOS_H
