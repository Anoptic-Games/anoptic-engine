/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private tuning for the lock-free ring logger (log_core.c).

#ifndef ANOPTICENGINE_LOG_CORE_H
#define ANOPTICENGINE_LOG_CORE_H

#include <anoptic_log.h>
#include <anoptic_memory.h>   // ANO_CACHE_LINE

// Stored line + wall-clock prefix = 4096. MSG_MAX = stored cap, TIME_RESV = prefix budget.
// Max entry spans ceil((16 + MSG_MAX) / ANO_CL) <= 64 lines.
#define ANO_LOG_TIME_RESV 16u                          // "HH:MM:SS " prefix budget
#define ANO_LOG_MSG_MAX   (4096u - ANO_LOG_TIME_RESV)  // stored + prefix = 4096

// Ring capacity in BYTES, power of two. Override: -DANO_LOG_RING_BYTES (64 KiB to 2 MiB).
#ifndef ANO_LOG_RING_BYTES
// 2MB
//#define ANO_LOG_RING_BYTES (2u * 1024u * 1024u)
// 512 KB
#define ANO_LOG_RING_BYTES (512u * 1024u)
#endif
#define ANO_LOG_RING_LINES (ANO_LOG_RING_BYTES / ANO_CACHE_LINE)
#define ANO_LOG_RING_ALIGN (ANO_LOG_RING_BYTES < (2u << 20) ? ANO_LOG_RING_BYTES : (2u << 20))

_Static_assert((ANO_LOG_RING_BYTES & (ANO_LOG_RING_BYTES - 1)) == 0, "ring bytes must be a power of two");
_Static_assert(ANO_LOG_RING_LINES >= 64, "ring must hold at least one max-size entry (64 lines)");

// One log file per session: "<stamp>" ANO_LOG_FILESUFFIX via ano_fs_session_stamp().
#define ANO_LOG_FILESUFFIX "_ano.log"

#endif // ANOPTICENGINE_LOG_CORE_H

