/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Internal OS surface for src/resources/. One POSIX TU shared by Linux/macOS, one Win64
// TU; resources_core.c stays platform-free. Paths arrive NUL-terminated with '/'
// separators (Win32 file APIs accept them; the Win64 TU converts to UTF-16 at the edge).
//
// Contracts, uniform across platforms:
//   - 0 success / -1 failure unless stated; no errno leaks across the boundary;
//   - reads go through a FRESH handle per file, never a cached one (remote-FS floor);
//   - rmos_size_hint is fstat and is a HINT ONLY -- the read loop believes EOF, not it;
//   - sync failures are real failures (never retried on the same handle: fsyncgate).

#ifndef ANOPTIC_RESOURCES_OS_H
#define ANOPTIC_RESOURCES_OS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One open file. POSIX: fd. Win64: HANDLE smuggled through the intptr.
typedef struct rmos_file {
    intptr_t h;
} rmos_file;

// The read-chunk ceiling; the core loop never asks for more per call.
#define RMOS_CHUNK_MAX (512u * 1024u)

// Advisory presence probe (open-for-read + close, not stat, so it cannot say yes to a
// directory). The read path never trusts it.
bool rmos_exists(const char *abs);

// Open for reading. 0 and a live handle, or -1 (absent, unreadable, is-a-directory).
int rmos_read_open(const char *abs, rmos_file *out);

// fstat size of an open read handle. >= 0 hint, -1 unknown. NEVER load-bearing.
int64_t rmos_size_hint(rmos_file f);

// Read up to cap bytes (cap <= RMOS_CHUNK_MAX enforced by the caller). Loops EINTR
// internally. 0 with *got == 0 at EOF; -1 on error.
int rmos_read_chunk(rmos_file f, void *buf, size_t cap, size_t *got);

void rmos_read_close(rmos_file f);

// mkdir -p: create dir and any missing parents (each component 0755 / default ACL).
// Existing directories are success. abs is used as scratch (separators temporarily
// NUL-ed) and restored before return.
int rmos_mkdir_p(char *abs);

// Exclusive create for the write protocol's temp file: fails if the name exists.
// 0 open handle / 1 name-exists (caller picks another) / -1 error.
// Win64 opens with share mode 0.
int rmos_open_excl(const char *abs, rmos_file *out);

// Write all len bytes, looping short writes and EINTR. 0 / -1.
int rmos_write_all(rmos_file f, const void *data, size_t len);

// fsync / FlushFileBuffers. 0 / -1. A failure poisons the handle: close and unlink,
// never sync it again.
int rmos_sync(rmos_file f);

// Close. 0 / -1 (handle is gone either way).
int rmos_close(rmos_file f);

// Atomic-replace rename: `to` may exist and is replaced (POSIX rename;
// ReplaceFileW / MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH) with 5x100ms
// sharing-violation retries on Windows). 0 / -1.
int rmos_rename_replace(const char *from, const char *to);

// fsync the directory containing a just-renamed entry (POSIX). No-op 0 on Windows.
int rmos_sync_dir(const char *dir);

// Delete a file. 0 / -1 (absent counts as -1).
int rmos_unlink(const char *abs);

// Enumerate a directory's entry names (files and dirs, no "."/".."), calling cb for
// each. Enumeration order is unspecified. 0 even for an empty dir; -1 if the dir
// cannot be opened.
int rmos_scan_dir(const char *dir, void (*cb)(const char *name, void *ctx), void *ctx);

#endif // ANOPTIC_RESOURCES_OS_H
