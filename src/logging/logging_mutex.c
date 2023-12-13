/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "anoptic_logging.h"

#include <anoptic_threads.h>
#include <anoptic_time.h>
#include <anoptic_memalign.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdalign.h>
#include <stdio.h>

// Queue of log messages
typedef struct {
    char *data;
    _Atomic int tail_index;
} log_queue_t;

static log_queue_t log_buffer;
static anothread_mutex_t log_buffer_mtx;

// Make sure this always matches up to anoptic_logging.h:log_types_t
static const char *log_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static _Atomic int log_write_interval;

/* Module Internal (Private) */
int write_and_flush() {

    ano_mutex_lock(&log_buffer_mtx);

    ano_mutex_unlock(&log_buffer_mtx);
    return 0;
}

// Version of log_immediate that takes a va_list instead of variadic function signature.
void ano_log_vimmediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, va_list args) {
    // Immediate mode implementation // TODO: Finish this lol
    vprintf(printFormat, args);
}

/* Public */
int ano_log_enqueue(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {

    va_list args;

    // Local buffers
    // TODO: Investigate if it's better to malloc these to the heap.
    char msgPrefix[LOG_PREFIX_MAX];
    char msgBody[LOG_MESSAGE_MAX - LOG_PREFIX_MAX];
    char msgFinal[LOG_MESSAGE_MAX];

    // Building the message prefix
    // TODO: Check for truncation
    snprintf(msgPrefix, sizeof(msgPrefix), "[%-6s] %s:%d:  ", log_strings[log_type], fileName, lineNumber);

    // Building the message body
    va_start(args);
    // TODO: Check for truncation
    vsnprintf(msgBody, sizeof(msgBody), printFormat, args);
    va_end(args);

    // Putting the prefix and message body together
    // TODO: Check for truncation and check msgLen against actual buffer size
    int msgLen = snprintf(msgFinal, sizeof(msgFinal), "\n%s %s\n", msgPrefix, msgBody);

    // Locking the mutex to log_buffer so no other threads can affect it.
    ano_mutex_lock(&log_buffer_mtx);

    // If the buffer got full, we'll write with log_immediate instead and issue an error.
    if (log_buffer.tail_index + msgLen >= LOG_MESSAGE_MAX) {
        ano_mutex_unlock(&log_buffer_mtx);
        ano_log_immediate(LOG_ERROR, __FILE__, __LINE__,
                          "Deferred Buffer full -> Writing in immediate mode instead.");

        va_start(args);
        ano_log_vimmediate(log_type, fileName, lineNumber, printFormat, args);
        va_end(args);

        return 1;   // Failed to enqueue deferred message, switched to immediate mode instead.
    }

    // Inserting *msgFinal into the secured sector of the write_buffer
    int localIndex = log_buffer.tail_index;
    log_buffer.tail_index += msgLen;
    int i = 0;
    while(msgFinal[i] != '\0' && localIndex < LOG_MESSAGE_MAX) {
        log_buffer.data[localIndex++] = msgFinal[i++];
    }

    // Unlocking the mutex so other threads can affect log_buffer.
    ano_mutex_unlock(&log_buffer_mtx);

    return 0; // Message enqueued successfully.
}

void ano_log_immediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {
    va_list args;
    va_start(args);
    ano_log_vimmediate(log_type, fileName, lineNumber, printFormat, args);
    va_end(args);
}

int ano_log_init() {

    if (ano_mutex_init(&log_buffer_mtx, NULL) != 0) {
        ano_log_fatal("ano_mutex_init -> Log Buffer mutex initialization failed!");
        return -1; // Mutex initialization failed.
    }

    log_buffer.tail_index = 0;
    log_buffer.data = ano_aligned_malloc(LOG_MESSAGE_MAX, 64);
    if (log_buffer.data == NULL) {
        ano_log_fatal("ano_aligned_malloc -> Log Buffer data allocation failed!");
        return -2; // Buffer initialization failed.
    }

    return 0; // Initialization success.
}

int ano_log_cleanup() {

    // Destroy the mutex.
    ano_mutex_destroy(&log_buffer_mtx);

    // Clean up the log buffer.
    ano_aligned_free(log_buffer.data);

    return 0;
}

void ano_log_interval(uint32_t ms)  {

}