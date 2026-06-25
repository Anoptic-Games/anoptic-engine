# TODO

The macOS Vulkan bring-up epic is done and merged to `main` (PR #62). This file is reset to the engine build sequence from `docs/notes.md` ("What Needs to Be Built", bottom-up dependency order). Terse here. The full spec, current-state audits, and rationale live in notes.md. Current branch: `feature-string-redux` (Step 4).

## Step 1 -- High-performance logger  ✅ DONE (2026-06-24)
Lock-free MPSC ring (variable-length, cache-line-granular; CAS reserve with a full-check; lap-counter reclaim with no zeroing) and an owned consumer drain thread; `ano_log_flush` is a synchronous inline pass. Caller-side bare-ticks timestamp, divisions deferred to drain. Two formatting strategies: eager (hand-rolled prefix + `fast_format`, ~48 ns) and deferred (capture pointers + typed args, render at drain, ~22 ns). Output file + `ano_log_output_dir` wired. Public header `include/anoptic_logging.h`, impl `src/logging/`. Validated four ways: TLA+/TLC + a Haskell model (`scratch/logger/`, gitignored), TSan-clean, byte-for-byte format battery + the `anotest_logfuzz` no-loss fuzzer, and the benchmark (`anotest_logbench`, run from `build.sh 7`). Design `docs/logger.md`; per-function line-count justification `docs/logger_explained.md`. The first module exercising atomics + threads together; instruments everything after.

## Step 2 -- Dependency update  ✅ DONE (2026-06-24)
Bumped glfw `3.3-781` master → tag **3.4**, mimalloc `v2.1.2` → tag **v2.3.2** (submodule pointers); stb_image.h v2.28 → **v2.30** (vendored). jsmn already byte-identical to upstream master (no change). Held mimalloc on the v2.x line: v3.3.2 exists and is upstream-"recommended" but is a major redesign, deferred as a deliberate separate bump. API audit (researched + adversarially verified): no source-level breakage for any symbol we use across all four. mimalloc revalidated via `anotest_memory` (heaps, `mi_heap_zalloc_aligned`, `LOCALHEAPATTR`→`ano_heap_release`, huge-page probe, `mimalloc-override.h` global override all pass on v2.3.2). Tests 10/10 on `build.sh 7`. Benchmarks (3+ iterations before/after) show no regression: enqueue latency flat ~24 ns, throughput within run-to-run variance. Full report: `scratch/bench/2026-06-24-deps.md`. Two macOS/MoltenVK items to eyeball after the glfw 3.4 bump on a Retina display: HiDPI framebuffer scaling and the no-op `glfwMakeContextCurrent` under GLFW_NO_API.

## Step 3 -- Windows high-resolution timing  ✅ DONE (2026-06-24, unverified on host)
`ano_sleep` rewritten in `src/time/time_win64.c`: per-thread hi-res `CreateWaitableTimerExW` (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Win10 1803+) for the coarse wait, falling back to a manual-reset timer floored by `timeBeginPeriod(1)` (one-time, CAS-guarded, `atexit`-paired) on older Windows, then an `ano_busywait` spin tail for the sub-ms remainder. errno-parity returns (0 / positive errno) like the Unix backends. Timer handle closed at thread exit via an FLS destructor (no leak under transient job threads). winmm linked in `src/time/CMakeLists.txt`; `_WIN32_WINNT` pinned; MSVC <17.5 C-atomics guarded. Designed via a 3-way panel and adversarially reviewed (3 lenses + verdict), all must-fix items applied. NOTE: cannot be compiled on the macOS dev host (Windows-only TU, excluded from the mac build) — needs a Windows build + a tick-jitter measurement to close out.


## Step 4 -- ano_strings  (current branch)
Owned string type `{char* ptr, uint32_t len, uint32_t capacity}` with `LOCALHEAPATTR`-style scoped cleanup. Allocations through a heap parameter so strings live in any arena. Copy-on-slice. ~150 lines. UTF-8 deferred (byte-transparent in storage; validation/iteration layered on later when the text renderer needs it). The signatures already exist on the `feature-strings` branch. notes.md flags it as the Step 4 spec to recover from.

## Step 5 -- Lock-free collections
Phase A: classic Michael & Scott queue + bounded MPMC ring (Vyukov-style), tested and benchmarked as baselines. Phase B (experimental): cache-line-striped structures. Make the 64-byte coherency unit the unit of ownership transfer (claim a stripe via `fetch_add`, fill with plain stores, publish via release commit flag; no per-item CAS). Open problems + design in notes.md.

## Step 6 -- Resource management
Per Game Engine Architecture. (notes.md also lists a parallel "additional data structures as needed": build structures alongside the features that use them. `stb_ds` is an acceptable prototyping stopgap.)

## Step 7 -- Renderer rewrite
Full Vulkan rewrite as a proper subsystem: scratch arenas, the real logger, the event bus. v1 scope: one render pass, one pipeline, geometry on screen, event-bus-driven. No PBR, rasterization only. `stb_image` retained for textures.

## Step 8 -- Event bus + input
Global, thread-agnostic event bus (possibly two: monotonic per-item for ordered events like input, cache-line-striped for high-throughput bulk events). GLFW callbacks enqueue input. The game loop dequeues. Clean producer/consumer boundary. Also serves future physics.

## Step 9 -- Main game loop + first visual output
Integration milestone (v0.1): input moves the camera, the event bus carries it, simulation updates transforms, the renderer draws, all allocated from frame arenas and all logged. A sphere on screen through the full pipeline. Proof every layer works together.

## Later -- DEBUG_TRACE (crash trace)
Sibling of the logger, distinct module. Same `rte_ring` skeleton + 16-byte marker, opposite durability policy: ring `mmap`'d to a file and never zeroed, so the last-N records survive the process and a debugger or the next boot reads them straight out of the mapping. The logger zeroes drained lines and reuses in place, so at a crash its ring is half-gone. Captures last-N before a fault. Survival/order over throughput. See `.claude/profiler-and-trace.md` (also covers the `anoptic_profiler.h` throughput sibling and the shared-ring-vs-per-producer ownership question that lands before Step 5).
