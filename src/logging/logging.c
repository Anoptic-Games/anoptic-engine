/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "anoptic_logging.h"
#include <anoptic_threads.h>
#include <anoptic_time.h>
#include <anoptic_memalign.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdalign.h>
#include <stdio.h>

// Queues of log messages
_Alignas(64) static char write_buffer[LOG_BUFFER_SIZE];
_Alignas(64) static char read_buffer[LOG_BUFFER_SIZE];

static _Atomic int swap_flag;
static _Atomic int tail_index;

static _Atomic int log_write_interval;

/* Module Internal */
int clear_log_queue() {

    return 0;
}

int print_log_message(const char* formattedString, const char* fileTarget) {

    return 0;
}

/* Public */
// TODO: Clean up this function cause it's a bit of a mess rn
int ano_log_enqueue(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {

    va_list args;
    int msgMaxLen = LOG_BUFFER_SIZE - tail_index;
    if (msgMaxLen <= 0) {
        ano_log_fatal("Not enough space to enqueue log message. Consider increasing LOG_BUFFER_SIZE");
        return -1; // Failed to enqueue message due to fatal error.
    }

    static const char* logTypes[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    if (log_type > (LOG_FATAL - LOG_DEBUG)) {
        ano_log_fatal("Deferred logger: invalid log_type");
        return -1;  // Failed to enqueue message due to fatal error.
    }

    // Building the message prefix
    int prefixLen;
    char *msgPrefix = ano_aligned_malloc(msgMaxLen, 64);
    if (msgPrefix == NULL) {
        ano_log_fatal("Deferred logger: malloc fail on msgPrefix");
        return -1;  // Failed to enqueue message due to fatal error.
    }
    snprintf(msgPrefix, msgMaxLen, "[%-6s] %s:%d:  ", logTypes[log_type], fileName, lineNumber);

    // Building the log message body
    int rqLen;
    char *msgBody = ano_aligned_malloc(msgMaxLen, 64);
    if (msgBody == NULL) {
        ano_log_fatal("Deferred logger: malloc fail on msgBody");
        ano_aligned_free(msgPrefix);
        return -1;  // Failed to enqueue message due to fatal error.
    }
    va_start(args, printFormat);
    vsnprintf(msgBody, msgMaxLen, printFormat, args);
    va_end(args);

    // Putting the prefix and message body together
    char *msgFinal = ano_aligned_malloc(msgMaxLen, 64);
    int msgLen = snprintf(msgFinal, msgMaxLen, "%s %s", msgPrefix, msgBody);
    ano_aligned_free(msgPrefix);
    ano_aligned_free(msgBody);

    // Securing a thread-private sector of the buffer to write within. (Full cache lines)
    int localIndex;
    int padding;
    do {
        padding = 64 - (msgLen % 64); // end padding after the message body
        localIndex = tail_index;
    } while (!atomic_compare_exchange_strong(&tail_index, &localIndex, tail_index + msgLen + padding));

    // ... Work of inserting *msgFinal into the secured sector of the writeBuffer

    // Cleanup
    ano_aligned_free(msgFinal);

    return 0;
}

/*
// do all the string formatting locally...
char *someResultingString; // log_type, filename, line number, formatted message




int padding;
// !!! CONCERN: what if the padding calculated here is no longer valid by the time the atomic operation occurs?

// Atomically fetch the current tail index and increment it by the message length + padding
int startPoint = atomic_fetch_add(&tail_index, messageLength + padding);

// Now you can safely write to the buffer starting at 'startPoint'

// Insert the message to this thread-reserved slice of the log queue?
// copy(someResultingString, logBuffer, startPoint)

    return 0;

*/

void ano_log_immediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {

}

int ano_log_init() {

    return 0;
}

int ano_log_cleanup() {

    return 0;
}

void ano_log_interval(uint32_t ms)  {

}