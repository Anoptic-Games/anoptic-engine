/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Module-private surface of src/resources/: path grammar, namespace access, the gulp
// primitive, and the save-frame format. resources_core.c owns the state; the registry
// and domain-extension TUs consume these.

#ifndef ANOPTIC_RESOURCES_INTERNAL_H
#define ANOPTIC_RESOURCES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <anoptic_filesystem.h>
#include <anoptic_memory.h>

#include "resources_os.h"

// ---------------------------------------------------------------------------------------------
// Path grammar (plan rule 3). Total: hostile input returns -1, never UB.

// Validate a logical FILE path: forward slashes, relative, no empty/'.'/'..' segments,
// no '\\', ':' or control bytes, 1..MAXPATH-1 bytes. 0 valid (+ length out), -1 not.
int res_path_validate(const char *logical, size_t *out_len);

// Validate a single path SEGMENT (a save slot name): same byte rules, no '/' at all.
int res_segment_validate(const char *seg, size_t *out_len);

// Canonicalize a mount prefix: "" stays empty; otherwise a valid logical path with an
// optional trailing '/', emitted WITH exactly one trailing '/'. 0 (+ length out) / -1.
// out must hold MAXPATH bytes.
int res_prefix_canon(const char *prefix, char *out, size_t *out_len);

// root + '/' + rel as an ano_fspath; length == 0 if it cannot fit MAXPATH - 1 or the
// root is empty. rel need not be NUL-terminated (rel_len bytes are taken).
ano_fspath res_join(const ano_fspath *root, const char *rel, size_t rel_len);

// ---------------------------------------------------------------------------------------------
// Namespace access for sibling TUs.

// True once ano_res_init has succeeded.
bool res_ready(void);

// Mark the mount table frozen (any later mount refuses). Called by every read-side
// entry point before it walks the table.
void res_freeze(void);

// Candidate absolute paths for a logical path, in shadow order (write root first,
// mounts newest-first, base last). Returns the count written, 0 if none fit.
// cap should be ANO_RES_MAX_MOUNTS + 2.
int res_candidates(const char *logical, size_t len, ano_fspath *out, int cap);

// The pinned write root (length 0 before init).
ano_fspath res_write_root(void);

// ---------------------------------------------------------------------------------------------
// The gulp primitive: whole file into heap, ANO_CACHE_LINE-aligned, one guard NUL at
// [size], fstat as a hint only, EOF decided by the read loop, <= RMOS_CHUNK_MAX per
// read. 0 (+ buffer, byte count) / -1 with nothing allocated. Caller logs.
int res_read_all(mi_heap_t *heap, const char *abs, void **out, size_t *out_size);

// The full durable-replace protocol (plan section 10) writing the concatenation of
// nparts buffers to final_abs: same-dir O_EXCL temp, write all, fsync, close, rename,
// parent-dir fsync. 0 only when durable; on failure the temp is gone and any previous
// final_abs content is intact.
typedef struct res_iovec { const void *data; size_t len; } res_iovec;
int res_write_protocol(const char *final_abs, const res_iovec *parts, int nparts);

// ---------------------------------------------------------------------------------------------
// FNV-1a-64 over arbitrary bytes (same constants as ANOSTR_SID / anostr_hash; hash_id 1
// in the save frame). Handles len > UINT32_MAX, which anostr_hash cannot.
uint64_t res_fnv1a64(const void *data, size_t len);

// ---------------------------------------------------------------------------------------------
// Save frame v1 (plan section 9). Little-endian, byte-exact:
//   header (48): magic 'ANOS' | u16 container_version=1 | u8 hash_id=1 | u8 flags=0 |
//                u32 format_version | u32 min_reader_version | u64 payload_len |
//                u64 seq | u64 header_hash (FNV-1a-64 over bytes 0..31) | u64 reserved
//   payload (payload_len)
//   footer (16): u64 payload_hash | 'ANOSDONE'
// Truncation is caught three independent ways (total length, header_hash, footer).

#define RES_SAVE_HDR_BYTES 48u
#define RES_SAVE_FTR_BYTES 16u
#define RES_SAVE_CONTAINER_VERSION 1u
#define RES_SAVE_HASH_FNV1A64 1u

// Fill a 48-byte header and 16-byte footer for one payload.
void res_save_frame(uint8_t hdr[RES_SAVE_HDR_BYTES], uint8_t ftr[RES_SAVE_FTR_BYTES],
                    uint32_t format_version, const void *payload, size_t payload_len,
                    uint64_t seq);

// Validate a whole in-memory save file. 0 valid (+ outs) / -1. Distinguishing header
// vs body damage is the caller's log line, via which check failed first:
//   -1 = too short / bad magic / bad header fields or header_hash (header damage)
//   -2 = length mismatch, footer magic, or payload_hash (body damage)
int res_save_validate(const uint8_t *bytes, size_t len, uint32_t *out_format_version,
                      uint64_t *out_seq, const uint8_t **out_payload,
                      size_t *out_payload_len);

#endif // ANOPTIC_RESOURCES_INTERNAL_H
