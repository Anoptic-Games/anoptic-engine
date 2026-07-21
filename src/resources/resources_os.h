/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Internal OS surface. One POSIX TU (Linux/macOS), one Win64 TU. resources_core.c stays platform-free.
// Paths are NUL-terminated with '/' separators.
// Contracts: 0/-1, no errno leak. Fresh handle per read. rmos_size_hint is HINT ONLY (EOF is truth). Sync failure poisons the handle.

#ifndef ANOPTIC_RESOURCES_OS_H
#define ANOPTIC_RESOURCES_OS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One open file. POSIX: fd. Win64: HANDLE as intptr.
typedef struct rmos_file {
    intptr_t h;
} rmos_file;

// Read-chunk ceiling. Core loop never asks for more per call.
#define RMOS_CHUNK_MAX (512u * 1024u)

// Advisory presence probe (open-for-read + close, not stat). Read path never trusts it.
bool rmos_exists(const char *abs);

// Open for reading. 0 and a live handle, or -1.
int rmos_read_open(const char *abs, rmos_file *out);

// fstat size hint. >= 0 or -1 unknown. NEVER load-bearing.
int64_t rmos_size_hint(rmos_file f);

// Read up to cap bytes (cap <= RMOS_CHUNK_MAX). Loops EINTR. 0 with *got == 0 at EOF. -1 on error.
int rmos_read_chunk(rmos_file f, void *buf, size_t cap, size_t *got);

<<<<<<< HEAD
// Positional read. *got == 0 is the ONLY EOF signal. Short read is not EOF. CALLER loops.
// WIN64: OVERLAPPED ReadFile also advances the file pointer. One handle is SINGLE-OWNER.
=======
// Positional read. *got == 0 is the ONLY EOF signal. 0 < *got < cap is a SHORT READ, not
// EOF -- 9P and SMB do this routinely -- and the CALLER loops. The loop is never folded
// into the seam, because its termination rule differs between "read to EOF" and "deliver
// exactly len".
//
// WIN64 CAVEAT, and it goes here so nobody assumes thread-safety and ships a data race:
// ReadFile with an OVERLAPPED offset on an ordinary synchronous handle is positional and
// blocking, but per MSDN it ALSO advances the file pointer. One handle is SINGLE-OWNER in
// Stage A. Stage C opens per-worker handles or flips to FILE_FLAG_OVERLAPPED + event
// without re-signaturing.
>>>>>>> block-b1-base
int rmos_read_at(rmos_file f, uint64_t off, void *buf, size_t cap, size_t *got);

typedef enum rmos_advice {
    RMOS_ADVICE_SEQUENTIAL = 1, RMOS_ADVICE_RANDOM, RMOS_ADVICE_WILLNEED
} rmos_advice;

<<<<<<< HEAD
// Advisory. NEVER load-bearing. 0 always.
int rmos_advise(rmos_file f, uint64_t off, uint64_t len, rmos_advice advice);

// ADVISORY ONLY. mtime LIES on 9P/SMB. Hot reload filters with it and confirms by content hash.
=======
// Advisory; NEVER load-bearing. posix_fadvise / no-op. 0 always (a refused hint is not a
// failure).
int rmos_advise(rmos_file f, uint64_t off, uint64_t len, rmos_advice advice);

// ADVISORY ONLY, exactly like rmos_size_hint. mtime LIES on 9P/SMB. Hot reload uses it as a
// FILTER and confirms with a content hash. 0 and both outputs set, or -1.
>>>>>>> block-b1-base
int rmos_stat_hint(const char *abs, uint64_t *mtime, uint64_t *size);

void rmos_read_close(rmos_file f);

// mkdir -p. Existing dirs succeed. abs is scratch and restored before return.
int rmos_mkdir_p(char *abs);

// Exclusive create for write-protocol temp. 0 open / 1 name-exists / -1 error. Win64 share mode 0.
int rmos_open_excl(const char *abs, rmos_file *out);

// Write all len bytes. Loops short writes and EINTR. 0 / -1.
int rmos_write_all(rmos_file f, const void *data, size_t len);

// fsync / FlushFileBuffers. Failure poisons the handle: close and unlink, never sync again.
int rmos_sync(rmos_file f);

// Close. 0 / -1 (handle is gone either way).
int rmos_close(rmos_file f);

// Atomic-replace rename. `to` may exist. 0 / -1.
int rmos_rename_replace(const char *from, const char *to);

<<<<<<< HEAD
// Rename only when `to` does not exist. 0 moved / 1 destination exists / -1. Orphan-save recovery.
int rmos_rename_new(const char *from, const char *to);

// fsync the directory of a just-renamed entry (POSIX). No-op 0 on Windows.
=======
// Rename only when `to` does not exist. 0 moved, 1 destination exists, -1 failure. Used
// by orphan-save recovery so a valid temp never overwrites another preserved generation.
int rmos_rename_new(const char *from, const char *to);

// fsync the directory containing a just-renamed entry (POSIX). No-op 0 on Windows.
>>>>>>> block-b1-base
int rmos_sync_dir(const char *dir);

// Delete a file. 0 / -1 (absent is -1).
int rmos_unlink(const char *abs);

// Enumerate directory entry names (no "."/".."). Order unspecified. 0 empty ok. -1 if unopenable.
int rmos_scan_dir(const char *dir, void (*cb)(const char *name, void *ctx), void *ctx);

#endif // ANOPTIC_RESOURCES_OS_H
