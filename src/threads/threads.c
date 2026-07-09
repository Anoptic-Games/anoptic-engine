/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_threads.h>
#include <anoptic_blackbox.h>
#include <anoptic_memory.h>
#include <pthread.h>


/* Thread Management */

// Spawn shim: every engine thread gets its crash stack armed (per-thread alternate signal stack on
// POSIX, stack-overflow guarantee on win64) before user code runs, and released however the thread
// exits -- the cleanup handler covers both a plain return and pthread_exit/ano_thread_exit.
typedef struct {
    void *(*func)(void *);
    void   *arg;
} thread_tramp_t;

static void tramp_disarm(void *unused)
{
    (void)unused;
    ano_blackbox_thread_disarm();
}

static void *thread_trampoline(void *p)
{
    thread_tramp_t t = *(thread_tramp_t *)p;
    mi_free(p);
    (void)ano_blackbox_thread_arm();    // best effort: an unarmed thread still runs, reports crash-naked
    void *ret;
    pthread_cleanup_push(tramp_disarm, NULL);
    ret = t.func(t.arg);
    pthread_cleanup_pop(1);
    return ret;
}

int ano_thread_create(anothread_t *thread, const anothread_attr_t *attr, void *(* func)(void *), void *arg) {

    thread_tramp_t *t = mi_malloc(sizeof *t);
    if (t == NULL)
        return pthread_create(thread, attr, func, arg);    // no shim beats no thread
    *t = (thread_tramp_t){ .func = func, .arg = arg };
    int rc = pthread_create(thread, attr, thread_trampoline, t);
    if (rc != 0)
        mi_free(t);
    return rc;
}

int ano_thread_join(anothread_t thread, void **res) {

    return pthread_join(thread, res);
}

void ano_thread_exit(void *res) {

    pthread_exit(res);
}

int ano_thread_detach(anothread_t thread) {

    return pthread_detach(thread);
}

anothread_t ano_thread_self(void) {

    return pthread_self();
}


/* Mutexes */

int ano_mutex_init(anothread_mutex_t *mutex, const anothread_mutexattr_t *attr) {

    return pthread_mutex_init(mutex, attr);
}

int ano_mutex_lock(anothread_mutex_t *mutex) {

    return pthread_mutex_lock(mutex);
}

int ano_mutex_unlock(anothread_mutex_t *mutex) {

    return pthread_mutex_unlock(mutex);
}

int ano_mutex_destroy(anothread_mutex_t *mutex) {

    return pthread_mutex_destroy(mutex);
}


/* Condition Variables */
int ano_thread_cond_init(anothread_cond_t *conditionVariable, const anothread_condattr_t *attr) {

    return pthread_cond_init(conditionVariable, attr);
}

int ano_thread_cond_wait(anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex) {

    return pthread_cond_wait(conditionVariable, external_mutex);
}

int ano_thread_cond_timedwait(anothread_cond_t *conditionVariable, anothread_mutex_t *external_mutex,
                              const struct timespec *abstime) {

    return pthread_cond_timedwait(conditionVariable, external_mutex, abstime);
}

int  ano_thread_cond_signal(anothread_cond_t *conditionVariable) {

    return pthread_cond_signal(conditionVariable);
}

int ano_thread_cond_broadcast(anothread_cond_t *conditionVariable) {

    return pthread_cond_broadcast(conditionVariable);
}

int ano_thread_cond_destroy(anothread_cond_t *conditionVariable) {

    return pthread_cond_destroy(conditionVariable);
}


/* Spinlocks */
#if !defined(__APPLE__)   // macOS: provided by threads_macos.c
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
#endif


/* Read-Write Locks */

int ano_thread_rwlock_init(anothread_rwlock_t *rwlock, const anothread_rwlockattr_t *attr) {

    return pthread_rwlock_init(rwlock, attr);
}

int ano_thread_rwlock_rdlock(anothread_rwlock_t *rwlock) {

    return pthread_rwlock_rdlock(rwlock);
}

int ano_thread_rwlock_wrlock(anothread_rwlock_t *rwlock) {

    return pthread_rwlock_wrlock(rwlock);
}

int ano_thread_rwlock_unlock(anothread_rwlock_t *rwlock) {

    return pthread_rwlock_unlock(rwlock);
}

int ano_thread_rwlock_destroy(anothread_rwlock_t *rwlock) {

    return pthread_rwlock_destroy(rwlock);
}


/* Thread Attributes */

int ano_thread_attr_init(anothread_attr_t *attr) {

    return pthread_attr_init(attr);
}

int ano_thread_attr_setdetachstate(anothread_attr_t *attr, int flag) {

    return pthread_attr_setdetachstate(attr, flag);
}

int  ano_thread_attr_getstacksize(const anothread_attr_t *attr, size_t *size) {

    return pthread_attr_getstacksize(attr, size);
}

int ano_thread_attr_setstacksize(anothread_attr_t *attr, size_t size) {

    return pthread_attr_setstacksize(attr, size);
}

int ano_thread_attr_destroy(anothread_attr_t *attr) {

    return pthread_attr_destroy(attr);
}


/* Thread-Data */

int ano_thread_key_create(anothread_key_t *key, void (*dest)(void *)) {

    return pthread_key_create(key, dest);
}

int ano_thread_key_delete(anothread_key_t key) {

    return pthread_key_delete(key);
}

int ano_thread_setspecific(anothread_key_t key, const void *value) {

    return pthread_setspecific(key, value);
}

void* ano_thread_getspecific(anothread_key_t key) {

    return pthread_getspecific(key);
}


/* Synchronization Barriers */
#if !defined(__APPLE__)   // macOS: provided by threads_macos.c

int ano_thread_barrier_init(anothread_barrier_t *barrier, const anothread_barrierattr_t *attr, unsigned int count) {

    return pthread_barrier_init(barrier, attr, count);
}

int ano_thread_barrier_wait(anothread_barrier_t *barrier) {

    return pthread_barrier_wait(barrier);
}

int ano_thread_barrier_destroy(anothread_barrier_t *barrier) {

    return pthread_barrier_destroy(barrier);
}
#endif