/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)

#include "anoptic_filesystem.h"

#include <stdio.h>
#include <stdlib.h>       // getenv
#include <string.h>       // memcpy
#include <direct.h>       // _chdir, _mkdir
#include <errno.h>        // errno, EEXIST
#include <windows.h>      // CreateFileA, WriteFile, FlushFileBuffers, CloseHandle
#include <libloaderapi.h>
#include <mimalloc.h>

// Windows paths are UTF-16 underneath -- GetModuleFileNameA returns the ANSI form.
// Output: directory of the running executable, no file name, by value.
// length == 0 on failure or a path that does not fit MAXPATH - 1.
ano_fspath ano_fs_gamepath(void) {

    ano_fspath result = {0};

    char pathBuffer[MAX_PATH];
    DWORD len = GetModuleFileName(NULL, pathBuffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH || len >= MAXPATH)
        return result; // failed or truncated

    // Trim the executable file name, leaving its containing directory.
    while (len > 0 && pathBuffer[len - 1] != '\\' && pathBuffer[len - 1] != '/')
        len--;
    if (len > 0)
        len--; // drop the trailing separator

    memcpy(result.str, pathBuffer, len);
    result.str[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

// %APPDATA%\anoptic, created if absent (the Factorio convention). %APPDATA% is the roaming
// user-data root and is set for every interactive session; no shell32 KnownFolder call needed.
ano_fspath ano_fs_userpath(void) {
    ano_fspath result = {0};

    const char *appdata = getenv("APPDATA");
    if (appdata == NULL || appdata[0] == '\0')
        return result;

    int len = snprintf(result.str, MAXPATH, "%s\\" ANO_GAME_NAME, appdata);
    if (len < 0 || len >= MAXPATH)
        return (ano_fspath){0};

    if (_mkdir(result.str) != 0 && errno != EEXIST)
        return (ano_fspath){0};

    result.length = (uint16_t)len;
    return result;
}

// Input: none. Output: true on success.
// Sets CWD to ano_fs_gamepath() so relative asset loads resolve regardless of launch directory.
bool ano_fs_chdir_gamepath(void)
{
    ano_fspath dir = ano_fs_gamepath();
    return dir.length > 0 && _chdir(dir.str) == 0;
}


/* Append-only file sink (Win32). The opaque handle wraps a single file HANDLE. */

struct ano_file {
    HANDLE handle;
};

// Output: handle opened FILE_APPEND_DATA, or NULL on failure. OPEN_ALWAYS creates if absent.
// FILE_SHARE_DELETE gives POSIX unlink parity: another process (or a test) may remove/replace
// the log file while we hold it open, as it can on Linux/macOS.
ano_file *ano_fs_open_append(const char *path)
{
    if (path == NULL)
        return NULL;

    HANDLE handle = CreateFileA(path, FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
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

// Output: 0 once all bytes are written, -1 on error. Chunks past the DWORD count limit.
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