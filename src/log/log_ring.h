/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private lock-free MPSC ring (log_core.c). Cache-line grain (DPDK rte_ring family).
// Producer: capture/format off-ring, CAS `tail`, copy, release-store `tag`. Consumer: acquire tags in order, emit, release-store `head`.
// Slot live iff tag committed and carries current lap (`cycle`). Only `tag` is synchronized.

#ifndef ANOPTICENGINE_LOG_RING_H
#define ANOPTICENGINE_LOG_RING_H

#include "log/log_core.h"   // ANO_LOG_MSG_MAX, ring sizing

#include <anoptic_memory.h>          // ANO_CACHE_LINE / ANO_THREAD_LINE
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

// Reservation grain = ANO_CACHE_LINE. Hot cursors pad to ANO_THREAD_LINE (false-sharing isolation).
#define ANO_CL ANO_CACHE_LINE
#define ANO_LOG_HDR  16     // head-line marker: commit word + raw timestamp

enum {
    ANO_LOG_COMMITTED = 1 << 0, // set on publish (committed tag nonzero even at len 0)
    ANO_LOG_DEFERRED  = 1 << 1, // body is deferred-format capture blob
    ANO_LOG_TOFILE    = 1 << 2, // sink: batched to output file at drain
    ANO_LOG_TOCON     = 1 << 3, // sink: echoed to terminal at drain
};

// Entry head-line marker. Only `tag` is atomic (publish gate). timestamp/text ride its release/acquire.
typedef struct {
    _Atomic uint64_t tag;   // 0 = free, nonzero = committed. Publish last (release), read first (acquire).
    uint64_t timestamp;     // raw ticks, rendered at drain
} log_marker_t;
_Static_assert(sizeof(log_marker_t) == ANO_LOG_HDR, "marker is 16 bytes");
_Static_assert(ANO_LOG_HDR <= ANO_CACHE_LINE, "marker fits in one cache line");

// View punned over `tag`. Thread-local only. Shared word always accessed atomically.
typedef union {
    // `len` is low 16 bits of `w`. 2+1+1+4 packs to 8 with zero padding.
    struct {
        uint16_t len;       // stored text bytes, span = ceil((16 + len) / ANO_CL)
        uint8_t  level;     // ano_loglevel_t for flusher routing
        uint8_t  flags;     // COMMITTED + DEFERRED + sink bits
        uint32_t cycle;     // lap (pos >> shift): stale prior-lap tag != this lap, no zeroing
    };
    uint64_t w;
} log_word_t;
_Static_assert(sizeof(log_word_t) == 8, "commit word is 8 bytes");

typedef struct {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t tail;    // producer reserve cursor, cache lines
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t head;    // consumer drain cursor, cache lines
    uint64_t    mask;   // N-1, N = capacity in cache lines, pow2
    uint32_t    shift;  // log2(N), cycle = pos >> shift
    char        *buf;   // N*ANO_CL bytes, cache-line aligned
} log_ring_t;

// Lap for monotonic line position, low 32 bits. Drainer rejects stale tags without zeroing.
static inline uint32_t log_cycle(const log_ring_t *r, uint64_t pos) { return (uint32_t)(pos >> r->shift); }


/* Pure Helpers */

// Cache lines for `len` text bytes, at least 1. Marker shares the head line.

static inline uint64_t log_span(uint16_t len)
{
    return (ANO_LOG_HDR + (uint64_t)len + ANO_CL - 1) / ANO_CL;
}

// Capacity in lines (N) and bytes.
static inline uint64_t log_lines(const log_ring_t *r) { return r->mask + 1; }
static inline size_t   log_bytes(const log_ring_t *r) { return (size_t)log_lines(r) * ANO_CL; }

// Marker at monotonic `pos` (physical line = pos & mask).
static inline log_marker_t *log_marker_at(const log_ring_t *r, uint64_t pos)
{
    return (log_marker_t *)(r->buf + (pos & r->mask) * ANO_CL);
}

// Copy `len` bytes into entry body at `pos`, seam-aware. Marker never wraps.
static inline void log_write_body(const log_ring_t *r, uint64_t pos, const char *src, uint16_t len)
{
    char  *body  = (char *)log_marker_at(r, pos) + ANO_LOG_HDR;
    size_t toend = log_bytes(r) - (size_t)(body - r->buf);
    if (len <= toend) {
        memcpy(body, src, len);
    } else {
        memcpy(body, src, toend);
        memcpy(r->buf, src + toend, (size_t)len - toend);
    }
}

// Contiguous pointer to entry text, or gather into `scratch` (>= ANO_LOG_MSG_MAX) on wrap.
static inline const char *log_gather(const log_ring_t *r, uint64_t pos, uint16_t len, char *scratch)
{
    char  *body  = (char *)log_marker_at(r, pos) + ANO_LOG_HDR;
    size_t toend = log_bytes(r) - (size_t)(body - r->buf);
    if (len <= toend)
        return body;
    memcpy(scratch, body, toend);
    memcpy(scratch + toend, r->buf, (size_t)len - toend);
    return scratch;
}

#endif // ANOPTICENGINE_LOG_RING_H
