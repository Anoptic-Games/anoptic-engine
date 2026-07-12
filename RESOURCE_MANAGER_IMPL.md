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
- [x] Phase A commit
