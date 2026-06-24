/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The logger follows a singleton pattern (one per program), owned by main. A producer formats its
// line on the calling thread and appends it to a shared buffer; the caller drains that buffer to
// the sink on its own schedule via ano_log_flush() (the logger runs no background thread).
// Design: docs/logger.md.

#ifndef ANOPTIC_LOGGER_H
#define ANOPTIC_LOGGER_H

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

// Policy when a producer's reservation would overrun an undrained record (a full ring).
// IMMEDIATE (default): write the finished line on the calling thread -- no loss, self-throttling.
// DROP_NEWEST: discard it and count it (ano_log_dropped). BLOCK: spin until space; debug only.
typedef enum {
    ANO_LOG_FULL_IMMEDIATE = 0,
    ANO_LOG_DROP_NEWEST    = 1,
    ANO_LOG_BLOCK          = 2
} ano_log_full_policy_t;

// Lifecycle. ano_log_init opens the default sink (the game directory); until it returns 0, only the
// immediate (stderr) path works. ano_log_cleanup drains any buffered records, then syncs and closes
// the sink. Both return 0 on success. Single-owner teardown: all producer threads must have stopped
// before ano_log_cleanup is called -- calling any ano_log_* concurrently with cleanup is undefined.
int ano_log_init(void);     // Build up, singleton initialization.
int ano_log_cleanup(void);  // Teardown, most likely at program exit.

// The usual method: format {level, file, line, fmt, ...} into one line on the calling thread, copy
// it into the ring, and publish with one release store. Never waits on another thread.
// Returns: 0 enqueued; 1 written immediately (full policy); -1 dropped (DROP_NEWEST).
// printFormat MUST be a string literal -- the format attribute checks the args against it.
int ano_log_enqueue(log_types_t log_type, const char* sourceFile, int lineNumber,
                    const char* printFormat, ...) __attribute__((format(printf, 4, 5)));

// Should report the event immediately: format, prepend wall-clock time, write then fsync, all on
// the calling thread. Bypasses the ring without consuming it (single-consumer preserved).
void ano_log_immediate(log_types_t log_type, const char* sourceFile, int lineNumber,
                       const char* printFormat, ...) __attribute__((format(printf, 4, 5)));

// Open dir/anoptic.log as the file sink (a per-run UTC-stamped name is deferred). Returns 0 on
// success; -1 if the directory is invalid or could not be opened, in which case a previously open
// sink is kept. With no sink set, records still drain to console.
// Should probably use a filepath type from anoptic_filesystem.h
int ano_log_output_dir(const char* directoryPath);

// Runtime severity gate: enqueues below min are refused with one relaxed load.
void ano_log_set_level(log_types_t min);

// Select the full-ring policy (default ANO_LOG_FULL_IMMEDIATE).
void ano_log_set_full_policy(ano_log_full_policy_t policy);

// Drain all buffered records to the sink, synchronously on the calling thread. This is the only
// flush mechanism: call it on your own schedule (e.g. once per tick). ano_log_cleanup drains a
// final time, and a full buffer self-drains via the full policy, so a missed flush delays records
// but never loses them.
void ano_log_flush(void);

// Count of records discarded under DROP_NEWEST since init.
uint64_t ano_log_dropped(void);


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

#endif // ANOPTIC_LOGGER_H