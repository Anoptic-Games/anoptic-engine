/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Preserved baseline: the pre-ring mutex logger, namespaced mtxlog_* for the optional benchmark
// (anotest_logbench). Body unchanged from the commit before the lock-free rewrite. Not in the
// standard build -- see logging_old.h.

// Mutex logger. Producers format on their own thread, then append to a shared buffer under one
// mutex. The caller drains it to the sink via mtxlog_flush() on its own schedule, so the logger
// owns no thread. FATAL and _now write straight through. Stop all producers before mtxlog_cleanup.

#include "logging/logging_old.h"

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

// One mutex guards the buffer, sink, and config. g_initialized is atomic because every entry reads
// it without the lock, before the mutex exists and after it dies.
static const char       *logStrings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
static char              g_buf[MTXLOG_BUF_CAP];    // newline-terminated lines, flushed as one batch
static size_t            g_bufLen;
static ano_file         *g_sink;                   // NULL drains to console
static anothread_mutex_t g_mtx;
static atomic_bool       g_initialized;
static int               g_minLevel;               // level gate
static int               g_fullPolicy;
static uint64_t          g_dropped;


/* Formatting */

// Wall-clock HH:MM:SS into out.
static void format_walltime(char *out, size_t cap)
{
    ano_datetime t = ano_localtime(ano_timestamp_unix());
    snprintf(out, cap, "%02d:%02d:%02d", t.hour, t.minute, t.second);
}

// Compose the prefix and message into out, no newline. Returns length clamped to cap-1.
// format(printf, 6, 0) marks this a printf forwarder, so the vsnprintf below passes -Wformat-nonliteral.
__attribute__((format(printf, 6, 0)))
static int build_line(char *out, int cap, log_types_t level,
                      const char *file, int line, const char *fmt, va_list ap)
{
    char ts[16];
    format_walltime(ts, sizeof ts);
    const char *name = (level >= LOG_DEBUG && level <= LOG_FATAL) ? logStrings[level] : "?????";
    int head = snprintf(out, cap, "%s %-5s %s:%d:  ", ts, name, file, line);
    if (head < 0) head = 0;
    if (head >= cap) return cap - 1;
    int body = vsnprintf(out + head, (size_t)(cap - head), fmt, ap);   // forwarded under the attribute
    if (body < 0) body = 0;
    int total = head + body;
    return total >= cap ? cap - 1 : total;   // clamp on truncation
}


/* Sink + buffer (all under g_mtx) */

// Write the buffer to the sink, or console if none, then reset it. Caller holds g_mtx.
static void flush_locked(void)
{
    if (g_bufLen == 0)
        return;
    if (g_sink != NULL) {
        if (ano_fs_write(g_sink, g_buf, g_bufLen) != 0)
            fwrite(g_buf, 1, g_bufLen, stderr);   // sink failed, fall back to stderr
    } else {
        fwrite(g_buf, 1, g_bufLen, stdout);
    }
    g_bufLen = 0;
}

static void drain(void)
{
    ano_mutex_lock(&g_mtx);
    flush_locked();
    ano_mutex_unlock(&g_mtx);
}

// Append line plus newline under the full-buffer policy. Caller holds g_mtx.
// Returns 0 buffered, 1 written now (IMMEDIATE on full), -1 dropped (DROP_NEWEST).
static int append_locked(const char *line, size_t len)
{
    size_t need = len + 1;
    bool full = g_bufLen + need > MTXLOG_BUF_CAP;
    if (full) {
        if (g_fullPolicy == MTXLOG_DROP_NEWEST) {
            g_dropped++;
            return -1;
        }
        flush_locked();   // make room, a line always fits an empty buffer
    }
    memcpy(g_buf + g_bufLen, line, len);
    g_buf[g_bufLen + len] = '\n';
    g_bufLen += need;
    if (full && g_fullPolicy == MTXLOG_FULL_IMMEDIATE) {
        flush_locked();
        if (g_sink != NULL) ano_fs_sync(g_sink);   // flush durably
        return 1;
    }
    return 0;
}


/* Sink open */

// Open <dir>/anoptic_mtx.log for append, NULL on failure.
static ano_file *open_log(const char *dir)
{
    char path[MAXPATH];
    int n = snprintf(path, sizeof path, "%s/%s", dir, MTXLOG_FILENAME);
    return (n > 0 && n < (int)sizeof path) ? ano_fs_open_append(path) : NULL;
}


/* Public interface */

int mtxlog_enqueue(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    if (!atomic_load(&g_initialized))
        return 0;

    char buf[MTXLOG_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    int len = build_line(buf, (int)sizeof buf, level, file, line, fmt, ap);
    va_end(ap);

    ano_mutex_lock(&g_mtx);
    // Signed compare, the enum may be unsigned but a caller can set a negative g_minLevel.
    int rc = ((int)level < g_minLevel) ? 0 : append_locked(buf, (size_t)len);
    ano_mutex_unlock(&g_mtx);
    return rc;
}

void mtxlog_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    char buf[MTXLOG_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    int len = build_line(buf, (int)sizeof buf, level, file, line, fmt, ap);
    va_end(ap);

    if (!atomic_load(&g_initialized)) {           // pre-init, stderr only
        fprintf(stderr, "%.*s\n", len, buf);
        return;
    }
    fprintf(level > LOG_WARN ? stderr : stdout, "%.*s\n", len, buf);   // console, by severity

    ano_mutex_lock(&g_mtx);
    flush_locked();                               // buffered lines first to keep order
    if (g_sink != NULL) {
        ano_fs_write(g_sink, buf, (size_t)len);
        ano_fs_write(g_sink, "\n", 1);
        ano_fs_sync(g_sink);
    }
    ano_mutex_unlock(&g_mtx);
}

int mtxlog_output_dir(const char *directoryPath)
{
    if (directoryPath == NULL || directoryPath[0] == '\0' || !atomic_load(&g_initialized))
        return -1;

    ano_file *sink = open_log(directoryPath);
    if (sink == NULL)
        return -1;                                // open failed, keep current sink

    ano_mutex_lock(&g_mtx);
    if (g_sink != NULL) {
        ano_fs_sync(g_sink);
        ano_fs_close(g_sink);
    }
    g_sink = sink;
    ano_mutex_unlock(&g_mtx);
    return 0;
}

void mtxlog_set_level(log_types_t min)
{
    if (!atomic_load(&g_initialized))
        return;
    ano_mutex_lock(&g_mtx);
    g_minLevel = (int)min;
    ano_mutex_unlock(&g_mtx);
}

void mtxlog_set_full_policy(mtxlog_full_policy_t policy)
{
    if (!atomic_load(&g_initialized))
        return;
    ano_mutex_lock(&g_mtx);
    g_fullPolicy = (int)policy;
    ano_mutex_unlock(&g_mtx);
}

void mtxlog_flush(void)
{
    if (atomic_load(&g_initialized))
        drain();
}

uint64_t mtxlog_dropped(void)
{
    if (!atomic_load(&g_initialized))
        return 0;
    ano_mutex_lock(&g_mtx);
    uint64_t n = g_dropped;
    ano_mutex_unlock(&g_mtx);
    return n;
}

int mtxlog_init(void)
{
    if (ano_mutex_init(&g_mtx, NULL) != 0)
        return -1;

    g_bufLen = 0;
    g_dropped = 0;
    g_sink = NULL;
    g_minLevel = LOG_DEBUG;
    g_fullPolicy = MTXLOG_FULL_IMMEDIATE;

    // Default sink is the game directory.
    filepath dir = ano_fs_gamepath();
    if (dir.pathString != NULL) {
        g_sink = open_log(dir.pathString);
        mi_free(dir.pathString);
    }

    atomic_store(&g_initialized, true);
    return 0;
}

int mtxlog_cleanup(void)
{
    if (!atomic_load(&g_initialized))
        return 0;

    atomic_store(&g_initialized, false);

    // Final drain, then close. No producers remain, so the lock is only for form.
    ano_mutex_lock(&g_mtx);
    flush_locked();
    if (g_sink != NULL) {
        ano_fs_sync(g_sink);
        ano_fs_close(g_sink);
        g_sink = NULL;
    }
    ano_mutex_unlock(&g_mtx);
    ano_mutex_destroy(&g_mtx);
    return 0;
}
