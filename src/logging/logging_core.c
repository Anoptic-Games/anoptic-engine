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
static char output_file_path[LOG_PREFIX_MAX];

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

    // Acquire exclusive lock over log_buffer.
    int mtxResult = ano_mutex_lock(&log_buffer_mtx);
    if (mtxResult != 0) {
        ano_mutex_unlock(&log_buffer_mtx);
        ano_log_fatal("ano_mutex_lock -> Failed to acquire mutex lock with return code: %d\n", mtxResult);
        return mtxResult;
    }

    // Bounds checking to ensure we're not writing past the tail.
    if ((log_buffer.tail_index + len) >= LOG_BUFFER_MAX) { // TODO: replace this with a memcpy
        ano_mutex_unlock(&log_buffer_mtx);
        fprintf(stderr, "enqueue_log_string -> Buffer Full, Writing Message in Immediate Mode!\n");
        // write_to_log_file(string, output_file_path);
        return -1;  // Failure to enqueue, wrote in immediate instead.
    }

    // Appending to Log Buffer
    int localIndex = log_buffer.tail_index;
    log_buffer.tail_index += len + 1;
    int i = 0;
    while(string[i] != '\0') {
        log_buffer.data[localIndex++] = string[i++];
    }
    log_buffer.data[localIndex++] = '\0';
    ano_mutex_unlock(&log_buffer_mtx);

    return 0;  // Enqueue Success
}

/*
// f4 implementation
void enqueue_cleanup() {
    // Cleanup, if any is required.
}

// f5 implementation
*/

// f6 implementation

// f7 implementation
int write_to_log_file(const char* logData, const char* fileName) {

    ano_mutex_lock(&log_file_mtx);
    FILE *pFile = fopen(fileName, "a");
    if (pFile == NULL) {
        ano_mutex_unlock(&log_file_mtx);
        fprintf(stderr,"write_to_log_file -> Couldn't open target file!\n");
        return -1; // Error
    }
    fprintf(pFile, "%s", logData);
    ano_mutex_unlock(&log_file_mtx);

    return 0; // Success
}

int write_all_buffered() {

    char fileMsg[LOG_BUFFER_MAX];

    // Claim mutex ownership over Log Buffer
    int mtxResult = ano_mutex_lock(&log_buffer_mtx);
    if (mtxResult != 0) {
        fprintf(stderr, "write_all_buffered -> Failed to acquire mutex lock with return: %d\n", mtxResult);
        return mtxResult;
    }

    // Copy Log Buffer data to a local fileMsg
    for(int i = 0; i < log_buffer.tail_index; i++) { // TODO: replace this with a clean memcpy
        char c = log_buffer.data[i];
        if (c == '\0' && i != (log_buffer.tail_index - 1))
            fileMsg[i] = '\n';
        else
            fileMsg[i] = c;
    }

    // Reset the Log Buffer to starting state
    log_buffer.tail_index = 0;
    ano_mutex_unlock(&log_buffer_mtx);

    // write_to_log_file(fileMsg, output_file_path);

    return 0;

}

/* Public */
int ano_log_enqueue(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {

    char logMessage[LOG_MESSAGE_MAX];

    // Building the message string.
    va_list args;
    va_start(args);
    int len = build_log_string(logMessage, LOG_MESSAGE_MAX,
                               log_type, fileName, lineNumber, printFormat, args);
    va_end(args);

    /*
    if(log_type > LOG_WARN) {
        fprintf(stderr,"%s", logMessage);
    } else {
        printf("%s", logMessage);
    }
    */

    // Adding message string to shared buffer.
    int status = enqueue_log_string(len, logMessage);

    return status;
}

void ano_log_immediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {
    char logMessage[LOG_MESSAGE_MAX];

    // Building the message string.
    va_list args;
    va_start(args);
    int len = build_log_string(logMessage, LOG_MESSAGE_MAX,
                               log_type, fileName, lineNumber, printFormat, args);
    va_end(args);

    if(log_type > LOG_WARN) {
        fprintf(stderr,"%s", logMessage);
    } else {
        printf("%s", logMessage);
    }

    // Print to relevant log streams immediately
    // write_to_log_file(logMessage, output_file_path);

    // TODO: Remove this
    write_all_buffered();
}

int ano_log_init() {

    int mtxResult = ano_mutex_init(&log_buffer_mtx, NULL);
    if (mtxResult != 0) {
        ano_log_fatal("ano_mutex_init -> Log Buffer mutex initialization failed with return code: %d\n", mtxResult);
        return mtxResult; // Mutex initialization failed.
    }

    mtxResult = ano_mutex_init(&log_file_mtx, NULL);
    if (mtxResult != 0) {
        ano_log_fatal("ano_mutex_init -> Log File mutex initialization failed with return code: %d\n", mtxResult);
        return mtxResult; // Mutex initialization failed.
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

    return 0; // Cleanup Success
}

void ano_log_interval(uint32_t ms)  {

}