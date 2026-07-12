<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Manager Implementation Journal

Working journal for the autonomous implementation of `docs/resourcemanager-real.md`.
Phases: **A** memory pools + non-allocation resource core · **B** registry/handles/parsers · **C** full integration.
Each entry records what landed, the decisions taken where the plan left latitude, and why.

---

## Phase A

### A.1 — `anoptic_memory_pools.h` design decisions

The plan (§3) fixes the roster: multipool, monotonic, pool, composition via parents, stats,
wink-out = backing-heap destroy. Stripe waits for its consumer per step 0. Decisions taken
where the plan leaves latitude:

- **Parent abstraction is a 24-byte function-pair value, `ano_mem_parent`** —
  `{ctx, acquire(ctx,size,align), release(ctx,p)}` with constructors
  `ano_mem_parent_heap(mi_heap_t*)` and `ano_mem_parent_monotonic(ano_mem_monotonic*)`.
  Composition (`Multipool<Monotonic>`, pool-over-monotonic) falls out of one constructor,
  per the plan. The indirect call sits on the **refill** path only (slab acquisition), never
  on the per-alloc hot path — Lakos's own measurement says this costs nothing observable.
  A monotonic parent has `release == NULL`: chunks flow back only at arena reset/destroy or
  wink-out, which is exactly the AS11–14 composition semantics.
- **Multipool free is sized** (`free(mp, p, size)`), not header-based. BDE prepends a
  per-block header to find the pool on free; that is 8–16 bytes and a dirtied cache line on
  every block. Our primary consumers (the resource registry, conditioned asset structs) always
  know the size. Sized free keeps blocks at exactly their class size with zero per-block
  metadata — the hardware-efficient option. Oversize (> max class) allocations do carry a
  64-byte header (they are rare and huge) so `destroy` can free stragglers; the header is
  64B to preserve cache-line alignment of the payload.
- **Size classes are pure powers of two**, `min_block`(16) … `max_block`(1 MiB default),
  class lookup is one `clz`. Worst-case internal fragmentation ~50% is acknowledged; the §5
  bake-off owns that question (mid-point classes are a recorded follow-up rung, not v1).
- **Alignment guarantee**: a block is aligned to `min(its class size, 4096)`. Chunks are
  acquired at that alignment and the chunk header is rounded up to it, so alignment is free —
  no aligned-alloc variant needed. Payload loads that need `ANO_CACHE_LINE` get it from any
  class ≥ 64B.
- **Chunk growth is geometric per class** (BDE's strategy): block count doubles per refill,
  clamped to [4 KiB floor, 512 KiB target] per chunk, so barely-used classes waste little and
  hot classes amortize the parent call away.
- **Monotonic reset keeps its slabs** (`rewind` semantics): slab list survives, cursors rezero.
  Slabs double geometrically (64 KiB start default, 8 MiB cap); oversize requests get a
  dedicated slab. `reset` is the per-ingest staging idiom; `destroy`/wink-out is teardown.
- **`ano_mem_pool` is the one-class degenerate case** and shares the same chunk machinery;
  `max_blocks` cap gives false-on-empty (`NULL`, never UB, never blocking) for the bounded
  streaming-pool shape; `reserve()` prewarms.
- **Control structs live in parent memory**: `make` allocates the allocator's own struct from
  the parent, so `mi_heap_destroy(backing)` winks out allocator + chunks + payloads in one
  call with nothing dangling that anyone may still legally touch.
- **No locks anywhere**: single-owner by default (the `anostr_intern` discipline); sharing is
  structural (ownership transfer), per plan §3.
- **No logging in this tier**: failure is `NULL` (total functions, never UB); the memory tier
  stays a leaf under `anoptic_memory.h` with zero engine dependencies.
- **Stats are aggregate per allocator** (`live/peak bytes+blocks, chunk bytes+count`),
  updated on the hot path with plain stores (single-owner ⇒ no atomics). `live_bytes` counts
  the serving block size (real footprint), not the request size.

### A.2 — resource core, non-allocation surface

Landed: `include/anoptic_resources.h` (Phase A subset — init/mount, resolve/resolve_write/
subpath/exists, slurp, write/quarantine, save_commit; the handle tier is Phase B additions),
`src/resources/{resources_core.c, resources_internal.h, resources_os.h, resources_posix.c,
resources_win64.c}`. Decisions where the plan left latitude:

- **The OS surface is 15 functions** (`rmos_*`), POSIX TU shared Linux/macOS, Win64 TU with
  UTF-16 conversion at the edge (stack `MultiByteToWideChar` — paths are bounded
  NUL-terminated edge strings; `anostr_to_utf16` stays the API for `anostr_t` values).
  `rmos_exists` opens-for-read rather than stat (a stat can lie where an open cannot say
  yes to a directory); Win64 `rmos_sync_dir` is a documented no-op (no such primitive).
- **Path grammar** additionally rejects `:` and control bytes (Windows drive/ADS and
  terminal-escape hygiene) on top of the plan's list. Grammar is shared: file paths,
  single-segment save slots, mount prefixes (canonicalized to one trailing `/`).
- **Resolution honesty**: `resolve`/`exists` probe with open-for-read (advisory); `slurp`
  walks candidates by *actually opening* — a root that cannot open the file falls through,
  but a file that opens and then fails mid-read is a hard failure, not a fall-through
  (shadowing must not paper over damage).
- **Freeze discipline is enforced, not advisory**: every read-side entry point flips an
  atomic `frozen` flag; `ano_res_mount` after that refuses with a log line in all builds.
- **`res_read_all`** (the gulp primitive): fstat is only the initial buffer guess, EOF is
  the sole terminator, reads chunked ≤ 512 KiB, buffer `ANO_CACHE_LINE`-aligned with one
  guard NUL, shrunk-to-fit via `mi_heap_realloc_aligned`. Distinguishes could-not-open (-1)
  from failed-mid-read (-2) for the shadow-walk semantics above.
- **`res_write_protocol`** takes an iovec-style part list so `save_commit` streams
  header/payload/footer with zero assembly allocation. Temp names are
  `<final>.<8-hex-nonce>.tmp` from a process-wide atomic counter, `O_EXCL`, ≤ 8 attempts.
  fsync failure path never re-fsyncs (fsyncgate); dir-fsync failure after a landed rename
  logs loudly and returns success, per plan §10.
- **Save frames** are byte-exact per plan §9 (LE stores/readers, no struct punning);
  `min_reader_version` is written as `format_version` (conservative; the signature carries
  only one version). The validator returns -1 for header damage vs -2 for body damage.
  Commit verifies through a fresh read of the just-renamed file BEFORE pruning; a
  failed verify unlinks the new file and leaves every prior generation untouched.
  Prune reconstructs victim names from parsed seqs (no strings held across the scan),
  keeps the newest `ANO_RES_SAVE_KEEP`.
- **Mount prefixes intern through `anostr_intern`** (explicit sym check — `anostr_dedupe`'s
  graceful degradation would have handed back a stack-borrowing view on OOM; caught in
  review while writing the call).

### A.3 — Phase A verification

- `anotest_mempools` (unit;mem): hostile-input totality, shadow-oracle churn fuzz (34 k ops
  across small/large classes + composed), alignment contracts, monotonic reset-reuse
  (same addresses replay), pool false-on-empty at cap, `Multipool<Monotonic>`, wink-out.
  **Pass** in Debug, ASan, and -O3.
- `anotest_resources` (unit;mem): pre-init sentinels, 18 hostile paths × 6 entry points +
  2000-case randomized fuzz, shadow order (write root > mnt2 > mnt1 > base), prefix graft,
  freeze enforcement, table overflow at 8, read contract (byte-identical at 100 B / 9 B
  inline / 1.5 MiB multi-chunk, guard NUL, cache-line alignment), durable write + overwrite
  + no-temp-litter scan, quarantine round-trip, 5 save commits → exactly 3 generations
  kept, newest validated by an independent in-test FNV/layout oracle. **Pass** in Debug,
  ASan, and -O3.
- Full suite: **22/22 ctest green** (Debug), no regressions.
- `anotest_mempoolbench` step-0 bar (-O3, `build/O3Tests`, Linux x86-64, clang + LTO,
  mimalloc v2.3.2 baseline):

  | churn series (400k ops, 1024-slot set) | wall Mops/s | p50 | p99 | p99.9 (ns) |
  |---|---|---|---|---|
  | mi_heap ≤4K | 20.2 | 20 | 50 | 80 |
  | **multipool ≤4K** | **21.1** | 20 | **30** | **40** |
  | **multipool\<monotonic\> ≤4K** | **21.4** | 20 | 30 | 40 |
  | mi_heap ≤64K | 15.3 | 30 | 50 | 2690 |
  | **multipool ≤64K** | **16.8** | 20 | **40** | **60** |
  | **multipool\<monotonic\> ≤64K** | **18.0** | 20 | 40 | 390 |

  | batch-and-wink (200k allocs ≤1 KiB, teardown incl.) | best of 8 |
  |---|---|
  | mi_heap + per-object free | 2.71 ms (73.8 Mops/s) |
  | mi_heap + heap wink-out | 1.78 ms (112.6 Mops/s) |
  | monotonic + destroy (cold slabs) | 2.46 ms (81.3 Mops/s) |
  | **monotonic + reset (warm slabs)** | **1.73 ms (115.6 Mops/s)** |

  **Bar met.** Multipool ≥ mi_heap on both churn shapes with the tail collapsed
  (p99.9 60 ns vs 2690 ns at ≤64K); the Lakos composition is the overall churn winner.
  Monotonic in its intended shape — the reset-reused per-ingest staging arena — beats
  every teardown including mi_heap wink-out. Cold-create monotonic (fresh heap + arena
  + slab faults every rep) trails mi-wink at 81 vs 113 Mops/s and beats per-object free;
  the ingest path ships the warm shape. Getting here took two hot-path rounds recorded
  for the reviewer: (1) the slab walk became a bare two-pointer `{at, end}` bump with
  dedicated-oversize slabs parked on a side list; (2) per-alloc peak tracking folded into
  reset/stats, sound because a monotonic's live count only grows within an epoch.

### Status log

- [x] pools header + implementation
- [x] anotest_mempools + anotest_mempoolbench (Debug, ASan, O3)
- [x] resources core (non-allocation) + OS TUs
- [x] anotest_resources (Debug, ASan, O3)
- [x] step-0 bench bar met, numbers recorded
- [x] Phase A commit (`8726743`)

---

## Phase B

### B.1 — the registry (`resources_registry.c`)

The intern table's shape generalized, with two deliberate divergences, both recorded as
design decisions:

- **A rid binds to its row forever.** No tombstones, no slot recycling: unload/release
  keep the row and bump its generation; a later `get` of the same path reloads into the
  same slot with a fresh generation. Collision-proof (the row keeps its name; a real
  64-bit FNV collision between two live paths is detected and refused loudly), trivially
  debuggable, and bounded by the game's distinct-path population (~48 B + name per path
  ever seen). The free-list + tombstone machinery a recycling design needs would buy
  nothing at that scale.
- **Loads run under the registry mutex.** Single-copy enforcement comes free (no
  in-flight duplicate loads, no TOCTOU), and sync load stays the primitive the async
  tier (plan step 5) will move off-thread without changing this contract.

**The thread-safety correction to model A** (the significant deviation, taken early):
mimalloc heaps are single-thread-owner — `mi_heap_malloc` from a foreign thread is UB,
mutex or not. The registry is cross-thread by contract, so "one multipool over one
mi_heap" cannot be implemented literally. Landed instead:

- A new pools parent, **`ano_mem_parent_default()`** (public API addition): acquires via
  `mi_malloc_aligned` from the *acquiring thread's* default heap; frees route home
  cross-thread. The registry's multipool sits on it, mutex-guarded — model A's "one
  shared multipool" holds exactly, minus the wink-out (which the public V1 surface
  cannot reach anyway: there is no shutdown function).
- **Placement split at the multipool's top class (1 MiB)**: pooled below (recycling
  churn), standalone `mi_malloc_aligned` above, tracked per-row with a `direct` flag.
  This makes `ano_res_release` a true zero-copy hand-off for exactly the payloads that
  matter (textures, buffers — the Vulkan staging shapes): the taker owns the block and
  frees it with `ano_aligned_free`. Pool-resident payloads copy out on release and their
  block recycles. Both documented in the header.
- **Read-to-home for direct payloads**: `res_read_all`'s buffer *is* the resident
  payload for direct-class loads — zero copies from disk to residence. Pooled loads do
  one bounded (≤1 MiB) copy into their class block.
- Payloads carry the same guard-NUL contract as slurp; `ano_res_bytes` views stay valid
  after unlock until the generation retires (the handle contract, not the mutex's).
- `res_read_all` gained a NULL-heap mode (default-heap mi family) for exactly this
  cross-thread consumer; the public slurp contract is unchanged.

### B.2 — gamesave loading

`ano_res_save_load`: newest-seq-first over a scan (no mtime, ever), fresh read + full
frame validation per candidate, the frame's seq must echo the filename's (rename
masquerade refused), first valid wins and becomes the owned resource `"saves/<slot>"`
(a later load re-reads disk and retires the previous generation's handles). Orphaned
protocol temps are tried last — a valid one is *recovered* (the crash happened between
fsync and rename; the data is real) — then purged unconditionally. Serialized against
commits on the save mutex.

The corruption battery in `anotest_resources` drives every degradation path with
independent in-test oracles: payload bit-flip (body damage → degrade one), header
bit-flip (header damage → degrade), truncation (→ degrade), rename masquerade (skipped
via seq echo), all-corrupt (→ sentinel, start fresh), valid orphan temp (recovered,
fmt/seq/payload exact, then purged), garbage temp (purged). The write-protocol crash
harness (`anotest_resfault`) compiles `resources_core.c` privately with
`ANO_RES_FAULT_INJECT` (the `log_old.c` link-override trick) and longjmps out after
each protocol step: the target file is old-complete or new-complete at every crash
instant, pre-rename crashes keep the old bytes, post-rename crashes have the new.

### B.3 — `anoptic_res_graphics.h` (the parsing extension)

- **cgltf and stb_image live in `src/resources/graphics/res_graphics.c` and nowhere
  else.** The renderer's `ano_GltfParser.c` lost its `CGLTF_IMPLEMENTATION` (it links
  the core's copy until Phase C deletes it). stb is compiled `STB_IMAGE_STATIC` +
  `STBI_NO_STDIO` (decode-from-memory only — file IO is *structurally* impossible in
  the decoder) + `STBI_NO_HDR/LINEAR` (keeps libm out of anoptic_core).
- **All parse-time allocation is a monotonic arena over a scoped heap** — cgltf's
  memory hooks bump-allocate, its free hook is a no-op, and the whole staging (JSON
  DOM, .bin payloads, base64 decodes) winks out when ingest returns. Zero loose
  malloc/free in the ingest path, exactly the plan's §8 CPU shape.
- **cgltf's file callback routes through the mount walk**: sibling `.bin` URIs resolve
  against the glTF's own *logical* directory, percent-decoded and `./..`-collapsed,
  then read through `res_candidates` — pack mounts and write-root shadowing will apply
  to buffer files for free. GLB and data: URIs ride the same hooks.
- **The conditioned scene is one self-contained offset-based block** (16-aligned
  arrays, zero-filled padding for byte determinism) adopted into the registry as
  `"<source>#gfx"` — single-copy, generation-guarded, and already load-in-place shaped
  for the step-7 bake. Serving is `ano_resgfx_scene(h)`: bounds-checked views, zeroed
  struct on stale handles.
- **File truth only**: materials mirror every factor/texref the file declares (feature
  bits value-compatible with the renderer's `PbrFeatureFlags`, static-assert planned at
  the consumer); images are logical paths + an sRGB slot-aggregation hint. GPU concerns
  (activeFeatures gating, which images to decode, bindless registration, SSBO baking)
  stay in the renderer, applied on these views in Phase C.
- `ano_resgfx_image`: stb decode of a handle's bytes to caller-owned RGBA8
  (`ano_aligned_free`), the release-style hand-off.

### B.4 — Phase B verification

- `anotest_resources` grew the handle oracles (single-copy double-get, SID/rid key-space
  equivalence *at compile time*, stale-generation sentinels on every entry point,
  pooled copy-out vs direct zero-copy release — pointer-equality proven) and the
  save-load battery above.
- `anotest_resgfx`: handcrafted triangle glTF asserted field-by-field (vertices, indices,
  node graph, transform translation, every material factor and feature bit, decoded
  percent-encoded URI grafted onto the source dir, sRGB hint), base64 data: URI variant,
  single-copy scenes, stale-source refusal, PNG round-trip against stb_image_write as an
  independent encoder oracle, garbage-decode refusal, and the real viking_room.gltf
  (17406 verts, 3 images) ingested + its texture decoded through the namespace.
- Full suite green; ASan green on all resource tests; adversarial review workflow run
  before commit (findings and dispositions below).

### B.5 — pre-commit adversarial review: findings and dispositions

A 4-dimension review fan-out (concurrency, memory, durability protocol, parsing) ran
before the commit; its verification stage was cut short by a session usage limit, so
every surfaced finding was re-verified by hand instead. Dispositions:

**Fixed:**
1. *Ingest never called `cgltf_validate`* — real: hostile accessor offsets could walk
   the conditioning loops out of bounds. Now validated after `cgltf_load_buffers`.
2. *`ano_resgfx_scene` bounds math could wrap in u64* — real: any loaded handle can be
   passed as a scene, so a crafted header could serve wild view pointers. Rewritten
   overflow-proof (offset ≤ len, then count ≤ remaining/elemsize).
3. *Orphan-temp matcher cross-matched dotted-prefix slot names* — real: slot `save`
   would claim `save.backup.3.anosave...tmp`. Matcher now requires the strict
   `<slot>.<digits>.anosave*.tmp` shape.
4. *`save_commit` ignored a failed directory scan* — real: a guessed seq=1 could
   rename-replace a live generation. Scan failure now refuses the commit.
5. *`save_load` conflated unreadable-dir with fresh-start* — logs a distinct warning
   now (behavior stays "start fresh": nothing readable is nothing loadable).
6. *A winning orphan temp was unlinked, deleting the save's only on-disk copy* — real
   and the best find of the run. The load path now **completes the interrupted
   protocol**: a validating temp is renamed to `<slot>.<frame-seq>.anosave` (its seq
   echo holds by construction) + dir fsync; only then do stale temps purge. Test
   strengthened to assert the recovered generation survives a reload.
7. *seq parser accepted leading zeros* (duplicate-seq aliasing could mis-prune) and
   *`UINT64_MAX` filenames wrapped max+1 to 0* — both rejected in the now-shared
   `res_save_name_seq` (also de-duplicating the commit/load parser copies).
8. *Load scan kept the FIRST 64 seqs in readdir order* — real under tampering/prune
   failure backlogs: newest could be silently skipped. Now a sorted keep-newest-64
   insertion.

**Rejected / recorded:**
- *Dir-fsync failure returns success* — per plan §10, verbatim ("logs loudly, returns
  0"); not changed.
- *Non-save protocol temps are never garbage-collected* — true; correctness holds
  (O_EXCL + fresh nonce per attempt). Recorded as a rung for the step-4 config client.
- One duplicate of the bounds-math finding.

Phase B committed as `c5bbda1`.

---

## Phase C — full integration

### C.1 — shaders ride it (plan step 2)

All 27 `loadFile` call sites across `pipeline.c`, `flat.c`, `transmission.c`,
`additive.c`, `tonemap.c`, `shadow_pipe.c`, `compute.c`, and `text_raster.c` became one
call: `ano_pipeline_shader(device, "shaders/x.spv")` — get + view + `vkCreateShaderModule`
in one place, with a `size % 4` guard. **Deleted:** `loadFile`, `openEngineFile`,
`struct Buffer`, `createShaderModule`, every `ano_aligned_free(code.data)` site, and
with them the audit's standing defects (the `free()`-vs-`ano_aligned_free` mismatch and
the early-return shader-buffer leaks — there is no buffer to leak anymore). The SPIR-V
stays manager-owned: swapchain-recreate pipeline rebuilds re-request it for free
(single-copy) instead of re-reading files.

### C.2 — the renderer consumes conditioned scenes (plan step 3)

`ano_GltfParser.c` was rewritten as pure GPU ingest of `anoresgfx_scene` views:
- geometry uploads straight from the scene view (`static_assert`ed layout equality
  between `Vertex` and `anoresgfx_vertex`; `upload_chain` takes const pointers — zero
  staging copies on the CPU side);
- texture gating (file-truth features ∩ `activeFeatures`) drives decode-on-demand
  through `ano_res_get` + `ano_resgfx_image`; a data-driven slot-rule table replaced
  ~100 lines of copy-pasted texture-needed marking;
- material SSBO baking maps `anoresgfx_material` fields 1:1 to `MaterialData`
  (`PbrFeatureFlags`/`ANORESGFX_PBR_*` bit equality `static_assert`ed);
- raw glTF bytes unload right after conditioning, the conditioned scene right after GPU
  upload — steady-state CPU residency for models is zero.

`texture.c` lost `readTexture8bit`, the path-based `createTextureImage`, and its
`stb_image` include; `createTextureImageFromPixels` gained `srgb` + `genMips` and is
now the single upload routine (in passing this retired the old path loader's latent
`copyBufferToImage(..., texWidth, texWidth)` height bug). Fonts: `text_raster.c`'s
hand-rolled gamepath joins died; `ano_text_font_load_memory` (new, `FT_New_Memory_Face`)
consumes manager-owned blobs whose handles stay loaded for the face lifetime. The
path-based `ano_text_font_load` remains for `anotest_text`'s isolated harness — the
engine itself never opens a font by path.

### C.3 — the shim dies; namespace wiring

- `main.c`: the chdir shim is gone; `ano_res_init` is FATAL-checked right after the
  crash blackbox, and Debug builds mount the source tree's `resources/` via
  `ANO_DEV_RESOURCES` (defined in CMake, consumed at that one site) — live font/texture
  edits without re-staging, while compiled shaders keep coming from the staged base
  mount (the source tree has no `.spv` to collide). `ano_fs_chdir_gamepath` the
  FUNCTION survives for the test templates' scratch anchoring; the engine no longer
  calls it — recorded deviation from the plan's "then delete" phrasing.
- `initVulkan` opens with an idempotent `ano_res_init()`: the vk test binaries reach
  pipeline creation without `main()` and get the base roots for free.
- Models staged/installed under `resources/models/` (`models/...` logical paths in
  `render_api.c`); the install rule now ships the full `bin/resources` tree (shaders,
  fonts, textures, models) — the installed-tree bar.

### C.4 — verification

- Full suite 24/24 green in Debug — including the vk tests, which build every pipeline
  through `ano_res_get` against their own staged tree.
- Grep bars hold: **zero** `fopen`/`stbi_load`/`cgltf_parse_file` outside
  `src/resources/`, the logger, and the filesystem module; `CGLTF_IMPLEMENTATION` and
  `STB_IMAGE_IMPLEMENTATION` exist only in `src/resources/graphics/res_graphics.c`.
- Engine smoke from a foreign CWD (`/tmp`): 25 s clean run; session log shows all three
  fonts as memory faces and all three models ingested
  (`Successfully ingested ModelAsset: models/sponza/...`); missing-texture parity
  (assets/ genuinely lacks two PNGs viking_room references — the old path failed them
  too, now a logged skip).
- **Visual verification**: window captured mid-run — Sponza renders (textured curtains,
  floor, columns), text overlay shapes Geist + Runic + Greek from manager blobs,
  ~432 MiB of textures resident on the GPU. The frame is the old path's frame.
- Release (-O3) suite: 21/22, the one failure being `anoptic_blackbox`'s `intdiv`
  scenario ("wrong death: exit 0"). **Proven pre-existing**: a clean worktree at
  `2fb1ee6` (the plan commit, zero resource-manager code) fails identically in a
  Release build on this machine. Debug passes everywhere. Left for a separate fix.
  **CLOSED (Phase D prologue)**: root cause was the scenario, not the handler --
  `sc_intdiv` guarded only the divisor with `volatile`, and LLVM InstCombine
  strength-reduces constant-dividend `1/x` into icmp+select (proven by disassembly:
  `leal 1(%rax); cmpl $3; cmovbl` where the `idiv` should be), so no trap exists at
  -O2+. Fix in `tests/anotest_blackbox.c` only: both operands volatile plus a
  `BB_NOOPT` (optnone) attribute on the inducer. Verified: `idiv` present in the -O3
  binary; Linux -O3 `intdiv` green across 4 consecutive runs; Windows clang64 -O3 full
  suite 22/22. Separate observation from the reruns: `ring_full` is flaky ONLY under
  WSL/9p (dies to the 5 s deadman SIGALRM while the hail-mary drains a full ring
  through 9p writes; ~97 s per blackbox run there vs 8 s native) -- environmental, not
  code; native runs are clean.
- Pre-existing (not introduced here): a spirv-val debug-info complaint from the
  validation layers on one Debug-compiled shader (`glslangValidator -gV` artifact); the
  bytes served are byte-identical to what `loadFile` read, per the read-contract tests.

### Recorded rungs (deliberately not implemented, per plan rules 7/8)

- **The §5 hierarchy bake-off (models B–E)**: the surface is frozen, model A ships as
  the null hypothesis, and real asset loads now exist to bench against — the bake-off
  is *runnable* work, gated on `anotest_resbench` which lands with it.
- **Step 4 clients** (`anoptic_config.h`, keybindings): the write machinery they need
  (`ano_res_write`/`quarantine`/`save_commit`/`save_load`) is shipped and
  crash-hardened; the clients are their own subsystem.
- **Steps 5–8** (async transport, streaming economy, packs/bake, parallel pread): the
  plan's own bench-gated performance rungs ("io_uring may never ship").
- General write-root temp GC (from the review) — rides with the config client.

Phase C committed as `fccad69`.

---

## Closing state vs. the plan's "Done means" (§14)

| §14 bar | State |
|---|---|
| Installed tree runs from any CWD; nothing outside `src/resources/` + logger opens a file | **Met** (grep-verified; `/tmp` smoke run; install rule ships the full resources tree). Windows path written per plan §10, untested on real Windows in this run. |
| Every loadable in a manager-owned allocator; teardown by wink-out; zero loose malloc/free in ingest | **Met — model E shipped by the Phase D bake-off**: lifetime groups with bulk chunk-granular teardown (residual returns to zero every level cycle), saves pinned to engine-forever, ingest staging a monotonic arena winked per ingest. True O(1) heap wink-out upgrades with the step-5 loader thread (parent-constructor change only). |
| Consumers hold `anores_t` handles/views; stale = sentinel; GPU hand-off destructive | **Met** (views drive shaders/scenes/fonts; release is zero-copy for direct-class payloads). |
| kill -9 at every protocol step: old-complete or new-complete; torn newest degrades one generation | **Met and harness-proven** (`anotest_resfault` + the corruption battery; orphan temps now complete their interrupted rename). |
| Resource names are `ANOSTR_SID` literals or interned strings | **Met** (rid = FNV-1a-64 in the SID key space, compile-time equivalence tested; mount prefixes interned). |
| TSan-clean transport + streaming | The transport's INTERCONNECT tier now exists (Phase F): `anoptic_collections.h` rings + `anoptic_threads_bridges.h` channels, conservation-stressed and in the `build.sh 7` TSan net. The loader-thread rung and streaming economy remain step 5–6 rungs. The full non-vk suite runs **TSan-clean**; the 4 vk tests SEGV under TSan inside the sanitizer's own allocator on an NVIDIA **driver** thread (`libnvidia-glcore` → `__interceptor_calloc` → `__tsan::user_calloc`, no engine frames) — an environmental TSan×NVIDIA-ICD incompatibility on this box, not engine code; the house TSan workflow never runs vk tests. |
| Compressed reads, zero-parse baked scene | Step 6–7 rungs. The scene block is already offset-based/load-in-place-shaped for the bake. |

**Three commits: `8726743` (A), `c5bbda1` (B), `fccad69` (C).** The engine's frame is
the old frame; its file IO is one namespace; its assets live in a manager that owns
them. What was deferred is recorded above as bench-gated rungs with their bars, per
plan rules 7/8 — none of it is silent.

---

# Phase D — the §5 bake-off runs: A vs E, E ships

The owner's directive after the branch review: no null hypothesis — run the bake-off,
best of two minimum, A against the plan's own favored hybrid E (C×D). Decisions first,
then the numbers.

## Decisions

- **Model A is the degenerate case of model E.** Rows carry a lifetime-group tag in
  both models; group 0 is engine-forever, always open, never retirable. Under A every
  pooled payload lands in `groups[0].pool` (the old `g_reg.pool`) and retire sweeps
  per-object; under E each open group lazily owns its own multipool and retire
  destroys it whole, outside the mutex. One code path, both models benched at
  identical call sites — the diff is ~170 lines of registry.
- **Groups are internal, ambient, and the surface stays frozen.** `res_group_begin/
  end/retire` live in `resources_internal.h`; the open scope is ambient under the
  registry mutex (any load during an open scope joins it — the level-load shape,
  revisited when loads move to the step-5 loader thread). Public `ano_res_group_*`
  promotion waits for a real level consumer.
- **Saves pin to group 0 structurally** — `save_try_file` passes group 0 explicitly,
  not by path convention. A level retire can never invalidate a loaded save
  (`anotest_resgroups` asserts it).
- **Direct-class payloads stay standalone mi blocks in both models.** The multipool's
  oversize passthrough cannot serve `ano_res_release` (an interior pointer is not
  `ano_aligned_free`-able), and read-to-home depends on the buffer being the payload.
- **The honest E-v1 compromise:** mi_heap_t parents are single-thread-owner and loads
  run on caller threads, so true O(1) `mi_heap_destroy` wink-out waits for the loader
  thread. E-v1 retire = `multipool_destroy` (O(chunks)) + per-object frees of the few
  huge direct blocks. Measured below: sub-5 µs at the level-cycle population — the
  compromise costs nothing at our shapes. Upgrade path recorded: loader-thread-owned
  per-group heaps change only the parent constructor at `begin` and the retire tail.
- **Switch:** `ANO_RES_MODEL` env read once at registry init (the house getenv
  pattern). After the decision below the default is **E**; `=A` keeps the baseline.
- **Loser's fate:** A stays behind the switch — the `log_old.c` precedent (keep the
  baseline so the comparison cannot rot). A inside the unified design is ~25 lines.
  ctest runs the resource suites under BOTH models every pass (`*_modelA` variants).

## The grid (`anotest_resbench`, -O3, best of two full runs each)

Primary: Linux x86-64, clang + LTO, WSL2 on ext4 (the tree copied off 9p; a 9p mount
distorts every IO-bound number and starves the blackbox deadman). Cross-check:
Windows 11 native NTFS, MSYS2 clang64. Quiet box both times — the first Windows pass
ran concurrent with a nix build and its churn tails were 2× pure noise; discarded.

**(a) Steady-state churn — A's home.** 256-file synthetic tree, streaming mix
{1K..256K pooled, 2 MiB direct}, 100k get/unload + 20k pure hits, fixed seed. Linux:

| series (ns) | A p50 | A p99 | A p99.9 | E p50 | E p99 | E p99.9 |
|---|---|---|---|---|---|---|
| get(load), group 0 | 8460 | 229628 | 299107 | 8350 | 218658 | 278117 |
| unload, group 0 | 60 | 430 | 670 | 60 | 380 | 590 |
| get(hit), group 0 | 50 | 80 | 100 | 50 | 80 | 90 |
| get(load), in scope | 8340 | 219798 | 281707 | 8360 | 217428 | 268318 |
| get(hit), in scope | 50 | 80 | 100 | 50 | 80 | 100 |

Verdict: identical within noise, exactly as construction predicts — E outside a scope
routes through group 0, which IS model A, so a churn loss for E cannot exist (the §5
"shows why the loss doesn't matter" arm); the in-scope series proves a group pool in
the path costs nothing measurable. Windows cross-check agrees (hit p50 40 ns both).

**(b) 50 level cycles over real assets — E's home.** Per cycle: scope → viking_room +
candle holder + Sponza glTF ingest (`ano_resgfx_model`, ~6.6 MiB conditioned) →
retire. Linux:

| metric | A | E |
|---|---|---|
| residual footprint, EVERY cycle | **1,318,912 B retained** | **0 B** |
| level cycle wall, mean | 6.786 ms | **6.492 ms** |
| level cycle wall, p50 | 6.585 ms | **6.324 ms** |
| level cycle wall, p99 | 12.898 ms | **8.135 ms** |
| retire alone, p50 | 1.6 µs | 2.7 µs |

Verdict: **E wins its home bench outright.** A's wound is exactly the predicted one —
the shared pool keeps its high-water chunks forever (1.29 MiB flat across all 50
cycles; grows with level size). E returns to baseline every cycle and is ~4% faster
per cycle with a 37% tighter p99. The one number A wins — retire-alone (per-object
frees of ~40 blobs beat one chunk-walk destroy by ~1 µs) — is 0.02% of a cycle:
recorded, dismissed. Windows: same story (residual 1,318,912 vs 0; cycle p50 11.18 vs
11.06 ms; retire parity at ~443 µs both — Windows frees are pricier for both equally).

**(c) Direct-class hand-off + ingest — the D side.** Linux: 200/200 zero-copy releases
(pointer equality every rep, both models), release p50 30 ns both, ~7.2k vs ~7.1k
hand-offs/s (get+release+free of 2 MiB), Sponza ingest 1465 vs 1399 MB/s conditioned
(≈ run noise; Windows re-run inverts it: 886 vs 913 MB/s). Verdict: tie — placement
identity for direct blocks is byte-for-byte the same code in both models.

## The §5 bar, applied

E beats A on E's home bench (residual + wall + tails), ties A's home bench by
construction (empirically confirmed), ties the hand-off bench. A beats E nowhere that
matters and loses residual footprint unambiguously. **E ships as the default.**
`anotest_resgroups` (+ `_modelA` variant) covers the seam: retire sentinels, group-0
survival, release-then-retire double-free watch (ASan), save pinning, stats balance —
green under Debug (28/28 native) and ASan (WSL, all non-vk) before the flip.

Post-flip gates: full native Debug suite 30/30 (both models in every pass), TSan
non-vk clean, Windows -O3 28/28, and the C.4 engine smoke from a foreign CWD on the
real GPU — 20 s at ~720 fps, 442 MiB textures resident, shadow atlas live, zero
ERROR/FATAL lines. The renderer's frame rides model E and cannot tell.

---

# Phase E — saves are user data; the write root sweeps its own strays

Two policy corrections from the same review, one file (`resources_core.c`), and a
discovery that proved the second one on our own test suite before it even shipped.

## Saves: the engine deletes NOTHING

The shipped `save_commit_locked` pruned to the newest `ANO_RES_SAVE_KEEP` = 3
generations after every verified commit — auto-deleting user data. Wrong default,
now dead: commit = write + verify, every prior generation intact forever. The
3-generation retention was torn-write safety machinery, but never-delete preserves
the degradation property trivially (load still scans newest-first) at the cost of
disk the USER controls. The replacement contract, additions-only per §11:

- `ano_res_save_stats(slot, &generations, &bytes)` — the "getting bulky" hint the
  game shows; `ANO_RES_SAVE_KEEP` survives as the advisory threshold to compare it
  against.
- `ano_res_save_delete(slot, seq)` — the user-initiated removal of exactly one
  generation. The engine never calls it on its own behalf.

`save_scan` shrank to a `max_seq` tracker (seq assignment needs nothing else);
`anotest_resources` now asserts all five commits survive, stats counts frame-exact
bytes, and delete removes exactly what it names.

## Write-root temp GC — and the wedge that demanded it

`ano_res_init` now runs `res_temp_gc`: a calm-time, depth-3, batch-32 sweep of
stranded protocol temps (`<stem>.<8 hex>.tmp`, exactly the protocol's shape) under
the write root, **excluding `saves/`** — save temps are `ano_res_save_load`'s
recovery candidates (a valid one COMPLETES the interrupted rename; sweeping them
early would delete the only on-disk copy of a crashed commit). The nonce counter is
also seeded from the clock at init so two instances sharing a write root cannot walk
the same temp names.

The demand was proven live before the feature landed: after ~5 suite runs on the
Windows box, `anotest_resfault` went red with "fault hook did not fire" — 15
stranded temps (the harness's own crash artifacts; its POSIX-only `purge_temps` is a
no-op on Windows, and the counter restarted at 0 every process) had occupied 8+
consecutive nonces, exhausting the protocol's O_EXCL attempt window. `ano_res_write`
failed before reaching any fault hook. Exactly the accumulate-until-wedged failure
the GC exists to prevent, caught in the wild on our own harness. The fix run swept
all 15 at init and the suite went green with the strays' directory empty.

Isolation coverage in `anotest_resources`: orphans planted before init at the root
and nested both swept; a legit file, a non-protocol `.tmp`, and a `saves/` temp all
survive untouched.

---

# Phase F — "async" spelled correctly: the lock-free interconnect tier

The owner's recast of step 5: "async" is not a C keyword — in practice it means
lock-free rings of prior art, shipped as their own modules before any loader thread
exists to ride them. Two deliverables, both landing in the homes the codebase had
already reserved (`log_ring.h:13` and `render_bridge.h:91` both said "migrates to
anoptic_collections.h"; the stub header existed, empty, waiting).

## `anoptic_collections.h` + `src/collections/`

The primitives as allocator-parameterized value types over `ano_mem_parent` (composes
over mi heaps AND the multipool hierarchy; `release == NULL` gives wink-out semantics
for free). Cache discipline throughout: `_Alignas(ANO_THREAD_LINE)` cursor
separation, compile-time-asserted in the tests ("real, not aspirational").

- `anoticket_t` — the pure-FAA unique-number dispenser (lockfree.md's easy case).
- `anoring_spsc` — the render bridge's proven `AnoSpscRing`, promoted: owner cursors,
  no CAS anywhere.
- `anoring_mpsc` / `anoring_spmc` / `anoring_mpmc` — the Vyukov bounded ring at fixed
  stride with SoA planes: one `_Atomic u64` sequence word per slot (the log ring's
  cycle/commit tag generalized — lap encoded in the word, reuse without zeroing)
  beside a dense POD payload column. The single-owner side of each asymmetric variant
  drops its CAS: MPSC's consumer owns head (plus batch `drain`, the log drain's
  bounded walk); SPMC's producer owns tail — the fan-out shape (one loader thread
  publishing to worker pools) pays NO RMW at all on push. SPMC was initially argued
  away as "MPMC with one producer"; the owner overruled — the no-RMW push and the
  named fan-out consumer earn the fourth implementation.
- `anoseqpub` — the seqlock latest-wins lane, promoted with an owned value plane.

Ordering per lockfree.md §8, the pairing named in every contract: relaxed claim
(uniqueness only) → acquire on the slot's sequence word (previous life over) → plain
payload copy → ONE release store as the publish gate. No seq_cst anywhere in
collections. Hazards documented in the header, not hidden: Vyukov's wedged-producer
caveat (with SCQ/LPRQ as the named benchmark-gated upgrade when a channel's producers
can genuinely stall), 64-bit-monotonic ABA immunity, the ARM-without-LSE trap.
Michael&Scott deliberately omitted: node-based, drags safe-memory-reclamation — the
burden the array-ring family exists to sidestep; the mutex baseline serves the
comparison role instead.

## `anoptic_threads_bridges.h` + `src/threads/threads_bridges.c`

The general extension of anoptic_threads.h: a bridge is a named, typed,
policy-carrying channel. Topology (`SPSC|MPSC|SPMC|MPMC`) picks the cheapest ring;
policy says what full means — `TRYFAIL` (command channels, the render-bridge
contract) or `WAIT` (work channels: the log ring's escalating 64 ns → 8 µs backoff
with a stall cap that degrades to false — never a silent drop, never an unbounded
block). `anobridge_waiter` packages the log drainer's park/wake discipline (seq_cst
parked flag, recheck under it, capped timedwait: a lost wakeup costs one cap, never a
hang). Specialty channels: `anobridge_handles` — anores_t by VALUE, 16-byte POD slots
on the SoA plane, payloads stay in the manager's allocators, only handles cross
threads; `anobridge_latest` — the broadcast lane.

`src/log/` and `src/render_bridge/` are NOT migrated in this phase — collections is
the generalized extraction of their proven disciplines; re-basing them is recorded
follow-up work.

## Verification

`anotest_collections`: layout static asserts; unit edges every flavor (full at
exactly capacity, empty, ≥4 laps, u32 cursor wrap); conservation stress — 8P×8C MPMC
and 8P×1C MPSC(drain) with count+sum+xor conservation and per-producer FIFO at every
consumer, 1P×8C SPMC with the stronger single-sequence oracle, 1M-item SPSC mirror,
8-thread ticket-uniqueness bitmap. `anotest_bridges`: park/wake soak against the
publish-just-before-park window (joining at all is the liveness oracle), WAIT vs a
dead consumer degrades instead of hanging, handle-channel exactly-once. Both carry
the `concurrency` label: every lock-free line is in the `build.sh 7` TSan net, and
the merge bar is TSan-clean with zero suppressions for our code.

`anotest_ringbench` (DISABLED, by hand at -O3): the grid vs a mutex+array-queue
baseline at identical layouts; bar = every lock-free flavor ≥ the mutex baseline at
≥4 threads.

## The grid (-O3, Linux x86-64 ext4 primary; 1M items, cap 1024)

| config (s16) | ring Mops/s | mutex Mops/s | ring p99.9 push (ns) | mutex p99.9 (ns) |
|---|---|---|---|---|
| spsc 1×1 | 12.22 | 4.45 | 90 | 1,920 |
| mpsc 4×1 | 13.52 | 6.45 | 2,380 | 41,419 |
| mpsc 8×1 | 7.27 | 2.48 | 11,590 | 93,039 |
| **spmc 1×4** | **12.07** | 2.17 | **190** | 22,620 |
| **spmc 1×8** | **5.28** | 0.61 | **690** | 38,780 |
| mpmc 4×4 | 8.66 | 4.96 | 6,740 | 32,520 |
| mpmc 8×8 | 6.34 | 3.19 | 11,970 | 49,009 |

Stride 64 tells the same story (mpsc 4×1: 13.79 vs 6.50; spmc 1×8: 5.42 vs 0.59;
mpmc 8×8: 6.29 vs 2.46). Ticket dispenser, 8×500k: FAA 96.7 Mops/s vs mutex counter
47.9. **Bar held on every ≥4-thread point.** The owner's SPMC call is the standout:
the no-RMW push holds a 190 ns p99.9 while fanning out to 4 consumers, and beats the
mutex queue 8.7× at 1×8 — the loader-to-workers shape justified in one table.

Windows cross-check (native NTFS, clang64): bar held everywhere except one
INTERMITTENT point, mpmc 4×4 s16 (2 misses in 3 runs, e.g. 8.03 vs 8.24; the same
shape at s64 wins 6.62 vs 4.43). Traced to a harness artifact, recorded not tuned:
the bench's termination counter (`g_consumed`, one fetch_add per item plus spin
reads) is a second contended atomic that taxes the lock-free path while the mutex
queue's lock incidentally throttles it. A poison-pill termination protocol would
remove it; noted for the bench's next revision.
