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

typedef struct {
    uint16_t length;
    char* pathString; // Managed by user.
} filepath; // User is expected to manage the lifetime of this struct.

// Game Executable Directory Path 
// - Directory the executable is in.
// - Assets, binaries, expansions.
// - Thread-safe: computed fresh per call with no shared state 
// - (the implementation avoids non-reentrant dirname()), may be called from any thread.
// Input: none.
// Output: filepath whose pathString is heap for the caller to free;
//      {length 0, pathString NULL} if the path could not be resolved.
filepath ano_fs_gamepath();


// Save Data Path
// - User Profiles
// - Savegames
// - Settings
// - Log Files
// Input: none.
// Output: filepath whose pathString is heap for the caller to free;
//      {length 0, pathString NULL} if the path could not be resolved.
filepath ano_fs_userpath();

// Change Working Directory to Game Executable
// Input: none.
// Output: true on success, false if path or chdir could not be resolved.
bool ano_fs_chdir_gamepath(void);


// Append-only file sink. Opaque handle: the platform struct (a POSIX fd, a Win32 HANDLE)
// lives in the per-OS source, so callers never see the underlying descriptor. One handle is
// meant to be opened once and written many times -- the logger's batched flush is the first user.
typedef struct ano_file ano_file;

// Open (creating if absent) `path` for appending. Concurrent appends from one process do not
// interleave (O_APPEND / FILE_APPEND_DATA).
// Input: NUL-terminated path. Output: handle, or NULL on failure.
ano_file *ano_fs_open_append(const char *path);

// Write all `length` bytes, looping past short writes.
// Input: open handle, buffer, byte count. Output: 0 on success, -1 on error.
int ano_fs_write(ano_file *file, const void *data, size_t length);

// Flush this handle's buffered data to the storage device (fsync / FlushFileBuffers).
// Input: open handle. Output: 0 on success, -1 on error.
int ano_fs_sync(ano_file *file);

// Close the handle and free it. Does not sync first -- call ano_fs_sync if durability is needed.
// Input: open handle. Output: 0 on success, -1 on error (the handle is freed regardless).
int ano_fs_close(ano_file *file);

#endif //ANOPTICENGINE_ANOPTIC_FILEPATH_H
