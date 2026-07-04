/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The logger follows a singleton pattern (one per program), owned by main. A producer formats its
// line on the calling thread and appends it to a shared ring; an owned consumer thread drains that
// ring to the output file continuously. ano_log_flush() drains inline for callers that need records
// on disk at once (e.g. per tick). Design: docs/logger.md.

#ifndef ANOPTIC_LOGGING_H
#define ANOPTIC_LOGGING_H

#include <stdint.h>

// Severity levels, ascending. The runtime gate (ano_log_set_level) admits >= its minimum. FATAL
// takes the immediate path, which echoes > LOG_WARN to stderr and the rest to stdout.
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_types_t;

// Full ring: a producer whose reservation would overrun an undrained record waits for the owned consumer
// to free room. No loss, self-throttling to the drain rate.

// Lifecycle. ano_log_init opens the default output file (the game directory); until it returns 0, only
// the immediate (stderr) path works. ano_log_cleanup drains any buffered records, then syncs and closes
// the output file. Both return 0 on success. Single-owner teardown: all producer threads must have stopped
// before ano_log_cleanup is called -- calling any ano_log_* concurrently with cleanup is undefined.
int ano_log_init(void);     // Build up, singleton initialization.
int ano_log_cleanup(void);  // Teardown, most likely at program exit.

// Scope-bound teardown, LOCALHEAPATTR-style (anoptic_memory.h). 
// Usage: int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
void ano_log_scope_release(const int *initStatus);
#define ANO_LOG_SCOPE_ATTR __attribute__((__cleanup__(ano_log_scope_release))) 

// The usual method: format {level, file, line, fmt, ...} into one line on the calling thread, copy
// it into the ring, and publish with one release store.
// Returns: 0 enqueued; 1 the ring was full, so this thread waited for the consumer to free room before
// buffering this line (self-throttling to the drain rate, never dropping).
// printFormat MUST be a string literal -- the format attribute checks the args against it.
int ano_log_enqueue(log_types_t log_type, const char* sourceFile, int lineNumber,
                    const char* printFormat, ...) __attribute__((format(printf, 4, 5)));

// Should report the event immediately: format, prepend wall-clock time, write then fsync, all on
// the calling thread. Bypasses the ring without consuming it (single-consumer preserved).
void ano_log_immediate(log_types_t log_type, const char* sourceFile, int lineNumber,
                       const char* printFormat, ...) __attribute__((format(printf, 4, 5)));

// Open dir/anoptic.log as the output file (a per-run UTC-stamped name is deferred). Returns 0 on
// success; -1 if the directory is invalid or could not be opened, in which case a previously open
// output file is kept. With no output file set, records still drain to console.
int ano_log_output_dir(const char* directoryPath);

// Runtime severity gate: enqueues below min are refused with one relaxed load.
void ano_log_set_level(log_types_t min);

// Drain all buffered records to the output file, synchronously on the calling thread. The owned consumer
// thread already drains continuously; call this when you need records on disk now (e.g. once per tick).
// ano_log_cleanup drains a final time, and a full ring makes producers wait for room, so a missed flush
// delays records but never loses them.
void ano_log_flush(void);


// Call-site macros: pass level + __FILE_NAME__ + __LINE__ + fmt straight through. The first three
// are compile-time constants; the line is formatted eagerly inside the call. fmt MUST be a string
// literal. ano_log_debug / _debug_now compile to nothing outside DEBUG_BUILD.
/// TODO: Make sure that the __
#ifdef DEBUG_BUILD
#define ano_log_debug(...)      ano_log_enqueue(LOG_DEBUG, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_debug_now(...)  ano_log_immediate(LOG_DEBUG, __FILE_NAME__, __LINE__, __VA_ARGS__)
#else
#define ano_log_debug(...)      /* Debug logging disabled */
#define ano_log_debug_now(...)  /* Debug logging disabled */
#endif

#define ano_log_info(...)   ano_log_enqueue(LOG_INFO, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_warn(...)   ano_log_enqueue(LOG_WARN, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_error(...)  ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_fatal(...)  ano_log_immediate(LOG_FATAL, __FILE_NAME__, __LINE__, __VA_ARGS__)

#endif // ANOPTIC_LOGGING_H