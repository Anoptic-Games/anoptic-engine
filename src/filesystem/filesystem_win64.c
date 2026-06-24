/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)

#include "anoptic_filesystem.h"

#include <stdio.h>
#include <stdlib.h>       // malloc
#include <string.h>       // memcpy
#include <direct.h>       // _chdir
#include <windows.h>      // CreateFileA, WriteFile, FlushFileBuffers, CloseHandle
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

    // Still a stub: no user directory is resolved yet. Return the {0, NULL} "unresolved" form the
    // other resolvers use rather than copying from the unset static (which would deref NULL).
    filepath result = {.length = 0, .pathString = NULL};
    if (game_user_path.pathString == NULL)
        return result;

    // mi_malloc to match ano_fs_gamepath and the mi_free the caller is expected to use.
    result.pathString = mi_malloc((size_t)game_user_path.length + 1);
    if (result.pathString == NULL)
        return result; // {0, NULL} on OOM
    memcpy(result.pathString, game_user_path.pathString, (size_t)game_user_path.length + 1);
    result.length = game_user_path.length;
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


/* Append-only file sink (Win32). The opaque handle wraps a single file HANDLE. */

struct ano_file {
    HANDLE handle;
};

// Output: handle opened FILE_APPEND_DATA, or NULL on failure. OPEN_ALWAYS creates if absent.
ano_file *ano_fs_open_append(const char *path)
{
    if (path == NULL)
        return NULL;

    HANDLE handle = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;

    ano_file *file = mi_malloc(sizeof *file);
    if (file == NULL) {
        CloseHandle(handle);
        return NULL;
    }
    file->handle = handle;
    return file;
}

// Output: 0 once all bytes are written; -1 on error. Chunks past the DWORD count limit.
int ano_fs_write(ano_file *file, const void *data, size_t length)
{
    if (file == NULL || (data == NULL && length != 0))
        return -1;

    const char *cursor = data;
    size_t remaining = length;
    while (remaining > 0) {
        DWORD chunk = remaining > 0x7fffffff ? 0x7fffffff : (DWORD)remaining;
        DWORD written = 0;
        if (!WriteFile(file->handle, cursor, chunk, &written, NULL))
            return -1;
        cursor += written;
        remaining -= written;
    }
    return 0;
}

// Output: 0 on success, -1 on error.
int ano_fs_sync(ano_file *file)
{
    if (file == NULL)
        return -1;
    return FlushFileBuffers(file->handle) ? 0 : -1;
}

// Output: 0 on success, -1 on error. The handle is freed either way.
int ano_fs_close(ano_file *file)
{
    if (file == NULL)
        return -1;
    int rc = CloseHandle(file->handle) ? 0 : -1;
    mi_free(file);
    return rc;
}

#endif // _WIN32