/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Mutex logger: producers format on their own thread, then append under one mutex to a shared
// buffer; a background flusher drains it to the file sink in batched writes. FATAL/_now write
// through synchronously. Single-owner lifecycle: stop producers before ano_log_cleanup.

#include "logging/logging_core.h"

#include <anoptic_threads.h>
#include <anoptic_filesystem.h>
#include <anoptic_time.h>

#include <mimalloc.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>


/* Internal state */

// One mutex guards the buffer, the sink, and all config. The two lifecycle flags are atomic
// because they are read without the lock: g_initialized before the mutex is live, g_running by
// the flusher between sleeps.
static const char       *log_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
static char              g_buf[ANO_LOG_BUF_CAP];   // newline-terminated lines, flushed as one batch
static size_t            g_buf_len;
static ano_file         *g_sink;                   // NULL => drain to console
static anothread_mutex_t g_mtx;
static anothread_t       g_flusher;
static atomic_bool       g_running;
static atomic_bool       g_initialized;
static int               g_min_level;              // log_types_t gate
static int               g_full_policy;            // ano_log_full_policy_t
static uint64_t          g_interval_us;
static uint64_t          g_dropped;


/* Formatting */

// Wall-clock "HH:MM:SS" into buf.
static void format_walltime(char *buf, size_t cap)
{
    ano_datetime t = ano_localtime(ano_timestamp_unix());
    snprintf(buf, cap, "%02d:%02d:%02d", t.hour, t.minute, t.second);
}

// Compose "HH:MM:SS LEVEL file:line:  <msg>" (no newline). Returns length, clamped to cap-1.
// format(printf, 6, 0): fmt is arg 6, args arrive as a va_list -- marks this a printf forwarder, so
// the vsnprintf below is a sanctioned non-literal forward rather than a -Wformat-nonliteral fault.
__attribute__((format(printf, 6, 0)))
static int build_line(char *out, int cap, log_types_t level,
                      const char *file, int line, const char *fmt, va_list ap)
{
    char ts[16];
    format_walltime(ts, sizeof ts);
    const char *name = (level >= LOG_DEBUG && level <= LOG_FATAL) ? log_strings[level] : "?????";
    int p = snprintf(out, cap, "%s %-5s %s:%d:  ", ts, name, file, line);
    if (p < 0) p = 0;
    if (p >= cap) return cap - 1;
    int m = vsnprintf(out + p, (size_t)(cap - p), fmt, ap);   // sanctioned forward (build_line's attribute)
    if (m < 0) m = 0;
    int total = p + m;
    return total >= cap ? cap - 1 : total;   // clamp on truncation
}


/* Sink + buffer (all under g_mtx) */

// Write the live buffer to the sink (or console if none) and reset it. Caller holds g_mtx.
static void flush_locked(void)
{
    if (g_buf_len == 0)
        return;
    if (g_sink != NULL) {
        if (ano_fs_write(g_sink, g_buf, g_buf_len) != 0)
            fwrite(g_buf, 1, g_buf_len, stderr);   // sink write failed: keep it on stderr
    } else {
        fwrite(g_buf, 1, g_buf_len, stdout);
    }
    g_buf_len = 0;
}

static void drain(void)
{
    ano_mutex_lock(&g_mtx);
    flush_locked();
    ano_mutex_unlock(&g_mtx);
}

// Append "line\n", applying the full-buffer policy. Caller holds g_mtx.
// Returns 0 enqueued, 1 written immediately (IMMEDIATE on full), -1 dropped (DROP_NEWEST).
static int append_locked(const char *line, size_t len)
{
    size_t need = len + 1;
    bool full = g_buf_len + need > ANO_LOG_BUF_CAP;
    if (full) {
        if (g_full_policy == ANO_LOG_DROP_NEWEST) {
            g_dropped++;
            return -1;
        }
        flush_locked();   // IMMEDIATE/BLOCK: drain to make room, in order (a line always fits empty)
    }
    memcpy(g_buf + g_buf_len, line, len);
    g_buf[g_buf_len + len] = '\n';
    g_buf_len += need;
    if (full && g_full_policy == ANO_LOG_FULL_IMMEDIATE) {
        flush_locked();
        if (g_sink != NULL) ano_fs_sync(g_sink);   // immediate => durable now
        return 1;
    }
    return 0;
}


/* Flusher thread */

// Sleep up to the interval in slices, so cleanup's join is not held up for a whole interval.
static void flusher_wait(void)
{
    ano_mutex_lock(&g_mtx);
    uint64_t total = g_interval_us;
    ano_mutex_unlock(&g_mtx);
    const uint64_t slice = 50000;   // 50 ms shutdown responsiveness
    for (uint64_t slept = 0; slept < total && atomic_load(&g_running); ) {
        uint64_t chunk = total - slept < slice ? total - slept : slice;
        ano_sleep(chunk);
        slept += chunk;
    }
}

static void *flusher_main(void *unused)
{
    (void)unused;
    while (atomic_load(&g_running)) {
        drain();
        flusher_wait();
    }
    drain();   // final drain after stop
    return NULL;
}


/* Sink open */

// Open "<dir>/anoptic.log" for append, or NULL on failure.
static ano_file *open_log(const char *dir)
{
    char path[MAXPATH];
    int n = snprintf(path, sizeof path, "%s/%s", dir, ANO_LOG_FILENAME);
    return (n > 0 && n < (int)sizeof path) ? ano_fs_open_append(path) : NULL;
}


/* Public interface */

int ano_log_enqueue(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    if (!atomic_load(&g_initialized))
        return 0;

    char buf[ANO_LOG_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    int len = build_line(buf, (int)sizeof buf, level, file, line, fmt, ap);
    va_end(ap);

    ano_mutex_lock(&g_mtx);
    // Compare as signed int: log_types_t may be an unsigned enum, and g_min_level can hold an
    // out-of-range (even negative) value a caller pushed through ano_log_set_level.
    int rc = ((int)level < g_min_level) ? 0 : append_locked(buf, (size_t)len);
    ano_mutex_unlock(&g_mtx);
    return rc;
}

void ano_log_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    char buf[ANO_LOG_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    int len = build_line(buf, (int)sizeof buf, level, file, line, fmt, ap);
    va_end(ap);

    if (!atomic_load(&g_initialized)) {           // pre-init: stderr only
        fprintf(stderr, "%.*s\n", len, buf);
        return;
    }
    fprintf(level > LOG_WARN ? stderr : stdout, "%.*s\n", len, buf);   // console, by severity

    ano_mutex_lock(&g_mtx);
    flush_locked();                               // buffered records first, preserving order
    if (g_sink != NULL) {
        ano_fs_write(g_sink, buf, (size_t)len);
        ano_fs_write(g_sink, "\n", 1);
        ano_fs_sync(g_sink);
    }
    ano_mutex_unlock(&g_mtx);
}

void ano_log_interval(uint32_t ms)
{
    if (!atomic_load(&g_initialized))
        return;
    ano_mutex_lock(&g_mtx);
    g_interval_us = (uint64_t)ms * 1000u;
    ano_mutex_unlock(&g_mtx);
}

int ano_log_output_dir(const char *directoryPath)
{
    if (directoryPath == NULL || directoryPath[0] == '\0' || !atomic_load(&g_initialized))
        return -1;

    ano_file *sink = open_log(directoryPath);
    if (sink == NULL)
        return -1;                                // open failed: keep the current sink

    ano_mutex_lock(&g_mtx);
    if (g_sink != NULL) {
        ano_fs_sync(g_sink);
        ano_fs_close(g_sink);
    }
    g_sink = sink;
    ano_mutex_unlock(&g_mtx);
    return 0;
}

void ano_log_set_level(log_types_t min)
{
    if (!atomic_load(&g_initialized))
        return;
    ano_mutex_lock(&g_mtx);
    g_min_level = (int)min;
    ano_mutex_unlock(&g_mtx);
}

void ano_log_set_full_policy(ano_log_full_policy_t policy)
{
    if (!atomic_load(&g_initialized))
        return;
    ano_mutex_lock(&g_mtx);
    g_full_policy = (int)policy;
    ano_mutex_unlock(&g_mtx);
}

void ano_log_flush(void)
{
    if (atomic_load(&g_initialized))
        drain();
}

uint64_t ano_log_dropped(void)
{
    if (!atomic_load(&g_initialized))
        return 0;
    ano_mutex_lock(&g_mtx);
    uint64_t n = g_dropped;
    ano_mutex_unlock(&g_mtx);
    return n;
}

int ano_log_init(void)
{
    if (ano_mutex_init(&g_mtx, NULL) != 0)
        return -1;

    g_buf_len = 0;
    g_dropped = 0;
    g_sink = NULL;
    g_min_level = LOG_DEBUG;
    g_full_policy = ANO_LOG_FULL_IMMEDIATE;
    g_interval_us = ANO_LOG_DEFAULT_INTERVAL_US;

    // Default sink: the game (executable) directory.
    filepath dir = ano_fs_gamepath();
    if (dir.pathString != NULL) {
        g_sink = open_log(dir.pathString);
        mi_free(dir.pathString);
    }

    atomic_store(&g_running, true);
    if (ano_thread_create(&g_flusher, NULL, flusher_main, NULL) != 0) {
        atomic_store(&g_running, false);
        if (g_sink != NULL) { ano_fs_close(g_sink); g_sink = NULL; }
        ano_mutex_destroy(&g_mtx);
        return -1;
    }
    atomic_store(&g_initialized, true);
    return 0;
}

int ano_log_cleanup(void)
{
    if (!atomic_load(&g_initialized))
        return 0;

    atomic_store(&g_initialized, false);
    atomic_store(&g_running, false);
    ano_thread_join(g_flusher, NULL);   // joins after a final drain; no producers remain (single owner)

    if (g_sink != NULL) {
        ano_fs_sync(g_sink);
        ano_fs_close(g_sink);
        g_sink = NULL;
    }
    ano_mutex_destroy(&g_mtx);
    return 0;
}
