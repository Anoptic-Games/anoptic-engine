/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_LOGGING_H
#define ANOPTICENGINE_ANOPTIC_LOGGING_H

#include <stdint.h>

#define LOG_PREFIX_MAX 256
#define LOG_MESSAGE_MAX 4096
#define LOG_BUFFER_MAX 8192

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_types_t;
static const char *log_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

#ifdef DEBUG_BUILD
#define ano_log_debug(...)  ano_log_enqueue(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define ano_log_debugnow(...)  ano_log_immediate(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#else
#define ano_log_debug(...)  /* Debug logging disabled */
#define ano_log_debugnow(...)  /* Debug logging disabled */
#endif

#define ano_log_info(...)  ano_log_enqueue(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define ano_log_warn(...)   ano_log_enqueue(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define ano_log_error(...)  ano_log_enqueue(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define ano_log_fatal(...)  ano_log_immediate(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

int ano_log_enqueue(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...);

void ano_log_immediate(log_types_t log_type, const char* fileName, int lineNumber, const char* printFormat, ...);

int ano_log_init();

int ano_log_cleanup();

void ano_log_interval(uint32_t ms);

int ano_log_output_dir(const char* directoryPath);

#endif //ANOPTICENGINE_ANOPTIC_LOGGING_H