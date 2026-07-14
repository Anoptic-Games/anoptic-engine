/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Resource Manager -- the core surface.
//
// Every owned load names its lifetime domain. Read handles are transparent immutable
// capabilities, but borrowed bytes exist only inside an explicit registered read scope.
// The synchronous Stage A owner remains mutex-serialized; publication is lock-free and
// C23 data-race-free through immutable descriptors and acquire/release atomics.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "anoptic_filesystem.h"
#include "anoptic_memory_pools.h"
#include "anoptic_strings.h"

#define ANO_RES_MAX_MOUNTS 8
#define ANO_RES_SAVE_WARN 16 // Advisory generation count for prompting the user; the engine never auto-deletes saves.
#define ANO_RES_READER_NONE UINT32_MAX

// -- Lifecycle and explicit ownership ----------------------------------------------------------

int  ano_res_init(void);

// Complete synchronous teardown. The init thread is the Stage A owner thread: it creates
// and destroys every backing heap. Shutdown invalidates publication, defers reader-pinned
// blocks, and returns -1 while a registered reader remains active; call again after its
// read scope ends. Output: 0 when every manager allocation has been reclaimed.
int  ano_res_shutdown(void);

int  ano_res_mount(const char *prefix, ano_fspath root);

typedef enum ano_res_lifetime_kind {
    ANO_RES_LIFETIME_ENGINE = 1,
    ANO_RES_LIFETIME_WORLD_LEVEL,
    ANO_RES_LIFETIME_STREAMING,
    ANO_RES_LIFETIME_TRANSIENT_IMPORT,
    ANO_RES_LIFETIME_SAVE_CONFIG,
    ANO_RES_LIFETIME_TOOL_IMPORT,
    ANO_RES_LIFETIME_SHARED_IMMUTABLE,   // APPENDED AFTER TOOL_IMPORT. ano_res_domain_open's
                                         // bound check moves in lockstep at M8.
} ano_res_lifetime_kind;

// Counted domain capability. Zero is invalid. owner and generation never silently wrap.
typedef struct ano_res_lifetime {
    uint32_t owner;
    uint32_t generation;
    ano_res_lifetime_kind kind;
    uint32_t reserved;
} ano_res_lifetime;

// The permanent engine domain created by ano_res_init.
ano_res_lifetime ano_res_lifetime_engine(void);

// The process-lived SHARED_IMMUTABLE domain, created by ano_res_init beside the engine
// domain and retired only at shutdown. An alias into a domain that CANNOT die is not a
// lifetime hazard, so cross-lifetime sharing needs no refcount to prove it (D22).
// Retiring it while any other domain is live is refused with one ERROR.
ano_res_lifetime ano_res_lifetime_shared(void);

// Open and retire an explicit domain. Stage A is synchronous: both calls must run on the
// init/owner thread. Retirement first removes publication, then reclaims after every reader
// registered before invalidation becomes quiescent. Output: 0 accepted/complete, -1 refusal.
int ano_res_domain_open(ano_res_lifetime_kind kind, ano_res_lifetime *out);
int ano_res_domain_retire(ano_res_lifetime lifetime);

// Attempt deferred reclamation. Output: number of retired objects reclaimed.
size_t ano_res_collect(void);

// -- Identity, publication, and read scopes ----------------------------------------------------

typedef struct { uint64_t rid; uint32_t slot; uint32_t gen; } anores_t;

// One registration belongs to one caller thread until unregistered. The fields are opaque.
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

// -- Consume: three verbs, three contracts, none disguised as another ---------------------------

// 1. BORROW. read must be active; the view dies at ano_res_read_end even when the handle
// remains live. No raw manager pointer is returned outside a read scope.
anostr_t ano_res_bytes(const ano_res_read *read, anores_t h);

// Kind-gated borrow: a font handle can no longer be reinterpreted as a scene block. Empty
// when the resident kind is not `tag`.
anostr_t ano_res_bytes_typed(const ano_res_read *read, anores_t h, uint32_t tag);

// 2. DESTRUCTIVE TRANSFER. The block knows its own home: `token` indexes an owner-side
// parcel table, so no internal placement layout leaks into this header. A parcel in flight
// is a TEARDOWN BARRIER on its domain -- never a refcount on a resource, never a reader
// count, and it frees nothing when it drains.
typedef struct ano_res_parcel { void *data; size_t size; uint64_t token; } ano_res_parcel;

// Take the block out of the registry. 0 accepted, -1 refusal, -2 a registered reader still
// pins the old publication (retry after its scope ends). Owner thread.
int  ano_res_take(ano_res_lifetime lifetime, anores_t h, ano_res_parcel *out);
void ano_res_parcel_free(ano_res_parcel *parcel);       // owner thread
bool ano_res_parcel_zero_copy(const ano_res_parcel *);  // did the take avoid a copy?

// 3. DERIVE. The ONE door every conditioned artifact enters through: one validate() site,
// one dependency-disclosure site, one accounting site, one place a hostile block can enter.
// The derived resource has NO string key -- its rid is seeded from the source's rid and the
// kind tag -- so derived-key type confusion is unrepresentable.
anores_t ano_res_derive(ano_res_lifetime lifetime, const ano_res_read *read,
                        anores_t src, uint32_t tag);

// DEPRECATED. Deleted at M9 (W2), replaced by ano_res_take + ano_res_parcel_free: the
// transferred block carries its own home instead of costing a global side-table lookup on
// every return.
int ano_res_release(ano_res_lifetime lifetime, anores_t h, void **data, size_t *size);

// Retire one resource owned by lifetime.
int ano_res_unload(ano_res_lifetime lifetime, anores_t h);

// Migration debt: fixed engine-domain wrappers. They are explicit fixed-token adapters,
// never ambient lifetime state and never cross-thread reader storage.
anores_t ano_res_get_engine(const char *logical);
int ano_res_unload_engine(anores_t h);
int ano_res_release_engine(anores_t h, void **data, size_t *size);   // DEPRECATED: deleted at M9

// -- Cross-lifetime: explicit, charged, never silent, never counted -----------------------------

typedef enum ano_res_share {
    ANO_RES_SHARE_REFUSE = 0,   // what plain ano_res_get does
    ANO_RES_SHARE_ALIAS,        // legal ONLY when the resident owner is SHARED_IMMUTABLE
    ANO_RES_SHARE_PROMOTE,
    ANO_RES_SHARE_DUPLICATE,
} ano_res_share;

anores_t ano_res_get_ex(ano_res_lifetime lifetime, const char *logical, ano_res_share share);
int ano_res_promote  (ano_res_lifetime from, ano_res_lifetime to, anores_t h, anores_t *out);
int ano_res_duplicate(ano_res_lifetime from, ano_res_lifetime to, anores_t h, anores_t *out);

// -- Dependency disclosure and prefetch ---------------------------------------------------------

typedef struct ano_res_dep { uint64_t rid; uint32_t tag; uint32_t flags; } ano_res_dep;

// COPY-OUT: the caller never holds a pointer into a bind record. Output: the TOTAL dep
// count; fills out[0..min(count,cap)).
size_t ano_res_deps(const ano_res_read *read, anores_t h, ano_res_dep *out, size_t cap);
int    ano_res_prefetch(ano_res_lifetime lifetime, const char *logical, ano_res_share share);

// -- Ranges, packs, hot reload ------------------------------------------------------------------

// Read [off, off+len) of a resource without binding it. 0 / -1 / ANO_RES_RANGE_EOF when the
// range runs past the end. Never a silent partial read.
#define ANO_RES_RANGE_EOF (-3)
int      ano_res_read_range(const char *logical, uint64_t off, size_t len, void *dst);
anores_t ano_res_get_range (ano_res_lifetime lifetime, const char *logical,
                            uint64_t off, size_t len);

// Mount an anopak archive under `prefix`. Packs are walked AFTER every loose directory, so
// a loose file always shadows a packed one, whatever the mount order.
int ano_res_mount_pack(const char *prefix, ano_fspath pack_file);
int ano_res_pack_build(const char *src_dir, const char *out_pack);

// Poll the namespace for changed sources and republish in place. Output: the number of
// handles that changed; fills changed[0..min(count,cap)). Derived resources cascade.
int ano_res_reload_poll(anores_t *changed, int cap);

// -- Accounting --------------------------------------------------------------------------------

// requested_bytes and serving_bytes keep their CUMULATIVE meaning: five tests already
// subtract snapshots of them. Everything below is additive.
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
    size_t hint_mismatch_copies;    // the size hint LIED and the read spilled
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
    size_t parse_count;             // MUST read 0 after loading a baked scene
    size_t alias_hits;
    size_t outstanding_parcels;
    size_t residual_bytes;
} ano_res_allocator_stats;

ano_res_allocator_stats ano_res_stats(void);
int ano_res_domain_stats(ano_res_lifetime lifetime, ano_res_allocator_stats *out);
ano_res_allocator_stats ano_res_stats_delta(const ano_res_allocator_stats *before,
                                            const ano_res_allocator_stats *after);

// The five-axis cube (kind, lifetime, role, operation, destination), copied out: a reader
// never touches a live cell. tel_overflow_hits != 0 VOIDS every number from that run.
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

// The active placement scaffold ("scoped-pool" in production). Stamped into every benchmark
// artifact: a bench run cannot lie about which strategy produced a number.
const char *ano_res_placement_name(void);

// -- Resolution migration debt ----------------------------------------------------------------
// DEPRECATED. Quarantined behind src/resources/resources_toolpath.h at M18 (W12): a
// test/tool-only internal header, out of the public surface.

ano_fspath ano_res_resolve(const char *logical);
ano_fspath ano_res_resolve_write(const char *logical);
ano_fspath ano_res_subpath(ano_fspath base, const char *relative);
bool ano_res_exists(const char *logical);

// -- Unowned reads ------------------------------------------------------------------------------
// DEPRECATED. Deleted at M18 (W12); callers migrate to ano_res_get + ano_res_bytes inside a
// read scope.

anostr_t ano_res_slurp(mi_heap_t *heap, const char *logical);

// -- Durable writes and saves ------------------------------------------------------------------

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

// Commit a new immutable generation. min_reader_version is the oldest reader allowed to
// interpret format_version; it may not exceed format_version. Existing generations survive.
int ano_res_save_commit_ex(const char *slot, uint32_t format_version,
                           uint32_t min_reader_version, const void *payload, size_t size);

// Compatibility entry: conservative frames require a reader at format_version.
int ano_res_save_commit(const char *slot, uint32_t format_version,
                        const void *payload, size_t size);
int ano_res_save_stats(const char *slot, uint32_t *generations, uint64_t *bytes);
int ano_res_save_delete(const char *slot, uint64_t seq);

// Examine every normal generation newest-first with bounded memory, then every orphan temp.
// The first structurally valid normal generation is authoritative: an older reader receives
// READER_TOO_OLD rather than silently rolling user state back. Save/config lifetime required.
ano_res_save_status ano_res_save_load_ex(ano_res_lifetime lifetime, const char *slot,
                                         uint32_t reader_version,
                                         ano_res_save_result *out);

// Compatibility entry: accepts every framed version and returns only the handle/version/seq.
anores_t ano_res_save_load(ano_res_lifetime lifetime, const char *slot,
                           uint32_t *format_version, uint64_t *seq);

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_H
