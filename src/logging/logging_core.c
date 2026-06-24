/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Lock-free MPSC ring logger. A producer formats its line on its own stack, reserves a run of cache
// lines by bumping `tail` with one CAS, copies the finished text in, and publishes with one release
// store of `tag`. It never waits on another thread. The caller drains the ring on its own schedule
// via ano_log_flush(), so the logger owns no thread. The producer path is lock-free. The cold paths
// (drain, immediate, output-file swap) take small locks: one active-consumer gate so any thread may
// call ano_log_flush() safely, and one mutex around the output file handle's lifecycle. FATAL and the
// full-ring overflow write straight through on the calling thread. Stop all producers before
// ano_log_cleanup. Design: docs/logger.md.

#include "logging/logging_ring.h"

#include <anoptic_threads.h>
#include <anoptic_filesystem.h>
#include <anoptic_time.h>

#include <mimalloc.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>


/* Internal state. The ring is the only shared producer surface; everything else is cold. */

static const char  *logStrings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static log_ring_t   g_ring;         // the shared MPSC ring (producers: tail, consumer: head)
static atomic_bool  g_initialized;  // every entry checks this lock-free
static atomic_int   g_minLevel;     // runtime severity gate, one relaxed load on enqueue
static atomic_bool  g_draining;     // active-consumer gate, one drainer at a time

static anothread_mutex_t g_outFileMtx; // guards the output file handle's lifecycle and writes
static ano_file    *g_outFile;         // NULL drains to console

// Timestamp anchor, a monotonic-raw and unix-ns pair captured once at init. wall = unix + (raw -
// anchor), so the second is fixed at the anchor and sub-second precision rides the monotonic delta.
// No per-record syscall, no drift. (§11)
static uint64_t     g_anchorRaw;
static uint64_t     g_anchorUnixNs;

// Consumer-private scratch, one active drainer enforced by g_draining. g_scratch gathers a
// seam-straddling record. g_batch accumulates a whole drain pass for one write().
static char         g_scratch[ANO_LOG_MSG_MAX];
static char        *g_batch;
static size_t       g_batchCap;

/* Formatting (eager, on the producer; §9) */

// Compose "<LEVEL> <file>:<line>:  <message>" into out, no wall-clock prefix and no newline. The
// flusher prepends the time at emit. Returns the byte length, clamped to cap-1 so an over-long line
// can never overrun the entry (the ring stores exactly that many bytes). format(printf, 6, 0) marks
// this a printf forwarder so the vsnprintf below passes -Wformat-nonliteral.
__attribute__((format(printf, 6, 0)))
static int format_line(char *out, int cap, log_types_t level,
                       const char *file, int line, const char *fmt, va_list ap)
{
    const char *name = (level >= LOG_DEBUG && level <= LOG_FATAL) ? logStrings[level] : "?????";
    int head = snprintf(out, (size_t)cap, "%-5s %s:%d:  ", name, file, line);
    if (head < 0) head = 0;
    if (head >= cap) return cap - 1;                                   // prefix alone filled the buffer
    int body = vsnprintf(out + head, (size_t)(cap - head), fmt, ap);   // forwarded under the attribute
    if (body < 0) body = 0;
    int total = head + body;
    return total < cap ? total : cap - 1;                             // clamp: never overrun the entry
}

// Render the wall-clock prefix ("HH:MM:SS") for a raw timestamp through the init anchor.
static int render_walltime(char *out, size_t cap, uint64_t raw_ts)
{
    // @CLAUDE, is this really the fastest it could be? Hmm?
    uint64_t wall_ns = g_anchorUnixNs + (raw_ts - g_anchorRaw);
    ano_datetime t = ano_localtime((int64_t)(wall_ns / 1000000000ull));
    int n = snprintf(out, cap, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    if (n < 0) return 0;
    return (n >= (int)cap) ? (int)cap - 1 : n;
}


/* Output file (under g_outFileMtx) */

// Open <dir>/anoptic.log for append, NULL on failure.
static ano_file *open_log(const char *dir)
{
    char path[MAXPATH];
    int n = snprintf(path, sizeof path, "%s/%s", dir, ANO_LOG_FILENAME);
    return (n > 0 && n < (int)sizeof path) ? ano_fs_open_append(path) : NULL;
}

// Write a finished batch to the output file, or console if none, looping handled inside ano_fs_write.
static void write_batch(const char *data, size_t len)
{
    if (len == 0)
        return;
    ano_mutex_lock(&g_outFileMtx);
    if (g_outFile != NULL) { // @Claude this branch shouldn't be on a hot path. Decide it in init, and pick a function or the other.
        if (ano_fs_write(g_outFile, data, len) != 0)
            fwrite(data, 1, len, stderr);   // write failed, fall back to stderr
    } else {
        // @CLAUDE in the current config, is this even possible? anoptic.log seems almost hardcoded.
        fwrite(data, 1, len, stdout);       // no output file: drain to console
    }
    ano_mutex_unlock(&g_outFileMtx);
}

// Write one finished record straight through on the calling thread (immediate path or full overflow)
// Assembles "<walltime> <text>\n" once so the record is one atomic append. `console` echoes by
// `sync` fsyncs after the write for FATAL durability.
static void emit_one(log_types_t level, uint64_t raw_ts, const char *text, uint16_t len,
                     bool console, bool sync)
{
    char out[ANO_LOG_MSG_MAX + ANO_LOG_TIME_RESV + 2];
    int  p  = render_walltime(out, ANO_LOG_TIME_RESV, raw_ts);
    out[p++] = ' ';
    memcpy(out + p, text, len); p += len;
    out[p++] = '\n';

    if (console) //@CLAUDE hot path branching again. Writing to console is its own thing, and should be an inline function.
        fwrite(out, 1, (size_t)p, (level > LOG_WARN) ? stderr : stdout);

    ano_mutex_lock(&g_outFileMtx);
    if (g_outFile != NULL) {    //@CLAUDE ditto, hot path branching on something that probably can't even happen.
        ano_fs_write(g_outFile, out, (size_t)p);
        if (sync) ano_fs_sync(g_outFile);// wtf?
    } else if (!console) {
        fwrite(out, 1, (size_t)p, stdout);  // no output file and not echoed yet: keep it visible once
    }
    ano_mutex_unlock(&g_outFileMtx);
}


/* The consumer: one synchronous, single-active drain pass (§6) */

static void drain_and_emit(void)
{
    // @CLAUDE this doesn't smell like a proper linearization point. Is it?
    uint64_t h0  = atomic_load_explicit(&g_ring.head, memory_order_relaxed); // consumer-private, gated
    uint64_t h   = h0;
    uint64_t cap = atomic_load_explicit(&g_ring.tail, memory_order_relaxed); // reserved frontier
    size_t   blen = 0;

    // @Claude I wonder if your busyloop is somehow less efficient than a mutex lock.
    for (;;) {
        if (h == cap)
            break;  // caught up to tail, the only stop at exact fullness
        log_marker_t *m = log_marker_at(&g_ring, h);
        log_word_t v = { .w = atomic_load_explicit(&m->tag, memory_order_acquire) };
        if (v.w == 0)
            break;  // gap: reserved but unpublished, resume here next pass
        uint64_t need = log_span(v.len);
        const char *line = log_gather(&g_ring, h, v.len, g_scratch);   // <= 2 memcpys (§8)

        int tn = render_walltime(g_batch + blen, ANO_LOG_TIME_RESV, m->timestamp);
        blen += (size_t)tn;
        g_batch[blen++] = ' ';
        memcpy(g_batch + blen, line, v.len); blen += v.len;
        g_batch[blen++] = '\n';
        h += need;
    }

    if (h != h0) {  // zero the drained range, all-zero line = free again
        uint64_t n = h - h0, a = h0 & g_ring.mask, N = log_lines(&g_ring);
        uint64_t first = (a + n <= N) ? n : (N - a);
        memset(g_ring.buf + a * ANO_CL, 0, (size_t)first * ANO_CL);         // up to the buffer end
        if (first < n) memset(g_ring.buf, 0, (size_t)(n - first) * ANO_CL); // wrap remainder
    }
    atomic_store_explicit(&g_ring.head, h, memory_order_release);   // frees [h0,h), memset happens-before reuse

    write_batch(g_batch, blen); // one syscall for the whole pass
}

// One drain pass, one thread at a time. A second caller that finds the gate held just returns, since
// the active drainer is already emptying the ring and the next flush sweeps anything reserved after
// its tail snapshot. Producers stay lock-free. Only this cold path is serialized, which keeps the
// single-consumer invariant for any flusher count.
static void drain(void)
{
    if (atomic_exchange_explicit(&g_draining, true, memory_order_acquire))
        return; // another consumer is active

    drain_and_emit();
    atomic_store_explicit(&g_draining, false, memory_order_release);
}

// Like drain(), but waits for the gate instead of skipping. The full-ring path uses this so a
// producer that hit a full ring is guaranteed to force a flush and free space, the same flush-then-
// keep-buffering behavior as the mutex logger. The wait is bounded: the active drainer finishes one
// pass (a batched write), and that pass is what frees the room. Cold path, off the producer fast path.
static void drain_wait(void)
{
    //@CLAUDE I don't see the point of this extra code. Is this for multiple consumers?
    while (atomic_exchange_explicit(&g_draining, true, memory_order_acquire))
        ;   // spin: the active drainer is mid-pass and freeing room as it goes

    drain_and_emit();
    atomic_store_explicit(&g_draining, false, memory_order_release);
}


/* Public interface */

int ano_log_enqueue(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    if (!atomic_load_explicit(&g_initialized, memory_order_relaxed))
        return 0;
    if ((int)level < atomic_load_explicit(&g_minLevel, memory_order_relaxed))
        return 0;   // runtime level gate, one relaxed load

    char blob[ANO_LOG_MSG_MAX]; // the final line, formatted on this thread, off-ring
    va_list ap; va_start(ap, fmt);
    int n = format_line(blob, (int)(ANO_LOG_MSG_MAX - ANO_LOG_TIME_RESV),
                        level, file, line, fmt, ap);
    va_end(ap);
    uint16_t len  = (uint16_t)n;
    uint64_t ts   = ano_timestamp_raw();
    uint64_t need = log_span(len);
    // @CLAUDE so where is a log_entry_t created?

    uint64_t pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);
    int flushes = 0; // @Claude I want this gone
    for (;;) {
        uint64_t hd = atomic_load_explicit(&g_ring.head, memory_order_acquire); // full and reuse-safety
        if ((pos + need) - hd > log_lines(&g_ring)) {   // would alias undrained: ring full
            // Full: flush the buffered batch to make room, then keep buffering this line (one
            // batched write, not a per-record one). Same policy as the mutex logger. 
            // 
            // The only thing
            // a flush cannot free is a gap -- a producer that reserved but has not published, or
            // died mid-window (§7). After ANO_LOG_FULL_FLUSH_TRIES flushes still find no room, that
            // gap is wedging the drain, so write this one line straight through rather than spin.

            // @CLAUDE OK so on the gap problem: it's basically nonsense. This literally shouldn't be possible,
            // and idk wtf you did to the data structure to make it possible if it is.
            // "reserved but not published" is a non-sequitur.
            // This is not proper atomic or lockfree behaviour, our entire linearization logic is fucked because
            // you are doing it wrong if this can occur.

            if (flushes++ >= ANO_LOG_FULL_FLUSH_TRIES) { // @Claude I want this gone.
                emit_one(level, ts, blob, len, false, false);
                return 1;
            }
            drain_wait();                                                       // batched flush -> room
            pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);     // re-snapshot, retry
            continue;
        }
        if (atomic_compare_exchange_weak_explicit(&g_ring.tail, &pos, pos + need,
                memory_order_relaxed, memory_order_relaxed))
            break;  // own lines [pos, pos+need)
        // CAS failed: pos reloaded with the current tail, retry.
    }

    log_marker_t *m = log_marker_at(&g_ring, pos);
    m->timestamp = ts;  // plain store, ordered by the release below
    log_write_body(&g_ring, pos, blob, len);    // <= 2 memcpys (§8)
    log_word_t v = { .len = len, .level = (uint8_t)level, .flags = ANO_LOG_COMMITTED };
    atomic_store_explicit(&m->tag, v.w, memory_order_release);  // publish: one gate, whole record
    return flushes > 0 ? 1 : 0;   // 1: the ring was full, so we flushed to make room before buffering
}

void ano_log_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    char blob[ANO_LOG_MSG_MAX];
    va_list ap; va_start(ap, fmt);
    int n = format_line(blob, (int)(ANO_LOG_MSG_MAX - ANO_LOG_TIME_RESV),
                        level, file, line, fmt, ap);
    va_end(ap);

    if (!atomic_load_explicit(&g_initialized, memory_order_relaxed)) {
        fprintf(stderr, "%.*s\n", n, blob); // pre-init: stderr only, no anchor yet
        return;
    }
    drain();    // flush buffered records first to keep order
    emit_one(level, ano_timestamp_raw(), blob, (uint16_t)n, /*console*/true, /*sync*/true);
}

int ano_log_output_dir(const char *directoryPath)
{
    if (directoryPath == NULL || directoryPath[0] == '\0'
        || !atomic_load_explicit(&g_initialized, memory_order_relaxed))
        return -1;

    ano_file *newOut = open_log(directoryPath);
    if (newOut == NULL)
        return -1;  // open failed, keep current output file

    ano_mutex_lock(&g_outFileMtx);
    if (g_outFile != NULL) {
        ano_fs_sync(g_outFile);
        ano_fs_close(g_outFile);
    }
    g_outFile = newOut;
    ano_mutex_unlock(&g_outFileMtx);
    return 0;
}

void ano_log_set_level(log_types_t min)
{
    if (atomic_load_explicit(&g_initialized, memory_order_relaxed))
        atomic_store_explicit(&g_minLevel, (int)min, memory_order_relaxed);
}

void ano_log_flush(void)
{
    if (atomic_load_explicit(&g_initialized, memory_order_relaxed))
        drain();
}

int ano_log_init(void)
{
    if (atomic_load_explicit(&g_initialized, memory_order_relaxed))
        return 0;
    if (ano_mutex_init(&g_outFileMtx, NULL) != 0)
        return -1;

    g_ring.mask = ANO_LOG_RING_LINES - 1;
    g_ring.buf  = ano_aligned_malloc((size_t)ANO_LOG_RING_LINES * ANO_CL, ANO_CACHE_LINE);
    if (g_ring.buf == NULL) { ano_mutex_destroy(&g_outFileMtx); return -1; }
    memset(g_ring.buf, 0, (size_t)ANO_LOG_RING_LINES * ANO_CL);
    atomic_store(&g_ring.tail, 0);
    atomic_store(&g_ring.head, 0);

    // Batch upper bound: all drained text (<= N*ANO_CL) plus a <= 16-byte prefix per record for at
    // most N records.
    g_batchCap = (size_t)ANO_LOG_RING_LINES * ANO_CL + (size_t)ANO_LOG_RING_LINES * 16 + 256;
    g_batch = mi_malloc(g_batchCap);
    if (g_batch == NULL) { ano_aligned_free(g_ring.buf); ano_mutex_destroy(&g_outFileMtx); return -1; }

    atomic_store(&g_draining, false);
    atomic_store_explicit(&g_minLevel, LOG_DEBUG, memory_order_relaxed);
    g_outFile = NULL;

    g_anchorRaw    = ano_timestamp_raw();
    g_anchorUnixNs = (uint64_t)ano_timestamp_unix() * 1000000000ull;

    // Default output file is in the game directory.
    filepath dir = ano_fs_gamepath();
    if (dir.pathString != NULL) {
        g_outFile = open_log(dir.pathString);
        mi_free(dir.pathString);
    }

    atomic_store_explicit(&g_initialized, true, memory_order_release);
    return 0;
}

int ano_log_cleanup(void)
{
    if (!atomic_load_explicit(&g_initialized, memory_order_relaxed))
        return 0;

    atomic_store_explicit(&g_initialized, false, memory_order_release);

    drain();    // one final drain, no producers remain by contract

    ano_mutex_lock(&g_outFileMtx);
    if (g_outFile != NULL) {
        ano_fs_sync(g_outFile);
        ano_fs_close(g_outFile);
        g_outFile = NULL;
    }
    ano_mutex_unlock(&g_outFileMtx);

    ano_aligned_free(g_ring.buf); g_ring.buf = NULL;
    mi_free(g_batch);             g_batch = NULL;
    ano_mutex_destroy(&g_outFileMtx);
    return 0;
}