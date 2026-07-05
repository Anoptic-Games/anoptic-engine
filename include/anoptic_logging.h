/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Lock-free MPSC logger, one per program, owned by main. Producers format on their own threads
// into a shared ring. An owned consumer thread drains it continuously. Design: docs/logger.md.
//
// Four macros over one function. Severity says how bad. Route says where and when.
//   ano_log(ANO_WARN, "fmt %d", x);                        the level's default route
//   ano_rlog(ANO_ERROR, ANO_TERM | ANO_NOW, "fmt %d", x);  explicit route
//   ano_debug_log(...) / ano_debug_rlog(...)               same, Debug builds only (conventions.md)
//
// The ANO_ prefix is load-bearing. Bare ERROR is a windows.h macro, bare FILE a stdio typedef.

#ifndef ANOPTIC_LOGGING_H
#define ANOPTIC_LOGGING_H

#include <stdarg.h>


/* Types */

// Severity in ascending order.
typedef enum {
    ANO_INFO = 0,
    ANO_WARN,
    ANO_ERROR,
    ANO_FATAL
} ano_loglevel_t;

// Route: when-where a record lands.
typedef enum {
    ANO_FILE = 1 << 0,              // the output file (terminal when none is open)
    ANO_TERM = 1 << 1,              // the terminal: stdout, ERROR+ to stderr, ANSI-colored on a tty
    ANO_BOTH = ANO_FILE | ANO_TERM,
    ANO_NOW  = 1 << 2,              // synchronous: drain, write, fsync on this thread
} ano_logroute_t;


/* Lifecycle Functions */

// Startup and Shutdown. Both return 0 on success.
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

// va_list variant, for wrappers forwarding their own variadic args.
int ano_log_vwrite(ano_loglevel_t level, ano_logroute_t route,
                   const char* sourceFile, int lineNumber,
                   const char* printFormat, va_list args) __attribute__((format(printf, 5, 0)));


/* Configuration Functions */

// Open dir/anoptic.log as the output file. Returns 0 on success; -1 keeps the previous file.
int ano_log_output_dir(const char* directoryPath);

// Runtime severity gate.
void ano_log_set_level(ano_loglevel_t min);

// Replace a level's default route. Must name a sink. Out-of-range levels are ignored.
void ano_log_set_route(ano_loglevel_t level, ano_logroute_t route);

// Drain all buffered records synchronously on the calling thread.
void ano_log_flush(void);


/* Call-site Macros */

// _log : normal log, specify level.
// _rlog: routed log, specify level and preferred route (output destinations).
// _olog: origin log, specify level, includes callsite's source file name and line number.
// _rolog: routed origin log, specify level, route, and includes callsite details as like olog.
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

#endif // ANOPTIC_LOGGING_H
