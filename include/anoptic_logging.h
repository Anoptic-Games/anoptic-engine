/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The logger follows a singleton pattern (one per program).

#ifndef ANOPTICENGINE_ANOPTIC_LOGGING_H
#define ANOPTICENGINE_ANOPTIC_LOGGING_H

#include <stdint.h>

#define LOG_PREFIX_MAX 256
#define LOG_MESSAGE_MAX 4096
#define LOG_BUFFER_MAX 8192
// This number--------^ can probably be bigger if we move the ring to heap.

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_types_t;
static const char *log_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

// The usual method, enqueues a log item to the buffer.
int ano_log_enqueue(log_types_t log_type, const char* sourceFile, int lineNumber, const char* printFormat, ...);

// Should report the event immediately.
void ano_log_immediate(log_types_t log_type, const char* sourceFile, int lineNumber, const char* printFormat, ...);

int ano_log_init();     // Build up, singleton initialization.

int ano_log_cleanup();  // Teardown, most likely at program exit.

// Should probably use a filepath type from anoptic_logging.h
int ano_log_output_dir(const char* directoryPath);


/// TODO: Make sure that the __
#ifdef DEBUG_BUILD
#define ano_log_debug(...)      ano_log_enqueue(LOG_DEBUG, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_debug_now(...)  ano_log_immediate(LOG_DEBUG, __FILE_NAME__, __LINE__, __VA_ARGS__)
#else
#define ano_log_debug(...)      /* Debug logging disabled */
#define ano_log_debug_now(...)  /* Debug logging disabled */
#endif

#define ano_log_info(...)   ano_log_enqueue(LOG_INFO, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_warn(...)   ano_log_enqueue(LOG_WARN, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_error(...)  ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_fatal(...)  ano_log_immediate(LOG_FATAL, __FILE_NAME__, __LINE__, __VA_ARGS__)

#endif //ANOPTICENGINE_ANOPTIC_LOGGING_H