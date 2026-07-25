/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Private: per-platform primitives for filesystem.c.

#ifndef FILESYSTEM_INTERNAL_H
#define FILESYSTEM_INTERNAL_H


/* Directory */

// Create `path` if absent (mkdir / _mkdir). Parents must already exist.
// EEXIST alone is not success: the existing entry must itself be a directory.
// Output: 0 when `path` is a directory afterwards, -1 otherwise.
int fs_mkdir(const char *path);

#endif // FILESYSTEM_INTERNAL_H
