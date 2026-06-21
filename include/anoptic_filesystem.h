/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_FILEPATH_H
#define ANOPTICENGINE_ANOPTIC_FILEPATH_H

#include <stdint.h>
#include <stdbool.h>

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

#endif //ANOPTICENGINE_ANOPTIC_FILEPATH_H
