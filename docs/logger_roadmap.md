# Logger roadmap — staged alterations

Status (M1, 4P+4E, 512 KiB ring): peak ~10.4 M/s @4 producers, ~9.7 M/s @2, ~5.8 M/s @8 (oversubscribed),
single-thread enqueue ~131 ns. Up from ~6.7 M/s peak / ~205 ns at the start. Built this pass: the owned
consumer thread (Stage 1), the lap-counter reclaim that retired per-drain zeroing, the CAS full path that
waits for the consumer instead of draining inline, and a hand-rolled prefix that took snprintf off the
producer (the single biggest win). Every item below is still a hypothesis with a number attached; the only
authority is the measurement on this hardware. The systems cited (Disruptor, Kafka, Redis, InnoDB, Aeron)
are precedent for what to try, not values to copy. Ordering is by impact under concurrency.

## Already done — do not redo
- Cursors `head`/`tail` padded to `ANO_THREAD_LINE` (128). No false sharing on the cursors.
- Timestamp render moved to the drainer and memoized per whole second (`g_drainHMS`).
- Severity gate is one relaxed atomic load on enqueue.
- Ring fixed at 512 KiB, power of two. The sweep showed a 256–512 KiB plateau = cache residency,
  not disk geometry. Stop sweeping that axis; 256 KiB halves the footprint at equal throughput.
- `fsync` already decoupled from `write`: the drain writes to page cache, `fsync` only on cleanup
  and FATAL. The logger already defaults durability away from per-record sync (loosest loss window).
- Owned consumer thread (Stage 1, below): `drainer_main` drains continuously, parks on an empty pass.
- Lap counter (`log_word_t.cycle` = pos>>shift) replaced per-drain zeroing. The drainer reclaims by
  advancing head only; a stale prior-lap tag is rejected by the cycle check, not by a memset. Halved
  the drainer's write traffic (it did not move the needle — the drain was never the bottleneck).
- Full path waits for the consumer instead of draining inline (was flush-then-buffer / path B). The
  reserve stays a CAS so the full-check precedes the claim; a head-stall escape writes a wedged line
  through `emit_one` (safe: nothing is claimed until the CAS). Never silent-drops.
- Hand-rolled producer prefix: only the user `fmt` hits `vsnprintf`; the fixed `<LEVEL> file:line:`
  prefix is composed by hand (no `snprintf`). The dominant single-thread win, ~205 ns → ~131 ns.

## Load-bearing constraints — do not "optimize" away
- Threads carry only the single message they are enqueuing. No thread-local / per-core buffers that
  need syncing on death. This is the genuinely load-bearing decision: it buys crash-survivability of
  the log (a worker can die mid-frame and lose only its in-flight line), and it rules out the usual
  high-end per-core-buffer-plus-collector design on purpose.
- Eager formatting: the ring holds finished text. A dead producer's enqueued line is intact, and
  there is no argument codec to maintain. The deferred-format path (capture args, format in the
  drainer) is `anoptic_profiler.h`'s model, not the logger's.

---

## Stage 1 — Dedicated consumer thread  (DONE — but it was not the ceiling)

Built: `ano_log_init` spawns `drainer_main`, which drains continuously and parks (`ano_sleep(100 us)`)
only on an empty pass; `ano_log_cleanup` clears `g_drainRun` and joins before teardown. `ano_log_flush`
still drains inline for a synchronous guarantee. `g_drainMtx` now serializes the owned drainer against
inline flushes. Outcome: on its own it barely moved throughput (the benchmark already ran a flusher
thread, so the drain was already overlapped; and the real ceiling turned out to be the producer's
`snprintf`, not the commit lock). Necessary groundwork — the full path can now wait on a live consumer —
but not the win. The original "write mutex is the ceiling" hypothesis was wrong; measured, not assumed.

Original reasoning kept for the record:
The library owns no drain thread today (the caller drives `ano_log_flush`; the benchmark
supplies its own flusher). A logger-owned consumer creates real producer/drain overlap, moves the
file write off whatever thread is enqueuing, and collapses the commit-side locking to single-consumer.
The write mutex is almost certainly the current ceiling: formatting parallelizes across cores, the
lock does not, and they stack (every producer formats, then queues behind one lock to commit).

Do.
- `ano_log_init` spawns one drain thread that loops: drain the ring, then park on a condvar / flush
  interval until there is work or the interval elapses.
- With a single owned consumer, `g_drainMtx` and the steady-state `g_outFileMtx` writes degenerate to
  "the consumer is the only writer" → drop the lock on the commit path. The immediate/FATAL path and
  `ano_log_output_dir` swap still need a guard.
- Keep `ano_log_flush()` as a manual nudge (signal the consumer, or drain inline) for callers that
  want tick-driven flushing.

Design note. This reverses §6 ("the logger owns no thread; the caller drives the drain"). Decide:
owned-thread by default with an opt-out, or owned-thread always. Either way `ano_log_flush` stays.

Re-measure after. Re-run BOTH the ring-size sweep and the producer-count sweep. Expect the 2 MiB
column to collapse back toward the cache-resident sizes once a single drainer reads the ring
sequentially instead of N threads touching it. The `@8` drop likely changes character (it is write
mutex / oversubscription, not false sharing — the cursors are already padded).

## Stage 2 — Measurement hygiene  (PARTLY CONFIRMED; pinning still open)
The oversubscription warning proved exactly right. This is an Apple M1: 4 performance + 4 efficiency
cores, 8 logical, no SMT. At @8 the bench runs 8 producers + the owned consumer + its own flusher =
10 threads on 8 cores, 4 of them slow E-cores — so the @8 "collapse" (now ~5.8 M/s) is the scheduler,
not the logger. Peak is @4 (~10.4 M/s, four P-cores) and @2 (~9.7 M/s). Treat @4 as the headline; @8 is
oversubscribed and not worth chasing without pinning.
Still open:
- Split metrics: enqueue latency p50/p99 reported separately from drain throughput.
- Ring-full counter: the full path now returns 1 when it waited; the bench could tally it to show how
  often producers actually hit backpressure vs the consumer keeping pace.
- Pin producers and the consumer to physical cores (and keep producer count <= 4 on this box) to get a
  clean scaling curve instead of an E-core/oversubscription artifact.

## Stage 3 — Formatting placement  (RESOLVED: stayed eager, but cut the snprintf fat)
- The real discovery this pass: the producer cost was NOT mainly the user-message `vsnprintf`. It was
  the SECOND printf — the fixed `"%-5s %s:%d:  "` prefix went through `snprintf`, and parsing that
  format string cost more than the actual work. Hand-rolling the prefix (level memcpy + file memcpy +
  `put_u32` for the line) took single-thread enqueue ~205 ns → ~131 ns and lifted @2/@4 over 8–10 M/s.
- Eager formatting KEPT, as recommended. The user message still goes through `vsnprintf` on the
  producer; the ring still holds finished text; crash-survivability intact. Moving the message format
  to the drainer would still mean the profiler's argument codec — not done, not wanted here.
- Remaining producer-side micro-fat if ever needed: `strlen(file)`/`strlen(level)` each call (file and
  level are literals; a length could be threaded through the macro). Marginal; not worth the API churn.

## Stage 4 — Syscall path  (mechanical; measure against current)
The drain already amortizes: memcpy each record into `g_batch`, one `write()` per pass. Already
syscall-cheap, so these are micro-opts over an already-batched path — chase only if Stage 1 leaves
the drain syscall-bound.
- Seam wrap via a 2-element `iovec` at the two ring spans, instead of gathering into `g_scratch`
  (saves the wrap copy; wraps are rare).
- `writev`/`pwritev` directly from the ring (header, text, newline) to skip the staging memcpy,
  capped at `IOV_MAX` (1024) records per call. Measure vs the current single-buffer write — the
  memcpy is sequential and cache-friendly, so this may not win.

## Stage 5 — Durability cadence  (the master variable; when you want a loss-window knob)
Currently write→page-cache on drain, `fsync` only on cleanup/FATAL = largest loss window.
- Add an explicit `fsync` cadence knob: per-second (Redis `everysec`) or per-N-bytes (Postgres
  `wal_writer_flush_after` = 1 MB). Plot throughput against the loss window each choice implies. This
  is a loss-window choice, not a perf tweak: how many ms of records may a crash erase.
- Platform: Linux `pwritev2` + `RWF_DSYNC` for a single durable flush without a global `O_DSYNC`;
  macOS uses `F_FULLFSYNC`. Both go behind the `ano_fs_*` abstraction.

## Stage 6 — Core claim: CAS vs FAA  (STILL DEFERRED — and now there's a reason to keep CAS)
- The producer still reserves with a CAS loop on `tail`. Kept deliberately, not just inertia: the
  full-check must precede the claim so the wedge-escape (write a stalled line through `emit_one`) leaks
  no slot. FAA claims first, so it cannot bail — a producer that FAA'd a slot is committed to waiting
  for the consumer, and a peer that died mid-publish would block every waiter forever. CAS preserves
  the degrade-to-immediate resilience the design wants.
- After Stage 3, CAS is not the ceiling on this box: @4 hits ~10.4 M/s with the CAS reserve. The CAS
  retry storm would only bite past 4 P-cores, which this M1 does not have. Re-measure FAA only on a
  many-P-core machine (and only if backpressure, not formatting, is shown to dominate there). If FAA is
  ever adopted, the wedge-escape has to be redesigned (e.g. publish a skip-marker for an abandoned slot)
  — do not drop the resilience to win a few percent.

## Stretch — only if buffered throughput stalls
- `fstat` `st_blksize` → size the drain buffer to it instead of assuming 4096.
- `O_DIRECT`: `posix_memalign` + align offset/length to `statvfs` output; benchmark against
  buffered + periodic-`fsync`. Expect buffered to win (the page cache coalesces); verify, don't assume.
