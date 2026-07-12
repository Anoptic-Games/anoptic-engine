/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Resource Manager -- the core surface.
//
// The place where loadables live. One namespace of ordered roots resolves logical paths
// ("shaders/flat.frag.spv"); the manager owns resources in purpose-built allocators and
// hands out handles, views, copies, or destructive hand-offs. Saves and configs fall out
// of doing that well. Design brief: anoptic_strings.h -- the value/view/intern/reclaim
// grammar at asset-population scale. Plan of record: docs/resourcemanager-real.md.
//
// Logical paths, everywhere: forward slashes, relative, no leading '/', no empty, '.'
// or '..' segments, no backslashes, no ':' or control bytes; root + '/' + path fits
// MAXPATH - 1; compiled literals fit ANOSTR_SID_MAX. Violations hit the failure
// sentinel, never UB.
//
// Threading: ano_res_init and all ano_res_mount calls happen on the main thread before
// other threads touch the namespace (the ano_log_init discipline). The mount table then
// freezes at first use: resolution is stateless and thread-safe, registry mutation is
// mutex-guarded, same-slot save commits serialize internally.
//
// Remote-FS floor: 9P/SMB is a real deployment target. Believe only bytes you have
// read -- never stat size or mtime, never file locks; content validates by framing and
// hashes. ano_res_exists is advisory ONLY.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "anoptic_filesystem.h"   // ano_fspath and the OS roots this module composes
#include "anoptic_memory_pools.h" // the allocators the manager owns
#include "anoptic_strings.h"      // anostr_t views, ANOSTR_SID, interning

#define ANO_RES_MAX_MOUNTS 8  // read-only roots beyond the two built-ins
#define ANO_RES_SAVE_KEEP  3  // ADVISORY bulk hint: generations beyond this suggest
                              // prompting the user (nothing is ever auto-deleted)

// -- Lifecycle and mounts ---------------------------------------------------------------------

// Pin write root (ano_fs_userpath(), created if absent) and base mount
// (<gamepath>/resources); create the registry and the manager's allocators.
// Main thread, after ano_log_init. Repeat calls are no-ops returning the first result.
// Output: 0, or -1 if either root failed.
int  ano_res_init(void);

// Register an additional read-only root, shadowing base and earlier mounts (the
// write root still wins). prefix scopes the mount to a logical subtree ("" = whole
// namespace); interned on registration. One dev-build call site in main().
// Output: 0; -1 on invalid prefix, root.length == 0, a full table, a frozen
// namespace, or before init.
int  ano_res_mount(const char *prefix, ano_fspath root);

// -- Identity and handles -----------------------------------------------------------------------

// A resource handle as a value. rid = FNV-1a-64 of the logical path (ANOSTR_SID's twin:
// same key space as compiled literals) inline for pointer-free identity checks; slot+gen
// index the registry; {0,0,0} is the failure sentinel. A retired gen makes stale copies
// politely invalid -- lookups yield the sentinel view, never UB.
typedef struct { uint64_t rid; uint32_t slot; uint32_t gen; } anores_t;

// The handle for a logical path, loading and taking ownership on first request
// (single-copy: same path twice = same handle). Placement is the manager's business.
// Sentinel handle on refusal or load failure, one log line.
anores_t ano_res_get(const char *logical);

// The whole payload as a byte view -- borrowed, never owned by the caller. One guard NUL
// sits past the end. Valid until the handle's generation retires; the empty view on
// sentinel/stale handles.
anostr_t ano_res_bytes(anores_t h);

// Destructively reclaim the payload: the manager relinquishes the block (the Vulkan
// staging hand-off), the generation retires, outstanding views die. The caller owns the
// block and frees it with ano_aligned_free. Large payloads transfer zero-copy; payloads
// small enough to live in the shared pool are copied out (their pool block recycles).
// Output: the block (size + one guard NUL) and its size via out-params; -1 on
// sentinel/stale handles.
int  ano_res_release(anores_t h, void **data, size_t *size);

// Drop the manager's copy without taking it (level teardown does this in bulk via
// allocator wink-out; this is the single-resource form). -1 on sentinel/stale handles.
int  ano_res_unload(anores_t h);

// -- Resolution (escape hatch; every call site is migration debt) ------------------------------

// Absolute OS path where a logical path's bytes live right now, loose mounts only
// (a packed asset resolves empty). For the transition while parsers still self-open.
// Output: the path by value; length == 0 if no mount contains it or the path is invalid.
ano_fspath ano_res_resolve(const char *logical);

// Where a write to this logical path would land under the write root; parents
// created, so a non-empty result is ready to open.
ano_fspath ano_res_resolve_write(const char *logical);

// Validated join of a relative fragment onto a base directory (the glTF image-URI
// case); kills ad-hoc snprintf joins. The fragment obeys logical-path rules.
// Output: the joined path; length == 0 on invalid input or overflow.
ano_fspath ano_res_subpath(ano_fspath base, const char *relative);

// Whether any mount currently contains the path. ADVISORY ONLY -- metadata caches
// lie (9P/SMB); gate on the load itself and handle the sentinel instead.
bool ano_res_exists(const char *logical);

// -- Unowned reads ------------------------------------------------------------------------------

// One-shot gulp into a caller-supplied heap, bypassing the registry: fresh handle,
// fstat as hint only, read loop to EOF in bounded chunks, ANO_CACHE_LINE-aligned
// buffer, one guard NUL past the end, length == bytes read. For config bootstrap and
// genuinely unowned reads; also the internal primitive every owned load is built on.
// Output: the bytes as an anostr_t in heap (inline if tiny, per the string rules).
// The empty string on refusal or failure (one log line), never UB.
anostr_t ano_res_slurp(mi_heap_t *heap, const char *logical);

// -- The write path: durable and atomic, always under the write root ---------------------------

// Durably replace a fixed-name file (config, keybindings). Full protocol
// (same-dir O_EXCL temp, write, fsync, close, rename, parent-dir fsync); on any error
// the temp is unlinked, fsync is never retried on the same handle, the caller's buffer
// is the source of truth -- call again to retry.
// Output: 0 only when the replacement is durable on disk; -1 otherwise (previous
// file intact).
int  ano_res_write(const char *logical, const void *data, size_t size);

// Rename a damaged file under the write root to "<name>.broken": regenerate
// defaults without destroying evidence. Output: 0; -1 if absent or rename failed.
int  ano_res_quarantine(const char *logical);

// Commit a gamesave generation: framed payload (48-byte header, hashed, 16-byte
// footer) via the full protocol to a BRAND-NEW filename "saves/<slot>.<seq>.anosave",
// verified through a fresh read handle. Saves are user data: NOTHING older is ever
// deleted by the engine -- hint with ano_res_save_stats, delete only on the user's
// say-so via ano_res_save_delete. slot is a single path segment. Same-slot commits
// serialize internally. Output: 0 when durable AND verified; -1 otherwise (every
// prior generation intact).
int  ano_res_save_commit(const char *slot, uint32_t format_version,
                         const void *payload, size_t size);

// Count and total on-disk bytes of a slot's generations -- the bulk hint the game
// shows the user (compare against ANO_RES_SAVE_KEEP if a threshold is wanted). Bytes
// are fstat sizes: prompt material, not a contract. An absent saves dir is zero
// generations. Output: 0 (+ optional out-params); -1 on bad slot or before init.
int  ano_res_save_stats(const char *slot, uint32_t *generations, uint64_t *bytes);

// USER-INITIATED deletion of exactly one generation, "saves/<slot>.<seq>.anosave".
// The engine never calls this on its own behalf. Output: 0; -1 if absent, invalid,
// or before init.
int  ano_res_save_delete(const char *slot, uint64_t seq);

// Load the newest VALID gamesave: newest-seq-first, fresh handle each, framing + hashes
// validated (never stat metadata), the frame's seq must echo the filename's, first pass
// wins; a torn newest degrades one generation, silently to the caller, loudly in the
// log. Orphaned protocol temps are tried last, then purged. The payload becomes an
// owned resource under the manager, keyed "saves/<slot>" (a later call re-reads disk
// and retires the previous generation's handles). format_version and seq out-params
// are optional. Sentinel handle when no valid generation exists -- "start fresh".
anores_t ano_res_save_load(const char *slot, uint32_t *format_version, uint64_t *seq);

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_H
