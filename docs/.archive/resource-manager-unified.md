<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Manager: The Unified Plan

Status: historical research and superseded planning material. The authoritative resource-manager specification is `../resourcemanager-comprehensive.md`; authority, API-freeze, implementation-state, threading, and completion claims below are not current unless restated there.

**Status:** the single plan of record for `anoptic_resourcemg.h`. What we code, we ship: every step lands whole, tested, and permanent 〜 no placeholders, no throwaway tier. Deferrals exist only as bench-gated rungs, never as "correct design later." Supersedes `resource-manager.md`, `resource-manager-SoA.md`, and `resource-manager-plan.md`; where they disagreed, the newest decision is recorded here.
**House premise:** the hard part is already in-tree. The logger is the async transport (lock-free MPSC ring, owned drain thread, 22–48 ns enqueue, TSan-clean, fuzz-oracled), the render bridge ships SPSC ownership transfer and false-on-full tickets, `anoptic_strings.h` ships the one key space (`ANOSTR_SID` / `anostr_hash`, intern table). The async tier is a port, not a design.

---

## 1. Founding rule

Remote filesystems (9P, SMB) are the deployment floor: the WSL2 dev loop and roaming `%APPDATA%` both cross them, and both can serve stale metadata on a fresh handle. **Believe only bytes you have read.** Never trust stat size or mtime, never depend on file locks, validate content by framing and hashes.

`anoptic_filesystem.h` stays the thin OS-path + append layer. `anoptic_resourcemg.h` is the strict superset above it: one loading and saving story for shaders, textures, models, sounds, levels, gamesaves, config, with a specified path from sync loose files to async streaming and packs without an API break.

## 2. Current state (audit, 2026-07-12)

| Site | What | Path mechanism today |
|---|---|---|
| `pipeline.c` `loadFile`/`openEngineFile` (~20 sites: `pipeline.c`, `flat.c`, `transmission.c`, `additive.c`) | shaders | gamepath-relative `"resources/shaders/X.spv"` interim shim; size-then-read and `free()`-vs-`ano_aligned_free` bugs live inside `loadFile` |
| `vulkanMaster.c` `parseGltf` sites | models | CWD-relative into gitignored `assets/`, behind the chdir shim at `main.c:588` |
| `ano_GltfParser.c` → `texture.c` `stbi_load` | textures | raw glTF image URI, CWD-relative |
| `text_raster.c` | fonts | hand-rolled gamepath join |
| logger | append stream | `ano_fs_logpath()` = `<gamepath>/logs`, `<stamp>_ano.log` |

In-tree assets the plan builds on: `resources/` exists (shaders, fonts, textures) and stages next to the exe; models still sit in `assets/`. `anoptic_collections.h` is an empty stub awaiting the ring port. `anoptic_strings.h` ships `ANOSTR_SID`/`anostr_hash` with the bridge `ANOSTR_SID(x) == anostr_hash(anostr_lit(x))`, the intern table, `anostr_split`/`anostr_join`/builder, and UTF-16 boundary converters (`anostr_from_utf16_cstr` / `anostr_to_utf16`). `anoptic_filesystem.h` has grown `ano_fs_logpath`, `ano_fs_session_stamp`, `ano_fs_open_trunc` since the original audit; it still has no read-open, stat, enumeration, atomic replace, or mkdir -p, so the `rmos_*` surface below is all new work. Identity today is `char name[64]` in `ModelAsset` plus three hardcoded `parseGltf` filenames.

## 3. Ground rules

1. **One name.** Call sites use logical paths (`"shaders/flat.frag.spv"`): forward slashes, relative, no leading `/`, no empty/`.`/`..` segments, no backslashes, `root + '/' + path` fits `MAXPATH - 1`, compiled literals fit `ANOSTR_SID_MAX` (128). Violations hit the failure sentinel, never UB.
2. **One read call.** Bytes arrive whole, cache-line-aligned, guard-NUL'd, in the caller's heap; size = bytes read to EOF.
3. **Writes are durable or refused.** Old-complete or new-complete on disk, never torn.
4. **Identity is an integer.** rid = FNV-1a-64 of the logical path: `ANOSTR_SID` compiled, `anostr_hash` runtime, one key space.
5. **Bytes vs meaning.** The resource manager never parses; cgltf, stb_image, FreeType, the config parser keep the meaning half and stop doing their own file I/O.
6. **Correctness never deferred, performance always deferred.** The full write protocol ships in step 3; io_uring may never ship.
7. **Async on sync, never the reverse.** `ano_res_load` is a pure function of (frozen mount table, logical path, caller's heap).

Every keeper passes the flatness test: hot path is an array walk, synchronization is one atomic word, keys are integers, lifetimes are regions (one `mi_heap` per group, teardown = bulk free), recovery is a linear scan.

## 4. The namespace

A static table of absolute roots, written once at init/mount on the main thread, read-only forever, lock-free resolution, debug atomic `ready` flag. First root containing the file answers.

1. **Write root** 〜 `ano_fs_userpath()`. Every write lands here; it shadows every read (user overrides, saves, mods, loose-over-pack hot reload 〜 one mechanism).
2. **Registered mounts**, newest-first. Dev build registers the source tree's `resources/` in `main()` via `ANO_DEV_RESOURCES`, consumed at that one site only. Mounts carry an optional logical prefix (`"models/"` can graft `assets/` during migration); the field exists from day one.
3. **Base mount** 〜 `<gamepath>/resources`. Installing = the exe with `resources/` next to it.

A pack (step 7) is just another mount in the same walk. The logger's append stream under `ano_fs_logpath()` (gamepath, not the write root) is the one blessed exception to write-root-only writes; whether logs move under userpath is decided at step 2.

## 5. Performance model

- The fight is request rate, not bandwidth: NVMe saturates on queue depth. Ceiling targets adopted from DirectStorage: 50K req/s at ≤10% of one core, 2 GB/s sustained.
- Two shapes: bulk level load (easy, any design passes) and steady-state streaming of small ranged reads (the hard shape everything serves).
- Default path is buffered and page-cache-friendly; the pack tier keeps the cold-start door open. Never trade the warm dev loop for a cold-start benchmark.
- Transport is already free (22–48 ns enqueue vs ~10–100 µs per NVMe op); the budget is queue depth, decompression, memory placement.
- Compression is bandwidth amplification, proven at step 6 or reverted.
- Prefetch is disclosure: the level file names its assets, the IO thread never guesses.
- Backend ladder, one completion-shaped interface: rung 0 one IO thread + blocking `pread` + `posix_fadvise`; rung 1 2–4 threads; rung 2 io_uring/IOCP. Each rung must beat the previous in `anotest_resbench` percentiles to merge.

## 6. Frozen formats

**Save frame v1.** Little-endian, 48-byte header: magic `'ANOS'` (4), container_version=1 (2), hash_id (1; 1=FNV-1a-64, 2 reserved xxh3-64), flags=0 (1), format_version (4), min_reader_version (4), payload_len (8), seq (8, echoed from the filename), header_hash (8, FNV-1a-64 over bytes 0–31), reserved (8); payload; 16-byte footer: payload_hash + `'ANOSDONE'`. Truncation caught three independent ways; header vs body corruption distinguishable.

**Pack TOC (anopak).** Header `{magic 'ANOPAK\0\1', u32 entry_count, u64 toc_offset}`; sorted flat TOC of `{u64 rid, u64 offset, u64 size, u64 csize, u8 codec, u8 hash_id, u16 flags, u64 payload_hash}` binary-searched by rid; payloads 4 KiB-aligned; TOC checksummed, verified at mount 〜 a corrupt pack refuses at startup. Codec byte reserves GDeflate's id.

**Ticket.** `{u32 idx, u32 gen}`; `{0,0}` on a full ring. Request payload `{rid or path, offset, length, band, ticket}` 〜 ranges in the format from day one.

## 7. Write protocol

POSIX: same-dir `O_EXCL` unique temp → write all (loop short/EINTR) → `fsync(fd)` → close → `rename` → parent dir `fsync`. Any write/fsync failure: unlink temp, return -1, never re-fsync the same fd; a retry is a fresh protocol run. Dir-fsync failure after a successful rename logs loudly, returns 0.

Windows: `CreateFileW` temp share-mode 0, `WriteFile` loop, `FlushFileBuffers`, `CloseHandle`; `ReplaceFileW` when the target exists else `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`; both retried 5× with 100 ms backoff on `ERROR_SHARING_VIOLATION`. Wide strings at the boundary via `anostr_to_utf16`.

**Saves.** Every generation is a brand-new filename `saves/<slot>.<seq>.anosave` (a name that never existed has no stale cache entry), framed per §6, re-opened fresh and re-validated before pruning (keep `ANO_RES_SAVE_KEEP` = 3, oldest-first, only after the newest verifies). Load: scan newest-seq-first, fresh handle each, first valid wins; orphaned `.tmp` tried last then purged; all fail → NULL sentinel, "start fresh", never garbage. Per-slot commits serialize on an internal save mutex. Migration: in-memory v(n)→v(n+1) chain at load, written back through commit, never in place.

## 8. The public surface

This is V1, the shipped API, not a prototype tier. Step 0 lands these eleven functions, two value types, two constants; step 5 adds the four async entries (`ano_res_load_async`, `ano_res_poll`, `ano_res_pump`, the ticket). Additions only 〜 nothing here is ever removed, renamed, or re-signatured.

Threading: `ano_res_init` and all `ano_res_mount` calls happen on the main thread before other threads load (the `ano_log_init` discipline); after that every read is stateless and thread-safe, writes are safe from any thread, same-slot commits serialize internally.

```c
#include "anoptic_filesystem.h" // ano_fspath and the OS roots this module composes
#include "anoptic_memory.h"     // mi_heap_t, ANO_CACHE_LINE

#define ANO_RES_MAX_MOUNTS 8  // read-only roots beyond the two built-ins
#define ANO_RES_SAVE_KEEP  3  // gamesave generations retained per slot

// -- Lifecycle and mounts -----------------------------------------------------------------

// Resolve and pin the built-in roots (write root = ano_fs_userpath(), created if absent;
// base mount = <ano_fs_gamepath()>/resources). Main thread, after ano_log_init, before
// any other ano_res_* call. Output: 0 on success, -1 if either root failed to resolve.
int ano_res_init(void);

// Register an additional read-only root, shadowing the base mount and earlier
// registrations (the write root still wins). prefix scopes the mount to a logical
// subtree ("" = whole namespace; "models/" grafts root at models/...). The dev build's
// single call site lives in main() -- never at a load site.
// Output: 0; -1 on invalid prefix, root.length == 0, or a full table.
int ano_res_mount(const char *prefix, ano_fspath root);

// -- Resolution (escape hatch; new code should prefer ano_res_load) ------------------------

// Absolute OS path where a logical path's bytes live right now -- for parser libraries
// that open files themselves (cgltf sibling URIs, stb_image). Loose-directory mounts
// only: once an asset moves into a pack (step 7), resolution for it returns the empty
// path. Every call site of this function is migration debt owed to ano_res_load.
// Output: path by value; length == 0 if invalid or absent from every mount.
ano_fspath ano_res_resolve(const char *logical);

// The absolute OS path a write to this logical path would land at, under the write root.
// Creates missing parent directories, so a non-empty result is ready to open. For
// subsystems streaming through long-lived handles (the logger is the intended first user).
ano_fspath ano_res_resolve_write(const char *logical);

// Join a relative fragment onto a base directory path, validating the same rules as
// logical paths. For the glTF image-URI-against-model-directory case; kills ad-hoc
// snprintf joins at call sites. Output: joined path; length == 0 on invalid input/overflow.
ano_fspath ano_res_subpath(ano_fspath base, const char *relative);

// Whether any mount currently contains the logical path. ADVISORY ONLY -- metadata
// caches lie (9P/SMB); never gate correctness on this, ask ano_res_load and handle the
// NULL blob instead.
bool ano_res_exists(const char *logical);

// -- The read path --------------------------------------------------------------------------

// A loaded resource as a value. data == NULL (and size == 0) means the load failed.
// The bytes live in the heap the caller passed and die with it -- no free function on
// purpose (the anostr_intern_t lifetime discipline).
typedef struct {
    void  *data;
    size_t size;
} ano_res_blob;

// Load a whole resource through the mount table. The allocation is ANO_CACHE_LINE-
// aligned (covers SPIR-V's uint32_t pCode requirement and future GPU-staging grain) with
// one 0x00 guard byte past the end, so text resources parse as C strings without a copy.
// size is the byte count actually read to EOF -- never the stat() size: network
// filesystems lie about metadata (the WSL-9P lesson); this module believes only bytes it
// has read. Total: no failure path is UB.
ano_res_blob ano_res_load(mi_heap_t *heap, const char *logical);

// -- The write path: durable and atomic, always under the write root -----------------------

// Durably replace a fixed-name file (config, keybindings -- raw bytes, human-editable).
// Full crash-consistency protocol (§7): same-dir O_EXCL temp, write, fsync, close,
// rename, directory fsync; ReplaceFileW/MoveFileExW + FlushFileBuffers on Windows with
// sharing-violation backoff. On any write/fsync error the temp is unlinked and fsync is
// NEVER retried on the same handle (fsyncgate); the caller's buffer is the source of
// truth -- call again to retry.
// Output: 0 only when the replacement is durable on disk; -1 otherwise (previous file
// intact).
int ano_res_write(const char *logical, const void *data, size_t size);

// Rename a damaged file under the write root to "<name>.broken" so the caller can
// regenerate defaults without destroying evidence. Completes the never-crash-on-bad-
// config story. Output: 0 on success, -1 if absent or the rename failed.
int ano_res_quarantine(const char *logical);

// Commit a gamesave generation: framed payload (self-proving completeness, §6) written
// via the full protocol to a BRAND-NEW filename "saves/<slot>.<seq>.anosave" -- a name
// that never existed cannot have a stale 9P/SMB cache entry. Verified via a fresh read
// handle before older generations are pruned (keep ANO_RES_SAVE_KEEP). Commits to the
// same slot serialize internally -- concurrent callers cannot clobber each other's
// generation.
// Input: slot name (letters, digits, '-', '_'), your format version, payload.
// Output: 0 when the new generation is durable AND verified; -1 otherwise (every prior
// generation intact).
int ano_res_save_commit(const char *slot, uint32_t format_version,
                        const void *payload, size_t size);

// A loaded gamesave. blob.data == NULL means no valid generation exists for the slot.
typedef struct {
    ano_res_blob blob;           // payload only, framing stripped; lives in the caller's heap
    uint32_t     format_version; // as passed to ano_res_save_commit
    uint64_t     seq;            // the generation that passed validation
} ano_res_savedata;

// Load the newest VALID gamesave: enumerate generations newest-seq-first, open each with
// a FRESH handle, validate framing + hashes (never stat metadata), return the first that
// passes. A torn/stale/half-synced newest generation degrades to the previous save, not
// to corruption. Orphaned temps from interrupted commits are tried last, then purged.
ano_res_savedata ano_res_save_load(mi_heap_t *heap, const char *slot);
```

Files: `include/anoptic_resourcemg.h`; `src/resourcemg/resourcemg_core.c` (platform-free: validation/join, mount walk, framing, generation selection) + per-platform TUs behind internal `resourcemg_os.h` (`rmos_exists`, `rmos_read_all`, `rmos_mkdir_p`, `rmos_open_temp_excl`, `rmos_write_all/sync/close`, `rmos_rename_replace`, `rmos_sync_dir`, `rmos_scan_dir`), POSIX half shared Linux/macOS. Path internals ride anostr (`anostr_view` over `ano_fspath`, `anostr_split` for segment validation, builder for joins); `ano_fspath` materializes only at the OS boundary. Platform layer reads in ≤ 512 KiB chunks from day one. Caller-visible allocations go in the caller's heap (heap-first parameter, `LOCALHEAPATTR` scoped heaps as the idiom); the module owns no long-lived allocations and needs no cleanup function: the mount table is static memory, everything else lives in caller heaps.

## 9. The sequence

Each step independently mergeable. **Lands** = new capability. **Deletes** = the hardcoded path it kills. **Bar** = merge condition.

### Step 0 〜 namespace and read path
- **Lands:** header + core + platform TUs; init/mount/resolve/resolve_write/subpath/exists/load; CMake `ANO_DEV_RESOURCES` (one use, `main()`) and the install rule (exe + `resources/`).
- **Deletes:** nothing.
- **Tests:** new `anotest_resourcemg` (`unit;mem`): hostile-path fuzz (all refuse, none UB); shadow order; read-contract oracles (byte-identical, size == bytes written, guard NUL, alignment, absent file → NULL blob + one log line). Scratch dirs per `templates/scratch.h`.
- **Bar:** any code loads any staged file by logical name from any CWD, Debug and Release, both toolchains.

### Step 1 〜 shaders ride it
- **Lands:** all ~20 `loadFile` sites become `ano_res_load(heap, "shaders/X.spv")` with a `LOCALHEAPATTR` scoped heap per pipeline-build function.
- **Deletes:** `loadFile`, `openEngineFile`, `struct Buffer`.
- **Tests:** existing vk suite green; one manual run from a foreign CWD.
- **Bar:** zero shader-path strings outside `ano_res_load` calls.

### Step 2 〜 models, textures, fonts; the shim dies
- **Lands:** glTF sites take logical names (`ano_res_resolve` bridging cgltf initially 〜 named migration debt); models move to `resources/models/` or ride the `"models/"` prefix; image URIs join via `ano_res_subpath` before `stbi_load`; fonts load as blobs + `FT_New_Memory_Face`. Decide the logger/logpath question (blessed exception vs move under userpath).
- **Deletes:** the chdir shim (`main.c:588`), then `ano_fs_chdir_gamepath`; the hand-rolled join in `text_raster.c`.
- **Tests:** engine smoke from a foreign CWD on both OSes; installed-tree run; `anotest_text` green.
- **Bar:** CWD is irrelevant; nothing but resourcemg and the logger opens a file by path (grep-enforceable).

### Step 3 〜 durable writes, saves, first write clients
- **Lands:** `ano_res_write`, `ano_res_quarantine`, `ano_res_save_commit`/`_load` per §7. Immediate clients: `anoptic_config.h` (jsmn, `ANOSTR_SID`-keyed typed store, quarantine-and-regenerate, one real settings file) and keybindings as a config domain (scancode → SID action-id table; hardcoded GLFW switches in `main.c`/`instanceInit.c` become action dispatches).
- **Deletes:** hardcoded key handling; the no-settings-persist state.
- **Tests:** frame round-trip + corruption battery (truncate, bit-flip header vs body, rename-masquerade 〜 all detected, degrade one generation); fault-injection harness (`#ifdef`-gated child killed at every protocol step, parent asserts old-or-new-complete); config round-trip + quarantine.
- **Bar:** `kill -9` at any instant leaves every user file readable; corrupt config cannot block boot; rebinds survive relaunch.

### Step 4 〜 identity registry, levels as client
- **Lands:** rid registry (open addressing over cached hashes, dense slots 〜 the `anostr_intern_t` shape; single-copy enforcement, debug assert-on-collision); diagnostics strings through the intern table (sparse rid for identity, dense sym for side arrays). Client: level module, `levels/demo.json` (jsmn) naming models/transforms/lights; the demo scene becomes data.
- **Deletes:** the three hardcoded `parseGltf` filenames; `char name[64]` in `ModelAsset`.
- **Tests:** registry round-trip and growth; the SID/hash bridge asserted at the module boundary; level-load equivalence (data-driven scene renders the hardcoded scene's frame, render suite as oracle).
- **Bar:** the shipped scene is a level file a mod can shadow via the write root.

### Step 5 〜 the transport (port, not design)
- **Lands:** the logger's variable-length MPSC ring moves to `anoptic_collections.h` as the generic ring (logger re-consumes it). On it: request ring per §6, one IO thread (drainer's park/wake, lap-counter reclaim, shutdown discipline verbatim), blocking `pread` + `posix_fadvise`, per-consumer SPSC completion rings drained by a per-frame pump. Public surface: `ano_res_load_async`, `ano_res_poll`, `ano_res_pump`, the 8-byte ticket. Completions polled, never callbacks. Two bands, NOW and LATER; a blocking wait on a LATER ticket boosts it. Missing file completes FAILED through the ticket, identical to sync. Workers call sync `ano_res_load` into worker-owned heaps; blob ownership transfers through the completion message. Async writes are copy-at-submit.
- **Deletes:** nothing 〜 sync `ano_res_load` remains the primitive.
- **Tests:** TSan mandatory (`build.sh 7` under WSL); `anotest_res_async` fuzz, logfuzz-oracle style: N producers under full-ring pressure, every ticket completes exactly once, async bytes == sync bytes, FAILED count == missing-file count; `anotest_resbench` (bench, DISABLED in ctest): p50/p99/p99.9 per band under a background stream, bulk-load wall time vs step-0 baseline.
- **Bar:** TSan-clean; oracle holds over a soak; a level streams in the background with zero frame hitches.

### Step 6 〜 the streaming economy
- **Lands:** chunk pool (fixed 512 KiB blocks, free list over a flat array, false-on-empty); real ranged reads (audio/mip shape); LZ4 for latency, plain zstd for bulk, decoded on a worker pipelined so chunk N decodes while N+1 reads; store-raw for already-compressed payloads.
- **Deletes:** nothing; capacity only.
- **Tests:** range correctness vs whole-file oracle (random offset/length fuzz); pool exhaustion returns false-on-empty, never blocks the IO thread; `anotest_resbench` compressed corpus.
- **Bar:** effective bandwidth on compressed assets exceeds raw drive bandwidth, or the codec work reverts.

### Step 7 〜 the pack and the bake
- **Lands:** `anopak` mount type per §6; ~200-line offline builder wired into install. Load-in-place bake for models end-to-end: PODS image, pointers as offsets, one fix-up loop at load, zero runtime parsing. Loose files keep shadowing packs. Dev hot reload rides the completion ring: 500 ms mtime+size poll, confirmed by content hash before a frame-boundary swap.
- **Deletes:** runtime JSON parsing for baked models; cgltf leaves the shipped path (dev-import tool only), retiring the last `ano_res_resolve` debt.
- **Tests:** bake determinism (byte-identical pack); TOC bit-flip refuses at mount; shadow test; load-equivalence (baked model renders identically to the cgltf path); `anotest_resbench` TOC-lookup series.
- **Bar:** the demo scene loads with zero parse work and zero path strings at runtime.

### Step 8 〜 parallel pread, on demand only
- **Lands (maybe never):** 2–4 IO threads on the same request ring.
- **Bar to start:** `anotest_resbench` shows rung 0 leaving the drive idle while requests queue. **Bar to merge:** beats single-thread p99 on the streaming series. io_uring/IOCP remain a recorded rung below this one.

## 10. Standing rejections

| Rejected | The flat alternative that won |
|---|---|
| reference-counted lifetimes | group = one mi_heap, teardown = bulk free |
| handles + generations | level-lifetime heaps; revisit when unload-while-running exists |
| four priority bands + byte budgets | two bands + boost-on-wait, until a starvation bench |
| io_uring / IOCP / SQPOLL / O_DIRECT default | parallel `pread`; keep the warm dev loop |
| mmap as a load path | fault stalls, SIGBUS over 9P/SMB (CIDR'22) |
| zstd dictionaries | plain zstd until a pack bench shows the gap |
| sectioned files (4-way) | two destinations: CPU blob, GPU staging |
| composite-resource integrity graphs | one baked contiguous image + integer ids |
| GPU decompression (GDeflate) dependency | reserved codec byte only |
| SQLite savefiles | WAL/fcntl broken over NFS/SMB/9P |
| 128-bit GUIDs + import databases | hashed logical paths; solo scale renames are greps |
| callbacks from the IO thread | polled completions at frame boundaries (house pattern) |
| lock-free registry/resolve | rings for queues, mutex for maps |
| pointer files / mtime save selection | generation scan; mtime untrustworthy on 9P/SMB |

## 11. Done means

- Installed tree (exe + `resources/`) runs from any CWD on Windows and Linux; no code but the resource manager and the logger opens a file.
- `kill -9` at every protocol step: every user file old-complete or new-complete; a torn newest save degrades one generation, silently to the user, loudly in the log.
- Every resource-naming site is an `ANOSTR_SID` literal or a runtime hash of data-file strings; no raw filename keys anywhere.
- TSan-clean transport whose fuzz oracle holds over a soak; streaming a level produces zero frame hitches.
- Compressed assets read faster than raw drive bandwidth; the baked demo scene loads with zero runtime parsing.

## 12. Sources

Gregory *GEA* 3rd ed. ch. 7; Haas & Leis VLDB'23; Patterson et al. SOSP'95 (TIP); Crotty et al. CIDR'22; Pillai et al. OSDI'14 + PostgreSQL fsyncgate; Didona et al. SYSTOR'22; Costa *Modern Storage is Plenty Fast*; safetensors PR #692; DirectStorage 1.4 + GACL (GDC 2026); kernel-internals.org registered buffers; TigerBeetle; sokol_fetch; Unreal IoStore/Zen; ripgrep; Godot `res://`/`user://`; Bevy AssetServer; SQLite how-to-corrupt docs. In-tree: `src/log/log_ring.h` + `log_core.c`, `docs/text/logger.md`, render bridge conventions, `tests/templates/`, the WSL-9P incident fix (`b589d43`).
