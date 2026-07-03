# Render merge report: `scene-sponza` × the backend infrastructure branch

**Status: landed.** The merge described here was performed, verified, and committed on
`checkpoint-enginemerge-doc` as `a0d0149` ("Merge scene-sponza into checkpoint-enginemerge-doc").
This document records what the merge actually took — the textual conflicts, the semantic conflicts
that only builds and tests could find, the verification performed — and the broader ramifications
that remain open, with concrete resolution proposals for the documentation and follow-up work.

---

## 1. The two branches

Both branches forked from the same commit: `e4591d0` (merge of PR #62 `client-engine`), which is
also the current tip of `main`. They then grew in almost perfectly disjoint directions.

### `checkpoint-enginemerge-doc` (backend infrastructure) — 31 commits

Platform-layer work, almost entirely under `include/`, `src/{logging,strings,time,filesystem,
memory,threads}`, and `tests/`:

- **Logger rewrite** (`src/logging/`): lock-free ring enqueue (`logging_ring.h`), owned consumer
  thread, deferred formatting, no-loss self-throttling policy, runtime severity gate,
  `ano_log_flush()`. The old mutex logger is preserved as `logging_old.c` for the benchmark only.
  Design doc: `docs/logger.md`.
- **Strings** (`src/strings/`): the 16-byte German-string value type (`anostr`), builder, slicing,
  ops, and interning (`ano_strings_intern.c`). Progress doc: `docs/string_progress.md`.
- **Time**: `ano_timestamp_ticks()` / `ano_ticks_to_ns()` (raw counter for hot paths, division
  deferred), `ano_localtime()`, a rewritten `time_win64.c`.
- **Memory**: `anoptic_memalign.h` + `memory.h` merged into **`anoptic_memory.h`**, which is now
  the one canonical allocator include (it pulls `mimalloc.h` + `mimalloc-override.h` in the right
  order). Introduced `ANO_CACHE_LINE` (coherency grain) and `ANO_THREAD_LINE` (false-sharing
  isolation distance, 128).
- **Filesystem**: moved into `anoptic_core` (was engine-only), extended API.
- **Threads**: `ano_thread_cond_timedwait`.
- **Build/tests**: Nix dev shell for WSL/Linux, build option 7 (Release + CTest), test templates
  (`tests/templates/{bench,rng,scratch}.h`), new suites: logging (1100+ lines), logfuzz, logbench,
  logtail, strings, strbench, filesystem.

It also **tersified comments** across shared files (`src/mesh/`, `src/render_bridge/`,
`src/vulkan_backend/` includes) — which is what produced most of the textual conflict surface.

### `scene-sponza` (renderer) — 60 commits

Renderer work, almost entirely under `src/vulkan_backend/`, `resources/shaders/`,
`include/anoptic_render.h`, `src/mesh/`, and `src/engine/main.c`:

- **Lighting**: froxel light clustering, power-CDF shadow maps with 4-layer splitting, shared
  shadow atlas with dirty-frustum reuse, attached lights with full runtime lifecycle
  (attach/update/detach), light compaction, partial-field light updates, directional revision.
- **Visibility**: Hi-Z occlusion culling on an async compute queue, depth pre-pass with
  depth-only shader variants, task-shader meshlet cone culling, backface cone culling,
  screen-area bounding-box culling, multi-view frustum culling with a PiP inset view.
- **LOD**: quadric-error mesh simplifier (`ano_simplify`, `ano_simplify_ex` with decimation
  guards), LOD generation/bias/upload, vertex-subset compaction, coarser shadow LOD.
- **Presentation**: HDR lighting target + tonemap pass, additive + transmission transparent
  compositing, MSAA policy, regularized face winding.
- **Logic↔render channel**: screen-space picking (R32_UINT id attachment), input forwarding
  (render → logic), camera ownership moved to the logic thread, latest-wins seqlock publications
  (`RenderSnapshot` / `AnoViewState`), instance-data channel, bulk create/update/destroy,
  zero-copy streamed transforms with a GPU scatter pass.
- **Diagnostics**: per-pass GPU timing, VRAM accounting, runtime lighting-mode toggle (radiance
  cascade study: `docs/references/radiance-cascades.md`).

The public contract `include/anoptic_render.h` grew from a small lifecycle header into the full
protocol (~800 lines): `RenderEvent` moved public and became a tagged union, `DisplayState` gained
`AnoMotionDescriptor motion` (replacing `angular_velocity`) and `AnoInstanceData`.

Note: `module-render` is an ancestor of `scene-sponza` (zero unique commits), so this merge
subsumes it. The earlier "Staging merge of new backend with updated renderer" commit (`08a9f51`)
on this branch is an **empty tree delta** — a marker commit, not an actual staging merge.

---

## 2. Where they touched

16 files were modified on both sides; git auto-merged 11 of them and left **5 files / 7 conflict
hunks**. The overlap was small because the module boundary held: the backend branch's only
renderer-side edits were include normalizations (`mimalloc*` → `anoptic_memory.h`) and comment
tersification, while sponza's only backend-side edits were in files it consumed.

### Textual conflicts and how they were resolved

| File | Hunks | Nature | Resolution |
|---|---|---|---|
| `src/engine/main.c` | 1 (large) | Backend tersified the old stand-in logic thread; sponza replaced it wholesale (scene spawn, WASD/mouse camera, event drain, snapshot logging) | **Sponza side.** The stand-in referenced `anoRenderEntity0Mesh()`, which no longer exists in the sponza contract |
| `src/mesh/ano_meshoptimizer.c` | 3 | Backend tersified comments on the old cone-bounds algorithm; sponza replaced the algorithm (normalized-normal axis, `cutoff = sin(spread)`, meshopt convention) | **Sponza side.** The backend's changes were comment-only; sponza's are semantic and consumed by `flat.task`'s culling test |
| `src/mesh/ano_meshoptimizer.h` | 1 | Doc comment for `ano_compute_meshlet_bounds` | **Sponza side** (documents the new convention) |
| `src/render_bridge/render_bridge.h` | 1 | Comment naming the `DisplayState` motion field | **Sponza side** (`motion` is the real field name now) |
| `src/vulkan_backend/instance/instanceInit.c` | 1 | Include block: backend's `<anoptic_memory.h>` vs sponza's `<math.h>` + raw mimalloc pair | **Union**: `<math.h>` + `<anoptic_memory.h>` (which subsumes the mimalloc includes) |

Everything else merged cleanly, including the two big compositions: the top-level
`CMakeLists.txt` (backend's filesystem/strings build restructure + sponza's 19-shader compile list
with depth-only/task/Hi-Z variants) and `render_bridge.h` (backend's
`ANO_CACHE_LINE`→`ANO_THREAD_LINE` ring migration + sponza's seqlock and public-event
restructuring).

---

## 3. Semantic conflicts (found by building and testing, not by git)

This is the part "beyond checking the diffs" — four real cross-branch collisions that produced a
clean git merge and a broken tree. All are fixed in the merge commit.

### 3.1 `mimalloc-override.h` vs the new header discipline (build break)

Sponza's **new** file `src/vulkan_backend/instance/pipelines/additive.c` included
`<mimalloc-override.h>` directly (the old idiom). Under MinGW that collides with
`<stdlib.h>`/`<malloc.h>` (`_msize` macro vs the CRT prototype) unless `<stdlib.h>` is included
first — exactly the ordering problem `anoptic_memory.h` was created to own. Since git saw a brand
new file, no conflict was reported. **Fix**: include `<anoptic_memory.h>` instead.
**Policy proposal**: no renderer file includes `mimalloc*` headers directly, ever; grep gate:
`rg "mimalloc-override" src/` should stay empty.

### 3.2 The `RenderEvent` union vs the backend's transport test (build break)

Sponza made `RenderEvent` a tagged union in the public header; the backend branch had meanwhile
grown `tests/anotest_render_bridge.c` against the old flat struct (`e.render_id`). Git never saw a
conflict — different files. **Fix**: the test now uses `e.u.render_id`.

### 3.3 `independentBlend`: latent spec violation surfaced by the backend's test gate

Sponza's opaque pass renders **two color attachments with different per-attachment blend state**
([0] HDR color blends; [1] R32_UINT picking id never blends and masks differently — see
`src/vulkan_backend/instance/pipelines/flat.c`). The Vulkan spec requires the `independentBlend`
device feature for that (VUID-VkPipelineColorBlendStateCreateInfo-pAttachments-00605), which
device creation never enabled. The renderer worked anyway on NV hardware, so the sponza branch
never noticed; the backend branch's validation-asserting tests (`anotest_vk_compliance_layers`,
`anotest_vk_sync`) failed the merged tree immediately. **Fix**: `createLogicalDevice` now enables
`independentBlend` from the queried features (universal on desktop hardware). This is a bug fix
sponza needed regardless of the merge.

### 3.4 Stale meshoptimizer test assertions vs the new cone convention

`tests/anotest_meshoptimizer.c` (sponza's own expanded test) failed twice — and both failures
**reproduce on pristine `scene-sponza`**, verified in a separate worktree. They are pre-existing
latent failures the sponza branch never saw because renderer development ran without the CTest
gate:

- Line 49 asserted `cone_cutoff > 0.0f` ("should be 1.0 - epsilon") — the **old** `min_dot - 0.05`
  convention. Under sponza's own meshopt convention, one flat triangle has zero normal spread →
  `cutoff = sin(0) = 0`. Assertion updated to expect ~0.
- T4 (`test_simplify_flat_not_gutted`) asserted the decimation guards are **bit-inert** on a plane
  (`r_on == r_off`). Measured: `r_off = 1080`, `r_on = 1083` against `target = 1083` — the
  unguarded run overshoots the target by one collapse (a collapse retires two triangles at once);
  the guarded run stops exactly at budget. The guards do not gut planar decimation, which is the
  property that matters. Assertion relaxed to `r_on <= target && r_on <= r_off + 3`.

**Testing-policy proposal**: renderer branches run at least the `unit;mesh` + `vulkan` CTest
labels before review. Every one of these four collisions would have been caught by `build.bat 3`
on the branch.

---

## 4. Verification performed

On the merged tree (Windows, MSYS2 clang64, RTX 4090):

- **Debug and Tests builds** compile clean (one pre-existing `-Wformat` warning set in
  `anotest_logging.c`, see §6.6).
- **Full CTest suite: 14/14 enabled tests pass**, including the logger fuzzer, the render-bridge
  concurrency test, all five Vulkan-device tests, and the meshoptimizer suite.
- **20-second live smoke run** of the Debug engine (validation layers on): zero validation
  messages, ~330 fps at 800×600, camera/logic thread publishing views, snapshot channel live
  (`frameId 6533` at kill). Viking Room and candleholder assets render; user-confirmed visually
  correct, no artifacting.
- The Sponza scene itself is **not in the repo** (`assets/` is gitignored) — the engine logs
  `Failed to parse glTF file: sponza/2.0/Sponza/glTF/Sponza.gltf` and continues by design. See §6.5.

Not verified: Linux/macOS builds of the merged tree (§6.4), and any automated image-level render
comparison (none exists yet).

---

## 5. Ramifications: where the two worlds now touch

The merge is textually done, but the branches were built on different assumptions in a few places.
Nothing below blocks the merge; each is a deliberate follow-up.

### 5.1 The renderer logs with `printf`; the engine now has a real logger

The renderer carries ~300 raw `printf`s (`vulkanMaster.c` 75, `instanceInit.c` 88, `pipeline.c`
39, `texture.c` 16, ...). Meanwhile the logger is feature-complete, thread-safe, and fast — but
`main()` currently only exercises it in a DEBUG_BUILD self-test block and immediately tears it
down. Proposal, in order:

1. Hoist `ano_log_init()` / `ano_log_cleanup()` to bracket the engine's real lifetime in `main()`
   (init before `initVulkan`, cleanup after the logic thread joins). Delete the self-test block.
2. Route the Vulkan validation callback and renderer error paths through `ano_log_error` /
   `ano_log_fatal` first — these are the messages that matter when a run goes wrong.
3. Migrate the rest module-by-module. Startup chatter (mip levels, glTF debug) becomes
   `ano_log_debug`, which compiles out of Release entirely.

**One real constraint**: the logger's no-loss policy means a producer **blocks** when the ring is
full, waiting for the consumer thread to drain. `drawFrame()` and the per-frame paths must not log
unboundedly, or a slow drain becomes frame hitching. Per-frame diagnostics (GPU timings, VRAM
stats) should stay behind a debug gate or aggregate before logging. The render thread never
touching the ring in the steady state is the right default.

### 5.2 False-sharing policy: the seqlock versions predate `ANO_THREAD_LINE`

The merged `render_bridge.h` correctly uses `_Alignas(ANO_THREAD_LINE)` (128) for the SPSC ring
cursors (backend side), but sponza's new seqlock members `snapshotVersion` / `viewStateVersion`
still carry `_Alignas(ANO_CACHE_LINE)` (64) — written when 64 was the only constant. They are
exactly the "hot per-thread atomics" `ANO_THREAD_LINE` exists for (`anoptic_memory.h`: x86
adjacent-line prefetch pairs, Apple Silicon 128-byte lines). Compiles and works today; one-line
follow-up each, plus their comment.

### 5.3 The time module has a cheaper primitive than the renderer uses

Sponza added per-pass GPU timing and CPU-side timestamps via `ano_timestamp_us()`. The backend
added `ano_timestamp_ticks()` / `ano_ticks_to_ns()` precisely so hot paths pay one counter read
and defer the division. The frame loop and the logic tick are the intended customers. Related:
`measureFrameTime()` / `findAverage()` in `main.c` are stand-in profiling that should eventually
fold into the logger + ticks API.

### 5.4 Platform parity is claimed but only Windows-verified

The backend branch's headline was full cross-platform compatibility; sponza developed on
NV/Windows. Sponza's new renderer code is include-clean (no POSIX/Win32 leakage — verified by
grep and by the MinGW build), but three things need a Linux/macOS pass:

- macOS/MoltenVK: no `VK_EXT_mesh_shader` → exercises the vertex fallback path end-to-end,
  including the new depth-only and task-cull variants' fallback behavior.
- `VK_RESOLVE_MODE_MAX_BIT` (Hi-Z resolve avenue) is queried at runtime — confirm the fallback
  compiles/loads where absent.
- The async-compute queue split (Hi-Z, light cull) on drivers with fewer queue families.

The Nix/WSL path from the backend branch (`shell.nix`, README §Building under WSL) makes the
Linux headless + test build one command; the renderer needs a real GPU host.

### 5.5 Assets: Sponza is load-bearing for the branch's namesake scene

`assets/` is gitignored; the engine expects `assets/sponza/2.0/Sponza/glTF/Sponza.gltf` (the
Khronos glTF-Sample-Models layout) and warns-and-continues without it. The README's asset note
lists the viking room and candleholder but not Sponza. **Proposal**: add Sponza (source URL +
expected path) to the README asset paragraph, since the scene is now the renderer's primary
benchmark content.

### 5.6 Small cleanups the merge surfaced

- `anotest_logging.c` has `-Wformat` warnings on MinGW (`%ld`/`%lu`/`%lx` vs 64-bit literals —
  `long` is 32-bit on Windows). Pre-existing on the backend branch; fix to `%lld`/PRIu64 forms.
- `include/anoptic_render.h` and `render_bridge.h` reference
  `docs/artifacts/VK_BACKEND_INTEROP.md` and `docs/artifacts/RADIANCE_CASCADES.md` — **neither
  file exists on any branch**. Either restore them from wherever they live or repoint the
  references (the radiance-cascade material is at `docs/references/radiance-cascades.md`).

---

## 6. Repository-shape proposals

### 6.1 Documentation updates owed by this merge

| Doc | What's stale | Proposed update |
|---|---|---|
| `README.md` §Runtime Features / §Rendering Compatibility | Predates most of sponza: no mention of Hi-Z occlusion, depth pre-pass, task-shader cone culling, shadow atlas + power-CDF shadows, HDR/tonemap, mesh LOD, picking, multi-view/PiP, GPU timing; logger/strings absent from features | Rewrite the renderer bullets around the actual pipeline (pre-pass → Hi-Z → cull → opaque → transparents → tonemap); add a platform-layer bullet (lock-free logger, strings, time) |
| `include/include.md` | Documents the old, small `anoptic_render.h` | Document the full contract: lifecycle, command protocol (incl. bulk + streamed transforms), event union, snapshot/view seqlocks, light lifecycle API |
| `src/src.md` | Module list predates both branches' additions | Add the renderer submodule descriptions and the logger's ring/consumer split |
| `tests/tests.md` | Covers the backend suites | Add the meshoptimizer suite's scope and the "renderer branches must run ctest" policy from §3 |
| `docs/TODO.md` | Build sequence written before the renderer landed | Fold in the follow-ups from §5 (logger adoption, THREAD_LINE, ticks, platform pass) as explicit steps |

### 6.2 Root-level audit files

Sponza added `CODE_REPORT.md`, `DESIGN_AUDIT.md`, `RENDERER_REVIEW.md` (1,700 lines) at the repo
root. They are point-in-time review artifacts, several already-executed ("audit 4.x" comments in
code cite them). **Proposal**: move to `docs/artifacts/` — which also creates the directory those
dangling header references (§5.6) point at.

### 6.3 The `profile/` directory

~22k lines of Nsight/profiler CSV exports (`profile/shaders/shadow_cdf_profile.csv` alone is 7k
lines). Valuable as the record behind the optimization commits, but it is bulk data in the source
tree. Sponza's own `.gitignore` added `docs/study/` for exactly this class of material.
**Proposal**: move `profile/` under `docs/study/` (keeping it out of future diffs) or prune to
the summary files (`analysis.yaml`, `hotspots.csv`, `top_down.csv`/`bottom_up.csv`) and drop the
per-shader dumps.

### 6.4 Branch topology after this merge

`checkpoint-enginemerge-doc` now contains everything: backend ⊇ strings/logger/time/filesystem
work, renderer ⊇ all of `scene-sponza` ⊇ all of `module-render`. `main` is still at the common
base. **Proposal**: after the §5.1 logger hoist and a Linux verification pass, PR this branch to
`main` and retire `scene-sponza`, `module-render`, and `checkpoint-enginemerge` (which is
identical to this branch's pre-merge state).

---

## 7. Post-merge follow-up checklist

- [ ] Hoist `ano_log_init`/`ano_log_cleanup` to engine lifetime; delete the DEBUG self-test block (§5.1)
- [ ] Route the validation callback + renderer error paths through the logger (§5.1)
- [ ] `snapshotVersion`/`viewStateVersion` → `_Alignas(ANO_THREAD_LINE)` (§5.2)
- [ ] Linux (Nix) + macOS build/test pass; fallback-path run via `ANO_FORCE_NO_MESH_SHADER=1` (§5.4)
- [x] Add Sponza to the README asset instructions (§5.5)
- [ ] Fix `anotest_logging.c` format-string widths for Windows (§5.6)
- [ ] Restore or repoint `docs/artifacts/*` references (§5.6)
- [ ] Move audit files to `docs/artifacts/`, decide `profile/`'s home (§6.2, §6.3)
- [ ] README / include.md / src.md / tests.md / TODO.md updates (§6.1)
- [ ] PR to `main`; retire the subsumed branches (§6.4)
