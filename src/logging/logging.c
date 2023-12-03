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
_Alignas(64) static char w_buffer[LOG_BUFFER_SIZE];
_Alignas(64) static char r_buffer[LOG_BUFFER_SIZE];

static char* write_buffer = w_buffer;
static char* read_buffer = r_buffer;

// Make sure this always matches up to anoptic_logging.h:log_types_t
static const char* log_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static atomic_bool write_flag;
static atomic_bool swap_flag;
static _Atomic int tail_index;

static _Atomic int log_write_interval;

/* Module Internal */
void swap_buffers() {

    swap_flag = 1;
    while(write_flag) {
        // wait
    }
    char* temp = write_buffer;
    write_buffer = read_buffer;
    read_buffer = temp;

    swap_flag = 0;
}

int flush_log_queue() {

    if (tail_index >= LOG_BUFFER_SIZE) {
        // TODO: Do something
    }

    swap_buffers();

    return 0;
}

// Version of log_immediate that takes a va_list instead of variadic function signature.
void ano_log_vimmediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, va_list args) {
    // Immediate mode implementation
}

/* Public */
int ano_log_enqueue(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {

    va_list args;

    int maxLen = LOG_BUFFER_SIZE - tail_index;
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
    ano_aligned_free(msgPrefix);
    ano_aligned_free(msgBody);

    // Securing a thread-private sector of the buffer to write within. (Full cache lines)
    int localIndex;
    int padding;
    do {
        padding = 64 - (msgLen % 64); // end padding after the message body
        localIndex = tail_index;
    } while (!atomic_compare_exchange_strong(&tail_index, &localIndex, tail_index + msgLen + padding));

    // If the buffer got full, we'll write with log_immediate instead and issue an error.
    if (tail_index >= LOG_BUFFER_SIZE || localIndex >= LOG_BUFFER_SIZE) {
        ano_log_immediate(LOG_ERROR, __FILE__, __LINE__,
                          "Log Buffer filled, Writing in immediate mode instead. [Performance Degradation]");

        va_start(args);
        ano_log_vimmediate(log_type, fileName, lineNumber, printFormat, args);
        va_end(args);

        return 1;   // Failed to enqueue deferred message, switched to immediate mode instead.
    }

    // Inserting *msgFinal into the secured sector of the write_buffer
    while(swap_flag) {
        localIndex = 0;
    }
    write_flag = 1;
    int i = 0;
    while(msgFinal[i] != '\0' && localIndex < tail_index) {
        write_flag = 1;
        write_buffer[localIndex++] = msgFinal[i++];
    }
    write_flag = 0;

    return 0; // Message enqueued successfully.
}

void ano_log_immediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...) {
    va_list args;
    va_start(args);
    ano_log_vimmediate(log_type, fileName, lineNumber, printFormat, args);
    va_end(args);
}

int ano_log_init() {



    return 0;
}

int ano_log_cleanup() {

    return 0;
}

void ano_log_interval(uint32_t ms)  {

}