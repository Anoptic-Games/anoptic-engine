/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTICENGINE_ANOPTIC_FILEPATH_H
#define ANOPTICENGINE_ANOPTIC_FILEPATH_H

#include <stdint.h>

typedef struct {
    char* pathString;
    uint32_t length;
} filepath;

// Game Directory Path
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


// Helper Utils for parsing filepaths

#endif //ANOPTICENGINE_ANOPTIC_FILEPATH_H
