/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__linux__)

#include "anoptic_filesystem.h"
#include "filesystem/filesystem_internal.h"

#include <unistd.h>     // readlink, chdir, write, fsync, close
#include <stdio.h>      // snprintf
#include <stdlib.h>     // getenv
#include <string.h>     // strlen, memcpy
#include <limits.h>     // PATH_MAX
#include <fcntl.h>      // open, O_*
#include <sys/stat.h>   // mkdir
#include <errno.h>      // errno, EINTR, EEXIST
#include <mimalloc.h>


/* Paths */

// readlink("/proc/self/exe"). Output: executable directory, no file name, by value.
// length == 0 on failure or path exceeding MAXPATH - 1.
// Hand-rolled split: dirname() is not portably reentrant.
ano_fspath ano_fs_gamepath(void)
{
    ano_fspath result = {0};

    char raw[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1);
    if (n <= 0)
        return result;
    raw[n] = '\0'; // readlink does not NUL-terminate

    // Trim to containing directory: drop after last '/'.
    size_t len = (size_t)n;
    while (len > 0 && raw[len - 1] != '/')
        len--;
    if (len > 1)
        len--; // drop trailing slash, keep "/" for root

    if (len >= MAXPATH)
        return result; // exceeds value type
    memcpy(result.str, raw, len);
    result.str[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

// ~/.anoptic, created if absent.
ano_fspath ano_fs_userpath(void)
{
    ano_fspath result = {0};

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        return result;

    int len = snprintf(result.str, MAXPATH, "%s/." ANO_GAME_NAME, home);
    if (len < 0 || len >= MAXPATH)
        return (ano_fspath){0};

    if (mkdir(result.str, 0755) != 0 && errno != EEXIST)
        return (ano_fspath){0};

    result.length = (uint16_t)len;
    return result;
}

// Sets CWD to ano_fs_gamepath() so relative asset loads resolve. Output: true on success.
bool ano_fs_chdir_gamepath(void)
{
    ano_fspath dir = ano_fs_gamepath();
    return dir.length > 0 && chdir(dir.str) == 0;
}

// Output: 0 on success or EEXIST, -1 on failure.
int fs_mkdir(const char *path)
{
    return (mkdir(path, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}


/* Append-Only File */

// Opaque handle wraps a single file descriptor.
struct ano_file {
    int fd;
};

// Output: O_APPEND handle, or NULL on failure.
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

// Output: O_APPEND handle after O_TRUNC, or NULL on failure.
ano_file *ano_fs_open_trunc(const char *path)
{
    if (path == NULL)
        return NULL;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
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

// Output: 0 once all bytes written, -1 on error. Loops past short writes and EINTR.
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
                continue; // EINTR: retry
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

// Output: 0 on success, -1 on error. Handle freed either way.
int ano_fs_close(ano_file *file)
{
    if (file == NULL)
        return -1;
    int rc = close(file->fd) == 0 ? 0 : -1;
    mi_free(file);
    return rc;
}

#endif // __linux__
