/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_FILEPATH_H
#define ANOPTICENGINE_ANOPTIC_FILEPATH_H

#include <stdint.h>

#define MAXPATH 256

typedef struct {
    uint16_t length;
    char* pathString; // Managed by user.
} filepath; // User is expected to manage the lifetime of this struct.

// Game Executable Directory Path
// - Assets
// - Binaries
// - Expansions
filepath ano_fs_gamepath();


// Save Data Path
// - User Profiles
// - Savegames
// - Settings
// - Log Files
filepath ano_fs_userpath();

#endif //ANOPTICENGINE_ANOPTIC_FILEPATH_H
