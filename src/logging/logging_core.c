/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "logging/logging_core.h"

#include <anoptic_threads.h>
#include <anoptic_memalign.h>
#include <stdio.h>

/* Internal */

// Queue of log messages
typedef struct {
    char *data;
    _Atomic int tail_index;
} log_queue_t;

static log_queue_t log_buffer;
static anothread_mutex_t log_buffer_mtx;

// f1 + f2 implementation
int build_log_string(char* output, int maxLen, log_types_t log_type,
                      const char* fileName, int lineNumber, const char* format, va_list args) {

    char msgPrefix[LOG_PREFIX_MAX];
    snprintf(msgPrefix, LOG_PREFIX_MAX, "[%-6s] %s:%d:  ", log_strings[log_type], fileName, lineNumber);

    char msgBody[LOG_MESSAGE_MAX - LOG_PREFIX_MAX];
    vsnprintf(msgBody, LOG_MESSAGE_MAX - LOG_PREFIX_MAX, format, args);

    int msgLen = snprintf(output, maxLen, "\n%s %s\n", msgPrefix, msgBody);

    return msgLen;
}

// f3 implementation
int enqueue_log_string(int len, const char* string) {

    // TODO: Error Handling
    ano_mutex_lock(&log_buffer_mtx);
    int localIndex = log_buffer.tail_index;
    log_buffer.tail_index += len;
    int i = 0;
    while(string[i] != '\0' && localIndex < LOG_MESSAGE_MAX) {
        log_buffer.data[localIndex++] = string[i++];
    }
    ano_mutex_unlock(&log_buffer_mtx);

    return 0;
}

// f4 implementation
void enqueue_cleanup() {
    // Cleanup, if any is required.
}

// f5 implementation

// f6 implementation

// f7 implementation


/* Public */

/* Public */
int ano_log_enqueue(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {

    char logMessage[LOG_MESSAGE_MAX];

    // Building the message string.
    va_list args;
    va_start(args);
    int len = build_log_string(logMessage, LOG_MESSAGE_MAX,
                               log_type, fileName, lineNumber, printFormat, args);
    va_end(args);

    // Adding message string to shared buffer.
    enqueue_log_string(len, logMessage);

    // Cleanup (if any required)

    return 0;
}

void ano_log_immediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {
    char logMessage[LOG_MESSAGE_MAX];

    // Building the message string.
    va_list args;
    va_start(args);
    int len = build_log_string(logMessage, LOG_MESSAGE_MAX,
                               log_type, fileName, lineNumber, printFormat, args);
    va_end(args);

    // Print to Error streams immediately
    // ...
}

int ano_log_init() {

    if (ano_mutex_init(&log_buffer_mtx, NULL) != 0) {
        ano_log_fatal("ano_mutex_init -> Log Buffer mutex initialization failed!");
        return -1; // Mutex initialization failed.
    }

    log_buffer.tail_index = 0;
    log_buffer.data = ano_aligned_malloc(LOG_BUFFER_MAX, 64);
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