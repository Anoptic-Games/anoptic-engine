# TODO

The macOS Vulkan bring-up epic is done and merged to `main` (PR #62). This file is reset to the engine build sequence from `docs/notes.md` ("What Needs to Be Built", bottom-up dependency order). Terse here. The full spec, current-state audits, and rationale live in notes.md. Current branch: `feature-string-redux` (Step 4).

## Step 1 -- High-performance logger
Standalone module. Lock-free MPSC enqueue (`fetch_add` + per-slot commit marker), timestamped records, flusher thread via `anoptic_threads`. Wire the sink (`ano_log_output_dir`, `ano_log_interval`) and test file output. The mutex version exists and is concurrency-correct, but output is entirely stubbed. See the audit, rewrite plan, and test plan in notes.md Step 1. First module to exercise arenas + atomics + threads together. Instruments everything after.

## Step 2 -- Dependency update
Bump GLFW, stb, jsmn, mimalloc submodules to latest stable. Audit API changes. Revalidate mimalloc heaps (`mi_heap_new`/`_destroy`/`_zalloc_aligned`), hugepages, `LOCALHEAPATTR` teardown, and the `mimalloc-override.h` global override. Low risk, low effort.

## Step 3 -- Windows high-resolution timing
Bring `ano_sleep` on Windows to Linux parity: `timeBeginPeriod(1)` + `WaitableTimer` (or `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, Win10 1803+) for the coarse wait, then spin tail (`ano_busywait`) for the sub-millisecond remainder. Today's `Sleep()` jitter (15.6ms) makes a deterministic tick length impossible.


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
