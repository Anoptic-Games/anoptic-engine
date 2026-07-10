/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_FILEPATH_H
#define ANOPTICENGINE_ANOPTIC_FILEPATH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // size_t

#define MAXPATH 256

// The game's directory name inside the platform's idiomatic user-data location.
// Windows: `%APPDATA%\ANO_GAME_NAME`  Linux: `~/.ANO_GAME_NAME`  macOS: `~/Library/Application Support/ANO_GAME_NAME`
// Callers lay out Config/, Saves/, etc. inside it.
#define ANO_GAME_NAME "anoptic"

// A filesystem path as a value. 
// str is always NUL-terminated (hand it to syscalls and fopen directly); 
// length counts the bytes before the NUL. length == 0 means the path could not be resolved.
// For string manipulation, borrow it: anostr_view(p.str, p.length), valid while p lives.
typedef struct {
    uint16_t length;
    char str[MAXPATH];
} ano_fspath;

// Game Executable Directory Path
// - Directory the executable is in, no file name, no trailing separator.
// - Assets, binaries, expansions.
// - Thread-safe -- computed fresh per call, no shared state, no dirname().
// Output: the path by value; length == 0 if it could not be resolved or exceeds MAXPATH - 1.
ano_fspath ano_fs_gamepath(void);

// Save Data Path
// - User Profiles
// - Savegames
// - Settings
// - Log Files
// Creates the directory if absent, so a non-empty result is ready to write into.
// Thread-safe.
// Output: the path by value
// length == 0 if the user-data root could not be resolved or the directory could not be created.
ano_fspath ano_fs_userpath(void);

// Log Directory Path
// - "<gamepath>/logs", home of the session log files (<stamp>_ano.log, <stamp>_CRASH.log).
// Creates the directory if absent.
// Thread-safe.
// Output: the path by value. length == 0 if it could not be resolved or created.
ano_fspath ano_fs_logpath(void);

// Session Stamp
// - "YYYY-MM-DD_XXXXXX": local date + low six digits of the raw tick counter, latched at first call.
// - Names this session's files.
// Thread-safe.
// Output: the stamp, NUL-terminated, static storage.
const char *ano_fs_session_stamp(void);

// Change Working Directory to Game Executable
// Input: none.
// Output: true on success, false if path or chdir could not be resolved.
bool ano_fs_chdir_gamepath(void);


// Append-only output file. Opaque handle -- the platform struct (a POSIX fd, a Win32 HANDLE)
// lives in the per-OS source, so callers never see the descriptor. Open once, write many.
// The logger's batched flush is the first user.
typedef struct ano_file ano_file;

// Open `path` for appending, creating if absent. Concurrent appends do not interleave.
// Input: NUL-terminated path. Output: handle, or NULL on failure.
ano_file *ano_fs_open_append(const char *path);

// Write all `length` bytes, looping past short writes.
// Input: open handle, buffer, byte count. Output: 0 on success, -1 on error.
int ano_fs_write(ano_file *file, const void *data, size_t length);

// Flush this handle's buffered data to the device (fsync / FlushFileBuffers).
// Input: open handle. Output: 0 on success, -1 on error.
int ano_fs_sync(ano_file *file);

// Close the handle and free it. Does not sync first -- call ano_fs_sync for durability.
// Input: open handle. Output: 0 on success, -1 on error -- the handle is freed regardless.
int ano_fs_close(ano_file *file);

#endif //ANOPTICENGINE_ANOPTIC_FILEPATH_H
