/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_logging.h>
#include <anoptic_threads.h>

#include <stdint.h>
#include <stdio.h>

/* Concurrent enqueue test: multiple threads hammer the log buffer.
 * Total volume stays under LOG_BUFFER_MAX so every enqueue must succeed. */

#define THREAD_COUNT 4
#define MSGS_PER_THREAD 20

static _Atomic int failures = 0;

static void *enqueue_worker(void *arg) {

    int id = (int)(intptr_t)arg;

    for (int i = 0; i < MSGS_PER_THREAD; i++) {
        if (ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__,
                            "thread %d message %d", id, i) != 0) {
            failures++;
        }
    }

    return NULL;
}

int main() {

    if (ano_log_init() != 0) {
        fprintf(stderr, "ano_log_init failed\n");
        return 1;
    }

    anothread_t workers[THREAD_COUNT];

    for (intptr_t i = 0; i < THREAD_COUNT; i++) {
        if (ano_thread_create(&workers[i], NULL, enqueue_worker, (void *)i) != 0) {
            fprintf(stderr, "ano_thread_create failed for worker %ld\n", (long)i);
            return 1;
        }
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        ano_thread_join(workers[i], NULL);
    }

    ano_log_cleanup();

    if (failures != 0) {
        fprintf(stderr, "%d of %d enqueues failed\n", failures,
                THREAD_COUNT * MSGS_PER_THREAD);
        return 1;
    }

    printf("anoptic_logging: %d concurrent enqueues from %d threads, all succeeded.\n",
           THREAD_COUNT * MSGS_PER_THREAD, THREAD_COUNT);
    return 0;
}
