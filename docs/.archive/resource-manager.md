<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Management: Report & Design of Record for `anoptic_resourcemg.h`

Status: historical research and superseded planning material. The authoritative resource-manager specification is `../resourcemanager-comprehensive.md`; authority, API-freeze, implementation-state, threading, and completion claims below are not current unless restated there.

**Status:** design of record, pre-implementation.
**Scope:** Part I reports everything that surfaced around `anoptic_filesystem.h` 〜 the bug
history, the audit of how the engine actually touches disk today, and the threat model that
falls out of it. Part II is the plan for `anoptic_resourcemg.h`: a strict superset of
`anoptic_filesystem.h` that gives every resource class 〜 shaders, textures, models, sounds,
levels, gamesaves, config 〜 one consistent loading and saving story, with a specified path
from synchronous loose files to SOTA async streaming and pack files without an API break.

---

## Part I 〜 What surfaced around `anoptic_filesystem.h`

### 1. The incident that started this

A cross-built Windows test exe, reading a log file that had just been written and closed on
the Linux side of WSL, saw a **stale, short file size** on a freshly opened read handle. The
`fseek(SEEK_END)`/`ftell` size-then-read idiom then silently truncated the read.

The root cause is general, not a WSL quirk. **Close-to-open coherence 〜 "a file I just
wrote and closed is fully visible to the next open" 〜 is a single-kernel, one-page-cache
guarantee.** Every *local* filesystem on one kernel has it, regardless of type (ext4, xfs,
btrfs, FAT, tmpfs 〜 mixed partitions are irrelevant). Every *remote or attribute-caching*
layer can break it: NFS, SMB/CIFS, sshfs/FUSE, and 9P/virtiofs (the WSL interop bridge).
Those layers cache file *metadata* (size, mtime) with its own lifetime, so a fresh handle
can see stale metadata for a file whose *bytes* are already durable.

And remote filesystems are not exotic for this project:

- **WSL2 9P** is the daily dev loop (Windows exe ↔ Linux-side files).
- **`%APPDATA%` may be an SMB share.** Enterprise/school roaming profiles redirect it over
  the network. `ano_fs_userpath()` writes exactly there on Windows. This is a real
  deployment target, not a hypothetical.

**Consequence adopted throughout this design: treat the remote filesystem as the floor.**
Never trust `stat()` size or mtime; never depend on file locks; validate content by what
was actually read, not what metadata claims.

The first fix already landed: the logging test's `slurp()` was rewritten from
size-then-read to a **read-until-EOF grow loop** (commit `b589d43`), which is
coherence-independent and behavior-identical on local filesystems. But the same antipattern
exists in the engine proper 〜 see §3.

### 2. What `anoptic_filesystem.h` is 〜 and deliberately lacks

The module is a thin, correct OS-path + append-file layer. Full public surface:

```c
ano_fspath ano_fs_gamepath(void);   // exe's own directory; length==0 on failure
ano_fspath ano_fs_userpath(void);   // per-user data root, mkdir'd if absent
bool       ano_fs_chdir_gamepath(void); // the interim CWD shim

ano_file  *ano_fs_open_append(const char *path);
int        ano_fs_write(ano_file *f, const void *data, size_t len); // loops EINTR/short
int        ano_fs_sync (ano_file *f);   // fsync / FlushFileBuffers
int        ano_fs_close(ano_file *f);   // does NOT sync; frees handle either way
```

with `ano_fspath` a 256-byte value type (`uint16_t length; char str[MAXPATH]`), and three
per-OS implementations (`readlink("/proc/self/exe")` / `GetModuleFileName` /
`_NSGetExecutablePath`+`realpath`; `~/.anoptic` / `%APPDATA%\anoptic` /
`~/Library/Application Support/anoptic`).

What it has **no API for at all** 〜 the gap `anoptic_resourcemg.h` fills:

| Missing | Consequence today |
|---|---|
| open-for-read / read-whole-file | every subsystem hand-rolls `fopen`+`fseek`/`ftell` |
| exists / stat / size | call sites probe by opening |
| directory enumeration | nothing can scan for save generations |
| path join / subpath | ad-hoc `snprintf` joins at call sites |
| atomic replace (temp+rename) | **no durable write path anywhere in the engine** |
| overwrite/truncate write | only append exists |
| mkdir -p | only the one-level `mkdir` inside `userpath` |

This is by design 〜 filesystem.h stays the thin layer 〜 but it means every consumer invented
its own path scheme, which is how the mess in §3 accumulated.

### 3. The audit: how the engine actually touches disk today

Every load/write site, and how its path is formed:

| Site | What | Path mechanism |
|---|---|---|
| `src/vulkan_backend/instance/pipeline.c:395,442` | compute shaders (`update.comp.spv`, `cull.comp.spv`) | **`PROJECT_ROOT`-baked** (compile-time source path) |
| `src/vulkan_backend/instance/pipelines/flat.c:58,63` | mesh/vert + frag shaders | **`PROJECT_ROOT`-baked** |
| `src/vulkan_backend/instance/pipelines/transmission.c:61,66` | geom + frag shaders | **`PROJECT_ROOT`-baked** |
| `src/vulkan_backend/vulkanMaster.c:1458,1477` | `parseGltf("viking_room.gltf")`, `"GlassHurricaneCandleHolder.gltf"` | **CWD-relative**, behind the `main.c:129` chdir shim; files live in gitignored `assets/` |
| `src/vulkan_backend/texture/texture.c:18` | `stbi_load(fileName,…)` with the raw glTF image URI (passed through `src/render/gltf/ano_GltfParser.c:253-255`) | **CWD-relative** |
| logger output file | append handle via `ano_fs_open_append` | gamepath + hand-rolled `snprintf` |
| `ano_fs_userpath()` probe/writes | per-user dir | correct, but **on Windows possibly SMB** |

Findings on the read helper the six shader sites share, `loadFile()`
(`src/vulkan_backend/instance/pipeline.c:23`):

- It uses the **same `fseek`/`ftell` size-then-read antipattern** the 9P incident exposed
  in the tests 〜 one `fread` sized by metadata. Same latent truncation class.
- It allocates with `ano_aligned_malloc(size, alignof(uint32_t))` (SPIR-V needs u32-aligned
  `pCode`) but frees with **plain `free()`** on the fread-failure path 〜 works only because
  of the mimalloc override; inconsistent with the `ano_aligned_free` calls at
  `pipeline.c:416/474`.

Other debts surfaced by the audit, inherited knowingly:

- **`MAXPATH` 256 + ANSI Windows APIs** (`GetModuleFileNameA`, `CreateFileA`,
  `getenv("APPDATA")`): a long or non-ASCII Windows profile path can fail resolution.
  This is filesystem.h's debt (wide-path `W` APIs); resourcemg deliberately does **not**
  fork its own path type around it 〜 one path type, one place to fix.
- Vestigial `src/filesystem/filesystem_win64.h` (`USER_SUBDIR = "Documents\\My Games\\…"`)
  is dead code superseded by `%APPDATA%`.
- `include/anoptic_collections.h` is an empty stub; the two lock-free rings in-tree
  (logger MPSC, render_bridge SPSC) are private and both note a future migration there 〜
  relevant because v1 async reuses those ring patterns.
- Resource *identity* today is a raw filename string (`char name[64]` inside `ModelAsset`).

### 4. The three path pathologies, named

1. **Shaders**: baked to a **compile-time source-tree absolute path** (`PROJECT_ROOT`);
   an installed or `nix build` binary points into a build sandbox that no longer exists.
   (An interim shim 〜 exe-relative resolution with `PROJECT_ROOT` as dev fallback 〜 lands
   with the build-system fix accompanying this document; resourcemg v0 deletes it.)
2. **Models/textures**: **CWD-relative** behind `ano_fs_chdir_gamepath()` in `main()` 〜
   the comment at `src/engine/main.c:128` names this module as its replacement.
3. **No install story**: nothing defines where assets live relative to the binary, so no
   install rule *could* be written before now.

One concept kills all three (§Part II): a logical namespace over an ordered set of
absolute roots. And one discipline kills the 9P bug class permanently: **believe only
bytes you have read; validate content, not metadata.**

---

## Part II 〜 The plan: `anoptic_resourcemg.h`

### 0. How this design was chosen

Three independent designs were produced against the same codebase inventory and research
base (Gregory's *Game Engine Architecture* ch. 7; engine prior art: Unreal IoStore/pak,
Godot `res://`+`user://`, Bevy AssetServer, sokol_fetch; SOTA IO literature: io_uring
studies, the CIDR'22 mmap-in-DBMS argument, TIP informed prefetching (SOSP'95),
Pillai et al. OSDI'14 "All File Systems Are Not Created Equal"/ALICE, the PostgreSQL
fsyncgate lesson) and judged through three lenses (engineering reality, correctness,
future-proofing):

- **"ANORES-9: One Namespace, Nine Functions"** (minimal-first) 〜 **winner, 2 of 3 judges**
  (9/9/6). Zero shared mutable state after init; the write path is the v0 flagship.
- "The Content Ledger" (identity/pipeline-first) 〜 best pack/manifest foundation; lost for
  deferring the write path (the module's founding mandate) to v1.
- "Everything Is A Request" (streaming-first) 〜 best async model; lost for shipping a
  256-slot concurrent state machine that today's 7-asset workload never exercises.

What follows is ANORES-9 **plus the panel's grafts** from the other two (marked ⊕ where
they land). Guiding convictions:

1. **A namespace over ordered roots** 〜 not a registry, not handles, not a cache 〜 is the
   minimal concept that fixes all three pathologies. Everything else is deferrable.
2. **Correctness machinery is never deferred; performance machinery always is.** The full
   crash-consistency write protocol ships in v0. io_uring does not.
3. **Async is built on sync, never the reverse.** v0's `ano_res_load` is a pure function of
   (frozen mount table, logical path, caller's heap) 〜 exactly the function a v1 IO worker
   calls. The seam is proven by construction.
4. **Bytes over metadata.** Fresh handle per read, `fstat` as a size *hint* only, read to
   EOF, in-file framing + hashes for anything that matters. stat/mtime/locks are advisory.
5. **Formats are forever.** ⊕ The save frame *and* the pack TOC are frozen on paper in this
   document, before code depends on them.

### 1. The core concept: logical namespace over ordered roots

Call sites name resources by **logical path** 〜 forward slashes, always relative:
`"shaders/flat.frag.spv"`, `"models/viking_room.gltf"`, `"config/video.toml"`,
`"saves/<slot>.<seq>.anosave"` 〜 and never see an OS path, the CWD, or a compile-time
define.

Resolution walks a small ordered table of absolute roots:

1. **Write root** 〜 `ano_fs_userpath()`. Every write lands here, and it shadows every
   read: user config overrides, saves, dropped-in mods win over shipped files, free.
2. **Registered mounts**, newest-first 〜 the dev build registers *one* extra mount (the
   source tree's `resources/`) in `main()`, and nothing else changes anywhere.
3. **Base mount** 〜 `<ano_fs_gamepath()>/resources`, the installed asset tree.

First root where the file exists answers. Installing the game becomes literally
"the exe with `resources/` next to it". `ano_fs_chdir_gamepath()` becomes vestigial the
day the last caller migrates, and is then deleted.

⊕ *Graft (mount prefixes):* mounts carry an optional logical **prefix**, so
`ano_res_mount("models/", assets_dir)` can graft the historical `assets/` directory into
the namespace without physically moving files during migration. v0 may only ever pass
`""`, but the field exists in the table from day one so enabling it never changes the
table format.

Logical path rules (checked by every function; violations hit the failure sentinel, never
UB): forward slashes only; no leading `/`; no empty, `.` or `..` segments; no backslashes;
`root + '/' + path` must fit `MAXPATH - 1`.

### 2. v0 public surface

Eleven functions (nine core + two grafted helpers), three value types, three constants.
Sketch in house style:

```c
/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Resource Manager -- one logical namespace over every resource class.
// Strict superset of anoptic_filesystem.h (which stays the thin OS-path/append layer).
// Design of record: docs/resourcesmg.md.
//
// Threading: ano_res_init and all ano_res_mount calls happen on the main thread before
// other threads load (the ano_log_init discipline). After that every read is stateless
// and thread-safe. Writes are safe from any thread; commits to the same slot serialize
// internally.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCEMG_H
#define ANOPTICENGINE_ANOPTIC_RESOURCEMG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "anoptic_filesystem.h" // ano_fspath and the OS roots this module composes
#include "anoptic_memory.h"     // mi_heap_t, ANO_CACHE_LINE

#define ANO_RES_MAX_MOUNTS 8  // read-only roots beyond the two built-ins
#define ANO_RES_SAVE_KEEP  3  // gamesave generations retained per slot

// -- Lifecycle and mounts ---------------------------------------------------------------

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

// -- Resolution (escape hatch; new code should prefer ano_res_load) ----------------------

// Absolute OS path where a logical path's bytes live right now -- for parser libraries
// that open files themselves (cgltf sibling URIs, stb_image). Loose-directory mounts
// only: once an asset moves into a pack file (v2), resolution for it returns the empty
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

// -- The read path ------------------------------------------------------------------------

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

// -- The write path: durable and atomic, always under the write root ----------------------

// Durably replace a fixed-name file (config, keybindings -- raw bytes, human-editable).
// Full crash-consistency protocol (docs/resourcesmg.md §4): same-dir O_EXCL temp, write,
// fsync, close, rename, directory fsync; ReplaceFileW/MoveFileExW + FlushFileBuffers on
// Windows with sharing-violation backoff. On any write/fsync error the temp is unlinked
// and fsync is NEVER retried on the same handle (fsyncgate); the caller's buffer is the
// source of truth -- call again to retry.
// Output: 0 only when the replacement is durable on disk; -1 otherwise (previous file
// intact).
int ano_res_write(const char *logical, const void *data, size_t size);

// Rename a damaged file under the write root to "<name>.broken" so the caller can
// regenerate defaults without destroying evidence. Completes the never-crash-on-bad-
// config story. Output: 0 on success, -1 if absent or the rename failed.
int ano_res_quarantine(const char *logical);

// Commit a gamesave generation: framed payload (self-proving completeness -- see the
// frozen frame layout in docs/resourcesmg.md §5) written via the full protocol to a
// BRAND-NEW filename "saves/<slot>.<seq>.anosave" -- a name that never existed cannot
// have a stale 9P/SMB cache entry. Verified via a fresh read handle before older
// generations are pruned (keep ANO_RES_SAVE_KEEP). Commits to the same slot serialize
// internally -- concurrent callers cannot clobber each other's generation.
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

#endif //ANOPTICENGINE_ANOPTIC_RESOURCEMG_H
```

⊕ *Grafts visible above:* `prefix` on `ano_res_mount`; `ano_res_subpath` promoted public;
`ano_res_exists` documented advisory-only; `ano_res_quarantine` shipped in v0 (not "a
future helper"); internal per-slot save serialization (a save mutex 〜 saves are rare
events; the seq-collision clobber window two committers would otherwise race is closed
inside the module, not by caller discipline).

### 3. v0 architecture

- **State:** one static mount table (~2.5 KB), written only during init/mount on the main
  thread, read-only forever after 〜 resolution needs no lock, ever. Debug builds enforce
  the discipline with an atomic `ready` flag.
- **Files:** `include/anoptic_resourcemg.h`; `src/resourcemg/resourcemg_core.c`
  (platform-free: validation/join, mount walk, framing, generation selection, FNV-1a-64);
  `src/resourcemg/resourcemg_{linux,win64,macos}.c` behind an internal `resourcemg_os.h`
  (`rmos_exists`, `rmos_read_all`, `rmos_mkdir_p`, `rmos_open_temp_excl`,
  `rmos_write_all/sync/close`, `rmos_rename_replace`, `rmos_sync_dir`, `rmos_scan_dir`).
  POSIX half shared by Linux/macOS, as filesystem already does.
- **Read path:** resolve → `open(O_RDONLY)` **fresh handle** → `fstat` as hint →
  `mi_heap_malloc_aligned(heap, hint+1, ANO_CACHE_LINE)` → **read loop to EOF**, growing
  via `mi_heap_realloc_aligned` if the stream outruns the hint (9P has done this) →
  `size` = bytes actually read; guard NUL. `posix_fadvise(SEQUENTIAL)` after open (free
  readahead on exactly this access pattern). EINTR/short reads looped. Failures log one
  `ano_log_warn` with the logical path and return the NULL blob.
- **Memory:** caller-visible allocations go in the caller's heap (heap-first parameter);
  idiomatic consumer is a scoped heap (`LOCALHEAPATTR`). No module-owned long-lived
  allocations in v0 〜 which is what makes "no cleanup function" honest.
- ⊕ **Read-chunk constant:** the platform layer reads in chunks ≤ 512 KiB from day one 〜
  the size that dodges the io_uring kernel-worker fallback cliff 〜 so the v2 backend swap
  inherits correct sizing instead of retrofitting it.

### 4. The write protocol (v0 flagship)

Every omitted step below is a named data-loss mode (Pillai et al. OSDI'14 found 60 such
omissions in 11 mature applications). POSIX:

1. Unique temp `target.tmp.<pid>.<counter>`, `O_WRONLY|O_CREAT|O_EXCL 0644`, in the
   **same directory** as the target (same filesystem ⇒ rename is metadata-only; `O_EXCL`
   ⇒ concurrent writers can't share a temp).
2. Write all bytes, looping short writes and EINTR (the existing `ano_fs_write`
   discipline).
3. `fsync(fd)` **before** rename 〜 prevents ext4-style delayed allocation from persisting
   the rename ahead of the data (a named zero-length save after crash). Never rely on
   `auto_da_alloc` or any FS-specific ordering.
4. `close`.
5. `rename(tmp, target)` 〜 atomic namespace swap: old file or new file, never a mix.
6. Open the parent directory `O_RDONLY|O_DIRECTORY`, `fsync` it, close 〜 without this the
   rename itself can revert after a crash.

**Fsyncgate rule, hard-coded:** any write/fsync failure ⇒ unlink the temp, return -1, and
**never re-fsync the same fd** 〜 a failed fsync marks dirty pages clean and drops them, so
a retry falsely succeeds. The caller's in-memory buffer is the source of truth; a retry is
a fresh protocol run. A *directory*-fsync failure after a successful rename logs loudly
but returns 0 (integrity intact; only durability of the rename is in question).

**Windows:** `CreateFileW` temp with share mode 0 (antivirus interference surfaces at step
1, not later), `WriteFile` loop, `FlushFileBuffers` (`FILE_FLAG_WRITE_THROUGH` alone does
not reliably flush the drive cache), `CloseHandle`; then `ReplaceFileW` when the target
exists (**preserves ACLs/attributes 〜 the enterprise `%APPDATA%` requirement**) else
`MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`; both retried up to 5× with
100 ms backoff on `ERROR_SHARING_VIOLATION` (scanners briefly holding fresh files is a top
real-world failure). `MoveFileEx`'s documented silent copy+delete fallback and
`ReplaceFileW`'s `UNABLE_TO_MOVE_REPLACEMENT_2` window are both neutralized by the
generation scheme: recovery never depends on a single name.

### 5. Gamesaves: framing + generations (formats frozen here)

Saves face a threat model the protocol alone can't cover: 9P/SMB attribute caches are
keyed by **pathname**, so every generation gets a **brand-new filename**
`saves/<slot>.<seq>.anosave` (decimal monotonic seq = max existing + 1 from a directory
scan). A name that never existed cannot have a stale cache entry 〜 and rename-replace
becomes unnecessary for saves entirely.

**Save frame, container v1 〜 FROZEN.** Little-endian. 48-byte header:

| off | size | field |
|----:|-----:|---|
| 0 | 4 | magic `'ANOS'` |
| 4 | 2 | container_version = 1 |
| 6 | 1 | hash_id (1 = FNV-1a-64, in-tree; 2 reserved = xxh3-64) |
| 7 | 1 | flags = 0 |
| 8 | 4 | format_version (caller's) |
| 12 | 4 | ⊕ min_reader_version 〜 an old build **refuses** a newer save cleanly instead of misparsing |
| 16 | 8 | payload_len |
| 24 | 8 | seq 〜 echoed from the filename so a renamed file cannot masquerade |
| 32 | 8 | header_hash (FNV-1a-64 over bytes 0–31) |
| 40 | 8 | reserved = 0 |

then payload, then a 16-byte footer: `payload_hash (FNV-1a-64)` + end magic `'ANOSDONE'`.

Truncation is caught three independent ways (length vs bytes read, missing footer, hash);
header corruption is distinguishable from body corruption. FNV-1a-64 because it is already
in-tree (`anostr_hash`) 〜 zero new dependencies; the `hash_id` byte is the agility seam
for xxh3 when saves get large. Post-commit, the file is **re-opened fresh and re-validated
before pruning** 〜 the direct, permanent countermeasure to the original 9P bug.

**Read-back** (`ano_res_save_load`): scan generations, newest seq first; fresh handle each;
validate everything; first pass wins; all fail ⇒ try orphaned `.tmp` files last (this
auto-recovers every partial-failure window, including Windows'
`UNABLE_TO_MOVE_REPLACEMENT_2`), then purge them; still nothing ⇒ NULL-blob sentinel. No
pointer file (reintroduces the atomicity problem it claims to solve); no mtime selection
(untrustworthy on 9P/SMB). Keep-3 pruning deletes oldest-first, only after the newest
verifies.

**Versioning:** `container_version` guards the frame (frozen, small). `format_version` is
the caller's; migration is a chain of in-memory v(n)→v(n+1) functions applied at load,
with the migrated result written back through `ano_res_save_commit` 〜 never in place; the
pre-migration generation survives as a natural consequence of keep-N. A slot whose every
generation fails is reported empty with an error log naming each rejected file and why 〜
the game offers "start fresh", never crashes, never loads garbage.

**Config** rides `ano_res_write` (fixed names, raw bytes, human-editable) with the
write-root-shadows-reads overlay giving user-overrides-shipped-defaults for free; parse
failure ⇒ `ano_res_quarantine` + regenerate defaults.

**SQLite-as-savefile: evaluated, rejected** for this engine 〜 WAL's `-shm` mapping and
fcntl locking are documented-broken over the NFS/SMB/9P family that is precisely our
deployment floor, and a write-a-blob save model gets equal integrity from this protocol at
a fraction of the complexity. Revisit only for incremental large-world saves on
guaranteed-local disk.

### 6. Migration plan (exact call sites)

| Step | Sites | Change |
|---|---|---|
| 1 | `pipeline.c:395,442`; `flat.c:58,63`; `transmission.c:61,66` | each `snprintf(...PROJECT_ROOT...)` + `loadFile` → `ano_res_load(heap, "shaders/X.spv")` with a `LOCALHEAPATTR` scoped heap per pipeline-build function. **`loadFile` (pipeline.c:23) is deleted outright** 〜 its size-then-read antipattern and free()-vs-aligned-free bug die with it. |
| 2 | root `CMakeLists.txt:115,148` | delete `-DPROJECT_ROOT`; add `-DANO_DEV_RESOURCES="${CMAKE_SOURCE_DIR}/resources"` consumed at exactly **one** site, `main()`: the dev mount. Install rule: exe + `resources/`. |
| 3 | `vulkanMaster.c:1458,1477` | move `assets/*` into `resources/models/` (or mount `"models/"` → assets dir via the prefix graft); `parseGltf(&ctx, p.str)` with `p = ano_res_resolve("models/…gltf")`. cgltf resolves sibling `.bin`/image URIs against the `.gltf`'s own directory 〜 zero cgltf changes. |
| 4 | `ano_GltfParser.c:253-255` → `texture.c:18` | image URI joined against the model's resolved directory via `ano_res_subpath` before `stbi_load`. |
| 5 | `src/engine/main.c:129` | delete the chdir shim; later delete `ano_fs_chdir_gamepath` itself when nothing needs it. |
| 6 | logger | optional later one-liner: output dir from `ano_res_resolve_write("logs/…")`. |

`ano_res_resolve` call sites (steps 3–4) are **named migration debt**: the doc comment
self-destructs them (returns empty for pak-backed assets in v2), making the cgltf
memory-buffer migration compulsory rather than aspirational.

### 7. v1 〜 async + identity (spec now, code when needed)

Triggered by the first real level/audio streaming or load-time pain. **New public surface
stays small:** `ano_res_load_async`, `ano_res_poll`, `ano_res_pump`, an 8-byte ticket.

- **Topology:** one IO thread (the logger's exact pattern: owned consumer, park/wake with
  timedwait cap). Inbound: per-band MPSC request rings (the DPDK-style variable-length
  ring from `log_ring.h`); outbound: SPSC completion ring per consumer thread, drained
  by `ano_res_pump()` once per frame on the logic thread. **Completions are only ever
  polled, never callbacks from the IO thread** 〜 the trap Gregory, Bevy, and both in-tree
  ring designs converge on avoiding.
- **Priority bands:** `BLOCKING > FRAME_CRITICAL > STREAMING > PREFETCH`, drained strictly
  in band order (TIP/SOSP'95 disclosure model: the game tells the IO thread its future;
  the IO thread never guesses).
- **Workers call the unchanged sync `ano_res_load`** into worker-owned heaps; blob
  ownership transfers through the completion message 〜 the `RenderCreateBatch`
  ownership-transfer convention, already in-tree.
- ⊕ **Grafted contracts, binding on the v1 implementation:**
  - Tickets are generation-checked `{u32 idx, u32 gen}` values; `{0,0}` on a full ring 〜
    the `ano_render_submit` false-on-full house convention.
  - **A missing file is not a submit error**: the request completes FAILED through the
    ticket path, so sync and async report identically.
  - **Ranged reads** (`offset`/`length`) are in the ticket payload format **from v1 day
    one** 〜 the actual streaming primitive for audio/level/mip paging; retrofitting ranges
    later would touch the ticket format.
  - **Async writes are copy-at-submit**: the payload is memcpy'd into module memory at
    submit, structurally enforcing the fsyncgate source-of-truth rule instead of relying
    on caller discipline.
  - **Prefetch is byte-budget-metered, refilled by `ano_res_pump`** each frame (frame-rate
    protection); a blocking wait on a ticket **boosts it to BLOCKING**.
- **Identity layer (with v1):** `ano_rid` = FNV-1a-64 of the canonical logical path
  (0 = invalid; debug assert-on-collision at registration), a mutex-protected rid→slot
  registry for single-copy loading with per-slot state (QUEUED/LOADING/READY/FAILED).
  Mutex, not lock-free: loads are rare; the rings earn their keep on queues, not maps.
  Group lifetime = one mi_heap per group (`mi_heap_destroy` = bulk free).
- **Hot reload (dev-only)** rides the completion ring for free: 500 ms mtime+size poll
  (inotify misses Windows-side 9P edits), **confirmed by content hash** before a
  frame-boundary swap. ⊕ A loose-file manifest (`manifest.anoidx`) of content hashes
  powers confirmation without packs.
- Backend stays blocking `pread` + `posix_fadvise(WILLNEED)` on queued requests: parallel
  pread saturates NVMe at game-scale IO ("modern storage is plenty fast"); naive io_uring
  buys nothing yet.

### 8. v2 〜 anopak + backend swaps

- **`anopak` mount type** 〜 dumb frozen format, TOC layout pinned now ⊕:
  header `{magic 'ANOPAK\0\1', u32 entry_count, u64 toc_offset}`; TOC entries
  `{u64 rid, u64 offset, u64 size, u64 csize, u8 codec, u8 hash_id, u16 flags, u64 payload_hash}`;
  payloads 4 KiB-aligned. Per-entry compression: LZ4 for latency-critical streaming,
  zstd-3 for bulk, **trained zstd dictionaries** for many-small-similar classes (SPIR-V,
  config), store-raw for already-compressed formats (PNG, Opus). Codec byte reserves
  GDeflate's ID without implementing it. ⊕ **TOC is checksum-verified at mount** 〜 a
  corrupt pack refuses loudly at startup, never lazily at load. A ~200-line builder tool
  wires into install.
- cgltf migrates to memory buffers + file callbacks, retiring every `ano_res_resolve`
  call site exactly as its doc comment promised.
- **io_uring (Linux) / IOCP (Windows) backends** swap in behind the unchanged rings if
  profiling demands: batched submission (depth 8–32), registered buffers, reads chunked
  ≤ 512 KiB (already the platform constant) 〜 one narrow completion-shaped interface,
  N backends (the TigerBeetle lesson).
- `hash_id` flips to xxh3 for large saves; offline conditioning (KTX2 textures, `.glb`)
  becomes worth doing when asset count justifies a cooker.

### 9. Permanent rejections (recorded so they stay rejected)

| Rejected | Why |
|---|---|
| **mmap as a load mechanism** | fault stalls + eviction bottlenecks (CIDR'22 "Are You Sure You Want to Use MMAP in Your DBMS?", libtorrent 2.0 regression); broken-to-forbidden over this project's known 9P/SMB deployment filesystems (SIGBUS on truncation, coherence) |
| **O_DIRECT** | unsupported over 9P/SMB; forfeits the page cache that repeat dev loads love; 4 KiB-aligned pack chunks keep the door open anyway |
| **SQPOLL / IOPOLL / NVMe passthrough** | core-burning database tricks; wrong power/complexity budget for a game |
| **DirectStorage/GDeflate as a dependency** | Win11/D3D12-coupled, MinGW-hostile, mixed shipping results; the reserved codec byte is its entire footprint |
| **SQLite savefiles** | WAL `-shm` + fcntl locking documented-broken over NFS/SMB/9P 〜 exactly our floor |
| **Relocatable defragmenting resource heaps** | mimalloc size-class pages already bound fragmentation; handles exist (v1) for unload/reload/async states, not defrag |
| **128-bit GUIDs + import databases** | team-scale rename-resilience machinery; solo scale greps (Godot's UID retrofit is the cautionary tale in the other direction 〜 mitigated here by hashing *logical* paths, which are already location-independent) |
| **Callbacks from the IO thread** | completion is always polled at a frame boundary; house pattern (logger, render_bridge) |
| **Lock-free registry/resolve** | loads are rare; rings for queues, mutex for maps |
| **Pointer files / mtime-based save selection** | reintroduce atomicity problems; mtime is untrustworthy on 9P/SMB |

### 10. Phasing and exit criteria

**v0** (~2–3 weeks solo; each function ≤ a week), in merge order:

1. Core + POSIX platform file + init/mount/resolve/subpath/exists/load; CMake gains
   `ANO_DEV_RESOURCES` (one use, `main()`) and the install rule; **the six PROJECT_ROOT
   sites and `loadFile` are deleted in the first merge**.
2. Asset migration (glTF/texture sites), chdir shim deleted.
3. `ano_res_write` + quarantine + `save_commit`/`save_load` with full framing, generations,
   Windows path; config overlay demonstrated on one real settings file.
4. Windows platform file exercised under the MinGW cross build; **hostile-FS smoke test**:
   a headless test target with a fault-injection `#ifdef` that kills the process at each
   protocol step and asserts the loader degrades to the previous generation.

**Exit criterion:** an installed tree (exe + `resources/`) runs from any CWD on both OSes,
and a save survives `kill -9` at every protocol step.

**v1** when streaming/load pain arrives 〜 execution work, not design work (spec in §7).
**v2** when shipping/packing matters (spec in §8).

### 11. Sources

- Jason Gregory, *Game Engine Architecture* (3rd ed.), ch. 7 〜 resource GUIDs, registries,
  handles, package files, streaming, load-and-stay-resident.
- Pillai et al., *All File Systems Are Not Created Equal* (OSDI'14) + the ALICE tool 〜
  crash-consistency protocol steps and the cost of skipping each.
- PostgreSQL "fsyncgate" (2018) 〜 failed fsync poisons the page cache; never retry on the
  same handle.
- Crotty et al., *Are You Sure You Want to Use MMAP in Your DBMS?* (CIDR'22).
- Patterson et al., *Informed Prefetching and Caching* (TIP, SOSP'95) 〜 disclosure-based
  prefetch, the priority-band model.
- Glauber Costa, *Modern storage is plenty fast…* 〜 parallel pread vs io_uring at
  application scale; recent io_uring measurement studies (batch depth, worker-fallback
  chunk cliff).
- SQLite documentation: *How To Corrupt An SQLite Database* / appropriate-uses 〜 WAL over
  network filesystems.
- Prior art: Unreal pak/IoStore/Zen loader; Godot `res://`+`user://` and the UID retrofit;
  Bevy AssetServer (typed handles, asset events); sokol_fetch (channels×lanes chunked
  state machine); TigerBeetle (single completion-shaped IO interface, N backends).
- In-repo history: the WSL-9P stale-size incident and its fix (`tests/anotest_logging.c`,
  commit `b589d43`); `src/engine/main.c:128` ("Interim shim until the Resource Manager
  owns asset paths").
