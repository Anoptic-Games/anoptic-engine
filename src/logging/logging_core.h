/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_LOGGING_CORE_H
#define ANOPTICENGINE_LOGGING_CORE_H

#include <anoptic_logging.h>
#include <stdarg.h>


//f1 + f2
// Builds a log string from all component parts.
// Returns: length of resulting string
int build_log_string(char* output, int maxLen, log_types_t log_type,
                      const char* fileName, int lineNumber,const char* format, va_list args);

//f3
// Adds a log string to the shared buffer.
// Returns: status code (0 = success, anything else = failure)
int enqueue_log_string(int len, const char* string);

//f4
void enqueue_cleanup();

//f5
int check_log_data();

//f6
void aggregate_log_strings();

//f7
int write_to_log_file();

#endif //ANOPTICENGINE_LOGGING_CORE_H
