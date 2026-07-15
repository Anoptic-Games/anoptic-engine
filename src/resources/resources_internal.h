/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Module-private surface: namespace access, ownership planning, registry publication, save-frame format.
// Kind axis is a FOURCC tag, never an enum.

#ifndef ANOPTIC_RESOURCES_INTERNAL_H
#define ANOPTIC_RESOURCES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <anoptic_filesystem.h>
#include <anoptic_memory.h>
#include <anoptic_memory_pools.h>
#include <anoptic_resources.h>

#include "resources_os.h"
#include "resources_place.h"

#define RES_DEPS_MAX 32u

int res_path_validate(const char *logical, size_t *out_len);
int res_segment_validate(const char *seg, size_t *out_len);
int res_prefix_canon(const char *prefix, char *out, size_t *out_len);
ano_fspath res_join(const ano_fspath *root, const char *rel, size_t rel_len);

bool res_ready(void);
void res_freeze(void);
ano_fspath res_write_root(void);

// Mount table for the candidate walk. Owner-written at mount, frozen at first read, read-only after.
int        res_mount_count(void);
ano_fspath res_mount_root(int i);
anostr_t   res_mount_prefix(int i);
ano_fspath res_base_root(void);

int  res_registry_init(void);
int  res_registry_shutdown(void);

typedef enum res_disposition {
    RES_DISPOSITION_RETAIN = 1,
    RES_DISPOSITION_PROMOTE,
    RES_DISPOSITION_DUPLICATE,
    RES_DISPOSITION_TRANSFER,
} res_disposition;

// Routing input. PURE: res_place_plan() allocates nothing, mutates nothing. Transferability is the arena grant (RES_SITE_TRANSFERABLE).
typedef struct res_place_plan {
    uint32_t         tag;          // FOURCC, not a dense id
    ano_res_lifetime lifetime;
    res_role         role;
    res_operation    operation;
    res_destination  destination;
    res_provenance   provenance;
    size_t           alignment;    // what the CALLER needs. Refused, never silently dropped.
} res_place_plan;

typedef struct res_dependency_meta {
    uint64_t rid;
    uint32_t tag;
    uint32_t flags;
} res_dependency_meta;

// Site carries serving/alignment/arena/backing/flags/cell so free() never recomputes alloc().
typedef struct res_owned_block {
    void          *data;
    size_t         size;
    res_site       site;
    res_place_plan plan;
} res_owned_block;

// Six verbs. plan/alloc/free live. stage/commit/move STUB (-1). Owner-thread gate. res_owned_free is for unpublished blocks only.
int  res_owned_plan  (const res_place_plan *, size_t, res_site *out);
int  res_owned_alloc (const res_place_plan *, size_t, res_owned_block *out);
int  res_owned_stage (const res_place_plan *, size_t hint, res_owned_block *out);
int  res_owned_commit(res_owned_block *staged, const res_place_plan *home, res_owned_block *out);
int  res_owned_move  (res_owned_block *from, const res_place_plan *to, res_owned_block *out);
void res_owned_free  (res_owned_block *, res_free_mode);

void res_account_copy(const res_place_plan *plan, size_t bytes);
void res_account_transfer(const res_place_plan *plan, size_t bytes);

// Policy gate for promote/duplicate/transfer. No destination-cell charge yet.
int  res_disposition_allowed(ano_res_lifetime from, ano_res_lifetime to,
                             res_disposition disposition, bool transferable);

int res_registry_name(const ano_res_read *read, anores_t h, char *out, size_t cap);
anores_t res_registry_find(const ano_res_read *read, const char *logical);
anores_t res_registry_adopt(const char *logical, res_owned_block *block,
                            const res_dependency_meta *deps, size_t dep_count);

// DEPRECATED. Prefer res_owned_alloc + ano_res_take.
void res_registry_external_allocation(const res_place_plan *plan, size_t bytes);

// Test-only private probes.
const void *res_test_row_address(uint32_t slot);
int res_test_set_generation(anores_t h, uint32_t generation);
int res_test_set_owner_generation(ano_res_lifetime lifetime, uint32_t generation);

/* Identity */

// Derived/duplicate resources have no string key. RES_IDENT_DERIVED cannot resolve from the filesystem.

uint64_t res_rid_file     (const char *logical, size_t len);            // FNV-1a-64, basis A
uint64_t res_rid_file2    (const char *logical, size_t len);            // FNV-1a-64, basis B
uint64_t res_rid_derived  (uint64_t src_rid, uint32_t kind_tag);
uint64_t res_rid_duplicate(uint64_t src_rid, uint32_t owner_index);

/* Read side */

// Two-pass source walk intended (DIR then PACK). Pass 2 is empty today: candidates are DIR only.
// Loose-shadows-pack is the walk invariant once packs emit.

typedef struct res_pack res_pack;

typedef enum res_source_kind { RES_SRC_DIR = 1, RES_SRC_PACK } res_source_kind;

typedef struct res_source {
    res_source_kind kind;
    ano_fspath      path;
    const res_pack *pack;
    uint32_t        entry;
} res_source;

int res_candidates(const char *logical, size_t len, ano_fspath *out, int cap);
int res_candidates_ex(const char *logical, size_t len, res_source *out, int cap);

// Destination-aware read. STUB (-1).
typedef struct res_sink {
    void *ctx;
    void *(*reserve)(void *ctx, size_t hint, size_t *out_cap);
    void *(*grow)   (void *ctx, size_t need, size_t *out_cap);
    void  (*commit) (void *ctx, size_t final);
} res_sink;

int res_read_sink(const res_sink *, const char *abs, size_t *out_size);
int res_source_read_sink(const res_source *, const res_sink *, size_t *out_size);

// Unowned whole-file read into heap. EOF is truth.
int res_read_all(mi_heap_t *heap, const char *abs, void **out, size_t *out_size);

#define RES_RANGE_EOF (-3)
int   res_read_range   (const res_source *, uint64_t off, size_t len, void *dst);
int   res_hash_file    (const res_source *, uint64_t *hash, uint64_t *size);
void *res_chunk_acquire(ano_res_lifetime);   // STUB NULL
void  res_chunk_release(ano_res_lifetime, void *);  // STUB no-op

/* The save protocol */

typedef struct res_save_guard {
    void *lane;
} res_save_guard;

// Exact-slot sequencing. Map lock held only while locating a lane. Unrelated slots run concurrently.
int  res_save_begin(const char *slot, size_t slot_len, res_save_guard *guard);
void res_save_end(res_save_guard *guard);

// Test probe after the exact-slot lane is held, before the operation begins.
extern void (*res_test_save_enter_hook)(const char *slot);

typedef enum {
    RES_FAULT_AFTER_OPEN_EXCL = 1,   // the state that produces orphan temps
    RES_FAULT_AFTER_WRITE,
    RES_FAULT_AFTER_SYNC,
    RES_FAULT_AFTER_CLOSE,
    RES_FAULT_AFTER_RENAME,
    RES_FAULT_AFTER_DIR_SYNC,
} res_fault_step;
#ifdef ANO_RES_FAULT_INJECT
extern void (*res_fault_hook)(res_fault_step step);
#define RES_FAULT(step) do { if (res_fault_hook) res_fault_hook(step); } while (0)
#else
#define RES_FAULT(step) ((void)0)
#endif

typedef struct res_iovec { const void *data; size_t len; } res_iovec;
int res_write_protocol(const char *final_abs, const res_iovec *parts, int nparts);

uint64_t res_fnv1a64(const void *data, size_t len);

#define RES_SAVE_HDR_BYTES 48u
#define RES_SAVE_FTR_BYTES 16u
#define RES_SAVE_CONTAINER_VERSION 1u
#define RES_SAVE_HASH_FNV1A64 1u

void res_save_frame(uint8_t hdr[RES_SAVE_HDR_BYTES], uint8_t ftr[RES_SAVE_FTR_BYTES],
                    uint32_t format_version, uint32_t min_reader_version,
                    const void *payload, size_t payload_len, uint64_t seq);
bool res_save_name_seq(const char *name, const char *slot, size_t slot_len,
                       uint64_t *out_seq);
int res_save_validate(const uint8_t *bytes, size_t len, uint32_t *out_format_version,
                      uint32_t *out_min_reader_version, uint64_t *out_seq,
                      const uint8_t **out_payload, size_t *out_payload_len);

#endif // ANOPTIC_RESOURCES_INTERNAL_H
