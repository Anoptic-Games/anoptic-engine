<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Phase A checkpoint — handoff to Fable

Written at a deliberate stopping point. The tree is GREEN and nothing is committed. Everything below is a report on work done by Opus 4.8; treat all of it as evidence, not as authority. You have full leeway to overturn any decision recorded here, including the architecture, the migration order, and this document.

## 0. State of the tree, right now

- HEAD is `2480020` ("Resource manager plan v2.1"). **Nothing was committed during this entire campaign.** Per CLAUDE.md, no commit happens without explicit approval.
- Working tree: 29 modified files, 54 new files/dirs, all uncommitted.
- **Verified green**: `build.bat 5` exits 0, `100% tests passed, 0 failed, out of 40` (30 pre-existing + 10 newly registered). I ran this myself after the freeze landed; it is not an agent's claim.
- 8 compiler warnings remain, all pre-existing and untouched (`src/resources/world/res_world.c:172,174` pointer-sign; `tests/anotest_logging.c` format).
- No agents or workflows are running. All queued agents (gate verifier, two retry integrators, postmortem) were cancelled.

## 1. What the plan was, and how it changed

The engagement began as a review of `feature-resourcemgr` and turned into an execution campaign. Three things were discovered that reframed everything, and you should know them before you trust any older document.

**The specification moved twice.** `docs/resourcemgr/resourcemanager-real.md` was the plan of record; it is now marked superseded by `docs/resourcemanager-comprehensive.md`, which is stricter and re-scoped the work upward into Stages A/B/C. Where the two disagree, comprehensive.md wins. real.md is still worth reading because it preserves the owner's original directives and design intent, which comprehensive.md assumes rather than restates.

**The headline result of the old plan was false.** real.md §5 claims an A-vs-E allocator bake-off ran and "E ships," with numbers. It did not. There is no `ANO_RES_MODEL` selector or model implementation anywhere in `src`/`include`/`tests`/CMake — those tokens exist only in documentation. The recorded "D side" used a byte-identical 2 MiB direct-allocation path in both builds. `RESOURCE_MANAGER_IMPL.md` now carries a dated self-retraction saying so. **No allocator model has shipped or won anything.** Stage B is entirely unbuilt.

**The tree was red.** Two tests failed: seven orphan-temp save-recovery assertions (a genuinely unimplemented feature) and one keybindings round-trip (a test-harness bug — `memcmp` over a struct with a 4-byte padding hole, one operand uninitialized). Both are now fixed, the first by actually implementing interrupted-save recovery, the second at the source rather than by weakening the assertion. An adversarial verifier confirmed the fix was not gamed (the test file is byte-identical to baseline in the orphan-temp region).

**Owner scope decisions** (binding, made explicitly this session):
1. Audio and script get **real resource domains** with headless-but-genuine consumers — audio decodes to PCM/planar-f32 into a manager domain and feeds an offline sink with byte oracles; script compiles to bytecode and runs on a fuel-capped stack VM. We do **not** build an audio engine or a scripting language.
2. Verification gate = native Windows + Linux under WSL (incl. ASan/UBSan and TSan) + the remote-FS floor over WSL's 9P `/mnt`. **macOS is recorded UNRUN with repro commands and never claimed green.**
3. Codecs: LZ4 vendored and on by default; zstd behind `option(ANOPTIC_ZSTD OFF)`.

**What was then executed.** Round 1: greened the tree, produced a 102-gap file-level ledger across six areas, and ran a four-way competing-architecture contest (VTABLE, SOA-PLANES, CAPABILITY, MINIMAL-CORE) judged adversarially on Correctness / Performance / Modularity / Usability. VTABLE died to a mechanical schema failure and never submitted; the other three scored 8.37 / 8.33 / 7.23. Round 2: M0 (baseline evidence + harness-defect fixes) and M1/W0 (the freeze).

**The chosen architecture** is SOA-PLANES as the spine, with MINIMAL-CORE's extension registry grafted in to dissolve the kind axis out of the model literals, plus `ano_res_derive` as the single adoption door. CAPABILITY was rejected: its headline claim ("illegal placement is unrepresentable") was proven false by its own code, because its mint checks alignment but carries no size, while multipool alignment is a function of the size class.

## 2. Documentation map — read in this order

- `docs/resourcemanager-comprehensive.md` — **THE SPEC.** Phase A is section "A." (A.1–A.5). §0 is the completion law; §A.2 is a current-vs-required delta table; §A.5 is the exit bar. This is authority.
- `docs/resourcemgr/phasea/blueprint.md` — the build contract produced by the contest. §2 is the frozen seams in concrete C, §4 is 13 workstreams with exclusive file ownership, §5 is the 15-item freeze list, §6 is the M0–M19 green-at-every-step migration, §7 is Phase B preparation, §8 is recorded wounds. **This is a hypothesis, not law.**
- `docs/resourcemgr/phasea/gap-ledger.json` — 102 verified gaps with file:line precision, from six recon agents that read the real code.
- `docs/resourcemgr/phasea/architectures.json` and `judges.json` — the three surviving designs and three adversarial judge panels. Read `judges.json` even if you throw out the architecture: the judges found real bugs in code that already exists, listed in §4 below.
- `docs/resourcemanager-comprehensive-report.md` — my independent audit of what actually existed before any of this work. Useful for separating what was genuinely built from what was merely claimed.
- `docs/resourcemgr/verification-matrix.md` — the evidence cells. Only Windows is filled. Do not let anything claim a cell it did not run.
- `docs/resourcemgr/resourcemanager-real.md` — superseded, but preserves the owner's directives and the reasoning behind the owner model, the anostr grammar, and the Lakos allocator tiers.
- `docs/resourcemgr/RESOURCE_MANAGER_IMPL.md` — implementation journal, not authority. Contains the self-retraction of the "E ships" claim.
- `CLAUDE.md` and `docs/conventions.md` — house rules. C23, platform abstraction via `ano_*()` behind `include/anoptic_<mod>.h`, per-platform TUs in `src/<mod>/`, no heavyweight deps, terse comments at the top of functions, do not add yourself as a contributor, do not commit without approval.

The workflow scripts that produced all this are on disk under the session's `workflows/scripts/` and can be edited and re-run.

## 3. What you should verify — pursue your own investigation

You have full leeway. Nothing here is settled. The single most valuable thing you can do is **not trust this report** and re-derive the important claims yourself. In particular:

Verify the freeze actually does what it claims. W0 built `resources_place.h` as a seam that is supposed to host all five complete allocator models (A one-multipool, B kind-major, C lifetime-major, D role-split, E C×D hybrid) without touching consumers. **All three judges said all three architectures could do this, but that was a paper argument.** Nobody has written Model B or D. If the seam cannot actually host them, Stage B is dead on arrival and it is far cheaper to discover that now than at M19. Try writing one hostile model against it and see what breaks.

Verify the green. I ran `build.bat 5` and got 40/40, but that is Windows-Debug only. Nothing has been run under Linux, ASan, UBSan, TSan, or the 9P floor, and the engine has not been launched since the freeze — **Sponza has not been brought up**. The A.5 exit bar requires all of it.

Verify the three rulings W0 made against the blueprint. It overturned the spec where the spec was uncompilable, which is correct behavior, but each ruling deserves a second look:
1. `res_place_plan()` as a function **cannot exist** — in C a typedef and a function share the ordinary-identifier namespace. The type keeps the frozen name; the verb became `res_place_route()`. This is right, but check every call site agrees.
2. `transfer_compatible` was deleted from the plan, so `owned_alloc_locked` now re-derives it via a `legacy_site_flags(plan, bytes)` stand-in. **This is a smell.** It reintroduces exactly what the freeze deleted. It is scheduled to die at M6/M10, but it may indicate the frozen `res_place_plan` is missing a field it actually needs.
3. Read the rest of W0's return value in the workflow journal before accepting the seam.

Verify the gap ledger. It was produced by agents reading code. Spot-check a few entries. If it is wrong, everything built on it is wrong.

Question the migration order itself. M0–M19 is one plausible sequence, not the only one. If you see a better decomposition — or a way to land the load-bearing allocator work earlier so Stage B can start sooner — take it.

## 4. Live bugs in the existing code — found by the judges, not yet fixed

These are defects in code that shipped before this campaign. They were found by adversarial review, not by running anything, so **confirm each one before acting.** They are listed roughly by severity.

**Payloads over 1 MiB never land in the domain heap at all.** `owned_alloc_locked` (registry) puts them on the *calling thread's default heap* via `mi_malloc_aligned`, not in `d->heap`. Residual footprint is the deciding metric for models C and E in Stage B, and `ano_res_stats` only sums multipool chunk bytes, so the number is short by every page mimalloc retains. **Until this is fixed, no allocator figure from this codebase means anything.** Blueprint schedules it at M6; it may deserve to come first.

**The cross-lifetime alias bug.** `ano_res_get`'s hit path returns an existing resource regardless of the requesting lifetime. The moment a second `WORLD_LEVEL` domain opens in production, level B's handles point into level A's `mi_heap`, and `ano_res_domain_retire(A)` calls `mi_heap_destroy` on memory level B is still reading. **This must land before any production code opens a second lifetime domain.** Blueprint M8.

**`ano_resgfx_scene` validates array extents and nothing inside them.** It is safe today only because cgltf constructs the indices. The instant a baked block is loaded from a pack, every `prim.material`, `node.mesh`, `node.parent`, child span, and index becomes an out-of-bounds read primitive handed straight to the renderer. The blueprint calls this the highest-severity item in the ledger and schedules a hostile-block battery under ASan at M11. Do not ship pack loading before this.

**`g_readers[64]` is a false-sharing bug.** The lanes are 16-byte `{_Atomic uint32_t cookie; _Atomic uint64_t epoch;}` — **four per cache line** — raced by ~1.2M reader observations in `anotest_resownership`. This is also the genuine production consumer that `ano_mem_stripe` was missing (A.2 complains that shipped allocators have no consumer). Only one squad found it.

**`'#'` is a legal path byte.** So `ano_res_get(lt, "models/viking_room.gltf#gfx")` will happily serve the conditioned scene block as raw bytes — the derived-resource key space collides with the path grammar. A type-confusion hole, not a tidiness issue. Note one design closed `'#'` and then reopened the same hole with `'@'` for duplicate identities; close both.

**Loose-over-pack shadowing is inverted.** The namespace walk is write-root > mounts-newest-first > base. A pack registered as a mount would therefore **shadow the loose base** — the exact inverse of the requirement that loose files shadow packs. This needs to become an invariant of the walk (two passes: all DIR candidates, then all PACK candidates), not a property of registration order.

**Model B's whole-kind teardown is a use-after-free in all three designs.** `mi_heap_destroy(kind_root)` without invalidating publication and crossing the epoch grace barrier. It must run the identical invalidate → `queue_retire_locked` → `reader_safe` → `collect_locked` sequence as `ano_res_domain_retire`. It is also contest-harness-only; production must never call it.

**Model B's kind-major registry is a fiction at chunk granularity** in two of the three designs: a 32-row chunk is one allocation carrying one kind, but holds rows of many kinds — which B.4 forbids by name. Only a per-binding plane avoids it. This is subtle and load-bearing for whether Stage B's contest is even valid.

**`anotest_resownership`'s race oracle is near-vacuous** — it observes ~99.6% stale sentinels, so "publication never mixed descriptor or bytes" rarely gets a chance to fail. The concurrency it is supposed to prove is weakly tested.

## 5. The parts that fought hardest — consider rewriting

These are where I would spend your skepticism.

**The telemetry design.** B.2 demands every allocation be attributed to (kind × lifetime × role × operation) so an aggregate win cannot hide one catastrophically weak class. Two of three designs failed this outright — one shipped a cube missing the operation axis, the other a 256-cell interned table whose capacity is the same order as its realized load, so attribution degrades *precisely* when audio/script/level/pack arrive. The adopted answer is a 19-bit five-axis key into a 128-byte AoS counter row (AoS specifically because SoA counter planes scatter one allocation across three cache lines on the allocation hot path — the one place SoA is wrong). W0 landed the static_asserts. **This is unproven and it is the instrument every Stage B number will be measured with. If the instrument is wrong, the contest is worthless.**

**The placement seam.** This is the load-bearing abstraction of the entire effort and the thing that is hardest to get right. It must express five genuinely different ownership hierarchies without giving any of them a private shortcut — the judges explicitly killed a per-model `plan_hook` escape as "a private shortcut enforced by review, not by the compiler," which B.1 forbids. Confirm no such hook exists. Then confirm the seam can express a model it was not designed around.

**`transfer_compatible` / `legacy_site_flags`.** See §3. The plan deleted the bool; the implementation immediately needed it back. That is usually a sign the abstraction is missing something real.

**The owner-thread-synchronous constraint.** Stage A is synchronous *by law* (A.4 bullet 4: the correctness of ranges, chunks, codecs, packs, baking and hot reload must not depend on Stage C's concurrency). The consequence is honest and ugly: opening a level serializes glTF ingest, image decode, WAV decode and script compile on the init thread, and Sponza's cold open becomes a multi-second hitch. **This must be measured and reported, not hidden — and it must not be "fixed" by sneaking a loader thread in ahead of Stage C.** `res_owned_alloc`, `ano_res_get`, `ano_res_domain_open` and `res_registry_adopt` are all owner-thread-gated and `mi_heap_destroy` is single-thread-owner, so a helpful worker thread corrupts mimalloc. Record the cold-open wall time as the baseline the ticket system will later have to beat.

**Audio and script.** The owner authorized headless-but-real consumers. The failure mode is obvious and must be avoided: a resource domain with no genuine consumer is scaffolding wearing a domain's name, which is exactly what A.2 complains about for `ano_mem_pool`. The audio sink must actually consume PCM and the byte oracle must actually be able to fail; the script VM must actually execute and a hostile script must actually terminate on its fuel cap.

## 6. What has to happen next

Immediate, in the blueprint's order (§6), though you may reorder:

- **M2** `res_ext` + identity: extension registry, fourcc kinds, `res_rid_derived`/`res_rid_duplicate`; delete the `res_kind` enum and `kind_from_path`'s switch.
- **M3** telemetry: interning, AoS cells, real `res_account_copy`/`res_account_transfer` (they currently `(void)plan;`).
- **M4** `ano_mem_stripe` + pool counters, with `g_readers` as its first real consumer (fixes the false-sharing bug above).
- **M5** the seam, behavior-identical, with the two truthful scaffold names (`global-pool`, `scoped-pool`) and the `ANO_RES_PLACEMENT` env var restored — A.4 bullet 1 requires these to survive until Stage B, and the working tree deleted them prematurely.
- **M6** domain-heap ownership. **This is the pivotal step**: every arena, control record, name, dep array and payload — including the >1 MiB class — comes from the domain root. Only after this is `mi_heap_destroy` genuinely a wink-out, and only then is C/E's marquee claim measurable at all.
- **M7** registry split (permanent ident directory vs per-binding shard in the owning domain).
- **M8** the alias bug. Before any second lifetime domain opens.
- **M9–M12** consume verbs, destination planning, block framing + the hostile-block battery, renderer retains handles (`ModelAsset` and `g_assets[16]` deleted).
- **M13–M17** graphics completion (skins/animations/GLB+data: images), stream/codec/pack/bake, hot reload, audio + script, data-driven levels.
- **M18** the deletion sweep: path-based font loading, `ano_fs_chdir_gamepath`, `ano_res_slurp`, alternate loaders. Note `tests/templates/scratch.h` must be rebased off `ano_fs_chdir_gamepath` in its own green commit *first*, or ~15 test binaries redden and the failure looks like a resource bug.
- **M19** the five models as data behind a test-only selector, with zero edits to the core and zero to any consumer. **Stage A ends here**, and Stage B becomes a bench campaign rather than a rewrite.

Then the A.5 exit bar: all non-allocator-specific resource behavior complete behind the neutral interface, every promised production resource class with a real owner and consumer, the synchronous reference path passing its complete correctness suite on Windows + Linux (+ the 9P floor), and the current hierarchy described honestly as scaffolding awaiting the contest. **No model is declared shipped at Stage A.**

Non-negotiables carried from the spec and the owner:

- Do not commit without explicit approval. Nothing has been committed for the whole campaign.
- Do not let a benchmark number exist without naming its implementation, commit, build, platform, corpus, run count and raw result. A disabled benchmark plus prose is not evidence.
- Do not claim a verification cell that was not run. macOS is UNRUN.
- Do not weaken a test to get green. If a test is wrong, fix the test and say so; if the code is wrong, fix the code.
- Do not declare a model name over an incomplete implementation. That single failure is what this entire campaign exists to correct.
