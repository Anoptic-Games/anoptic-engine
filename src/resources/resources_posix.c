/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// POSIX half of resources_os.h, shared by Linux and macOS.

#if defined(__linux__) || defined(__APPLE__)

#include "resources_os.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>      // rename
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool rmos_exists(const char *abs)
{
    int fd = open(abs, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    // A directory is not a resource.
    struct stat st;
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
    close(fd);
    return ok;
}

int rmos_read_open(const char *abs, rmos_file *out)
{
    int fd = open(abs, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }
    out->h = fd;
    return 0;
}

int64_t rmos_size_hint(rmos_file f)
{
    struct stat st;
    if (fstat((int)f.h, &st) != 0 || st.st_size < 0)
        return -1;
    return (int64_t)st.st_size;
}

int rmos_read_chunk(rmos_file f, void *buf, size_t cap, size_t *got)
{
    *got = 0;
    for (;;) {
        ssize_t n = read((int)f.h, buf, cap);
        if (n >= 0) {
            *got = (size_t)n;
            return 0;
        }
        if (errno != EINTR)
            return -1;
    }
}

void rmos_read_close(rmos_file f)
{
    close((int)f.h);
}

int rmos_mkdir_p(char *abs)
{
    if (abs == NULL || abs[0] == '\0')
        return -1;
    // Fast path: already there.
    struct stat st;
    if (stat(abs, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;
    // Walk the components, creating as needed. Skip the leading '/'.
    for (char *p = abs + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        int rc = mkdir(abs, 0755);
        *p = '/';
        if (rc != 0 && errno != EEXIST)
            return -1;
    }
    if (mkdir(abs, 0755) != 0 && errno != EEXIST)
        return -1;
    return stat(abs, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

int rmos_open_excl(const char *abs, rmos_file *out)
{
    int fd = open(abs, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0)
        return errno == EEXIST ? 1 : -1;
    out->h = fd;
    return 0;
}

int rmos_write_all(rmos_file f, const void *data, size_t len)
{
    const char *p = data;
    while (len > 0) {
        ssize_t n = write((int)f.h, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p   += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

int rmos_sync(rmos_file f)
{
    return fsync((int)f.h) == 0 ? 0 : -1;
}

int rmos_close(rmos_file f)
{
    return close((int)f.h) == 0 ? 0 : -1;
}

int rmos_rename_replace(const char *from, const char *to)
{
    return rename(from, to) == 0 ? 0 : -1;
}

int rmos_sync_dir(const char *dir)
{
    int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int rc = fsync(fd);
    close(fd);
    return rc == 0 ? 0 : -1;
}

int rmos_unlink(const char *abs)
{
    return unlink(abs) == 0 ? 0 : -1;
}

int rmos_scan_dir(const char *dir, void (*cb)(const char *name, void *ctx), void *ctx)
{
    DIR *d = opendir(dir);
    if (d == NULL)
        return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        cb(e->d_name, ctx);
    }
    closedir(d);
    return 0;
}

#endif // __linux__ || __APPLE__
