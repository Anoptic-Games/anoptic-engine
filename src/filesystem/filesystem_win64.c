/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "anoptic_filesystem.h"

#include <stdio.h>
#include <libloaderapi.h>
#include <mimalloc.h>

// Cache the paths.
static filepath game_root_path;
static filepath game_user_path;

// Windows Paths are in UTF-16 -- might need to use wchar storage
// Linux Paths are arbitrary binary blobs that are usually parsed as UTF-8
filepath ano_fs_gamepath() {

    // Return cached path value, if it exists.
    if (game_root_path.length != 0) {
        filepath result = {.pathString = mi_malloc(game_root_path.length + 1), .length = game_root_path.length};
        strcpy(result.pathString, game_root_path.pathString);
        return result;
    }

    // Cache the path and return its values.
    char pathBuffer[MAX_PATH];
    GetModuleFileName(NULL, pathBuffer, MAX_PATH - 1);

    int pathLen = strlen(pathBuffer);
    game_root_path.pathString = mi_malloc(pathLen + 1);
    game_root_path.length = pathLen;
    strcpy(game_root_path.pathString, pathBuffer);

    return ano_fs_gamepath(); // XD !?
}

filepath ano_fs_userpath() {

    filepath result = {.pathString = malloc(game_user_path.length + 1), .length = game_user_path.length};
    strcpy(result.pathString, game_user_path.pathString);
    return result;
}