<!-- SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors

SPDX-License-Identifier: LGPL-3.0 -->

# Resource Manager: State of the Art

**Status:** the machinery rationale and source base.
**Scope:** `resource-manager-plan.md` is the ordered implementation and testing sequence
(the keepers); this document is the reasoning underneath: how bytes move at hardware
speed, and which sources say so.
Sources are the three streams the engine always draws from — Gregory's *Game Engine
Architecture* ch. 7, the I/O research canon, and the open-source and industry systems that
gulp files fastest today — plus the in-tree system that already solved most of this shape:
the logger.

---

## 1. The performance model: request rate, not bandwidth

Every source agrees on where the fight is, and it is not peak MB/s:

- **NVMe is built for depth.** The spec allows 64K queues × 64K entries; the drive
  saturates only when many requests are in flight at once. Legacy synchronous I/O stacks
  fall over at high *request rates* long before the drive runs out of *bandwidth*
  (Haas & Leis, VLDB'23; the DirectStorage documentation makes the same point as its
  founding premise).
- **The industry bar is explicit.** DirectStorage's design goal: **50,000 requests/second
  at ≤ 10% of one CPU core**, minimum 2 GB/s raw over any 250 ms window. Those are the
  numbers a modern asset streamer is expected to hit. Adopt them as our ceiling-check
  targets; our workloads are smaller, our per-request overhead should not be worse.
- **Two workload shapes, two easy/hard splits.** Bulk level load = few large sequential
  reads — trivially saturates with readahead, any design passes. Steady-state streaming =
  thousands of small *ranged* reads at LOD/chunk granularity — this is the hard shape and
  the one every architectural decision below serves.
- **Warm and cold caches invert the rankings.** With a warm page cache (the dev loop:
  same assets, hundreds of runs) buffered reads tie or beat everything — mmap and
  O_DIRECT tricks buy nothing. On a cold cache (first run, the shipped game)
  io_uring + O_DIRECT measured 3.6–3.8× faster than mmap on NVMe with thousands of
  requests in flight (safetensors' 2026 Linux fast path). Consequence: **the default
  path is buffered and page-cache-friendly; the pack tier keeps the cold-start door
  open.** Never optimize the dev loop away to win a benchmark the dev loop never runs.
- **The transport layer is already free.** Our logger's ring enqueue costs 22–48 ns —
  three to four orders of magnitude below one NVMe operation (~10 µs warm, ~100 µs
  cold). Queueing overhead is noise. The entire budget is queue depth, decompression,
  and memory placement. This is why the plan reuses the logger's transport instead of
  inventing a new one.

## 2. The shape: rings and a pump (the logger, generalized)

Gregory's async-I/O blueprint (§7.1.3) is a thread pulling requests off a queue, doing
blocking reads, signalling completion. That is the logger's exact topology, already
built, benchmarked, and TSan-clean in this codebase — with better answers than the book's
semaphore-and-callback in every slot. The mapping is mechanical:

| Logger mechanism (in-tree, proven) | Resource manager equivalent |
|---|---|
| variable-length cache-line MPSC ring, reserve by bumping `tail`, publish with one release store (`logging_ring.h`) | request ring: variable payloads `{rid, range, dest, band, ticket}` from any thread |
| owned consumer thread; parks on empty pass, timedwait cap bounds lost wakeups; producers wake on empty→nonempty | the IO thread, identically; submit is the wake |
| lap-counter (`cycle`) reclaim, no zeroing | same — slots reused by lap check |
| full-ring: escalating backoff + stall fallback | submit returns false-on-full (the render bridge convention); caller retries next frame |
| deferred formatting — capture cheap at the call site, render at drain | verify/decompress on the IO/decode side, never the requesting thread |
| `ano_log_flush` synchronous inline pass | `ano_res_load` synchronous path IS the primitive; a blocking wait on a ticket boosts its band |
| bare-ticks stamps, divisions deferred to drain | per-request telemetry stamped raw, converted in the stats pass |

Completions return on **SPSC rings drained by a per-frame pump** on the consuming thread
— polled, never callbacks from the IO thread (house rule; also where Gregory's model,
Bevy, and sokol_fetch all converge). The ring code itself ports out of `src/logging/`
into `anoptic_collections.h` as the planned generic variable-length MPSC — the port that
header has been waiting for.

## 3. Memory: fixed chunks, registered once, loaded in place

- **A pool of fixed-size chunks (512 KiB) is the streaming currency.** Three independent
  sources land on the same grain: Naughty Dog shipped 512 KiB (PS3) / 1 MiB (PS4)
  streaming pools (GEA §7.2.2); io_uring reads above ~512 KiB fall off the kernel-worker
  cliff; registered-buffer amortization pays off precisely for small-frequent operations.
  Pool allocation is O(1), fragmentation-immune, and the chunk array is exactly what
  `io_uring_register_buffers` wants handed to it later (+8–18% throughput, kernel CPU
  per-op measurably reduced — the `get_user_pages` cost paid once instead of per read).
- **Load-in-place is the parse killer** (GEA §7.2.2.8–.10). Baked resources are PODS
  images: serialize contiguously, store pointers as offsets plus a fix-up table, load the
  whole image into one arena block, add the base address to each fix-up slot, done. C23
  makes this *free* — no placement-new, no constructor ordering, the C-over-C++ advantage
  the book half-admits. At ship, "parsing" a model is a memcpy and a fix-up loop.
- **Sectioned resources** (GEA §7.2.2): a baked file carries main-RAM / VRAM /
  temporary-load-time / debug sections. The loader routes them: VRAM section → staging
  chunk → GPU upload; temp section → scratch arena freed after post-load init; debug
  section skipped in release. Post-load init is a type → `{init, teardown}` function
  table — the book's own C-native suggestion.
- The loose-file tier keeps the plan doc's blob-in-caller's-heap contract unchanged;
  chunks and sections are the pack tier's economy.

## 4. Decompression is part of the read path, not after it

Compressed storage is bandwidth amplification: a 2 GB/s drive delivering 2:1-compressed
assets is a 4 GB/s drive, provided decode keeps up and overlaps the next read.

- **Codecs:** LZ4 where decode latency gates (streaming chunks), **zstd** for bulk —
  the choice DirectStorage 1.4 (GDC 2026) just standardized on for the entire Windows
  ecosystem, for the same reasons we would pick it: open, fast, everywhere. Trained zstd
  dictionaries for many-small-similar classes (SPIR-V modules, JSON); store-raw for
  already-compressed payloads (PNG, Opus).
- **Placement:** decode runs on worker threads (the job system, once it exists; a decode
  thread until then), pipelined so chunk N decodes while chunk N+1 reads — the
  channels × lanes overlap sokol_fetch demonstrates in 100 lines of state machine.
- **The shuffle lesson:** Microsoft's Game Asset Conditioning Library gets up to ~50%
  better zstd ratios on BCn textures by byte-shuffling before compression and unshuffling
  after decode. File the idea with the future texture cooker — it is an offline-tool
  trick, not a runtime dependency.
- **GPU decompression** (DirectStorage's GDeflate-to-VRAM path): watched, not adopted —
  Win11/D3D12-coupled and MinGW-hostile. The pack format's codec byte reserves the id;
  that is its entire footprint until the day it earns more.

## 5. Prefetch: disclosure, not divination

TIP (Patterson et al., SOSP'95) settled this three decades ago: prefetching works when
the application *discloses* its future accesses, not when the storage layer guesses.
The game always knows its future — the level file names every asset it needs; the
streaming system knows which chunks border the player. So:

- **Priority bands** `BLOCKING > FRAME_CRITICAL > STREAMING > PREFETCH`, drained in
  strict band order. Requests carry their band; the IO thread never reorders across
  bands.
- **PREFETCH is byte-budget-metered per frame**, budget refilled by the pump — prefetch
  can never starve the frame or the higher bands.
- A blocking wait on any ticket **boosts it to BLOCKING** — the disclosure was wrong,
  correct it instead of stalling.
- The level file doubles as the disclosure list; a mount-time manifest of content hashes
  doubles as the hot-reload confirmation source in dev builds.

## 6. The backend ladder: one interface, N backends

TigerBeetle's storage lesson, adopted whole: define **one completion-shaped IO
interface** and let backends compete underneath it. The rings above are that interface.
The ladder, climbed only on measured demand:

- **Rung 0 — one IO thread, blocking `pread`, `posix_fadvise`** (`SEQUENTIAL` on open,
  `WILLNEED` on queued requests). Saturates game-scale bulk loads outright — "modern
  storage is plenty fast" (Costa) holds at our asset counts, and ripgrep's measurements
  (plain reads beating mmap across many files) back the same instinct for the loose tier.
- **Rung 1 — 2–4 IO threads, parallel `pread`.** Queue depth via threads: the cheapest
  possible answer to NVMe's parallelism appetite (Haas & Leis: concurrent requests are
  the entire game), no new API surface, portable everywhere including 9P/SMB.
- **Rung 2 — io_uring (Linux) / overlapped-IOCP (Windows)** behind the unchanged rings:
  batched submission (accumulate SQEs, submit in batches — the single most important
  io_uring discipline), registered buffers over the chunk pool, fixed files for pack
  handles, reads chunked ≤ 512 KiB. Cold-start O_DIRECT on packs as an experiment flag,
  never the default (loses the warm dev loop; unsupported over 9P).
- **Never:** mmap as a load path (CIDR'22; SIGBUS-on-truncation and coherence breakage
  over the 9P/SMB deployment floor; loses cold-start anyway per §1), SQPOLL/IOPOLL
  (core-burning database tricks), DirectStorage as a dependency (§4).

Each rung must beat the previous one in `anotest_resbench` percentile tables — p50/p99
per band under a streaming background load, bulk-load wall time, requests/sec at fixed
CPU budget — before it merges. Means don't count; the logger's benchmark culture
(percentiles or it didn't happen) applies unchanged.

## 7. Pack format: no filenames at runtime

Unreal's IoStore drew the right conclusion: at ship, the runtime should never touch a
path string — chunked containers keyed by hashed ids, resolved through a table of
contents. Ours, minimally:

- TOC entries `{rid, offset, size, csize, codec, hash}`, payloads 4 KiB-aligned, TOC
  checksummed and verified **at mount** — a corrupt pack refuses at startup, never
  lazily mid-game.
- **`rid` is the same 64-bit FNV-1a the whole engine keys on**: `ANOSTR_SID` at compiled
  call sites, `anostr_hash` over parsed strings — so a pack lookup is "binary-search the
  TOC for the integer the compiler already baked into the call site." No string ever
  moves at load time.
- A pack is just another mount in the namespace walk; loose files shadow it during dev
  (write root first), which is the hot-reload story and the mod story in one mechanism.
- The builder is a ~200-line offline tool; it and the load-in-place baker (§3) are the
  two halves of Gregory's offline conditioning pipeline, built to the same "formats are
  frozen on paper first" rule as the save frame.

## 8. Build order

1. **Sync core** — namespace, read contract, durable writes (`resource-manager-plan.md`
   §8 steps 1–4). The synchronous `ano_res_load` is the primitive everything above wraps.
2. **The transport** — port the logger ring to `anoptic_collections.h` as the generic
   variable-length MPSC; request rings + IO thread (rung 0) + completion rings + pump;
   tickets, bands, budget. *Bar:* background-stream a level's worth of bytes with zero
   frame hitches; bulk load saturates the drive from one thread.
3. **The economy** — chunk pool, ranged reads, decode workers with LZ4/zstd,
   pipeline overlap. *Bar:* effective read bandwidth exceeds raw drive bandwidth on
   compressed assets.
4. **The pack** — TOC + builder + load-in-place bake for one class end-to-end (models),
   mount-time verification, loose-file shadowing intact. *Bar:* the demo scene loads
   with zero runtime parsing and zero path strings.
5. **Rung climb** — parallel pread, then io_uring/IOCP, each gated by its bench.

Steps 2–5 each land with their `anotest_resbench` series and a hostile-FS smoke test
(kill the process at every protocol step; assert degradation, never corruption) — the
same validation culture the logger set: model it, sanitize it, benchmark it in
percentiles, fuzz the oracle.

## 9. Sources

- Jason Gregory, *Game Engine Architecture* 3rd ed., ch. 7 — async-I/O topology, chunk
  pools, sectioned files, load-in-place + fix-up tables, post-load init tables.
- Haas & Leis, *What Modern NVMe Storage Can Do, And How To Exploit It* (VLDB'23) —
  queue depth and parallelism as the saturation levers.
- Patterson et al., *Informed Prefetching and Caching* (SOSP'95) — disclosure-based
  prefetch; the priority-band model.
- Crotty et al., *Are You Sure You Want to Use MMAP in Your DBMS?* (CIDR'22) — the mmap
  rejection.
- Pillai et al., *All File Systems Are Not Created Equal* (OSDI'14) + PostgreSQL
  fsyncgate — the write-path discipline (inherited from the plan doc).
- Didona et al., *Understanding Modern Storage APIs* (SYSTOR'22) — libaio/io_uring/SPDK
  cost anatomy; where the syscall overhead actually lives.
- Glauber Costa, *Modern Storage is Plenty Fast* — parallel reads at application scale.
- safetensors PR #692 (Jan 2026) — io_uring + O_DIRECT cold-start fast path: 3.6–3.8×
  vs mmap cold, 4096 in flight, adaptive 64 KiB–16 MiB chunks; and the warm-cache
  caveat that shapes our default.
- DirectStorage 1.4 + GACL (GDC 2026) — the 50K IOPS / 10%-core bar; zstd as the
  ecosystem codec; BCn shuffle transforms; request shape `{file, offset, len, dest,
  codec}` (ours, minus the D3D12 coupling).
- kernel-internals.org, *Fixed Buffers and Files* — registered-buffer gains
  (+8–18% throughput, per-op CPU reductions) and their small-frequent-ops scope.
- TigerBeetle — one completion-shaped IO interface, N backends. sokol_fetch —
  channels × lanes decode/read overlap in fixed memory. Unreal IoStore/Zen — hashed-id
  containers, no runtime path strings. ripgrep — buffered reads over mmap for many-file
  workloads.
- In-tree: `src/logging/logging_ring.h` + `logging_core.c` (the transport, the park/wake
  discipline, the validation culture), `docs/text/logger.md`, the render bridge's
  ownership-transfer and false-on-full conventions, `tests/templates/bench.h`.
