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

// Queues of log messages
typedef struct {
    char *log_buffer;
    _Atomic int tail_index;
} log_queue_t;
static log_queue_t write_buffer;
static anothread_mutex_t write_buffer_mtx;

// Make sure this always matches up to anoptic_logging.h:log_types_t
static const char *log_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static atomic_bool write_flag;
static atomic_bool swap_flag;

static _Atomic int log_write_interval;

/* Module Internal */
int flush_log_queue() {

    ano_mutex_lock(&write_buffer_mtx);
    if (write_buffer.tail_index >= LOG_BUFFER_SIZE) {
        // TODO: Do something
    }

    ano_mutex_unlock(&write_buffer_mtx);
    return 0;
}

// Version of log_immediate that takes a va_list instead of variadic function signature.
void ano_log_vimmediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, va_list args) {
    // Immediate mode implementation
}

/* Public */
int ano_log_enqueue(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {

    va_list args;

    int maxLen = LOG_BUFFER_SIZE - write_buffer.tail_index;
    if (maxLen <= 0) {
        // If buffer is already full, we'll write with log_immediate instead and issue an error.
        ano_log_immediate(LOG_ERROR, __FILE__, __LINE__,
                          "Log Buffer already full, Writing in immediate mode instead. [Performance Degradation]");

        va_start(args);
        ano_log_vimmediate(log_type, fileName, lineNumber, printFormat, args);
        va_end(args);

        return 1; // Failed to enqueue message, switched to immediate mode instead.
    }

    // Local buffers
    char msgPrefix[LOG_PREFIX_SIZE];
    char msgBody[LOG_BUFFER_SIZE - LOG_PREFIX_SIZE];
    char msgFinal[LOG_BUFFER_SIZE];

    // Building the message prefix
    snprintf(msgPrefix, maxLen, "[%-6s] %s:%d:  ", log_strings[log_type], fileName, lineNumber);

    // Building the message body
    va_start(args);
    vsnprintf(msgBody, maxLen, printFormat, args);
    va_end(args);

    // Putting the prefix and message body together
    int msgLen = snprintf(msgFinal, maxLen, "\n%s %s\n", msgPrefix, msgBody);

    // Securing a thread-private sector of the buffer to write within. (Full cache lines)
    ano_mutex_lock(&write_buffer_mtx);
    int localIndex = write_buffer.tail_index;
    write_buffer.tail_index += msgLen;

    // If the buffer got full, we'll write with log_immediate instead and issue an error.
    if (write_buffer.tail_index >= LOG_BUFFER_SIZE) {
        ano_mutex_unlock(&write_buffer_mtx);
        ano_log_immediate(LOG_ERROR, __FILE__, __LINE__,
                          "Log Buffer filled, Writing in immediate mode instead. [Performance Degradation]");

        va_start(args);
        ano_log_vimmediate(log_type, fileName, lineNumber, printFormat, args);
        va_end(args);

        return 1;   // Failed to enqueue deferred message, switched to immediate mode instead.
    }

    // Inserting *msgFinal into the secured sector of the write_buffer
    int i = 0;
    while(msgFinal[i] != '\0') {
        write_buffer.log_buffer[localIndex++] = msgFinal[i++];
    }

    ano_mutex_unlock(&write_buffer_mtx);

    return 0; // Message enqueued successfully.
}

void ano_log_immediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {
    va_list args;
    va_start(args);
    ano_log_vimmediate(log_type, fileName, lineNumber, printFormat, args);
    va_end(args);
}

int ano_log_init() {

    if (ano_mutex_init(&write_buffer_mtx, NULL) != 0) {
        return -1;
    }

    write_buffer.tail_index = 0;
    write_buffer.log_buffer = ano_aligned_malloc(LOG_BUFFER_SIZE, 64);


    return 0;
}

int ano_log_cleanup() {

    // Destroy the mutex.
    ano_mutex_destroy(&write_buffer_mtx);

    // Clean up the log buffer.
    ano_aligned_free(write_buffer.log_buffer);

    return 0;
}

void ano_log_interval(uint32_t ms)  {

}