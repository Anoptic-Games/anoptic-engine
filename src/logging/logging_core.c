/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Lock-free MPSC ring logger. A producer formats its line on its own stack, reserves a run of cache
// lines with one CAS on `tail`, copies the text in, and publishes with one release store of `tag`. It
// never waits on another thread. The caller drains the ring via ano_log_flush() on its own schedule,
// so the logger owns no thread. The producer path is lock-free. The cold paths (drain, immediate,
// output-file swap) take small locks: a drain mutex (one consumer) and an output-file mutex. FATAL
// writes straight through. A full ring flushes, then keeps buffering. Stop all producers before
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


/* Internal state. Only the ring is producer-shared, the rest is cold. */

static const char  *logStrings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static log_ring_t   g_ring;         // the shared MPSC ring (producers: tail, consumer: head)
static atomic_bool  g_initialized;  // every entry checks this lock-free
static atomic_int   g_minLevel;     // runtime severity gate, one relaxed load on enqueue

static anothread_mutex_t g_drainMtx;   // gates the single consumer: one drainer at a time
static anothread_mutex_t g_outFileMtx; // guards the output file handle's lifecycle and writes
static ano_file    *g_outFile;         // the open output file, or NULL

// Output target, bound once when the file opens or closes, so the write path never tests g_outFile.
// g_persist writes a buffer (file, else console). g_syncOut fsyncs (file) or no-ops. g_haveFile gates
// the immediate echo.
static void (*g_persist)(const void *data, size_t len);
static void (*g_syncOut)(void);
static bool         g_haveFile;

// Timestamp anchor, a raw and unix-ns pair captured once at init. wall = unix + (raw - anchor), so the
// second is fixed and sub-second precision rides the monotonic delta. No per-record syscall. (§11)
static uint64_t     g_anchorRaw;
static uint64_t     g_anchorUnixNs;

// Drainer-private, touched only under g_drainMtx. g_scratch gathers a seam-straddling record. g_batch
// holds a whole drain pass for one write. g_drainHMS caches the "HH:MM:SS" prefix for its second, so
// the civil-time conversion runs once per second, not per record.
static char         g_scratch[ANO_LOG_MSG_MAX];
static char        *g_batch;
static size_t       g_batchCap;
static uint64_t     g_drainSec;
static char         g_drainHMS[8];
static bool         g_drainHMSValid;

/* Formatting (eager, on the producer; §9) */

// Compose "<LEVEL> <file>:<line>:  <message>" into out, no time prefix, no newline. The flusher adds
// the time at emit. Returns the byte length clamped to cap-1 so an over-long line never overruns the
// entry. format(printf, 6, 0) marks this a printf forwarder so the vsnprintf below passes -Wformat-nonliteral.
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

// Two digits (00-99) at p, advance. No printf machinery.
static inline char *put2(char *p, int v)
{
    *p++ = (char)('0' + (v / 10) % 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

// Render "HH:MM:SS" (8 bytes) for wall-clock second `sec` into out8. Hand-rolled, no snprintf.
static void render_hms(char *out8, uint64_t sec)
{
    ano_datetime t = ano_localtime((int64_t)sec);
    char *p = put2(out8, t.hour);   *p++ = ':';
    p      = put2(p, t.minute);     *p++ = ':';
    (void)   put2(p, t.second);
}

// Wall-clock second for a raw timestamp, through the init anchor.
static inline uint64_t wall_second(uint64_t raw_ts)
{
    return (g_anchorUnixNs + (raw_ts - g_anchorRaw)) / 1000000000ull;
}

// Render the 8-byte "HH:MM:SS" prefix for a raw timestamp. Immediate path only. The drain path
// memoizes per second instead (see drain_and_emit).
static int render_walltime(char *out, uint64_t raw_ts)
{
    render_hms(out, wall_second(raw_ts));
    return 8;
}


/* Output file (under g_outFileMtx) */

// Open <dir>/anoptic.log for append, NULL on failure.
static ano_file *open_log(const char *dir)
{
    char path[MAXPATH];
    int n = snprintf(path, sizeof path, "%s/%s", dir, ANO_LOG_FILENAME);
    return (n > 0 && n < (int)sizeof path) ? ano_fs_open_append(path) : NULL;
}

// The two persist targets, bound by select_output() when the file opens or closes. The console one
// only fires when the default file failed to open.
static void persist_file(const void *data, size_t len)
{
    if (ano_fs_write(g_outFile, data, len) != 0)
        fwrite(data, 1, len, stderr);   // write failed, fall back to stderr
}
static void persist_console(const void *data, size_t len) { fwrite(data, 1, len, stdout); }
static void sync_file(void) { ano_fs_sync(g_outFile); }
static void sync_none(void) { }

// Bind the writer set to the current output file (or the console when none). Caller holds g_outFileMtx.
static void select_output(void)
{
    g_haveFile = (g_outFile != NULL);
    g_persist  = g_haveFile ? persist_file : persist_console;
    g_syncOut  = g_haveFile ? sync_file    : sync_none;
}

// Echo a finished line to the console by severity (>WARN to stderr, else stdout). Immediate path only.
static inline void echo_console(const char *line, size_t len, log_types_t level)
{
    fwrite(line, 1, len, (level > LOG_WARN) ? stderr : stdout);
}

// Write a finished batch to the chosen output, one locked call, no per-write target test.
static void write_batch(const char *data, size_t len)
{
    if (len == 0)
        return;
    ano_mutex_lock(&g_outFileMtx);
    g_persist(data, len);
    ano_mutex_unlock(&g_outFileMtx);
}

// Write one finished record straight through on the calling thread (immediate path or full-ring gap
// fallback). Assembles "<walltime> <text>\n" once so the append is atomic. `console` echoes by
// severity, suppressed with no file. `sync` fsyncs after the write for FATAL durability.
static void emit_one(log_types_t level, uint64_t raw_ts, const char *text, uint16_t len,
                     bool console, bool sync)
{
    char out[ANO_LOG_MSG_MAX + ANO_LOG_TIME_RESV + 2];
    int  p  = render_walltime(out, raw_ts);
    out[p++] = ' ';
    memcpy(out + p, text, len); p += len;
    out[p++] = '\n';

    ano_mutex_lock(&g_outFileMtx);
    if (console && g_haveFile)              // no file: the persist below already hits the console
        echo_console(out, (size_t)p, level);
    g_persist(out, (size_t)p);
    if (sync) g_syncOut();
    ano_mutex_unlock(&g_outFileMtx);
}


/* The consumer: one synchronous, single-active drain pass (§6) */

static void drain_and_emit(void)
{
    // Not one linearization point: a batch drain is N consume events, each linearizing at its own `tag`
    // acquire below. head is drainer-private. The tail load is a relaxed count bound on the walk. The
    // reclaim linearizes at the head release-store at the end.
    uint64_t h0  = atomic_load_explicit(&g_ring.head, memory_order_relaxed); // drainer-private
    uint64_t h   = h0;
    uint64_t cap = atomic_load_explicit(&g_ring.tail, memory_order_relaxed); // reserved frontier, a count bound
    size_t   blen = 0;

    for (;;) {  // bounded walk, not a spin: stops at the tail bound or the first uncommitted gap
        if (h == cap)
            break;  // caught up to tail, the only stop at exact fullness
        log_marker_t *m = log_marker_at(&g_ring, h);
        log_word_t v = { .w = atomic_load_explicit(&m->tag, memory_order_acquire) };
        if (v.w == 0)
            break;  // gap: reserved but unpublished, resume here next pass
        uint64_t need = log_span(v.len);
        const char *line = log_gather(&g_ring, h, v.len, g_scratch);   // <= 2 memcpys (§8)

        uint64_t sec = wall_second(m->timestamp);
        if (!g_drainHMSValid || sec != g_drainSec) {   // civil-time conversion once per second, not per record
            render_hms(g_drainHMS, sec);
            g_drainSec = sec;
            g_drainHMSValid = true;
        }
        memcpy(g_batch + blen, g_drainHMS, 8); blen += 8;
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

// One drain pass, serialized so exactly one thread drains at a time. The full-ring path calls this too,
// so it must block, not skip, hence a mutex.
// WORKAROUND (FOR NOW): A BLOCKING MUTEX GATES THE SINGLE CONSUMER. REVISIT WITH A LOCK-FREE HANDOFF
// SO CONCURRENT FLUSHERS DON'T SERIALIZE ON IT (THE SPIN THAT WAS HERE REGRESSED 8-PRODUCER THROUGHPUT).
static void drain(void)
{
    ano_mutex_lock(&g_drainMtx);
    drain_and_emit();
    ano_mutex_unlock(&g_drainMtx);
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
    int n = format_line(blob, (int)ANO_LOG_MSG_MAX,
                        level, file, line, fmt, ap);
    va_end(ap);
    uint16_t len  = (uint16_t)n;
    uint64_t ts   = ano_timestamp_raw();
    uint64_t need = log_span(len);

    // No log_entry_t: an "entry" is a marker (tag + timestamp) plus inline text, laid straight into the
    // ring's reserved cache lines below. The ring is the storage.
    uint64_t pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);
    bool flushed = false;
    for (;;) {
        uint64_t hd = atomic_load_explicit(&g_ring.head, memory_order_acquire); // full and reuse-safety
        if ((pos + need) - hd > log_lines(&g_ring)) {   // would alias undrained: ring full
            if (flushed) {
                // WORKAROUND (FOR NOW): A GAP (PREEMPTED/DEAD PRODUCER MID-PUBLISH) WEDGED THE DRAIN; WRITE THIS ONE THROUGH.
                emit_one(level, ts, blob, len, false, false);
                return 1;
            }
            drain();    // flush the buffered batch to make room, then keep buffering this line
            flushed = true;
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
    return flushed ? 1 : 0;   // 1: the ring was full, so we flushed to make room before buffering
}

void ano_log_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    char blob[ANO_LOG_MSG_MAX];
    va_list ap; va_start(ap, fmt);
    int n = format_line(blob, (int)ANO_LOG_MSG_MAX,
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
    select_output();    // rebind the writer set to the new file
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
    if (ano_mutex_init(&g_drainMtx, NULL) != 0) { ano_mutex_destroy(&g_outFileMtx); return -1; }

    g_ring.mask = ANO_LOG_RING_LINES - 1;
    g_ring.buf  = ano_aligned_malloc(ANO_LOG_RING_BYTES, ANO_LOG_RING_ALIGN);
    if (g_ring.buf == NULL) { ano_mutex_destroy(&g_drainMtx); ano_mutex_destroy(&g_outFileMtx); return -1; }
    memset(g_ring.buf, 0, ANO_LOG_RING_BYTES);
    atomic_store(&g_ring.tail, 0);
    atomic_store(&g_ring.head, 0);

    // Batch upper bound: all drained text (<= N*ANO_CL) plus a <= 16-byte prefix per record for at
    // most N records.
    g_batchCap = (size_t)ANO_LOG_RING_LINES * ANO_CL + (size_t)ANO_LOG_RING_LINES * 16 + 256;
    g_batch = mi_malloc(g_batchCap);
    if (g_batch == NULL) {
        ano_aligned_free(g_ring.buf);
        ano_mutex_destroy(&g_drainMtx); ano_mutex_destroy(&g_outFileMtx);
        return -1;
    }
    g_drainHMSValid = false;

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
    select_output();    // bind g_persist/g_syncOut/g_haveFile to the chosen output

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
    ano_mutex_destroy(&g_drainMtx);
    ano_mutex_destroy(&g_outFileMtx);
    return 0;
}