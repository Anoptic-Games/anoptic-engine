/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "anoptic_filesystem.h"

#include <stdio.h>
#include <stdlib.h>       // malloc
#include <string.h>       // strcpy, strlen
#include <direct.h>       // _chdir
#include <libloaderapi.h>
#include <mimalloc.h>

// Backs the user-data stub below; the game path is queried fresh per call.
static filepath game_user_path;

// Windows paths are UTF-16 underneath; GetModuleFileNameA returns the ANSI form.
// Output: directory of the running executable (no file name); pathString is
// mi_malloc'd for the caller to free. {0, NULL} on failure.
filepath ano_fs_gamepath() {

    filepath result = {.length = 0, .pathString = NULL};

    char pathBuffer[MAX_PATH];
    DWORD len = GetModuleFileName(NULL, pathBuffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return result; // failed, or truncated (path length >= MAX_PATH)

    // Trim the executable file name, leaving its containing directory.
    while (len > 0 && pathBuffer[len - 1] != '\\' && pathBuffer[len - 1] != '/')
        len--;
    if (len > 0)
        len--; // drop the trailing separator

    result.pathString = mi_malloc((size_t)len + 1);
    if (result.pathString == NULL)
        return result;
    memcpy(result.pathString, pathBuffer, len);
    result.pathString[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}


// This is meant to be a persisting user directory. 
// Eg: "C:\Users\Pyrus\Documents\anoptic" or "...\My Games\anoptic", etc.
filepath ano_fs_userpath() {

    // Right now it's a stub that harmlessly does nothing.
    filepath result = {.pathString = malloc(game_user_path.length + 1), .length = game_user_path.length};
    strcpy(result.pathString, game_user_path.pathString);
    return result;
}

// Inputs: none. Output: bool, true on success.
// Points the working directory at the directory holding the running executable so
// CWD-relative asset loads resolve regardless of where the binary was launched from.
bool ano_fs_chdir_gamepath(void)
{
    filepath dir = ano_fs_gamepath();
    if (dir.pathString == NULL)
        return false;
    bool ok = dir.length > 0 && _chdir(dir.pathString) == 0;
    mi_free(dir.pathString);
    return ok;
}