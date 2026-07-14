/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private module header: per-platform primitives for the common TU (filesystem.c).

#ifndef FILESYSTEM_INTERNAL_H
#define FILESYSTEM_INTERNAL_H

// Create `path` as a directory if absent (mkdir / _mkdir). Parents must already exist.
// Output: 0 when the directory exists afterward, -1 on failure.
int fs_mkdir(const char *path);

#endif // FILESYSTEM_INTERNAL_H
