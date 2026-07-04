/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private tuning constants for the lock-free ring logger (logging_core.c).

#ifndef ANOPTICENGINE_LOGGING_CORE_H
#define ANOPTICENGINE_LOGGING_CORE_H

#include <anoptic_logging.h>
#include <anoptic_memory.h>   // ANO_CACHE_LINE (the cache-line / ring reservation grain)

// A stored line plus the wall-clock prefix total 4096 bytes. ANO_LOG_MSG_MAX is the stored cap.
// ANO_LOG_TIME_RESV is the prefix budget. A max-size entry spans ceil((16 + MSG_MAX) / ANO_CL) <= 64 lines.
#define ANO_LOG_TIME_RESV 16u                          // budget for the "HH:MM:SS " prefix
#define ANO_LOG_MSG_MAX   (4096u - ANO_LOG_TIME_RESV)  // stored line, stored + prefix = 4096

// Ring capacity in BYTES, a power of two, so the byte size matches on every platform. The line size does
// not (64 on x86-64, 128 on Apple Silicon), so a fixed line count would drift. Aligned to a power of two
// so the ring sits in one self-sized region: page-allocator, Windows cache-view, Linux large-folio,
// hugepage at 2 MiB. Override -DANO_LOG_RING_BYTES to experiment from 64 KiB to 2 MiB. Line count derives.
// 2 MiB gained ~15% on benchmarks only, not worth 4x the footprint.
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

#define ANO_LOG_FILENAME "anoptic.log"

#endif //ANOPTICENGINE_LOGGING_CORE_H
