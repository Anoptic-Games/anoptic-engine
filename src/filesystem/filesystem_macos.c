/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__APPLE__)

#include "anoptic_filesystem.h"

#include <mach-o/dyld.h>   // _NSGetExecutablePath
#include <unistd.h>        // chdir, write, fsync, close
#include <stdlib.h>        // realpath
#include <string.h>        // strlen, memcpy
#include <limits.h>        // PATH_MAX
#include <fcntl.h>         // open, O_*
#include <errno.h>         // errno, EINTR
#include <mimalloc.h>

// Output: directory of the running executable, no file name.
// pathString is mi_malloc'd for the caller to free. {0, NULL} on failure.
// The split is hand-rolled because dirname() is not portably reentrant.
filepath ano_fs_gamepath()
{
    filepath result = { .length = 0, .pathString = NULL };

    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0)
        return result; // PATH_MAX too small for the executable path

    char resolved[PATH_MAX];
    if (realpath(raw, resolved) == NULL)
        return result; // could not canonicalize (symlinks, '.', '..')

    // Trim to the containing directory: drop everything after the last '/'.
    size_t len = strlen(resolved);
    while (len > 0 && resolved[len - 1] != '/')
        len--;
    if (len > 1)
        len--; // drop the trailing slash -- but keep "/" for the root

    result.pathString = mi_malloc(len + 1);
    if (result.pathString == NULL)
        return result;
    memcpy(result.pathString, resolved, len);
    result.pathString[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

// Output: true on success. Sets CWD to ano_fs_gamepath() so relative asset loads
// resolve regardless of launch directory.
bool ano_fs_chdir_gamepath(void)
{
    filepath dir = ano_fs_gamepath();
    if (dir.pathString == NULL)
        return false;
    bool ok = dir.length > 0 && chdir(dir.pathString) == 0;
    mi_free(dir.pathString);
    return ok;
}


/* Append-only file sink (POSIX). The opaque handle wraps a single file descriptor. */

struct ano_file {
    int fd;
};

// Output: handle opened O_APPEND, or NULL on failure (bad path, open error, OOM).
ano_file *ano_fs_open_append(const char *path)
{
    if (path == NULL)
        return NULL;

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return NULL;

    ano_file *file = mi_malloc(sizeof *file);
    if (file == NULL) {
        close(fd);
        return NULL;
    }
    file->fd = fd;
    return file;
}

// Output: 0 once all bytes are written, -1 on error. Loops past partial writes and EINTR.
int ano_fs_write(ano_file *file, const void *data, size_t length)
{
    if (file == NULL || (data == NULL && length != 0))
        return -1;

    const char *cursor = data;
    size_t remaining = length;
    while (remaining > 0) {
        ssize_t written = write(file->fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR)
                continue; // interrupted before any byte moved -- retry
            return -1;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return 0;
}

// Output: 0 on success, -1 on error.
int ano_fs_sync(ano_file *file)
{
    if (file == NULL)
        return -1;
    return fsync(file->fd) == 0 ? 0 : -1;
}

// Output: 0 on success, -1 on error. The handle is freed either way.
int ano_fs_close(ano_file *file)
{
    if (file == NULL)
        return -1;
    int rc = close(file->fd) == 0 ? 0 : -1;
    mi_free(file);
    return rc;
}

#endif // __APPLE__
