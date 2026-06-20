/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "anoptic_filesystem.h"

#include <mach-o/dyld.h>   // _NSGetExecutablePath
#include <unistd.h>        // chdir
#include <stdlib.h>        // realpath
#include <string.h>        // strlen, memcpy
#include <limits.h>        // PATH_MAX
#include <mimalloc.h>

// Output: directory of the running executable (no file name); 
// pathString is
// mi_malloc'd for the caller to free. {0, NULL} on failure.
//
// The split is hand-rolled rather than dirname(): this is a thread-safe public API,
// and dirname() is not portably reentrant -- macOS returns a pointer to a static
// buffer (dirname_r is the safe form, but it is BSD-only and absent from glibc), so
// a manual scan is the only formulation that is uniformly thread-safe across POSIX.
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
        len--; // drop the trailing slash, but keep "/" for the filesystem root

    result.pathString = mi_malloc(len + 1);
    if (result.pathString == NULL)
        return result;
    memcpy(result.pathString, resolved, len);
    result.pathString[len] = '\0';
    result.length = (uint16_t)len;
    return result;
}

// Output: true on success. Sets the working directory to ano_fs_gamepath() so
// CWD-relative asset loads resolve regardless of where the binary was launched from.
bool ano_fs_chdir_gamepath(void)
{
    filepath dir = ano_fs_gamepath();
    if (dir.pathString == NULL)
        return false;
    bool ok = dir.length > 0 && chdir(dir.pathString) == 0;
    mi_free(dir.pathString);
    return ok;
}
