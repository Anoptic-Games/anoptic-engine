/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Lock-free MPSC logger: producers capture/format off-ring, publish into a shared ring, one owned consumer drains.
// ano_log_flush drains inline on the caller. NOW drains then write-through (+ fsync when a file is open).
// Four macros over ano_log_write. Severity = how bad, route = where/when.
//   ano_log(ANO_WARN, "fmt %d", x);                        level's default route
//   ano_rlog(ANO_ERROR, ANO_TERM | ANO_NOW, "fmt %d", x);  explicit route
//   ano_debug_log(...) / ano_debug_rlog(...)               Debug builds only
// ANO_ prefix is load-bearing (windows.h ERROR, stdio FILE).

#ifndef ANOPTIC_LOG_H
#define ANOPTIC_LOG_H

#include <stdarg.h>


/* Types */

// Severity ascending.
typedef enum {
    ANO_INFO = 0,
    ANO_WARN,
    ANO_ERROR,
    ANO_FATAL
} ano_loglevel_t;

// Route: when-where a record lands.
typedef enum {
    ANO_FILE = 1 << 0,              // output file (terminal when none open)
    ANO_TERM = 1 << 1,              // stdout, ERROR+ to stderr, ANSI on tty
    ANO_BOTH = ANO_FILE | ANO_TERM,
    ANO_NOW  = 1 << 2,              // sync: drain, write-through, fsync if file open
} ano_logroute_t;


/* Lifecycle Functions */

// Startup / shutdown. 0 on success.
int ano_log_init(void);
int ano_log_cleanup(void);

// Scope-bound teardown, LOCALHEAPATTR-style (anoptic_memory.h).
void ano_log_scope_release(const int *initStatus);
#define ANO_LOG_SCOPE_ATTR __attribute__((__cleanup__(ano_log_scope_release)))

/* Entry Points */

int ano_log_write(ano_loglevel_t level, ano_logroute_t route,
                  const char* sourceFile, int lineNumber,
                  /* printFormat MUST be a string literal. */
                  const char* printFormat, ...) __attribute__((format(printf, 5, 6)));

// va_list variant for wrappers.
int ano_log_vwrite(ano_loglevel_t level, ano_logroute_t route,
                   const char* sourceFile, int lineNumber,
                   const char* printFormat, va_list args) __attribute__((format(printf, 5, 0)));


/* Configuration Functions */

// Open dir/<session-stamp>_ano.log (stamp: ano_fs_session_stamp). 0 ok, -1 keeps previous.
int ano_log_output_dir(const char* directoryPath);

// Runtime severity gate.
void ano_log_set_level(ano_loglevel_t min);

// Replace a level's default route. Must name a sink. Out-of-range ignored.
void ano_log_set_route(ano_loglevel_t level, ano_logroute_t route);

// Drain all buffered records on the calling thread.
void ano_log_flush(void);


/* Call-site Macros */

// _log  : level
// _rlog : level + route
// _olog : level + callsite file/line
// _rolog: level + route + callsite
#define ano_log(level, ...)                 ano_log_write((level), 0, NULL, 0, __VA_ARGS__)
#define ano_rlog(level, route, ...)         ano_log_write((level), (route), NULL, 0, __VA_ARGS__)
#define ano_olog(level, ...)                ano_log_write((level), 0, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_rolog(level, route, ...)        ano_log_write((level), (route), __FILE_NAME__, __LINE__, __VA_ARGS__)

#ifdef DEBUG_BUILD
#define ano_debug_log(level, ...)           ano_log_write((level), 0, NULL, 0, __VA_ARGS__)
#define ano_debug_rlog(level, route, ...)   ano_log_write((level), (route), NULL, 0, __VA_ARGS__)
#define ano_debug_olog(level, ...)          ano_log_write((level), 0, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_debug_rolog(level, route, ...)  ano_log_write((level), (route), __FILE_NAME__, __LINE__, __VA_ARGS__)
#else
#define ano_debug_log(...)  ((void)0)
#define ano_debug_rlog(...) ((void)0)
#define ano_debug_olog(...) ((void)0)
#define ano_debug_rolog(...)((void)0)
#endif

#endif // ANOPTIC_LOG_H
