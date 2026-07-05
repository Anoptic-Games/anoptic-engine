/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The pre-ring mutex logger, the benchmark baseline for anotest_logbench. NOT part of anoptic_core,
// never in the standard build, compiled only into the optional benchmark. The API mirrors
// anoptic_logging.h, namespaced mtxlog_*.

#ifndef ANOPTICENGINE_LOGGING_OLD_H
#define ANOPTICENGINE_LOGGING_OLD_H

#include <stdint.h>

// The retired 5-tier severities, kept with the baseline.
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_types_t;

#define MTXLOG_MSG_MAX  4096u             // max formatted line, NUL included
#define MTXLOG_BUF_CAP  (1u << 16)        // shared buffer, must exceed MSG_MAX
#define MTXLOG_FILENAME "anoptic_mtx.log" // distinct from the ring logger's file

// The mutex logger's own full-buffer policy enum (the ring logger dropped runtime policy entirely).
typedef enum {
    MTXLOG_FULL_IMMEDIATE = 0,
    MTXLOG_DROP_NEWEST    = 1,
    MTXLOG_BLOCK          = 2
} mtxlog_full_policy_t;

int  mtxlog_init(void);
int  mtxlog_cleanup(void);
int  mtxlog_enqueue(log_types_t level, const char *file, int line, const char *fmt, ...)
        __attribute__((format(printf, 4, 5)));
void mtxlog_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
        __attribute__((format(printf, 4, 5)));
int  mtxlog_output_dir(const char *directoryPath);
void mtxlog_set_level(log_types_t min);
void mtxlog_set_full_policy(mtxlog_full_policy_t policy);
void mtxlog_flush(void);
uint64_t mtxlog_dropped(void);

#endif // ANOPTICENGINE_LOGGING_OLD_H
