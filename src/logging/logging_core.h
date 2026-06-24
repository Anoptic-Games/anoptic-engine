/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private tuning constants for the mutex logger (logging_core.c).

#ifndef ANOPTICENGINE_LOGGING_CORE_H
#define ANOPTICENGINE_LOGGING_CORE_H

#include <anoptic_logger.h>

#define ANO_LOG_MSG_MAX             4096u        // max formatted line, NUL included
#define ANO_LOG_BUF_CAP             (1u << 16)   // shared buffer, 64 KiB (must exceed MSG_MAX)
#define ANO_LOG_DEFAULT_INTERVAL_US 100000u      // flusher cadence, 100 ms
#define ANO_LOG_FILENAME            "anoptic.log"

#endif //ANOPTICENGINE_LOGGING_CORE_H
