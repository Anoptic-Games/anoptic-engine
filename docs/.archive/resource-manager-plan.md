<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Manager: Implementation & Testing Sequence

Status: historical research and superseded planning material. The authoritative resource-manager specification is `../resourcemanager-comprehensive.md`; authority, API-freeze, implementation-state, threading, and completion claims below are not current unless restated there.

**Status:** the build order. Machinery rationale and sources live in
`resource-manager-SoA.md`; this document is the keepers only, in ascending order, each
step with its test battery and its merge bar.
**House premise:** the hard part is already in-tree. The logger *is* an async system 〜
lock-free MPSC ring in, owned consumer draining behind the callers, 22–48 ns enqueue,
TSan-clean, fuzz-oracled. The render bridge ships SPSC ownership transfer. The async
tier below is a port of shipped machinery, not new design.

---

## 1. Ground rules

1. **One name.** Call sites use logical paths (`"shaders/flat.frag.spv"`); no OS path,
   CWD, or compile-time constant ever appears at a load site.
2. **One read call.** Bytes arrive whole, aligned, guard-NUL'd, in the caller's heap;
   size is bytes read to EOF, never stat metadata (9P/SMB lie).
3. **Writes are durable or refused.** Old-complete or new-complete on disk, never torn.
4. **Identity is an integer.** rid = FNV-1a-64 of the logical path: `ANOSTR_SID` at
   compiled sites (zero cost), `anostr_hash` for parsed/discovered strings 〜 one key
   space.
5. **Bytes vs meaning.** The resource manager never parses. cgltf, stb_image, FreeType,
   the config parser, the future audio decoder keep the meaning half; they stop doing
   their own file I/O.

Every keeper below passes the flatness test: hot path is an array walk, synchronization
is one atomic word, keys are integers, lifetimes are regions, recovery is a linear scan.

## 2. The keepers

| # | Keeper | Shape |
|---|---|---|
| 0 | namespace over ordered roots | static table ≤ 10 rows, linear scan, written once |
| 1 | blob reads into caller heaps | one call, one allocation, heap = lifetime |
| 2 | durable write + save generations | straight-line protocol; directory = the data structure, newest-that-verifies wins |
| 3 | integer identity + flat registry | rid-keyed open addressing (the intern-table shape) |
| 4 | ring transport | the logger's ring + drain thread + polled SPSC completions, generalized |
| 5 | chunk pool + ranged reads + codec | fixed 512 KiB blocks, free list over a flat array; LZ4/zstd plain |
| 6 | pack + load-in-place bake | TOC = sorted array binary-searched by rid; model = PODS image + offset fix-up loop |
| 7 | parallel pread | queue depth via 2–4 threads; merges only on a failed bench |

## 3. The sequence

Each step is independently mergeable. **Lands** = new capability. **Deletes** = the
hardcoded path it kills (proof the generalization is real). **Tests** = the battery.
**Bar** = what must be true to merge.

### Step 0 〜 the namespace and the read path

- **Lands:** `include/anoptic_resourcemg.h`; `src/resourcemg/` (platform-free core +
  POSIX/win64 TUs behind an internal os header, the filesystem module's split).
  `ano_res_init` (write root = `ano_fs_userpath()`, base = `<gamepath>/resources`),
  `ano_res_mount` (dev build adds the source tree's `resources/` in `main()`, one call
  site), `ano_res_resolve`/`_subpath`/`_exists` (escape hatches, documented as such),
  `ano_res_load` with the full read contract: fresh handle, fstat as hint only, read
  loop to EOF, cache-line-aligned allocation in the caller's heap, one guard NUL.
  CMake: `ANO_DEV_RESOURCES` define consumed only in `main()`; the install rule
  (exe + `resources/`).
- **Deletes:** nothing yet 〜 this step is pure capability.
- **Tests:** new `anotest_resourcemg` (labels `unit;mem`): logical-path validation is
  total (fuzz hostile paths 〜 backslashes, `..`, leading `/`, overlong; all refuse, none
  UB); shadow order (same name in two roots, write root wins); read contract oracles 〜
  bytes byte-identical to a reference buffer, size == bytes written, guard NUL present,
  pointer alignment, absent file → NULL blob + one log line. Scratch dirs per
  `templates/scratch.h`, deleted on exit.
- **Bar:** any code loads any staged file by logical name from any CWD, Debug and
  Release, both toolchains.

### Step 1 〜 shaders ride it

- **Lands:** all ~20 `loadFile` sites (`pipeline.c`, `flat.c`, `transmission.c`,
  `additive.c`) become `ano_res_load(heap, "shaders/X.spv")` with a `LOCALHEAPATTR`
  scoped heap per pipeline-build function.
- **Deletes:** `loadFile`, `openEngineFile`, `struct Buffer`, and with them the
  `fseek`/`ftell` size-then-read antipattern and the `free()`-vs-`ano_aligned_free`
  inconsistency.
- **Tests:** the existing suite is the harness 〜 `build.bat 5` and `8` green, the vk
  tests exercising every migrated pipeline; one manual engine run launched from a
  foreign CWD.
- **Bar:** zero shader-path strings outside `ano_res_load` calls; suite green.

### Step 2 〜 models, textures, fonts ride it; the shim dies

- **Lands:** glTF sites take logical names (`ano_res_resolve` bridging cgltf's
  self-opened files initially 〜 each such call site is named migration debt); image
  URIs join via `ano_res_subpath` before `stbi_load`; fonts load as blobs +
  `FT_New_Memory_Face` (the text module's heap already owns bake blobs, the font bytes
  join them).
- **Deletes:** the `main()` chdir shim, then `ano_fs_chdir_gamepath` itself; the
  hand-rolled gamepath join and 512-byte buffer in `text_raster.c`.
- **Tests:** engine smoke from a foreign CWD on both OSes; an installed-tree run
  (exe + `resources/`, no source tree); `anotest_text` unchanged and green.
- **Bar:** CWD is irrelevant to the engine; nothing but the resource manager and the
  logger's append stream opens a file by path (grep-enforceable).

### Step 3 〜 durable writes, saves, and the first write clients

- **Lands:** `ano_res_write` (same-dir exclusive temp → write → fsync → rename → dir
  fsync; ReplaceFileW path on Windows; failed fsync = unlink and report, never retry the
  handle), `ano_res_quarantine` (`<name>.broken`), `ano_res_save_commit`/`_load`
  (framed payload 〜 magic, versions, length, FNV-1a-64 content hash, end marker; each
  generation a brand-new `saves/<slot>.<seq>.anosave` filename; load = scan, newest
  that verifies wins, keep 3, prune only after the newest verifies). First clients ride
  immediately: `anoptic_config.h` (jsmn parse, `ANOSTR_SID`-keyed switch into a typed
  store, quarantine-and-regenerate, one real settings file) and keybindings as a config
  domain (scancode → `ANOSTR_SID` action-id table; the hardcoded GLFW switches in
  `main.c`/`instanceInit.c` become action dispatches).
- **Deletes:** hardcoded key handling; the "no settings persist" state of the engine.
- **Tests:** frame round-trip and corruption battery in `anotest_resourcemg` (truncate,
  bit-flip header vs body, rename-masquerade a generation 〜 all detected, loader
  degrades one generation); **fault-injection harness**: a `#ifdef`-gated child process
  killed at every protocol step, parent asserts old-complete-or-new-complete and that
  save_load never returns garbage (the hostile-FS smoke test); config round-trip +
  quarantine test (corrupt file → boot succeeds, `.broken` preserved, defaults
  regenerated).
- **Bar:** `kill -9` at any instant leaves every user file readable; a corrupt config
  cannot prevent boot; rebinding movement keys survives relaunch.

### Step 4 〜 identity registry, and levels as its client

- **Lands:** the rid registry 〜 open addressing over cached hashes, dense slots (the
  `anostr_intern_t` shape re-instantiated), single-copy enforcement, debug
  assert-on-collision at registration; diagnostics strings through the intern table
  (sparse rid for identity, dense sym for side arrays). Client: the level module 〜
  `levels/demo.json` (jsmn) naming models/transforms/lights; each reference hashed at
  parse into the registry; the demo scene becomes data.
- **Deletes:** the three hardcoded `parseGltf` filenames and `char name[64]` identity
  in `ModelAsset`; no asset filename remains in any `.c` file.
- **Tests:** registry round-trip and growth (the intern-table test shape); the
  `ANOSTR_SID(x) == anostr_hash(anostr_lit(x))` bridge asserted at the module boundary;
  level-load equivalence 〜 the data-driven scene renders the same frame the hardcoded
  scene did (the render suite as oracle).
- **Bar:** the shipped scene is a level file a mod can shadow via the write root.

### Step 5 〜 the transport (port, not design)

- **Lands:** the logger's variable-length MPSC ring moves to `anoptic_collections.h`
  as the generic ring it was always destined to be (the logger re-consumes it 〜 one
  ring, two users). On it: request ring (payload `{rid or path, offset, length, band,
  ticket}` 〜 **ranges in the format from day one**), one IO thread (the drainer's
  park/wake and shutdown discipline verbatim), blocking `pread` +
  `posix_fadvise`, per-consumer SPSC completion rings drained by a per-frame pump.
  Tickets are `{u32 idx, u32 gen}`, false-on-full (render bridge convention). **Two
  bands: NOW and LATER** 〜 a blocking wait on a LATER ticket boosts it; four-band
  scheduling and byte-budget metering stay rejected until a starvation bench exists.
  Missing file completes FAILED through the ticket, identical to sync.
- **Deletes:** nothing 〜 sync `ano_res_load` remains the primitive the IO thread calls.
- **Tests:** **TSan is mandatory** (`build.sh 7` under WSL 〜 concurrent code touched);
  `anotest_res_async` fuzz with the logfuzz oracle style: N producer threads submit
  randomized loads under full-ring pressure, every ticket completes exactly once, async
  bytes byte-identical to a sync load of the same name, FAILED count == missing-file
  count; `anotest_resbench` (bench, DISABLED in ctest): p50/p99/p99.9 per band while a
  background stream runs, bulk-load wall time vs step-0 sequential baseline.
- **Bar:** TSan-clean; oracle holds over a soak; a level's bytes stream in the
  background with zero frame hitches on the demo scene.

### Step 6 〜 the streaming economy

- **Lands:** the chunk pool (fixed 512 KiB blocks, free list over a flat array,
  false-on-empty); ranged reads exercised for real (audio/mip shape); LZ4 and plain
  zstd decode on a worker, pipelined so chunk N decodes while N+1 reads. Store-raw for
  already-compressed payloads.
- **Deletes:** nothing; capacity only.
- **Tests:** range correctness vs whole-file oracle (random offset/length fuzz); pool
  exhaustion returns false-on-empty, never blocks the IO thread; `anotest_resbench`
  gains the compressed corpus 〜 **bar: effective bandwidth on compressed assets exceeds
  raw drive bandwidth** (the compression-as-bandwidth-amplification claim, proven or
  the codec work reverts).

### Step 7 〜 the pack and the bake

- **Lands:** `anopak` mount type 〜 TOC as a sorted flat array of
  `{rid, offset, size, csize, codec, hash}` binary-searched by the same integer
  compiled call sites already hold; TOC checksum verified at mount (corrupt pack
  refuses at startup, never mid-game); payloads 4 KiB-aligned; a ~200-line offline
  builder. Load-in-place bake for one class end-to-end (models): PODS image, pointers
  as offsets, one fix-up loop over an offset table at load 〜 zero runtime parsing.
  Loose files keep shadowing packs (dev loop and modding unchanged).
- **Deletes:** runtime JSON parsing for baked models; cgltf leaves the shipped path
  (remaining a dev-import tool), retiring the last `ano_res_resolve` debt.
- **Tests:** bake determinism (same input → byte-identical pack); TOC bit-flip refuses
  at mount; shadow test (loose file wins over pack entry); load-equivalence (baked
  model renders identically to the cgltf path 〜 the fontTools-oracle culture applied to
  models); `anotest_resbench` TOC-lookup series.
- **Bar:** the demo scene loads with zero parse work and zero path strings at runtime.

### Step 8 〜 parallel pread, on demand only

- **Lands (maybe never):** 2–4 IO threads pulling from the same request ring.
- **Bar to even start:** `anotest_resbench` shows rung 0 leaving the drive idle while
  requests queue. **Bar to merge:** beats single-thread p99 on the streaming series.
  io_uring/IOCP remain a recorded rung below this one, expected to stay unclaimed at
  this engine's asset scale.

## 4. Standing rejections

| Rejected | The flat alternative that won |
|---|---|
| reference-counted lifetimes | group = one mi_heap, teardown = bulk free |
| handles + generations (now) | level-lifetime heaps; revisit only when unload-while-running exists |
| four priority bands + byte budgets | two bands + boost-on-wait, until a starvation bench |
| io_uring / IOCP / SQPOLL / O_DIRECT default | parallel `pread`; cold-start tricks lose the warm dev loop |
| zstd dictionaries | plain zstd until a pack bench shows the gap |
| sectioned files (4-way) | two destinations: CPU blob, GPU staging |
| composite-resource integrity graphs | one baked contiguous image + integer ids through one registry |
| mmap loads, callbacks from the IO thread, pointer files, mtime save selection, SQLite saves | per `resource-manager-SoA.md` §6 and the write protocol |

## 5. Done means

- Installed tree runs from any CWD on Windows and Linux; no code but the resource
  manager and the logger opens a file.
- `kill -9` at every protocol step: every user file old-complete or new-complete; a
  torn newest save degrades one generation, silently to the user, loudly in the log.
- Every resource-naming site is an `ANOSTR_SID` literal or a runtime hash of data-file
  strings; no raw filename keys anywhere.
- TSan-clean transport whose fuzz oracle (every ticket exactly once, bytes identical to
  sync) holds over a soak; streaming a level produces zero frame hitches.
- Compressed assets read faster than the drive's raw bandwidth; the baked demo scene
  loads with zero runtime parsing.
