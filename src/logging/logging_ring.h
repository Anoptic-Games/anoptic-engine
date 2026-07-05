/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private ring for the lock-free MPSC logger (logging_core.c). Variable-length, cache-line-granular
// (DPDK rte_ring family). One shared bounded ring carries finished text from many producers to one
// active consumer. The producer formats off-ring, reserves a run of cache lines by bumping `tail`,
// copies the line in, and publishes with one release store of `tag`. The consumer walks claim order,
// emits, and frees the range with one `head` store. A slot is live iff its tag is committed and carries
// the current lap (`cycle`), so reuse needs no zeroing. Only `tag` is synchronized. Timestamp and text
// ride its release/acquire as plain memory.
// Design: docs/logger.md. Migrates to anoptic_collections.h at the lock-free port.

#ifndef ANOPTICENGINE_LOGGING_RING_H
#define ANOPTICENGINE_LOGGING_RING_H

#include "logging/logging_core.h"   // ANO_LOG_MSG_MAX, ring sizing

#include <anoptic_memory.h>          // ANO_CACHE_LINE / ANO_THREAD_LINE
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

// The reservation grain is the true coherency line: 64 on x86-64, 128 on Apple Silicon. It sets
// packing density. The hot cursors pad to ANO_THREAD_LINE (128), the false-sharing isolation
// distance. Both from anoptic_memory.h.
#define ANO_CL ANO_CACHE_LINE
#define ANO_LOG_HDR  16     // head-line marker: one commit word plus one raw timestamp

enum {
    ANO_LOG_COMMITTED = 1 << 0, // set on publish, so a committed tag is nonzero even at len 0
    ANO_LOG_DEFERRED  = 1 << 1, // body is a deferred-format capture blob, not finished text (§9 proto)
    ANO_LOG_TOFILE    = 1 << 2, // sink: batched to the output file at drain
    ANO_LOG_TOCON     = 1 << 3, // sink: echoed to the terminal at drain
};

// An entry's head line begins with this 16-byte marker. The rest of the head line and every
// continuation line are finished text. Only `tag` is atomic, the single publish gate for the whole
// record. `timestamp` and the text are plain memory whose visibility rides tag's release.
typedef struct {
    _Atomic uint64_t tag;   // 0 = free, nonzero = committed. Published last (release), read first (acquire).
    uint64_t timestamp;     // raw ticks, display only, rendered at drain.
} log_marker_t;
_Static_assert(sizeof(log_marker_t) == ANO_LOG_HDR, "marker is 16 bytes");
_Static_assert(ANO_LOG_HDR <= ANO_CACHE_LINE, "marker fits in one cache line");

// The view punned over `tag`. Lives only on thread-local values copied in and out of `tag` with one
// atomic op. The shared word is always accessed atomically. Reading a union member you did not write
// is defined in C (C99 6.5.2.3, carried through C23).
typedef union {
    // Order is irrelevant to packing: 2+1+1+4 already tiles 8 bytes with zero padding (the
    // _Static_assert below proves it). It is chosen so `len` is the low 16 bits of `w`.
    struct {
        uint16_t len;       // stored text bytes, span = ceil((16 + len) / ANO_CL)
        uint8_t  level;     // ano_loglevel_t copy for the flusher's severity routing
        uint8_t  flags;     // ANO_LOG_COMMITTED + ANO_LOG_DEFERRED + sink bits
        uint32_t cycle;     // lap number (pos >> shift), so a stale prior-lap tag is told from this lap's.
                            //   Lets the drainer reclaim by cycle check, no per-slot zeroing.
    };
    uint64_t w;
} log_word_t;
_Static_assert(sizeof(log_word_t) == 8, "commit word is 8 bytes");  // compile-time proof it packs to 8

typedef struct {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t tail;    // producer reserve cursor, in cache lines
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t head;    // consumer drain cursor, in cache lines
    uint64_t    mask;   // N-1 where N = capacity in cache lines, pow2
    uint32_t    shift;  // log2(N), so cycle = pos >> shift (the lap a monotonic pos belongs to)
    char        *buf;   // N*ANO_CL bytes, cache-line aligned
} log_ring_t;

// The lap a monotonic line position belongs to, low 32 bits. A committed tag carries its own lap. The
// drainer compares against this so a reclaimed slot's old tag never reads as live without zeroing.
static inline uint32_t log_cycle(const log_ring_t *r, uint64_t pos) { return (uint32_t)(pos >> r->shift); }


// --- pure helpers over a ring, no globals, all the structural arithmetic lives here ---

// Cache lines an entry with `len` text bytes occupies, at least 1. The marker shares the head line.
static inline uint64_t log_span(uint16_t len)
{
    return (ANO_LOG_HDR + (uint64_t)len + ANO_CL - 1) / ANO_CL;
}

// Capacity in lines (N) and in bytes.
static inline uint64_t log_lines(const log_ring_t *r) { return r->mask + 1; }
static inline size_t   log_bytes(const log_ring_t *r) { return (size_t)log_lines(r) * ANO_CL; }

// Marker for the entry whose head line is monotonic counter `pos` (physical line = pos & mask).
static inline log_marker_t *log_marker_at(const log_ring_t *r, uint64_t pos)
{
    return (log_marker_t *)(r->buf + (pos & r->mask) * ANO_CL);
}

// Copy `len` text bytes from `src` into the entry body at `pos`, seam-aware. One memcpy when it fits
// before the buffer end, two when it wraps. The marker never wraps, it is one in-bounds line.
static inline void log_write_body(const log_ring_t *r, uint64_t pos, const char *src, uint16_t len)
{
    char  *body  = (char *)log_marker_at(r, pos) + ANO_LOG_HDR;
    size_t toend = log_bytes(r) - (size_t)(body - r->buf);  // bytes from body to the buffer end
    if (len <= toend) {
        memcpy(body, src, len); // common: fits before the seam
    } else {
        memcpy(body, src, toend);                           // to the buffer end
        memcpy(r->buf, src + toend, (size_t)len - toend);   // wrap remainder to line 0
    }
}

// Contiguous pointer to the entry's `len` text bytes. The body itself when it does not wrap, else
// `scratch` (>= ANO_LOG_MSG_MAX) gathered from the two pieces.
static inline const char *log_gather(const log_ring_t *r, uint64_t pos, uint16_t len, char *scratch)
{
    char  *body  = (char *)log_marker_at(r, pos) + ANO_LOG_HDR;
    size_t toend = log_bytes(r) - (size_t)(body - r->buf);
    if (len <= toend)
        return body;    // already contiguous
    memcpy(scratch, body, toend);
    memcpy(scratch + toend, r->buf, (size_t)len - toend);
    return scratch;
}

#endif // ANOPTICENGINE_LOGGING_RING_H
