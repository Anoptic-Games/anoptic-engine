<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Manager: what actually happened, and how it tracks the plan

Independent review of the `feature-resourcemgr` branch: every resource-manager diff (committed phases A/B/C plus the uncommitted working tree as of the morning of 2026-07-13), read against `docs/resourcemgr/resourcemanager-real.md`, cross-checked against the `resourcemanager-comprehensive*` docs, and verified with a real build and CTest run on Windows (`build.bat 5`, Debug). Findings are from the code, not the prose. A postscript records what the same day's follow-on campaign resolved.

## Verdict in one paragraph

The synchronous core is real, well-built, and largely faithful to `resourcemanager-real.md`: the memory-pools tier, the namespace/registry/handle/read path, the durable write protocol, the save framing, and the graphics parsing extension all exist and mostly do what the plan says. Three qualifications. First, `resourcemanager-real.md` is itself marked superseded (its own banners) by `docs/resourcemanager-comprehensive.md`, a stricter plan that re-scoped the work upward. Second, at review time the branch was mid-pivot: a committed baseline following `real.md` steps 0–4, then a large uncommitted session rewriting toward the new plan's Stage A that left the tree red — 2 of 30 enabled tests failing, nothing committed. Third, the marquee deliverable of `real.md` §5, the A–E allocator bake-off with "E ships," never happened: no model-selection code exists anywhere in the tree, one placement strategy ships, and the impl journal retracts the result as synthetic. High craft, honest documentation, incomplete scope.

## 1. What "the plan" even is now

`resourcemanager-real.md` opens by declaring itself superseded planning material, and its §"The Resource Manager, For Real" carries a second banner retracting its own audit, mutex doctrine, A-vs-E result, frozen V1 surface, and done bar. The current specification is `docs/resourcemanager-comprehensive.md`. This report judges the code against `real.md` as asked and notes where `comprehensive.md` moved the goalposts, because much of the uncommitted work chases the newer target.

The three `comprehensive*` docs are not a record of completion. `resourcemanager-comprehensive.md` is a forward-looking completion plan: Stage A finishes the synchronous manager behind a neutral allocator interface, Stage B implements all five allocator models and runs a real contest, Stage C converts the winner to a lock-free ticketed async system. `-progress.md` and `_annotated.md` are the same session retrospective, one standalone and one appended to a copy of the plan. Their own summary: Stage A incomplete, no contest run, Stages B and C not started, no phase commit.

## 2. Commit topology, and the red tree at review time

Committed resource-manager code runs through phase C (`fccad69`) and continues into `485efb6` ("Resource Manager progress") — a substantial code commit, not a document: the ambient-groups scaffold, collections, thread bridges, and the resource benches. The plan commits `6455651`/`2480020` carry small registry and test adjustments alongside the v2/v2.1 plan text; `984fea0` is journal-only. HEAD is `2480020`; no source commit was made during the session that produced the working-tree diff.

- Committed baseline: the memory-pools tier, a mutex-guarded registry with an ambient `cur_group` / `ANO_RES_PLACEMENT=global|scoped` scaffold, the write and save paths, the graphics parsing extension, and the renderer riding the namespace. Green at its checkpoint.
- Uncommitted working tree (the reviewed session): a registry rewrite replacing ambient groups with explicit lifetime domains and a lock-free reader/publication scheme; the config, keybindings, and world-save clients; and save-path changes (per-slot mutex lanes, `min_reader_version`, bounded-memory directory scans). This is what reddened the tree.

Verified at review time (`build.bat 5`, Debug, this machine): 28 of 30 enabled tests passed; 11 benchmark/optional binaries disabled by configuration.

- `anoptic_resources` failed 7 assertions, all orphan-temp save recovery (purge invalid temp, recover valid one, echo its format/seq, serve its payload, purge after recovery, rename to canonical generation, reload it). The bounded-memory save-load rewrite dropped the interrupted-save recovery path: a real correctness regression.
- `anoptic_persistence` failed 1 assertion, "keybindings exact round trip". A test-harness bug, not data loss: a `memcmp` over `ano_keybindings`, which has a 4-byte padding hole after `count` (`entries[]` is 8-aligned for the `anostr_sid`), with the hole left unwritten on one side. The serialization itself round-trips exactly.

## 3. What was actually built

Honest inventory, all verified in code.

- Memory tier (`anoptic_memory_pools.h`, `src/memory/pools.c`, committed). Three allocators: `ano_mem_monotonic`, `ano_mem_multipool`, `ano_mem_pool`. The fourth, `ano_mem_stripe`, is absent — which the plan permits ("ships only with a consumer and a bench"). Parent-composition (`ano_mem_parent_heap/default/monotonic`) so `Multipool<Monotonic>` falls out of the constructor; wink-out is `mi_heap_destroy`; per-allocator stats. Tested by `anotest_mempools` plus a disabled bench.
- Core (`src/resources/resources_core.c`, `resources_registry.c`, internal/os headers, both platform TUs). Namespace with write-root-over-mounts-over-base shadowing and newest-first mount order; a strict logical-path grammar (rejects `\`, `:`, control bytes, `.`/`..`, empty and trailing segments); FNV-1a-64 identity; a permanent 4096-slot registry over non-moving 64-row chunks; the `res_read_all` gulp (size-as-hint, read-to-EOF in bounded chunks, cache-line aligned, guard NUL); `ano_res_slurp`; resolve/subpath/exists escape hatches; an init-time stranded-temp GC that deliberately excludes `saves/`.
- Durable writes and saves. The §10 protocol verbatim: same-dir `O_EXCL` unique temp, write-all loop, one-shot `fsync` (never retried, fsyncgate-aware), close, `rename`-replace, parent-dir fsync that logs loudly and still returns 0. The §9 save frame exactly: 48-byte header (`ANOS`, container v1, FNV hash id, `format_version`, `min_reader_version`, `payload_len`, `seq`, header hash, reserved) and 16-byte `payload_hash` + `ANOSDONE` footer, header-vs-body damage distinguished. Fault-injection hooks at every protocol step. Commits verify through a fresh read handle before returning and never touch older generations. `save_stats`/`save_delete` count and user-delete only.
- Graphics extension (`anoptic_res_graphics.h`, `src/resources/graphics/res_graphics.c`). Parsing genuinely lives here: cgltf and stb_image appear in code only in this TU. Ingest runs cgltf entirely through a monotonic staging arena over a scratch heap that winks out at return, zero loose `malloc`/`free`; the only surviving allocation is the conditioned scene block, adopted into the registry and served as counted views. `ano_GltfParser.c` shrank 813→443 lines into a thin GPU-upload consumer; `loadFile`, `openEngineFile`, and `struct Buffer` were deleted from `pipeline.c`.
- Clients and engine integration. `config` (jsmn parse, v1→v2 migration, corrupt-config quarantine-and-regenerate, durable write), `keybindings` (13 SID-keyed actions, v1→v2, duplicate-key refusal, a process-global installed table), `res_world` (v2 save codec, v1→v2 migration, `min_reader_version`, exhaustive status mapping). `main.c` opens a `SAVE_CONFIG` lifetime, drives camera/menu settings from config, dispatches input through `ano_keybindings_current_action` (the hardcoded `GLFW_KEY_*` switches are gone), and loads/commits an `autosave` world generation around the run.
- Tests. Enabled: resources, resgfx, resgroups, resownership, resfault, resdurability, persistence, mempools, plus collections/bridges. Disabled benches: resbench, mempoolbench, ringbench.

## 4. Fidelity to `resourcemanager-real.md`, section by section

| Plan section | State in code | Direction |
|---|---|---|
| §1 rules 1–4, 6, 9 (remote-FS floor, owner model, one logical name, integer identity, stay-in-lane) | Honoured. Never trusts stat/mtime; believes only bytes read. Owner model realized. Logger/crash untouched. | Faithful |
| §1 rule 5 (parsing is the job, in the extensions) | Realized: cgltf/stb live only in `src/resources/graphics/`. | Faithful, positive |
| §1 rules 7–8 (correctness never deferred / performance always deferred; async on sync) | Half-inverted: lock-free reader/epoch reclamation landed in the synchronous stage — performance/async machinery ahead of its rung. | Diverges |
| §3 memory tier | 3 of 4 allocators; stripe correctly deferred; composition, wink-out, stats all present. | Faithful, positive |
| §4 `anores_t` grammar | Handle shape `{rid,slot,gen}` kept; registry is the intern-table generalized. But borrowed bytes now require an explicit registered read scope, not just a live generation. | Faithful shape, diverged access model |
| §5 hierarchy bake-off ("E ships", with numbers) | Did not happen. No `ANO_RES_MODEL` selector or models B–E exist anywhere; one placement ships; impl journal retracts the result as synthetic. | Not delivered |
| §9 frozen formats (save frame) | Byte-exact. Pack TOC (`anopak`) not built (a later step). | Faithful |
| §10 write protocol | Byte-exact, both platforms, fault-hooked. Save-load recovery broken at review time. | Faithful (write); regressed (recovery) |
| §11 frozen V1 API ("additions only, never re-signatured") | Broken by design: `get`/`bytes`/`release`/`unload`/`save_load` re-signatured to take a lifetime and/or read scope; old signatures survive as `_engine` adapters. | Diverges (freeze abandoned) |
| §12 steps 0–4 | Substantially attempted (0,1,2,3 done in spirit; 4 partial and red). | Partial |
| §12 steps 5–8 (async transport, streaming economy, pack+bake, parallel pread) | Not started. No loader thread, tickets, ring transport, chunk pool, ranged reads, codecs, `anopak`, bake, or hot reload. | Not delivered |
| §14 done-means | Mixed: owner model, handles/views, durable writes, save framing met; single-wink-out teardown, recorded bake-off winner, TSan-clean transport, zero-runtime-parse baked scene not met. | Partial |

## 5. The load-bearing divergences, and where each one points

Explicit lifetime domains threaded through the API. `real.md` §4/§11 said placement is "the manager's business" and froze `ano_res_get(const char*)`. The code instead makes the caller name a lifetime (`ENGINE`, `WORLD_LEVEL`, `STREAMING`, `TRANSIENT_IMPORT`, `SAVE_CONFIG`, `TOOL_IMPORT`) on every get/release/unload. Direction: positive. Only the caller knows when a level dies, and surfacing lifetime makes wink-out-by-lifetime a first-class API concept — exactly the model-C/E teardown the plan wanted. The cost: §11's freeze proved premature, ergonomics got heavier, and the `_engine` wrappers are migration debt.

Read scopes and lock-free epoch reclamation in the synchronous stage. `real.md` §11 said reads are "stateless and thread-safe" over a mutex-guarded registry. The code ships far more: a 4096-slot directory of atomic pointers to immutable `res_pub` descriptors, release/acquire publication, per-reader epoch lanes, deferred reclamation gated on a reader grace period, and generation-exhaustion refusal instead of ABA aliasing. Genuine, carefully-built machinery — and mistimed: Stage-C-flavoured work landed in Stage A, against the plan's own "async on sync" rule, and unproven under TSan at review time. Net-neutral today; positive once proven and load-bearing for async.

The A–E bake-off and "E ships." The biggest gap. §5 presents the contest as run and decided with a numeric grid; the code contains no model machinery at all, only one placement (lifetime-major domains, one multipool each for payloads ≤1 MiB, direct mimalloc above), and production loads mostly ride the immortal engine domain. The impl journal carries a dated self-retraction: the old grid compared a global-pool scaffold against a scoped-pool scaffold, models B, C, and D never competed, the "D side" was byte-identical direct allocation in both builds, and no model has shipped or won. The retraction is the honest move; the work it retracts is outstanding. `comprehensive.md` re-files the contest as its Stage B.

Save policy: never auto-delete, plus `min_reader_version`. `real.md` §11 listed only `save_commit`/`save_load` and a keep-three retention constant. The code renamed that to an advisory `ANO_RES_SAVE_WARN 16`, never deletes a user save on its own, adds `save_stats`/`save_delete`, and carries a forward-compat `min_reader_version` so an old build refuses a too-new save rather than silently rolling state back. Direction: positive; the safer policy for user data.

Zero-copy hand-off only for large payloads. §4/§14 want destructive release to be a pointer move, never a copy. In practice only transfer-compatible blocks (>1 MiB, on the default heap) move zero-copy; a pooled resource is copied out on release, because a multipool block cannot be handed to an external `free`. Minor negative against the ideal, defensible and correctly accounted (`copies`, `duplications`, `transfers` are counted).

## 6. Correctness state at review time: proven, red, and unproven

Proven by enabled tests with real oracles: namespace shadow order and hostile-path fuzz; the read contract (byte-identity, size, guard NUL, alignment, absent→sentinel); handle single-copy and stale-generation sentinel; glTF ingest against a handcrafted ground truth and PNG decode against an independent stb oracle; reader-pinned retirement; one real 4-reader-vs-retiring-owner publication race; durable-write crash safety two ways — in-process `longjmp` at each protocol step and a real subprocess kill at each step, both asserting old-complete-or-new-complete; and mempool class/alignment/LIFO/stats plus a shadow-pattern churn oracle.

Red: orphan-temp save recovery (7 assertions, a real unimplemented feature); the keybindings round-trip (a test-only padding-`memcmp` bug).

Unproven or claimed-only: the A–E contest (no code exists); crash recovery during a save (neither fault harness drives `save_commit`/`save_load`); TSan cleanliness of the new publication scheme on this tree; wink-out leak freedom outside sanitizer builds; and the hard-coded bounds (4096 slots, 32 domains, 64 readers), whose sizing evidence was never collected. A 12-second smoke run does render (RTX 4090, three fonts as memory faces, three models including Sponza, 396–427 fps, no resource-manager errors), so the synchronous path is functional end to end even with the test tree red.

## 7. Half-migrated seams worth knowing

- Decoded image pixels are the one asset class still caller-owned: `ano_resgfx_image` returns the raw `stbi` buffer for the renderer to `ano_aligned_free`; the manager only accounts the bytes.
- Embedded and `data:` glTF images are unsupported; external-URI `.gltf` works, but a `.glb` with embedded textures would silently lose them.
- Models copy their conditioned scene out to plain `calloc` CPU blueprints and unload the manager handle, whereas fonts retain their handle and read scope — a deliberate but inconsistent retention model.
- `ano_fs_chdir_gamepath` was not retired: the `main()` shim is gone, but the function survives in all three filesystem TUs and is still used by tests.
- Demo model names are still hardcoded string literals in `render_api.c`, so §14's "every naming site is an `ANOSTR_SID` or interned string" is not met there (they are hashed to rids inside `ano_res_get`, so they are not raw registry keys).
- Config parses jsmn over `ano_res_get`+`ano_res_bytes` rather than `ano_res_slurp` as §Step-4 says, and is a flat named-field struct rather than the SID-keyed store the plan describes (SID-keying is realized only in keybindings). Functionally equivalent; literally divergent.
- Autosave-on-shutdown is unconditional and unreviewed, and its persisted `simulation_tick` increments once per process run — a demonstration of the save machinery, not real world serialization.
- Line 26 of `resources_core.c` still describes "the one save mutex"; the code uses per-slot lanes. Stale comment, already self-flagged.

## 8. Bottom line

Against `resourcemanager-real.md` as written, this is a faithful, high-quality realization of the correctness half of the plan — memory tier, value/handle grammar, namespace, durable write protocol, save framing, parsing-lives-in-the-extension — and a non-delivery of the performance half: the §5 bake-off did not run, "E ships" is retracted as synthetic, and §12 steps 5–8 were not started. On top, the uncommitted session began an ambitious pivot toward `comprehensive.md` (explicit lifetimes, lock-free read scopes, real persistence clients) that is good engineering but was unfinished, unproven, and red at review time.

Clearly positive divergences: explicit lifetime domains, never-auto-delete saves with `min_reader_version`, and the neutral placement interface that lets the deferred contest slot in without touching consumers. Clearly negative: `real.md` §5 advertised a completed bake-off that never existed. Genuinely mixed: landing lock-free epoch reclamation in the synchronous stage — excellent code, out of sequence, unproven at the time.

Two things before a phase boundary: implement the orphan-temp save recovery the bounded-scan rewrite dropped, and zero-initialize the keybindings padding or compare structurally. Then the tree is green and the honest next step is `comprehensive.md`'s Stage A completion, then the Stage B contest both plans keep promising.

## Postscript — same-day resolution (2026-07-13, evening)

The follow-on campaign fixed both blockers, and this session verified them on the tree. Orphan-temp recovery was implemented for real (`recover_all_temps` in `resources_registry.c`: validate each temp, purge the damaged, rename a valid one to its canonical generation and fsync the directory — completing the interrupted protocol rather than discarding it). The keybindings hole was zeroed at the source (loader and defaults `memset` the whole aggregate; the assertion was not weakened). Fresh evidence, raw logs in `docs/resourcemgr/logs/`: Windows Debug 40/40 enabled, Windows O3 38/38, WSL TSan 33/33 with zero ThreadSanitizer reports, and an 18-second RTX 4090 engine smoke (~660–690 fps, zero error lines; hard-killed, so the clean-exit autosave path was not exercised). The TSan run discharges §6's "unproven on this tree" for the current interleavings, with the standing caveat that the `anotest_resownership` oracle observes 99.7% stale sentinels (measured this session: complete=5658, stale=1717390), so it is evidence, not proof of the protocol. §6's other unproven items stand: the contest, save-path crash-fault coverage, wink-out leak freedom outside sanitizer builds, and the 4096/32/64 sizing. The project frontier is now `docs/resourcemgr/phasea/CHECKPOINT.md` (M0 baseline evidence and the W0 placement freeze landed; M2–M19 open) with the evidence cells in `docs/resourcemgr/verification-matrix.md`. This report remains the audit of the pre-campaign tree.
