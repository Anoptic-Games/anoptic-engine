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
#include <anoptic_memory_pools.h>   // ano_mem_stats for res_reg_stats
#include <anoptic_resources.h>      // anores_t

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
// read. heap == NULL allocates from the calling thread's DEFAULT heap (mi_malloc
// family: thread-safe, mi_free-able from any thread -- the registry's mode; a mi_heap_t
// is single-thread-owner and only sound when the caller owns it).
// 0 (+ buffer, byte count) / -1 could not open / -2 opened but read or alloc failed
// (nothing stays allocated on failure). Caller logs.
int res_read_all(mi_heap_t *heap, const char *abs, void **out, size_t *out_size);

// Registry construction (resources_registry.c), called once from ano_res_init.
int res_registry_init(void);

// ---------------------------------------------------------------------------------------------
// The section-5 bake-off switch, read once from ANO_RES_MODEL at registry init.
// Model E is the shipped default (the Phase D decision); "A" selects the baseline so
// the bench comparison cannot rot. One binary serves both; the bench runs twice.

typedef enum { RES_MODEL_A = 0, RES_MODEL_E = 1 } res_model_t;
res_model_t res_model(void);

// Lifetime groups (internal-only for the bake-off; public promotion waits for a real
// level consumer). The open group is AMBIENT under the registry mutex: any load on any
// thread during an open scope joins it -- the level-load shape, revisited at step 5.
// Group 0 is engine-forever: always open, never retirable; saves pin to it
// structurally. Under model A groups are registry tags over the one shared pool and
// retire sweeps per-object; under model E each group owns a multipool destroyed whole
// at retire (chunk-granular teardown; heap wink-out arrives with the loader thread).

// Open a scope and make it ambient. Output: group id >= 1, or -1 (table full, not
// ready, or the group pool could not be made).
int  res_group_begin(void);

// End the ambient scope (payloads stay live until retire). Hostile ids are a no-op.
void res_group_end(int g);

// Retire a group: every loaded row of g goes sentinel (gen bump, the unload contract),
// direct blocks free, pooled payloads free per-object (A) or die with the group pool
// (E, destroyed outside the lock). Output: 0; -1 on group 0, unopened, or junk ids.
int  res_group_retire(int g);

// Aggregate placement stats for tests and the bench: pool stats summed over open
// groups, plus live direct-class payloads (guard NUL included in bytes).
typedef struct res_reg_stats {
    ano_mem_stats pools;
    size_t direct_bytes;
    size_t direct_blocks;
} res_reg_stats;
res_reg_stats res_registry_stats(void);

// Registry access for the domain extensions.
// The logical name of a LIVE handle (NUL-terminated into out). 0 / -1.
int res_registry_name(anores_t h, char *out, size_t cap);
// A live handle for a logical path WITHOUT loading anything, or the sentinel.
anores_t res_registry_find(const char *logical);
// Hand a default-heap mi allocation (size + 1 bytes, guard NUL at [size]) to the
// registry under `logical`. Takes ownership of buf in every outcome: on success it is
// the payload (or copied into placement and freed); if the path is already loaded it
// is freed and the EXISTING handle returns (single-copy); on failure it is freed and
// the sentinel returns.
anores_t res_registry_adopt(const char *logical, void *buf, size_t size);

// The save mutex, shared by commit (core) and save_load (registry).
void res_save_lock(void);
void res_save_unlock(void);

// Fault-injection points for the protocol crash harness. Production builds compile the
// hook away; the test compiles this TU privately with ANO_RES_FAULT_INJECT and installs
// a hook that dies mid-protocol.
typedef enum {
    RES_FAULT_AFTER_WRITE = 1,      // temp bytes written, not yet fsynced
    RES_FAULT_AFTER_SYNC,           // temp fsynced, not yet closed
    RES_FAULT_AFTER_CLOSE,          // temp closed, not yet renamed
    RES_FAULT_AFTER_RENAME,         // renamed over final, dir not yet fsynced
} res_fault_step;
#ifdef ANO_RES_FAULT_INJECT
extern void (*res_fault_hook)(res_fault_step step);
#define RES_FAULT(step) do { if (res_fault_hook) res_fault_hook(step); } while (0)
#else
#define RES_FAULT(step) ((void)0)
#endif

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

// "<slot>.<seq>.anosave" -> seq. Strict: canonical digits only (no leading zeros), seq
// capped at UINT64_MAX/2 (a planted max-value name must not wrap max+1 to zero).
// Anything else is not ours. Shared by commit-side pruning and load-side scanning.
bool res_save_name_seq(const char *name, const char *slot, size_t slot_len,
                       uint64_t *out_seq);

// Validate a whole in-memory save file. 0 valid (+ outs) / -1. Distinguishing header
// vs body damage is the caller's log line, via which check failed first:
//   -1 = too short / bad magic / bad header fields or header_hash (header damage)
//   -2 = length mismatch, footer magic, or payload_hash (body damage)
int res_save_validate(const uint8_t *bytes, size_t len, uint32_t *out_format_version,
                      uint64_t *out_seq, const uint8_t **out_payload,
                      size_t *out_payload_len);

#endif // ANOPTIC_RESOURCES_INTERNAL_H
