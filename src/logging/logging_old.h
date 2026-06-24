/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The pre-ring mutex logger, preserved as the benchmark baseline for anotest_logbench. It is NOT part
// of anoptic_core and never enters the standard build -- it is compiled only into the optional
// benchmark, so the lock-free ring logger can be timed head-to-head against it. The API mirrors
// anoptic_logger.h but is namespaced mtxlog_* so both implementations link into one executable. The
// log_types_t / LOG_* severities are shared with the ring logger. Design history: docs/logger.md §14.

#ifndef ANOPTICENGINE_LOGGING_OLD_H
#define ANOPTICENGINE_LOGGING_OLD_H

#include <anoptic_logger.h>   // log_types_t (LOG_DEBUG..LOG_FATAL), shared with the ring logger
#include <stdint.h>

#define MTXLOG_MSG_MAX  4096u             // max formatted line, NUL included
#define MTXLOG_BUF_CAP  (1u << 16)        // shared buffer, must exceed MSG_MAX
#define MTXLOG_FILENAME "anoptic_mtx.log" // distinct from the ring logger's file, so both can run

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
