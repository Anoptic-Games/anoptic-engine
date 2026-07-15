/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Resource Manager, the core surface.
// Owned loads name a lifetime domain. Borrowed bytes live only inside a registered read scope.
// Owner thread is mutex-serialized. Publication is lock-free via immutable descriptors and acquire/release.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "anoptic_filesystem.h"
#include "anoptic_memory_pools.h"
#include "anoptic_strings.h"

#define ANO_RES_MAX_MOUNTS 8
#define ANO_RES_SAVE_WARN 16 // Advisory generation count for user prompts. Engine never auto-deletes saves.
#define ANO_RES_READER_NONE UINT32_MAX

/* Lifecycle and explicit ownership */

int  ano_res_init(void);

// Synchronous teardown on the init/owner thread: creates/destroys every backing heap. Defers reader-pinned blocks. Returns -1 while a registered reader is active (retry after its scope ends). Output: 0 when every manager allocation is reclaimed.
int  ano_res_shutdown(void);

int  ano_res_mount(const char *prefix, ano_fspath root);

typedef enum ano_res_lifetime_kind {
    ANO_RES_LIFETIME_ENGINE = 1,
    ANO_RES_LIFETIME_WORLD_LEVEL,
    ANO_RES_LIFETIME_STREAMING,
    ANO_RES_LIFETIME_TRANSIENT_IMPORT,
    ANO_RES_LIFETIME_SAVE_CONFIG,
    ANO_RES_LIFETIME_TOOL_IMPORT,
    ANO_RES_LIFETIME_SHARED_IMMUTABLE,   // after TOOL_IMPORT; domain_open refuses it today
} ano_res_lifetime_kind;

// Counted domain capability. Zero is invalid. owner and generation never silently wrap.
typedef struct ano_res_lifetime {
    uint32_t owner;
    uint32_t generation;
    ano_res_lifetime_kind kind;
    uint32_t reserved;
} ano_res_lifetime;

// Permanent engine domain from ano_res_init.
ano_res_lifetime ano_res_lifetime_engine(void);

// STUB. Returns a zero lifetime.
ano_res_lifetime ano_res_lifetime_shared(void);

// Open/retire an explicit domain on the init/owner thread. Retire removes publication then reclaims after pre-invalidation readers go quiescent. Output: 0 accepted/complete, -1 refusal.
int ano_res_domain_open(ano_res_lifetime_kind kind, ano_res_lifetime *out);
int ano_res_domain_retire(ano_res_lifetime lifetime);

// Deferred reclamation. Output: number of retired objects reclaimed.
size_t ano_res_collect(void);

/* Identity, publication, and read scopes */

typedef struct { uint64_t rid; uint32_t slot; uint32_t gen; } anores_t;

// One registration belongs to one caller thread until unregistered. Fields are opaque.
typedef struct ano_res_reader {
    uint32_t lane;
    uint32_t cookie;
} ano_res_reader;

typedef struct ano_res_read {
    ano_res_reader *reader;
    uint64_t epoch;
} ano_res_read;

int  ano_res_reader_register(ano_res_reader *reader);
int  ano_res_reader_unregister(ano_res_reader *reader);
int  ano_res_read_begin(ano_res_reader *reader, ano_res_read *read);
void ano_res_read_end(ano_res_read *read);

// Load into the explicit lifetime. Same path in the same live generation is single-copy.
anores_t ano_res_get(ano_res_lifetime lifetime, const char *logical);

/* Consume: three verbs, three contracts, none disguised as another */

// BORROW. read must be active. View dies at ano_res_read_end even if the handle remains live. No raw manager pointer outside a read scope.
anostr_t ano_res_bytes(const ano_res_read *read, anores_t h);

// STUB. Always empty.
anostr_t ano_res_bytes_typed(const ano_res_read *read, anores_t h, uint32_t tag);

// Parcel token for take/transfer. STUB APIs below.
typedef struct ano_res_parcel { void *data; size_t size; uint64_t token; } ano_res_parcel;

// STUB. Always -1.
int  ano_res_take(ano_res_lifetime lifetime, anores_t h, ano_res_parcel *out);
void ano_res_parcel_free(ano_res_parcel *parcel);       // STUB no-op
bool ano_res_parcel_zero_copy(const ano_res_parcel *);  // STUB always false

// STUB. Always sentinel.
anores_t ano_res_derive(ano_res_lifetime lifetime, const ano_res_read *read,
                        anores_t src, uint32_t tag);

// DEPRECATED. Prefer ano_res_take + ano_res_parcel_free.
int ano_res_release(ano_res_lifetime lifetime, anores_t h, void **data, size_t *size);

// Retire one resource owned by lifetime.
int ano_res_unload(ano_res_lifetime lifetime, anores_t h);

// Fixed engine-domain wrappers. Explicit fixed-token adapters, never ambient lifetime or cross-thread reader storage.
anores_t ano_res_get_engine(const char *logical);
int ano_res_unload_engine(anores_t h);
int ano_res_release_engine(anores_t h, void **data, size_t *size);   // DEPRECATED

/* Cross-lifetime: explicit, charged, never silent, never counted */

typedef enum ano_res_share {
    ANO_RES_SHARE_REFUSE = 0,   // what plain ano_res_get does
    ANO_RES_SHARE_ALIAS,
    ANO_RES_SHARE_PROMOTE,
    ANO_RES_SHARE_DUPLICATE,
} ano_res_share;

// REFUSE delegates to ano_res_get. Other share modes STUB (sentinel).
anores_t ano_res_get_ex(ano_res_lifetime lifetime, const char *logical, ano_res_share share);
int ano_res_promote  (ano_res_lifetime from, ano_res_lifetime to, anores_t h, anores_t *out);  // STUB -1
int ano_res_duplicate(ano_res_lifetime from, ano_res_lifetime to, anores_t h, anores_t *out);  // STUB -1

/* Dependency disclosure and prefetch */

typedef struct ano_res_dep { uint64_t rid; uint32_t tag; uint32_t flags; } ano_res_dep;

// STUB. Always 0.
size_t ano_res_deps(const ano_res_read *read, anores_t h, ano_res_dep *out, size_t cap);
int    ano_res_prefetch(ano_res_lifetime lifetime, const char *logical, ano_res_share share);  // STUB -1

/* Ranges, packs, hot reload */

// Read [off, off+len) without binding. 0 / -1 / ANO_RES_RANGE_EOF. Never a silent partial read. DIR candidates only today.
#define ANO_RES_RANGE_EOF (-3)
int      ano_res_read_range(const char *logical, uint64_t off, size_t len, void *dst);
anores_t ano_res_get_range (ano_res_lifetime lifetime, const char *logical,
                            uint64_t off, size_t len);   // STUB sentinel

// Mount/build anopak. Mount stores header+TOC. Candidate walk does not yet emit PACK sources.
int ano_res_mount_pack(const char *prefix, ano_fspath pack_file);
int ano_res_pack_build(const char *src_dir, const char *out_pack);

// STUB. Always 0 changes.
int ano_res_reload_poll(anores_t *changed, int cap);

/* Accounting */

// requested_bytes and serving_bytes are CUMULATIVE. Everything below is additive.
typedef struct ano_res_allocator_stats {
    size_t requested_bytes;
    size_t serving_bytes;
    size_t live_bytes;
    size_t live_blocks;
    size_t peak_bytes;
    size_t peak_blocks;
    size_t chunk_bytes;
    size_t chunk_count;
    size_t allocations;
    size_t frees;
    size_t copies;
    size_t bytes_copied;
    size_t promotions;
    size_t duplications;
    size_t transfers;
    size_t transfer_bytes;
    size_t retired_pending;
    size_t stalled_readers;
    size_t descriptors_live;
    size_t domains_live;
    size_t rows_bound;

    size_t live_requested_bytes;    // live internal frag = live_bytes - live_requested_bytes
    size_t parent_acquires;
    size_t parent_releases;
    size_t parent_bytes;
    size_t class_hits;
    size_t oversize_hits;
    size_t external_frag_bytes;
    size_t staging_bytes;
    size_t staging_peak;
    size_t hint_mismatch_copies;    // size hint lied, read spilled
    size_t releases_zero_copy;
    size_t releases_copied;
    size_t release_copy_bytes;
    size_t retire_ns;
    size_t registry_probes;
    size_t max_probe;
    size_t rehashes;
    size_t ranged_reads;
    size_t range_bytes;
    size_t chunk_acquires;
    size_t chunk_pool_exhaustions;
    size_t pack_lookups;
    size_t pack_hits;
    size_t pack_bytes_stored;
    size_t pack_bytes_served;
    size_t codec_decodes;
    size_t codec_bytes_in;
    size_t codec_bytes_out;
    size_t reload_candidates;
    size_t reload_confirmed;
    size_t reload_rejected_same_content;
    size_t parse_count;             // graphics ingest counter (not charged today)
    size_t alias_hits;
    size_t outstanding_parcels;
    size_t residual_bytes;
} ano_res_allocator_stats;

ano_res_allocator_stats ano_res_stats(void);
int ano_res_domain_stats(ano_res_lifetime lifetime, ano_res_allocator_stats *out);  // STUB -1
ano_res_allocator_stats ano_res_stats_delta(const ano_res_allocator_stats *before,
                                            const ano_res_allocator_stats *after);

// Five-axis cube copy-out. Reader never touches a live cell. tel_overflow_hits != 0 VOIDS every number from that run.
typedef struct res_tel_cell_public {
    uint32_t key;                 // packed kind:5 | lifetime:3 | role:3 | op:4 | dest:4
    uint32_t _pad0;
    uint64_t allocs, frees;
    uint64_t requested_bytes, serving_bytes;
    uint64_t live_bytes, peak_bytes;
    uint64_t live_blocks;
    uint64_t copies, bytes_copied;
    uint64_t transfers, transfer_bytes;
    uint64_t promotions, duplications;
    uint64_t releases_zero_copy, releases_copied;
} res_tel_cell_public;

size_t ano_res_stats_cells(res_tel_cell_public *out, size_t cap);
size_t ano_res_stats_overflow_hits(void);

// Active placement scaffold name. Stamped into every benchmark artifact.
const char *ano_res_placement_name(void);

/* Resolution migration debt */

// DEPRECATED.

ano_fspath ano_res_resolve(const char *logical);
ano_fspath ano_res_resolve_write(const char *logical);
ano_fspath ano_res_subpath(ano_fspath base, const char *relative);
bool ano_res_exists(const char *logical);

/* Unowned reads */

// DEPRECATED. Prefer ano_res_get + ano_res_bytes inside a read scope.

anostr_t ano_res_slurp(mi_heap_t *heap, const char *logical);

/* Durable writes and saves */

int  ano_res_write(const char *logical, const void *data, size_t size);
int  ano_res_quarantine(const char *logical);

typedef enum ano_res_save_status {
    ANO_RES_SAVE_OK = 0,
    ANO_RES_SAVE_NOT_FOUND,
    ANO_RES_SAVE_CORRUPT,
    ANO_RES_SAVE_READER_TOO_OLD,
    ANO_RES_SAVE_IO_ERROR,
    ANO_RES_SAVE_RESOURCE_ERROR,
    ANO_RES_SAVE_INVALID_ARGUMENT,
} ano_res_save_status;

typedef struct ano_res_save_result {
    anores_t resource;
    uint32_t format_version;
    uint32_t min_reader_version;
    uint64_t seq;
} ano_res_save_result;

// Commit a new immutable generation. min_reader_version may not exceed format_version. Existing generations survive.
int ano_res_save_commit_ex(const char *slot, uint32_t format_version,
                           uint32_t min_reader_version, const void *payload, size_t size);

// Compatibility entry. Conservative frames require a reader at format_version.
int ano_res_save_commit(const char *slot, uint32_t format_version,
                        const void *payload, size_t size);
int ano_res_save_stats(const char *slot, uint32_t *generations, uint64_t *bytes);
int ano_res_save_delete(const char *slot, uint64_t seq);

// Newest-first normal generations then orphan temps. First structurally valid normal is authoritative. Older reader gets READER_TOO_OLD. Save/config lifetime required.
ano_res_save_status ano_res_save_load_ex(ano_res_lifetime lifetime, const char *slot,
                                         uint32_t reader_version,
                                         ano_res_save_result *out);

// Compatibility entry. Accepts every framed version, returns handle/version/seq only.
anores_t ano_res_save_load(ano_res_lifetime lifetime, const char *slot,
                           uint32_t *format_version, uint64_t *seq);

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_H
