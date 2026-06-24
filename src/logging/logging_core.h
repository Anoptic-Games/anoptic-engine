/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private tuning constants for the lock-free ring logger (logging_core.c).

#ifndef ANOPTICENGINE_LOGGING_CORE_H
#define ANOPTICENGINE_LOGGING_CORE_H

#include <anoptic_logger.h>

// Cap on the stored line (severity, file:line, message). The flusher prepends the wall-clock prefix
// at emit. ANO_LOG_TIME_RESV reserves room for it so an emitted line never exceeds ANO_LOG_MSG_MAX.
// A max-length entry spans ceil((16 + 4096) / 64) = 65 cache lines.
#define ANO_LOG_MSG_MAX   4096u     // max stored line
#define ANO_LOG_TIME_RESV 16u       // room reserved for the "HH:MM:SS " prefix

// Ring capacity in cache lines, a power of two. 1024 lines = 64 KiB x86-64 / 128 KiB Apple Silicon.
// Far above the 65-line max entry, so any record fits an empty ring.
#define ANO_LOG_RING_LINES (1u << 10)

// @CLAUDE An unpublished or dead producer shouldn't be able to wedge anything. 
    // Bumping the tail is atomic. If it failed to produce useable log data in the meantime that's too bad!
    // We just overwrite it, it's a ring buffer.
    // Flush when full means flush when full. There's no conceiveable reason why (at least as I designed this in my drawings) this should occur.
// On a full ring the producer flushes to make room, then keeps buffering. If this many flushes in a
// row still free nothing, a gap (an unpublished or dead producer) is wedging the drain, so the line
// is written straight through instead of spinning. A flush normally frees the whole committed run.
#define ANO_LOG_FULL_FLUSH_TRIES 8

#define ANO_LOG_FILENAME "anoptic.log"

#endif //ANOPTICENGINE_LOGGING_CORE_H
