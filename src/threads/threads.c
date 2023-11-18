/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_threads.h>
#include <pthread.h>


/* Thread Management */

int ano_thread_create(anothread_t *thread, const anothread_attr_t *attr, void *(* func)(void *), void *arg) {

    return 0;
}

int ano_thread_join(anothread_t thread, void **res) {

    return 0;
}

int ano_thread_exit(void *res) {

    return 0;
}

int ano_thread_detach(anothread_t thread) {

    return 0;
}

anothread_t ano_thread_self(void) {

    return 0;
}


/* Mutexes */

int ano_mutex_init(anothread_mutex_t **mutex) {

    return 0;
}

int ano_mutex_lock(anothread_mutex_t *mutex) {

    return 0;
}

int ano_mutex_unlock(anothread_mutex_t *mutex) {

    return 0;
}

int ano_mutex_destroy(anothread_mutex_t *mutex) {

    return 0;
}


/* Condition Variables */
int ano_thread_cond_init(anothread_cond_t *conditionVariable, const anothread_condattr_t *attribute) {

    return 0;
}

int ano_thread_cond_wait(anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex) {

    return 0;
}

int  ano_thread_cond_signal(anothread_cond_t *conditionVariable) {

    return 0;
}

int ano_thread_cond_broadcast(anothread_cond_t *conditionVariable) {

    return 0;
}

int ano_thread_cond_destroy(anothread_cond_t *conditionVariable) {

    return 0;
}


/* Spinlocks */
int ano_thread_spin_init(anothread_spinlock_t *lock, int pshared) {

    return 0;
}

int ano_thread_spin_destroy(anothread_spinlock_t *lock) {

    return 0;
}

int ano_thread_spin_lock(anothread_spinlock_t *lock) {

    return 0;
}

int ano_thread_spin_trylock(anothread_spinlock_t *lock) {

    return 0;
}

int ano_thread_spin_unlock(anothread_spinlock_t *lock) {

    return 0;
}


/* Read-Write Locks */

int ano_thread_rwlock_init(anothread_rwlock_t *rwlock, const anothread_rwlockattr_t *attr) {

    return 0;
}

int ano_thread_rwlock_rdlock(anothread_rwlock_t *rwlock) {

    return 0;
}

int ano_thread_rwlock_wrlock(anothread_rwlock_t *rwlock) {

    return 0;
}

int ano_thread_rwlock_unlock(anothread_rwlock_t *rwlock) {

    return 0;
}

int ano_thread_rwlock_destroy(anothread_rwlock_t *rwlock) {

    return 0;
}


/* Thread Attributes */

int ano_thread_attr_init(anothread_condattr_t *attribute) {

    return 0;
}

int ano_thread_attr_setdetachstate(anothread_attr_t *attribute, int flag) {

    return 0;
}

int ano_thread_attr_setstacksize(anothread_attr_t *attribute, size_t *size) {

    return 0;
}

int ano_thread_attr_destroy(anothread_attr_t *attr) {

    return 0;
}


/* Thread-Data */

int ano_thread_key_create(anothread_key_t *key, void (*destructor)(void *)) {

    return 0;
}

int ano_thread_key_delete(anothread_key_t key) {

    return 0;
}

int ano_thread_setspecific(anothread_key_t key, const void *value) {

    return 0;
}

int ano_thread_getspecific(anothread_key_t key) {

    return 0;
}


/* Synchronization Barriers */

int ano_thread_barrier_init(anothread_barrier_t *barrier, const anothread_barrierattr_t *attr, unsigned int count) {

    return 0;
}

int ano_thread_barrier_wait(anothread_barrier_t *barrier) {

    return 0;
}

int ano_thread_barrier_destroy(anothread_barrier_t *barrier) {

    return 0;
}


/* Thread Cancellation */

int ano_thread_cancel(anothread_t thread) {

    return 0;
}

int ano_thread_setcancelstate(int state, int *oldstate) {

    return 0;
}

int ano_thread_setcanceltype(int type, int *oldtype) {

    return 0;
}