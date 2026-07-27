<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Manager Comprehensive Progress

Date: 2026-07-13. Branch: `feature-resourcemgr`. Starting and current committed HEAD: `2480020`. No commit created this run. Below: inspection results, uncommitted working-tree changes, and verification against that tree.

## Request

Execute `docs/resourcemanager-comprehensive.md` in full; commit between Stages A, B, and C. Reached partial Stage A. Stage A incomplete, no allocator contest, Stage B/C not started, no phase commit.

## Repository and specification inspection

Read `CLAUDE.md`, `docs/conventions.md`, `docs/resourcemanager-comprehensive.md`, `docs/resourcemgr/RESOURCE_MANAGER_IMPL.md`, `flake.nix`, root `CMakeLists.txt`, `src/src.md`, `tests/tests.md`, current resource public/private headers and implementations, resource tests, and the latest three commits. Established module rules, C23/PAL requirements, build matrix, historical scaffold state, save-data ruling, and the comprehensive plan as sole current specification.

Inspected commits `2480020`, `6455651`, and `485efb6`. First two reconcile the comprehensive specification and rename live global/scoped allocator paths as scaffolds; third holds earlier resource-manager implementation work. Measured current code against new authoritative completion bars, not the superseded implementation journal.

## Ultracode audit

Ran a 14-agent workflow: ownership/publication, graphics, worlds/levels, config/saves, audio/scripts, IO/packs, allocators, interconnects, public APIs and owner migration, tests/build/platform evidence, checked-in assets, and documentation history. Synthesis agent produced a dependency-ordered execution program; adversarial critic checked it against every Stage A/B/C exit bar and deletion gate.

Pre-run implementation had: namespace, whole-file reads, durable writes, save framing and recovery, stable logical IDs, generation-checked handles, graphics conditioning, renderer shader/font/model consumers, memory pools, and generic ring/bridge primitives. Missing: stable lock-free handle publication, read-side reclamation, explicit production lifetime domains, complete resource-domain consumers, ranges/codecs/packs/baking/hot reload, the five allocator contestants, an allocator winner, a resource owner service, generational ticket lifecycles, and end-to-end lock-free resource transport.

Audit changed no repository files. Temporary workflow artifacts and task-list entries only.

## Stage A ownership foundation implemented

Direct implementation batch rewrote the resource-manager ownership foundation: one neutral ownership grammar and a safe read contract for later domains, allocator contestants, and ticket transport.

`include/anoptic_resources.h` declares explicit lifetime kinds: engine, world/level, streaming, transient/import, save/config, and tool/import. `ano_res_lifetime` is a counted owner/generation capability. Engine lifetime created at init; additional domains open and retire explicitly. Removed ambient `cur_group` and `ANO_RES_PLACEMENT=global|scoped` from live code.

`src/resources/resources_internal.h` defines neutral Stage A placement vocabulary: resource kind, lifetime, role, operation, destination, provenance, disposition, transfer compatibility, dependency metadata, owned blocks, and placement plans. Names describe allocation facts; they do not claim Models A–E. Same semantic/accounting contract for all five later contestants; no private contestant API.

`src/resources/resources_registry.c` substantially rewritten. Bounded permanent publication directory of 4096 slots, non-moving row chunks of 64 rows, up to 32 explicit domains, up to 64 registered reader lanes. Immutable descriptors published through atomics. Resource reads require reader registration plus `ano_res_read_begin`/`ano_res_read_end`; `ano_res_bytes` no longer returns a raw manager pointer without an announced read scope. Retirement invalidates publication first; reclamation deferred until pre-existing readers go quiescent. Generation and owner exhaustion refuse rather than silently wrap.

Registry records requested and serving bytes, live and peak allocation state, chunks, allocations/frees, copies and bytes copied, promotions, duplications, transfers and transferred bytes, pending retirement, stalled readers, live descriptors, live domains, and bound rows. Existing payload load/adopt/release/unload paths, registry rows, names, hash storage, dependency metadata, graphics scenes, decoded images, and save payload installation routed through the neutral placement/accounting layer.

Added `ano_res_shutdown` and registry shutdown. Shutdown invalidates publication and reclaims manager-owned domains and allocations; returns failure while a registered reader still pins reclamation. Meets plan requirements for real teardown, zero-residue tests, and backing-domain ownership (not an immortal process-global scaffold).

`include/anoptic_threads.h` and `src/threads/threads.c` gained `ano_thread_equal`. Owner-thread enforcement stays behind the thread PAL; resource module does not compare platform thread types.

## Existing consumers migrated to read scopes and explicit lifetimes

`src/vulkan_backend/instance/pipeline.c` registers a reader, enters a read scope, loads shader bytes in the engine lifetime, creates the shader module, and ends the scope. Shader bytes are borrowed manager memory and may not escape an unannounced read lifetime.

`src/vulkan_backend/text_raster.c` and `src/vulkan_backend/structs.h` retain a registered resource reader and read scope while FreeType memory faces borrow manager-owned font blobs. Scope ends during text teardown. Zero-file-open font path preserved; pointer lifetime now explicit.

`src/render/gltf/ano_GltfParser.c` receives the explicit engine lifetime, uses registered read scopes for source/scene/image views, and unloads through the lifetime-aware API. `src/resources/graphics/res_graphics.c` and `include/anoptic_res_graphics.h` updated so graphics model conditioning, scene views, and image decoding accept the required read scope and lifetime. Renderer stays on the real production resource path; unsafe borrowed pointers eliminated.

`src/engine/main.c` calls `ano_res_shutdown` on resource-aware exit paths and opens a save/config lifetime during startup. Real production lifetime owner instead of immortal implicit group.

## Tests added and existing tests migrated for the ownership foundation

`tests/anotest_resownership.c` added. Covers explicit domain opening and retirement, reader registration, read scopes, publication races, stale handles, reader-pinned reclamation, non-moving row growth, copy and transfer accounting, promotion/duplication/transfer disposition helpers, owner/generation exhaustion refusal, and zero-residue shutdown.

`tests/anotest_resources.c`, `tests/anotest_resgfx.c`, `tests/anotest_resgroups.c`, and `tests/anotest_resbench.c` migrated to explicit lifetimes and read scopes. `anotest_resgroups` rewritten around public explicit world and streaming domains instead of private ambient group helpers. `tests/CMakeLists.txt` updated to build and run `anotest_resownership`.

Before the later persistence batch modified the tree, ownership implementation reported: successful Windows Debug test profile, 4/4 focused resource suite, 28/28 enabled tests, successful TSan build, 6/6 TSan concurrency suite, focused TSan resource-ownership pass, no TSan diagnostics. Those results describe the ownership-foundation checkpoint, not the current final working tree.

## Partial persistence, config, keybinding, and world-save implementation

Second implementation batch started for config/keybinding clients, save migration, production world/save consumer, first-valid-generation fallback without fixed candidate windows, and per-slot save ordering. Interrupted before its own review and verification. Partial files remain in the working tree.

`include/anoptic_config.h`, `src/config/config.c`, and `src/config/CMakeLists.txt` added. Version-2 typed settings schema: camera movement speed, look sensitivity, initial menu visibility. Strict JSMN parsing; rejects unknown or duplicate structure; validates numeric bounds; migrates version-1 flat schema; quarantines malformed data; writes validated defaults when data is missing or damaged; durably replaces `config/settings.json` through `ano_res_write`. Real config client for validation, migration, quarantine, and durable replacement through the resource namespace.

`include/anoptic_keybindings.h`, `src/keybindings/keybindings.c`, and `src/keybindings/CMakeLists.txt` added. Thirteen stable action SIDs: movement, menu, lighting mode, model LOD, shadow LOD, and Hi-Z, with configurable key/modifier pairs. Defaults, strict validation, duplicate-key refusal, version-1 migration, version-2 serialization, quarantine, durable replacement, and a process-global installed binding table. Real keybinding client; removes hardcoded production bindings.

`src/vulkan_backend/instance/window.c` dispatches renderer debug actions through the installed keybinding table instead of hardcoded GLFW key comparisons. `src/engine/main.c` uses the same table for movement and menu actions. Camera movement speed, look sensitivity, and initial menu visibility come from loaded config.

`include/anoptic_res_world.h`, `src/resources/world/res_world.c`, and the resource CMake list added. Portable world-save payload: simulation tick, seed, camera position, yaw, pitch, and flags; byte-exact version-2 encoder/decoder; version-1 migration; validation; `min_reader_version` handling; exact status reporting; source-preserving migration writeback. Real save-migration client beyond raw frame primitives.

`src/engine/main.c` currently loads named `autosave` into world state during startup and commits a new `autosave` generation on normal renderer shutdown. Every prior generation remains. Logic thread starts camera from loaded world state and writes final camera state back before shutdown. Partial behavior; no completed design review yet.

`CMakeLists.txt` now includes config and keybinding modules in `anoptic_core`.

## Save-path changes

`src/resources/resources_core.c` replaced one global save mutex with a map mutex plus one persistent mutex lane per exact slot name. Same-slot commits, statistics, and deletion serialize on that lane; distinct slot names may proceed concurrently. Owner-side per-slot ordering before Stage C moves the same semantic onto the single resource owner and tickets.

Save frame now carries a separately supplied `min_reader_version`. `ano_res_save_commit_ex` writes it; `ano_res_save_load_ex` accepts a reader version and reports `READER_TOO_OLD`; compatibility commit/load entries remain adapters. Existing generations still immutable and preserved.

`src/resources/resources_registry.c` replaced the fixed newest-64 normal-generation list and eight-temp list with bounded-memory iterative directory scans. Walks every normal generation newest-first and every orphan temp without retaining a fixed candidate array. `rmos_rename_new` added to the resource OS interface and both POSIX and Windows implementations so orphan recovery cannot overwrite an already-preserved generation.

Public warning threshold is now `ANO_RES_SAVE_WARN 16`. Advisory only; no deletion behavior. `ANO_RES_SAVE_KEEP` removed (name implied the rejected keep-three retention policy). Engine never automatically deletes a successful save generation. `ano_res_save_delete(slot, seq)` remains the only save-generation deletion API; documented as user-directed.

## Persistence and durability tests added

`tests/anotest_persistence.c` added. Tests config defaults, exact serialization intent, version-1 config migration, config quarantine, keybinding defaults, rebinding, version-1 keybinding migration, keybinding quarantine, fallback through more than 64 corrupt recent save generations, orphan recovery after more than eight temps, world-save round trips, version-1 world migration, migration failure without source deletion, reader-version refusal, unsupported-version status, concurrent progress for distinct save slots, and serialization for the same slot.

`tests/anotest_resdurability.c` added. Launches a subprocess and terminates it after write, sync, close, and rename boundaries, then checks that the fixed-name logical resource is old-complete or new-complete, never torn. Complements the existing in-process longjmp fault harness with real process termination.

`tests/CMakeLists.txt` now builds and registers `anotest_persistence` and `anotest_resdurability`.

## Direct corrections made after reviewing the partial batch

Naked `ANO_RES_SAVE_KEEP 3` identified as misleading (unused; previously intended only as advisory threshold). Removed; replaced with `ANO_RES_SAVE_WARN 16` and an explicit comment that the engine never automatically deletes saves.

Save-commit comment in `resources_core.c` corrected: same-slot operations serialize, distinct slots proceed independently, every generation is a new verified file, only the user deletes save data.

## Current build and test result

Ran `build.bat 5` against the current complete working tree after the interrupted persistence changes. Configuration and compilation succeeded, including `anopticengine.exe`, `anotest_resownership.exe`, `anotest_persistence.exe`, and `anotest_resdurability.exe`.

CTest currently registers 41 tests: 30 enabled and 11 optional/benchmark tests disabled by existing configuration. Current result: 28 enabled passed, 2 enabled failed.

`anoptic_resources` fails seven orphan-temp assertions: purging an invalid orphan, recovering a valid orphan, returning its format and sequence, returning its payload, purging the temp after recovery, renaming it to the canonical generation, and reloading the recovered generation. Regression from the incomplete bounded-memory save candidate/temp rewrite; must fix before any Stage A commit.

`anoptic_persistence` fails the keybinding exact-round-trip assertion. Functional migration and other persistence checks in that executable reached that assertion, but the serialized/reloaded aggregate is not byte-identical under the current test. Not yet diagnosed or accepted.

`anotest_resownership` passes in the current Windows suite. `anotest_resdurability` passes in the current Windows suite. All five enabled Vulkan tests pass.

## Engine runs

Engine had not been launched after the persistence changes until challenged about runtime verification. Then launched `build/Tests/anopticengine.exe` for a 12-second smoke run. Initialized Vulkan on NVIDIA GeForce RTX 4090, loaded all three fonts as memory faces, ingested `models/viking_room.gltf`, `models/GlassHurricaneCandleHolder.gltf`, and Sponza, reached the render loop, emitted snapshots, rendered at roughly 396–427 fps during the captured interval. No resource-manager ERROR or FATAL message.

Smoke log contained the already-recorded Debug SPIR-V validation complaint concerning `NonSemantic.Shader.DebugInfo.100 DebugGlobalVariable`. Same issue documented in the historical implementation journal as a debug-info artifact; this run did not establish a new resource-manager cause.

Launched the engine again as a visible background process and left it running for the user. Process later exited normally. No screenshot taken; no visual-correctness claim beyond render-loop, asset-ingest, snapshot, and frame-timing evidence in the log.

## Current regressions and risks

Tree is red: `anoptic_resources` and `anoptic_persistence` fail. No phase commit while those failures remain.

Bounded-memory orphan-temp rewrite regressed previously passing recovery behavior. Incomplete, despite intended improvement over the old eight-temp cap.

Keybinding round-trip test is red. Defect may be serialization order, aggregate padding in the test oracle, or actual field loss; not yet established.

Global/scoped placement scaffolds and dual test registrations removed during the ownership rewrite before the full Stage B allocator contest exists. Premature relative to `docs/resourcemanager-comprehensive.md`, which keeps truthful scaffolds until all five complete contestants are implemented and compared. Historical `anotest_resbench` source migrated to explicit lifetimes but no longer selects the former placement paths.

Public synchronous resource API changed substantially: get, unload, release, graphics views, and save load now require explicit lifetimes and/or read scopes. Internal production callers compile; this is an uncommitted interface break without the required final interface review.

Permanent publication directory, domain table, and reader table hard bounded at 4096 slots, 32 domains, and 64 readers. Wrap/exhaustion refusal tested; population evidence to justify those production bounds not collected.

Config and keybinding loaders currently write default files automatically when the file is absent or invalid. Damaged files quarantined first. Intentional in the partial implementation; not yet reviewed as final product behavior.

Engine currently commits a new autosave generation on every normal renderer shutdown. Never deletes an older generation, but this automatic write policy and its relationship to `ANO_RES_SAVE_WARN 16` have not been reviewed or wired to a user prompt.

Several comments in the interrupted persistence diff remain stale, including an early `resources_core.c` state comment that still describes one global save mutex. Code now uses per-slot lanes.

Stage A still largely incomplete: data-driven level/world definitions and dependency disclosure, production lifetime domains for complete worlds, graphics skins/skeletons/animations and embedded images, manager-owned decoded resources, audio resources and a real audio consumer, script resources and runtime, ranged reads, bounded chunk pools, LZ4/zstd paths, packs, deterministic baking, loose-over-pack equivalence, hot reload, remote-filesystem evidence, alternate-loader deletion, and the full native platform matrix.

Stage B entirely incomplete: no full Model A/B/C/D/E implementation, no preregistered contest, no raw benchmark reports, no winner selected, no losing production paths removed after a complete contest.

Stage C entirely incomplete: registry mutex remains, final owner/service thread does not exist, generational tickets do not exist, resource requests and completions do not ride lock-free lanes, striped SPMC consume primitive does not exist, resource reclamation not integrated with tickets, streaming/packs/reload do not use tickets, Stage C deletion gate not met.

## Files currently modified or added

Tracked files modified: `CMakeLists.txt`, `include/anoptic_render.h`, `include/anoptic_res_graphics.h`, `include/anoptic_resources.h`, `include/anoptic_threads.h`, `src/engine/main.c`, `src/render/gltf/ano_GltfParser.c`, `src/resources/CMakeLists.txt`, `src/resources/graphics/res_graphics.c`, `src/resources/resources_core.c`, `src/resources/resources_internal.h`, `src/resources/resources_os.h`, `src/resources/resources_posix.c`, `src/resources/resources_registry.c`, `src/resources/resources_win64.c`, `src/threads/threads.c`, `src/vulkan_backend/instance/pipeline.c`, `src/vulkan_backend/instance/window.c`, `src/vulkan_backend/structs.h`, `src/vulkan_backend/text_raster.c`, `tests/CMakeLists.txt`, `tests/anotest_resbench.c`, `tests/anotest_resgfx.c`, `tests/anotest_resgroups.c`, and `tests/anotest_resources.c`.

Files added: `include/anoptic_config.h`, `include/anoptic_keybindings.h`, `include/anoptic_res_world.h`, `src/config/CMakeLists.txt`, `src/config/config.c`, `src/keybindings/CMakeLists.txt`, `src/keybindings/keybindings.c`, `src/resources/world/res_world.c`, `tests/anotest_persistence.c`, `tests/anotest_resdurability.c`, and `tests/anotest_resownership.c`.

This annotation file is also newly added by the final request of this run.

## Operational record

First ownership batch completed with a coherent verification report. Second persistence batch delegated too broadly; stopped while still active after a status-report interruption. Partial files not discarded. Inspected them directly, corrected the save-warning name, rebuilt the complete tree, reran the test suite, and ran the engine twice.

High-effort workflow-backed code review launched near end of run; result not retrieved or incorporated after tool interaction interrupted. No review finding from that workflow claimed here.

No source or documentation commit made. Working tree intentionally uncommitted so the two current test regressions and incomplete Stage A design can be corrected before the first phase boundary.

## Complete step-by-step activity record

1. Read repository instructions and current-step requirements in `CLAUDE.md`.
2. Read `docs/conventions.md` before implementation.
3. Inspected latest three commits and file-level changes with `git log` and `git show`.
4. Located and read the authoritative comprehensive resource-manager plan.
5. Read the historical implementation journal to distinguish completed machinery from superseded completion claims.
6. Read `flake.nix` and root `CMakeLists.txt` for native, cross, sanitizer, test, renderer, and headless build paths.
7. Read `src/src.md`, `tests/tests.md`, `resources/resources.md`, current public resource headers, resource-private header, resource CMake file, resource core, registry, graphics extension, and test registrations.
8. Created four top-level tracking tasks: complete Stage A, complete Stage B, complete Stage C, and perform final exhaustive verification. Established Stage A → Stage B → Stage C → final-verification dependencies.
9. Ran the 14-agent ultracode survey/synthesis/critique workflow described above. Workflow read the repository across twelve subsystem dimensions, synthesized an executable program, and adversarially identified omitted exit bars and evidence requirements.
10. Started a direct implementation agent for the Stage A ownership foundation: no commit; implement explicit lifetimes, neutral placement, stable publication, read scopes, reclamation, shutdown, accounting, consumer migration, and tests.
11. Waited for that implementation, asked it to stop expanding indefinitely and return a coherent compilable foundation, and received its completed change set and verification report.
12. Inspected the resulting working tree with `git status`, `git diff --stat`, `git diff --check`, and targeted symbol searches.
13. Started a second direct implementation agent for config, keybindings, save migration, per-slot ordering, deep save fallback, world-save integration, subprocess durability, and tests.
14. Reported status while that second implementation was still active. Implementation agent then stopped before completing its own review and final verification. Partial changes remained in the main working tree.
15. Inspected the partial persistence changes rather than discarding them. Read the new config, keybinding, world-save, persistence-test, and subprocess-durability files and inventoried every modified and untracked path.
16. Explained config and keybindings: explicit Stage A requirements; real durable-write, validation, migration, quarantine, and production-consumer clients.
17. Audited the save-data policy. Confirmed no code limits save slots or retained generations to three; automatic pruning remains absent.
18. Removed misleading unused `ANO_RES_SAVE_KEEP 3`, then followed the owner's correction by defining `ANO_RES_SAVE_WARN 16` as an advisory prompt threshold with an explicit never-auto-delete comment.
19. Corrected the nearby save-commit comment to describe per-slot serialization and user-only deletion rather than one global mutex or keep-three behavior.
20. Read `build.bat` to confirm the native Windows Debug test profile, then ran `build.bat 5` against the current complete working tree.
21. First attempted `cmd.exe /c build.bat 5` invocations produced only an interactive command prompt and did not execute the intended profile. Running `./build.bat 5` executed the real configure, scrub, full rebuild, link, and CTest path.
22. Full native Windows build succeeded. Automatic CTest returned nonzero; CTest rerun directly with `--output-on-failure` for the exact two failing executables and assertions recorded above.
23. Invoked repository runtime verification and launched `build/Tests/anopticengine.exe` under a 12-second timeout. Read stdout capture and generated session log for Vulkan initialization, resource-backed font loading, three model ingests, snapshots, frame timings, and absence of resource-manager ERROR/FATAL messages.
24. Launched the same engine executable again without a timeout as a visible desktop process and left it running. Process later exited normally.
25. Started a high-effort workflow-backed review of the complete uncommitted diff for correctness, policy, concurrency, and integration regressions. Review result not retrieved or incorporated after interaction interrupted; no finding from it treated as established evidence.
26. Created a separate hyphenated progress artifact at the wrong requested path, then appended its report into the owner's existing `docs/resourcemanager-comprehensive_annotated.md` and removed the mistaken file. Owner subsequently created this dedicated progress document so the report could remain separate from the annotated plan.
27. Updated this dedicated progress document to retain the complete implementation, verification, regression, and operational history without changing or deleting any other current worktree file.

## Still in flight

- Stage A active and incomplete. Ownership/publication foundation exists; tree not at Stage A exit bar.
- Save candidate/temp traversal rewrite in flight; currently regresses orphan-temp recovery.
- Keybinding persistence in flight; currently fails exact-round-trip test.
- Persistence batch has not received a complete implementation review, cleanup pass, sanitizer pass, or final runtime policy review.
- High-effort code-review workflow launched but output not incorporated; must not be mistaken for a completed review.
- Global/scoped scaffold removal unresolved; must reconcile with the comprehensive plan's requirement to preserve truthful scaffolding until the five-model contest exists.
- Hard bounds of 4096 resource slots, 32 domains, and 64 readers lack production-population evidence.
- Automatic creation of default config/keybinding files and automatic shutdown autosave are implemented but not yet accepted as final product policy.
- Current branch has no Stage A commit. All source, test, build, and documentation changes remain uncommitted.
- Stage B and Stage C have not begun.

## Planned next steps

1. Freeze the current working tree; make no phase claim while tests are red.
2. Fix the bounded-memory normal-generation and orphan-temp iterator so every candidate is visited, invalid temps purge correctly, valid temps recover without replacing preserved generations, and every pre-existing `anotest_resources` orphan oracle passes again.
3. Diagnose the keybinding round-trip failure; distinguish padding-only test defect from serialization or field-loss defects; fix the correct layer; strengthen the test to compare semantic fields without hiding byte-format errors.
4. Rerun focused resource, persistence, ownership, graphics, group, save-fault, and subprocess-durability tests, then the full Windows Debug suite.
5. Rerun ASan/UBSan and TSan on the complete post-fix Stage A tree; do not reuse the earlier ownership-checkpoint sanitizer result as evidence for later persistence changes.
6. Review the explicit-lifetime/read-scope API, reclamation protocol, shutdown behavior, hard bounds, accounting, and renderer/font/graphics call sites for correctness and interface altitude.
7. Restore or otherwise preserve the truthful historical allocator scaffolds and their comparison coverage until full Models A through E are implemented, unless a reviewed replacement keeps the same baseline evidence without leaking into production.
8. Review config/keybinding default-file creation and autosave-on-exit as product behavior. Keep `ANO_RES_SAVE_WARN 16` advisory only; retain user-only save deletion without exception.
9. Complete remaining Stage A resource domains and synchronous reference capabilities: data-driven levels, real production lifetime retirement, dependency disclosure and prefetch, graphics skins/skeletons/animations/embedded images, manager-owned decoded resources, audio, scripts, ranges, chunks, codecs, packs, deterministic baking, loose shadowing, hot reload, alternate-loader deletion, remote-filesystem verification, and the native platform matrix.
10. Update the implementation journal and benchmark/evidence records without rewriting historical claims.
11. Run a complete Stage A review and all available correctness/runtime gates. Commit Stage A only when its exit bar is genuinely met and the working tree is green.
12. Implement and run the full Stage B allocator contest with complete Models A, B, C, D, and E; preserve raw evidence; select the reproducible winner; remove losing production paths; rerun the winner; commit Stage B.
13. Implement Stage C owner service, ticket lifecycle, stable publication integration, lock-free ingress/completions, striped SPMC consume lanes, cancellation, shutdown, reclamation, and ticketed streaming/pack/reload path; satisfy the literal deletion and proof gates; then commit Stage C.
14. Run the final native Windows/Linux/macOS, sanitizer, remote-filesystem, engine-runtime, hardware-counter, benchmark, and documentation verification required by the comprehensive plan.
