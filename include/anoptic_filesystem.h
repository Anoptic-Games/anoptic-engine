/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_FILEPATH_H
#define ANOPTICENGINE_ANOPTIC_FILEPATH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // size_t


/* Paths */

#define MAXPATH 256

// Game dir name under the platform user-data root.
// Windows: `%APPDATA%\ANO_GAME_NAME`  Linux: `~/.ANO_GAME_NAME`  macOS: `~/Library/Application Support/ANO_GAME_NAME`
// Callers lay out Config/, Saves/, etc. inside it.
#define ANO_GAME_NAME "anoptic"

// Path value: NUL-terminated str, length = bytes before NUL. length == 0 = unresolved.
// Borrow for strings: anostr_view(p.str, p.length), valid while p lives.
typedef struct {
    uint16_t length;
    char str[MAXPATH];
} ano_fspath;

// Executable directory: no file name, no trailing separator (kept for drive/FS root). Thread-safe.
// Output: path by value. length == 0 if unresolved or exceeds MAXPATH - 1.
ano_fspath ano_fs_gamepath(void);

// User data path (profiles, saves, settings). Creates if absent. Thread-safe.
// Output: path by value. length == 0 if unresolved or mkdir failed.
// Non-empty result is ready to write into.
ano_fspath ano_fs_userpath(void);

// Log directory: "<gamepath>/logs". Creates if absent. Thread-safe.
// Output: path by value. length == 0 if unresolved or mkdir failed.
ano_fspath ano_fs_logpath(void);

// Session stamp: "YYYY-MM-DD_XXXXXX", latched at first call. Thread-safe.
// Output: NUL-terminated stamp, static storage.
const char *ano_fs_session_stamp(void);

// Set CWD to the executable directory.
// Output: true on success, false if path or chdir failed.
bool ano_fs_chdir_gamepath(void);


/* Append-Only File */

// Opaque handle: platform fd/HANDLE stays in the per-OS source.
typedef struct ano_file ano_file;

// Open `path` for append (OS append mode), create if absent.
// Input: NUL-terminated path. Output: handle, or NULL on failure.
ano_file *ano_fs_open_append(const char *path);

// Open `path` for append after truncate to zero, create if absent.
// Input: NUL-terminated path. Output: handle, or NULL on failure.
ano_file *ano_fs_open_trunc(const char *path);

// Write all `length` bytes, looping past short writes.
// Input: open handle, buffer, byte count. Output: 0 on success, -1 on error.
int ano_fs_write(ano_file *file, const void *data, size_t length);

// Flush buffered data to the device (fsync / FlushFileBuffers).
// Input: open handle. Output: 0 on success, -1 on error.
int ano_fs_sync(ano_file *file);

// Close and free without sync. Call ano_fs_sync for durability.
// Input: open handle. Output: 0 on success, -1 on error. Handle freed either way.
int ano_fs_close(ano_file *file);

#endif // ANOPTICENGINE_ANOPTIC_FILEPATH_H
