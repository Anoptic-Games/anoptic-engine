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
static anothread_mutex_t log_file_mtx;

// f1 + f2 implementation
int build_log_string(char* output, int maxLen, log_types_t log_type,
                      const char* fileName, int lineNumber, const char* format, va_list args) {

    char msgPrefix[LOG_PREFIX_MAX];
    snprintf(msgPrefix, LOG_PREFIX_MAX, "%-6s %s:%d:  ", log_strings[log_type], fileName, lineNumber);

    char msgBody[LOG_MESSAGE_MAX - LOG_PREFIX_MAX];
    vsnprintf(msgBody, LOG_MESSAGE_MAX - LOG_PREFIX_MAX, format, args);

    int msgLen = snprintf(output, maxLen, "%s %s", msgPrefix, msgBody);

    return msgLen;
}

// f3 implementation
int enqueue_log_string(int len, const char* string) {

    // TODO: Error Handling
    ano_mutex_lock(&log_buffer_mtx);
    int localIndex = log_buffer.tail_index;
    log_buffer.tail_index += len + 1;
    int i = 0;
    while(string[i] != '\0') {
        log_buffer.data[localIndex++] = string[i++];
    }
    log_buffer.data[localIndex++] = '\0';
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
int write_to_log_file(uint32_t len, const char* logData, const char* targetFile) {

    ano_mutex_lock(&log_file_mtx);
    FILE *pFile = fopen(targetFile, "a");
    if (pFile == NULL) {
        fprintf(stderr,"write_to_log_file -> Couldn't write to log file!");
    }
    ano_mutex_unlock(&log_file_mtx);

    return 0; // Success
}

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

    // Bounds checking to ensure we're not writing past the buffer.
    if ((log_buffer.tail_index + len) >= LOG_BUFFER_MAX) {
        // message that would trail past the edge of this cycle's buffer.
        printf("%s", logMessage);
        fprintf(stderr, "[Log Buffer Full] -> Message Written in Immediate Mode!\n");
    }

    // Adding message string to shared buffer.
    enqueue_log_string(len, logMessage);

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

    // Print to relevant log streams immediately
    // ...
}

int ano_log_init() {
`
    if (ano_mutex_init(&log_buffer_mtx, NULL) != 0) {
        ano_log_fatal("ano_mutex_init -> Log Buffer mutex initialization failed!\n");
        return -1; // Mutex initialization failed.
    }

    if (ano_mutex_init(&log_file_mtx, NULL) != 0) {
        ano_log_fatal("ano_mutex_init -> Log File mutex initialization failed!\n");
        return -2; // Mutex initialization failed.
    }

    log_buffer.data = ano_aligned_malloc(LOG_BUFFER_MAX, 64);
    log_buffer.tail_index = 0;
    if (log_buffer.data == NULL) {
        ano_log_fatal("ano_aligned_malloc -> Log Buffer data allocation failed!\n");
        return -3; // Buffer initialization failed.
    }

    return 0; // Initialization success.
}

int ano_log_cleanup() {

    // Destroy the mutexes.
    ano_mutex_destroy(&log_buffer_mtx);
    ano_mutex_destroy(&log_file_mtx);

    // Clean up the log buffer.
    ano_aligned_free(log_buffer.data);

    return 0;
}

void ano_log_interval(uint32_t ms)  {

}