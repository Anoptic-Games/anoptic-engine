# Logger — Design (variable-cache-line MPSC ring)

Anoptic Engine uses a MPSC buffered logging system with single-ownership per logger instance. The canonical format would be to have the logger be owned by main.

The ring is variable-length and cache-line-granular (DPDK `rte_ring` family). Each entry is a contiguous run of cache lines. A 16-byte head-line marker (one atomic commit word doubling as the free/uncommitted sentinel, plus a timestamp) is followed by the finished log line as plain text. The producer formats the line on its own thread before it ever touches the ring. The consumer only copies bytes. The earlier fixed-slot Vyukov draft was retired in favor of this layout.

Scope: the producer enqueue path, the single-consumer flusher, the output file, and the public interface in `include/anoptic_logger.h`, on Linux, macOS, and Windows.

---

## 1. What the logger must do

`ano_log_*` is called from ECS worker threads during the parallel tick. A logging call must return without waiting on another thread on the common path. A producer that waits on a lock or on the consumer holds up the tick barrier and serializes the frame. So the producer path is lock-free and bounded in steps. The one exception is deliberate backpressure: a producer that finds the ring full waits for the owned consumer to free room rather than drop the record (§10), self-throttling to the drain rate. Sizing the ring so it does not saturate keeps that path cold. On the common path it formats the full line into a thread-local stack buffer first (producer-local, touching no shared state), then copies the finished bytes into the ring and publishes with one release store. Nothing downstream ever interprets those bytes again.

Four further requirements come from the `notes.md` audit: batched output (one syscall per drain pass), a monotonic timestamp per record, a lossless full-ring path (wait-for-room, §10), and a working output file.

Formatting happens on the producer, so the ring carries finished text. The other option is to capture the raw arguments and format later on the consumer, the way NanoLog and Quill do. That model fits a high-throughput binary profiler, where the producer fires often enough that the `vsnprintf` cost has to leave the hot path. The future `anoptic_profiler.h` uses it. The logger's producers are sparse, so it formats on the producer and stores the finished line. The mechanics are in §9.

---

## 2. Architecture

One shared bounded ring carries records from many producers to one consumer, the logger's owned drain thread. A producer formats its line, reserves a contiguous run of cache lines, copies the line into it, and publishes the run with one release store. The drain pass walks the ring in claim order, prepends each record's wall-clock time, and writes the batch with one syscall. It does no parsing. The message text is already final. `ano_log_flush` runs one extra synchronous drain pass on the caller's thread, for callers that need records on disk at once.

The ring follows the DPDK `rte_ring` family: a `head`/`tail` pair of monotonic counters over a power-of-two array of cache lines. A producer reserves by bumping `tail`. The flusher frees by advancing `head`. Free space is `N − (tail − head)`, and the same comparison guards reuse. A reservation that would pass `head` finds the ring full. Each entry occupies `ceil((HDR + len) / cacheline)` lines and carries one commit word (`tag`) on its first line. A long record is just a longer run under one `tag`, freed with the same single `head` store. Because claim order is one global sequence, the drained stream is already in order and the per-record timestamp is for display only (§11).

The nearest prior art is xtr: a bounded ring, batched I/O. Like xtr, this logger drains on a logger-owned background thread; `ano_log_flush` adds a synchronous inline pass for callers that want records on disk now. It diverges on two axes: where formatting happens and how many rings. xtr (with NanoLog, Quill) formats on the consumer to keep the producer's hot path in the single-digit-ns range, which matters when the producer rate is a firehose. This logger's producers are sparse, so it formats on the producer and keeps the consumer a pure byte copy: the ring holds text. xtr runs one ring per output (SPSC). This design uses one shared ring (MPSC) so claim order is a single total order. See `docs/references/lockfree.md` Part II.

---

## 3. The ring

```c
// The reservation grain is the cache line. ANO_CACHE_LINE (anoptic_memory.h) is the true
// coherency line — 64 on x86-64, 128 on Apple Silicon — and sets packing density. The hot
// cursors below pad to ANO_THREAD_LINE (128), the false-sharing isolation distance.
#define ANO_CL            ANO_CACHE_LINE
#define ANO_LOG_HDR       16                         // head-line header: the 16-byte marker, below
#define ANO_LOG_TIME_RESV 16                         // flusher's "HH:MM:SS " prefix budget
#define ANO_LOG_MSG_MAX   (4096 - ANO_LOG_TIME_RESV) // stored-line cap; stored + prefix = 4096, so an
                                                     //   entry is at most ceil((16 + 4080) / 64) = 64 lines

enum { ANO_LOG_COMMITTED = 1 << 0 };                 // set on publish; keeps a committed tag nonzero

// An entry's head line begins with this 16-byte marker. The remaining (ANO_CL - 16) bytes of the
// head line, then every following line, are the finished text. Only `tag` is atomic: it is the
// single publish gate for the whole record — `timestamp` and the text are plain memory whose
// visibility rides tag's release/acquire (§12).
typedef struct {
    _Atomic uint64_t tag;        // commit word. live iff COMMITTED set AND its `cycle` is the current lap;
                                 //   published LAST (release), read FIRST (acquire), never zeroed (§6).
    uint64_t         timestamp;  // ano_timestamp_raw() ns, display only; sourced through the
                                 //   platform clock (§11)
} log_marker_t;
_Static_assert(sizeof(log_marker_t) == ANO_LOG_HDR, "marker is 16 bytes");
_Static_assert(ANO_LOG_HDR <= ANO_CACHE_LINE, "marker fits in one cache line");

// The view punned over `tag`. In C, reading a union member you did not write is defined
// (C99 6.5.2.3, carried through C23). It lives only on thread-local values copied in and out of
// `tag` with a single atomic op; the shared word is always accessed atomically (§12).
typedef union {
    struct {
        uint16_t len;    // formatted-line bytes (<= ANO_LOG_MSG_MAX); span = ceil((16 + len) / ANO_CL)
        uint8_t  level;   // log_types_t, so the flusher routes by severity without reading the text
        uint8_t  flags;   // ANO_LOG_COMMITTED, the commit marker
        uint32_t cycle;   // lap number (pos >> shift); the drainer reclaims by cycle check, not zeroing
    };
    uint64_t w;
} log_word_t;
_Static_assert(sizeof(log_word_t) == 8, "commit word is 8 bytes");

typedef struct {
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t tail;  // producer reserve cursor, in cache lines (the hot line)
    _Alignas(ANO_THREAD_LINE) _Atomic uint64_t head;  // consumer drain cursor, in cache lines (release)
    uint64_t          mask;                           // N-1; N = capacity in cache lines, power of two
    uint32_t          shift;                          // log2(N); cycle = pos >> shift (the lap of a position)
    char             *buf;                            // N*ANO_CL bytes, cache-line aligned
} log_ring_t;
```

The record carries no call-site references and no format string. `level`, `file`, `line`, and the formatted message are all baked into the text by the producer (§5, §9). The marker keeps only `level` (a copy, for severity routing) and the raw timestamp. A slot is committed iff `tag` has `ANO_LOG_COMMITTED` set and its `cycle` equals the current lap; `len` is the low 16 bits.

Invariants:

- Addressing: counter `c` maps to physical line `c & mask`. `tail`/`head` are monotonic 64-bit counters. `tail − head` is the live (reserved-or-undrained) line count, bounded by `tail − head ≤ N`.
- Commit / visibility: `tag` is the single publish gate. The producer fills `timestamp` and the text with plain stores, then publishes `tag = {len,level,flags,cycle}` with one release store, where `cycle = pos >> shift` stamps the lap. The consumer acquire-loads `tag` and, seeing `ANO_LOG_COMMITTED` set and `cycle` equal to the current lap, reads the now-visible `timestamp` and text (§12). A slot is "live" only on that pair of conditions: the buffer starts zeroed (lap-0 unwritten lines read `flags == 0`, not committed), the drainer never zeroes on reclaim (§6), and a reused slot keeps the previous lap's tag until republished, whose stale `cycle` the check rejects. A reserved-but-uncommitted head line reads not-committed-for-this-lap, so the drain stops at a gap. The lap check alone is not a sufficient stop. When the ring is exactly full (`tail − head == N`) with every entry committed, `[head, tail)` covers all `N` lines and every tag is live, so the consumer also bounds its walk by a `tail` snapshot (next invariant). The lap counter is 32-bit, but a stale slot is at most one lap behind the consumer, so the prior-lap `cycle` never collides with the current one: no ABA.
- Length integrity: `len` is carried inside `tag` and published by that one release store, so it commits in the same atomic event as the gate. The consumer reads `len` only from a tag that is committed-for-this-lap, so it always reads the exact value the producer committed, and `need` always lands on the next record's head line. The body is plain bytes that affect only one message's text. The structure the reader walks lives entirely in the atomic `tail`/`head`/`tag` words. The invariant this rests on: the length must be published by the same store as the commit flag. Widening the body past what the publish word can carry (moving `len` out of `tag`) reintroduces torn lengths and would require a separate structural backstop. Keep `len` in the word.
- Full-detection: the producer reads `head` (acquire) and refuses a reservation that makes `(pos + need) − head > N`.
- Drain bound: the consumer snapshots `tail` once per pass and drains `[head, tail)`, stopping early at the first slot that is not committed-for-this-lap. The bound is load-bearing only at exact fullness, where every line is live. Below capacity the line at `tail` carries a stale prior-lap tag (or a fresh zero), which the lap check rejects, so the walk stops there on its own. Without the bound, an exactly-full all-committed ring has no rejected line to stop at, so the walk re-reads the buffer forever. The flusher wedges and never releases `head`. This mirrors the SPSC bridge ring, which bounds its consumer by the producer `tail` and carries no content sentinel (`src/render_bridge/render_bridge.h`). The snapshot can be `relaxed`: it is a count bound only, and each record's data visibility still rides its own `tag` acquire.
- Reuse-safety: the same acquire `head` read orders the producer's writes after the consumer's reads on the previous lap. A producer always reserves a run disjoint from `[head, tail)`, and only its own release of `tag` (carrying the new lap's `cycle`) makes the head line live; the prior occupant's stale tag is inert under the lap check until then.

Entry span is `ceil((ANO_LOG_HDR + len) / ANO_CL)`: `1` for a short line (head line = 16-byte marker + first 48 text bytes), more for a long one, no per-line header on continuation lines. `tag` is the only synchronized word. Everything else rides its release/acquire as plain memory (§12).

---

## 4. Diagrams

### 4.1 Ring at rest — head/tail on a cache-line grid

```
 monotonic cache-line axis  ->   (physical line = counter & (N-1),  N = capacity in lines)

 |<------ drained, reusable ------>|<--- committed, awaiting drain --->|<-writing->|<-- free -->
 ================================== H ================================= R ========== T ~~~~~~~~~~~
                                    ^                                   ^            ^
                                    head                          uncommitted     tail
                                 (consumer                        entry = "gap"  (producer
                                  drain cursor)                                   reserve cursor)

 invariant : tail - head <= N           free lines : N - (tail - head)
 each entry occupies  ceil((16 + len) / 64)  cache lines, never split into fixed slots
 reclaimed lines keep their old tag (not zeroed); a slot is live iff COMMITTED and its lap is current
 the consumer stops at the first not-live slot OR at h == tail
   (the tail bound is the only stop when the ring is exactly full and every slot is live)
```

### 4.2 One entry — the commit word lives on the head line

```
 entry = ceil((16 + len) / 64) cache lines

 head line (byte offsets):
  0                 8                 16                                            64
  +-----------------+-----------------+---------------------------------------------+
  | tag (commit)    | timestamp       | text[0 .. 47]                               |
  +-----------------+-----------------+---------------------------------------------+
   ^                                   ^ payload begins (ANO_CL - 16 = 48 inline text bytes)
   |
   tag = { uint16 len; uint8 level; uint8 flags; uint32 cycle }   (COMMITTED set on publish)
   published LAST (release), read FIRST (acquire);  live iff COMMITTED and cycle == current lap

 continuation line(s)  (only if text > 48 bytes):
  +--------------------------------------------------------------+
  | text[48..111], text[112..175], ...  finished line, no header |
  +--------------------------------------------------------------+
```

### 4.3 Producer — format → reserve → write → publish (lock-free)

```
 format full line -> text, len       (vsnprintf into a thread-local stack buffer; §9)
      |                               (producer-local; no shared state touched yet)
      v
 need = ceil((16 + len) / 64)         lines this entry needs
      |
      v
 pos = load(tail, relaxed) <-------------------------------+
      |                                                    | CAS lost / stale, OR full backoff:
      v                                                    | reload pos
 hd  = load(head, ACQUIRE)   (full-check + reuse-safety)   |
      |                                                    |
      +--- (pos + need) - hd > N ? --yes--> FULL: busywait(128), let the consumer free room
      |                                          |          (self-throttle to the drain rate) --+
      no                                         +-- head frozen FULL_STALL_LIMIT rechecks? --+ |
      v                                                yes: consumer wedged -> immediate (§10) | |
 CAS(tail : pos -> pos + need, relaxed) ---- fail -------------------------------------------+-+
      |
      success   -- now owns lines [pos, pos+need) --+
      v                                             |
 plain stores: timestamp, then text  --------------+   (into [pos, pos+need))
      |
      v
 store(tag = {len,level,flags}, RELEASE)  <-- publish: one gate, happens-before for the consumer
      |
      v
    done                              (never waited on another thread)
```

### 4.4 Consumer — owned drain thread, owns head

```
 h = head ;  h0 = head
 tcap = load( tail, RELAXED )                  -- reserved frontier; bounds this pass (a count only)
 +-> h == tcap ? --yes--> STOP                 (caught up to tail; the ONLY stop when the ring is
 |        no                                     exactly full, where every slot in [head,tail) is live)
 |        v
 |   t = load( tag@(h & (N-1)), ACQUIRE )
 |        |
 |        +-- COMMITTED and t.cycle == h>>shift ? --no--> STOP   (gap: free / unpublished / stale lap;
 |        yes                                                      resume here next pass)
 |        v
 |   len  = {t}.len                            (visible via tag's release)
 |   need = ceil((16 + len) / 64)
 |        v
 |   emit({t}.level, ts, text over [h, h+need)) -> batch   (wall-time prefix + verbatim bytes)
 |        v
 |   h += need
 +--------+
   after loop:
        store(head = h, RELEASE)    -- frees [h0, h); no zeroing, the lap check rejects stale tags
        one ano_fs_write(batch)     -- single syscall
        return (h - h0)             -- lines reclaimed; the drain thread parks when this is 0
```

### 4.5 The gap — preempted vs dead producer

```
        head                         tail
         |                            |
         v                            v
 [..D..][  E1  ][ E2 ][ P: reserved ][..free..]
                       ^^^^^^^^^^^^^^
                       tag not committed-for-this-lap (stale/zero; producer writes tag last)

 consumer drains E1, E2, reaches P, reads tag -> not live for this lap -> STOP, waits.

 P preempted  : wakes <= 1 quantum, does its release-store, next pass sweeps past.
 P *dies* here: head never passes P -> the drain wedges. Producers fill the ring and wait for
                room; when head stays frozen FULL_STALL_LIMIT rechecks they take the immediate
                path (§10), so a wedge degrades to direct output rather than blocking forever.
                P's window is a bounded copy + one store, so it can be preempted but not blocked.
                One ring = claim order is total order, so the consumer waits on the gap rather
                than skipping it (the lap counter is for reclaim freshness, not gap-skipping;
                lockfree.md II §6).
```

### 4.6 The seam — a record that crosses the buffer end

```
 a record reserved at line N-2 with need=4 -> physical lines N-2, N-1, 0, 1:

   line N-2 (head): [ tag | ts | text.. ]      <- marker never splits (one line, in-bounds)
   line N-1       : [ text ................. ]
   ---- buffer end / wrap ----
   line 0         : [ text ................. ]   <- remainder continues at the start
   line 1         : [ text .....            ]

 reservation is one CAS on tail (logically contiguous). the text copy is <= 2 memcpys:
   [ body .. buffer end ]  then  [ buffer start .. remainder ]
 one memcpy when the record fits before the end (the common case). that's the whole "wrap".
```

---

## 5. The enqueue path

```c
int ano_log_enqueue(log_types_t level, const char *file, int line, const char *fmt, ...) {
    if (level < atomic_load_explicit(&g_log.min_level, memory_order_relaxed))
        return 0;                                   // runtime level gate, one relaxed load

    char blob[ANO_LOG_MSG_MAX];                     // the FINAL line, formatted on this thread,
    va_list ap; va_start(ap, fmt);                  //   on the stack, before any shared access
    int n = format_line(blob, sizeof blob, level, file, line, fmt, ap); // §9; vsnprintf
    va_end(ap);
    uint16_t len  = (uint16_t)n;
    uint64_t ts   = ano_timestamp_raw();
    uint64_t need = (ANO_LOG_HDR + len + ANO_CL - 1) / ANO_CL;   // cache lines, >= 1

    uint64_t cap = g_ring.mask + 1;
    uint64_t pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);
    uint64_t lastHead = 0; uint32_t stall = 0;
    bool waited = false;
    for (;;) {
        uint64_t hd = atomic_load_explicit(&g_ring.head, memory_order_acquire); // full + reuse-safety
        if ((pos + need) - hd > cap) {              // would alias an undrained line → full
            waited = true;                          // don't drain inline (that serializes producers);
            if (hd != lastHead) { lastHead = hd; stall = 0; }   //   back off and let the consumer free room
            else if (++stall > FULL_STALL_LIMIT)    // head frozen → a gap is wedging it (§7)
                return on_full(level, ts, blob, len);   // last resort: write this line straight through
            ano_busywait(128);                      // brief, off the consumer's cache line
            pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);
            continue;                               // re-snapshot and retry once room frees
        }
        if (atomic_compare_exchange_weak_explicit(&g_ring.tail, &pos, pos + need,
                memory_order_relaxed, memory_order_relaxed))
            break;                                  // own lines [pos, pos+need)
        // CAS failed: pos was reloaded with the current tail; retry
    }

    log_marker_t *m = (log_marker_t *)(g_ring.buf + (pos & g_ring.mask) * ANO_CL);
    m->timestamp = ts;                              // plain store, ordered by the release below
    char  *body  = (char *)m + ANO_LOG_HDR;         // text goes here; the marker never wraps (one line)
    size_t toend = (size_t)(g_ring.buf + cap * ANO_CL - body); // bytes to the buffer end
    if (len <= toend) memcpy(body, blob, len);      // common: fits before the seam — one copy
    else { memcpy(body, blob, toend);               // straddles the seam — split into two (§8)
           memcpy(g_ring.buf, blob + toend, len - toend); }
    log_word_t v = { .len = len, .level = level, .flags = ANO_LOG_COMMITTED,
                     .cycle = pos >> g_ring.shift };            // stamp the lap so reclaim needs no zeroing
    atomic_store_explicit(&m->tag, v.w, memory_order_release);    // publish — one gate, whole record
    return waited ? 1 : 0;                          // 1: ring was full, waited for the consumer to free room
}
```

Notes.

- Format off the shared path. `format_line` runs entirely on producer-local stack memory, before the `tail` load. Its `vsnprintf` cost (~90ns) never touches a contended line and never grows the critical section, which is exactly the copy + one release store below.
- Lock-free, bounded steps. The only contended line is `tail`. Its CAS retries only under concurrent reservation. The full-check `head` read is an acquire load of a line written once per drain pass.
- One publish. The producer writes the run as one (or seam-split two) `memcpy` and a single release store of `tag` publishes the whole entry. `len` lives in `tag`, so it commits with the gate.
- No capture codec, no fallback. The line is finished text, so there is no argument blob to misread and no unsupported-conversion path. `vsnprintf` handles every conversion natively. The record is self-contained bytes.
- Full ring waits for the consumer. On a full ring the producer does not drain inline (that serialized every full producer on the drain gate and collapsed throughput at high producer counts). It backs off (`ano_busywait(128)`) and re-checks `head` until the owned consumer frees room, self-throttling to the drain rate. While `head` advances this is plain backpressure; the full-check precedes the `tail` CAS, so no slot is reserved while waiting. `on_full` is only the last-resort path for a gap that wedges the consumer (`head` frozen for `FULL_STALL_LIMIT` rechecks, §7); since nothing is reserved yet, writing that one line straight through leaks no slot.
- The earlier mutex logger is preserved as the trivially-correct reference and benchmark baseline (`src/logging/logging_old.c`, namespaced `mtxlog_*`), compiled only into the optional `anotest_logbench` (§14).

---

## 6. The consumer: the owned drain thread

The logger owns one drain thread, spawned at the end of `ano_log_init` and joined at the start of `ano_log_cleanup`. It loops: run one drain pass, and park briefly (`ano_sleep(DRAIN_IDLE_US)`, 100 µs) only after a pass that reclaimed nothing, so it stays hot under load and costs nothing idle. `ano_log_flush()` runs one extra synchronous pass on the caller's thread for callers that need records on disk now, and `ano_log_cleanup` runs one final pass at teardown after the thread is joined.

```c
static void *drainer_main(void *arg) {              // the owned consumer
    while (atomic_load_explicit(&g_drainRun, memory_order_relaxed))
        if (drain() == 0) ano_sleep(DRAIN_IDLE_US); // empty pass: park; else stay hot
    return NULL;
}
void ano_log_flush(void) { if (initialized) drain(); }  // one extra synchronous pass

uint64_t drain(void) {                              // serialized: one active drainer
    ano_mutex_lock(&g_drainMtx);
    uint64_t n = drain_and_emit();
    ano_mutex_unlock(&g_drainMtx);
    return n;
}
```

`drain_and_emit` walks the ring in claim order, stops at the first slot not committed-for-this-lap, and frees the drained range with one `head` store (no zeroing). It returns the lines reclaimed, so the drain thread knows whether to park:

```c
uint64_t h0  = g_ring.head;                         // consumer-private until the release store
uint64_t h   = h0;
uint64_t cap = atomic_load_explicit(&g_ring.tail, memory_order_relaxed); // reserved frontier;
                                                    //   relaxed: a count bound, data rides each tag
for (;;) {
    if (h == cap)
        break;                                       // caught up to tail → stop; the ONLY stop when
                                                     //   the ring is exactly full (every slot live, §3)
    log_marker_t *m = (log_marker_t *)(g_ring.buf + (h & g_ring.mask) * ANO_CL);
    log_word_t v = { .w = atomic_load_explicit(&m->tag, memory_order_acquire) };
    if (!(v.flags & ANO_LOG_COMMITTED) || v.cycle != (uint32_t)(h >> g_ring.shift))
        break;                                       // gap (unpublished / stale lap) → stop, resume next pass
    uint64_t need = (ANO_LOG_HDR + v.len + ANO_CL - 1) / ANO_CL;
    const char *line = gather_if_seam((char *)m + ANO_LOG_HDR, v.len, scratch, &g_ring); // ≤2 memcpys (§8)
    emit_record(v.level, m->timestamp, line, v.len);    // wall-time prefix + verbatim text bytes
    h += need;
}
// No zeroing: a reused slot keeps last lap's tag until republished, and the lap check above rejects it.
atomic_store_explicit(&g_ring.head, h, memory_order_release); // frees [h0, h) for reuse
// then: one ano_fs_write of the whole batch; return (h - h0).
```

- Order: claim order is a single global serialization, so the batch is already ordered. The pass drains `[head, tail)`. The `tail` snapshot is the upper bound, and the only thing that ends the pass when the ring is exactly full and every slot is live (§3). A producer that reserved but has not published stops the walk early at its not-committed-for-this-lap slot. The drainer resumes there next pass (§7).
- Reclaim: the `head` release alone frees the drained range `[h0, h)`. The consumer does not zero the lines. A reused slot keeps the previous lap's tag until a producer republishes it with the new lap's `cycle`, and the lap check rejects the stale tag in the meantime, so "free" needs no write. This halves the drainer's memory traffic versus zeroing every line. The earlier draft zeroed each drained line (lowering to `dc zva` on ARM) to keep "all-zero = free"; the lap counter retires that whole pass.
- Batch: `emit_record` does no parsing. The text is the final message. The flusher only prepends the wall-clock time (and routes by `level`), appending `"<wall-time> <text>"` into a reusable consumer-owned buffer. The `"HH:MM:SS"` prefix is memoized for the whole second it was rendered (records in a drain burst share a second), so the civil-time conversion runs once per second, not per record, and the digits are written by hand, not through `snprintf`. A seam-straddling record is gathered into contiguous scratch first (§8). This is the only formatting the consumer does, and it is uniform per record. No `fmt` walk, no dictionary lookup.
- One write: `ano_fs_write(g_outFile, batch, len)`, one syscall. No fsync here. Echo to console if configured (`>WARN` to stderr, else stdout), routed by `level`.
- High-watermark: the owned drain thread keeps the ring near-empty under normal load. A full event (§10) makes producers wait for the consumer to free room, throttling them to the drain rate. Full is "wait," not "drop."

Single drainer at a time, enforced by a blocking drain mutex (`g_drainMtx`): it serializes the owned drain thread against any inline `ano_log_flush` so the two never overlap, and `ano_log_flush` stays safe to call from any thread. A mutex, not a spin, so neither drainer burns a core waiting on the other. (An earlier spin here regressed throughput at high producer counts.)

---

## 7. The gap problem

When a producer has reserved its lines but not yet stored `tag`, the consumer hits a slot that is not committed-for-this-lap mid-ring, stops there, and resumes next pass. That stall is bounded. Between reserve and publish the producer does only a bounded text copy (≤ `ANO_LOG_MSG_MAX`) and one store (the `vsnprintf` already ran before the reservation, §5), so it can be preempted but not blocked, and it finishes within a scheduler quantum. A producer that dies in that window wedges the drain: the consumer stays stopped at the gap, the ring fills, and the producers waiting for room watch `head` stay frozen. After `FULL_STALL_LIMIT` rechecks with no advance they conclude the consumer is wedged and write their lines straight through the immediate path (§10), so a dead-producer gap degrades to direct output rather than blocking every producer forever.

One ring means claim order is the total order, so an unpublished slot is a gap to wait on, not to skip. The logger therefore skips the cycle-number skip LCRQ/SCQ carry and the cross-lane timestamp grace period Quill needs (5 µs, §2). The lap counter (§3) is for reclaim freshness, a separate concern from gap-skipping; the consumer still waits on the gap in claim order. Lineage in `docs/references/lockfree.md` Part II §6.

---

## 8. The seam

The buffer is circular, so a reserved run that starts near the end wraps to line 0. Reservation is unchanged: the `tail` CAS claims the logically contiguous range `[pos, pos+need)`, wrap or no wrap. Only the physical layout differs. A wrapped record sits in two pieces. The head line is always one in-bounds cache line, so `tag` and `timestamp` never cross. Only the text does.

Handling is a split copy: write and read the text in at most two `memcpy`s, the part to the buffer end and the remainder from the start (producer §5, consumer gathers symmetrically into scratch before `emit_record` §6). One copy in the common case. Records are bounded at `ANO_LOG_MSG_MAX` (≤ 64 lines), so a straddling record is small.

A double-mapped "magic" buffer (map the pages twice back-to-back so a wrapping write needs no split) is a throughput micro-optimization costing platform-specific VM mapping. Deferred to the shared event-bus ring (§14).

---

## 9. Formatting (eager, on the producer)

The producer formats the complete line; the consumer copies bytes.

- `format_line(out, cap, level, file, line, fmt, ap)` composes the whole record on the producer's stack. The fixed-shape `"<LEVEL> <file>:<line>:  "` prefix is hand-rolled, no `snprintf`: `memcpy` the severity name padded to 5, a space, `memcpy` the file (clamped to 256 bytes so a freak name can't crowd out the message), `':'`, the line number via a hand-rolled decimal (`put_u32`), then `":  "`. This mirrors the hand-rolled walltime (`put2`/`render_hms`, §6/§11). Only the user message goes through `vsnprintf(out + head, cap - head, fmt, ap)`. It returns the byte length actually written, clamped to `cap − 1` when `vsnprintf` reports a would-be length `≥ cap` so an over-long line never overruns the entry (the ring stores exactly those `len` raw bytes and needs no terminator). With `cap = ANO_LOG_MSG_MAX = 4096 − ANO_LOG_TIME_RESV = 4080`, `len ≤ 4079`, the emitted line (with the prepended walltime) stays within 4096, and the entry is `≤ ceil((16 + 4080) / 64) = 64` lines. The wall-clock time is the one field left out. It is cheap, uniform, and locale-bound, so the flusher prepends it from the raw timestamp at emit (§6, §11). `fmt` must be a string literal at the call site. Enforce with `__attribute__((format(printf, 4, 5)))` plus `-Wformat-nonliteral -Werror`, which gives full compile-time argument checking against the conversions. The attribute checks callers, but `format_line` then forwards its non-literal `fmt` parameter into `vsnprintf`, which trips `-Wformat-nonliteral` inside `logging_core.c` itself. The `__attribute__((format(printf, 6, 0)))` on `format_line` marks it a printf forwarder so that one call passes the check while call-site enforcement stays on everywhere else.
- Every conversion `printf` supports is handled by `vsnprintf` directly. A `%s` is formatted into the line at the call, so the record holds only bytes. Once a record is in the ring, its text is final.
- The cost is producer-local and lands before the `tail` reservation (§5), so it never enlarges the critical section or touches a contended line. Hand-rolling the prefix (no prefix `snprintf`, only the message through `vsnprintf`) roughly halved the single-thread enqueue cost (~205ns → ~131ns measured on M1). On sparse logger traffic that cost disappears into the gaps between calls. The case where it would matter, a producer formatting often enough to saturate a thread, belongs to `anoptic_profiler.h`, which defers formatting behind a binary schema.

The deferred alternative captures the raw arguments and formats them later, on the consumer or out of process. Reconstructing the line then requires a call-site dictionary and a two-pass argument codec. That machinery belongs to `anoptic_profiler.h`.

---

## 10. The immediate path and the full ring

`ano_log_immediate` (FATAL, `_now`) writes on the calling thread instead of through the flusher. The full-ring path waits for the owned consumer to free room (§below); only its gap-wedge fallback writes a single line straight through. Both reuse the same eager `format_line` (§9).

- Format synchronously. An immediate write has no flusher pass. `ano_log_immediate` runs `format_line` from its own live `va_list` and prepends the wall-clock time directly, then writes to console (`>WARN` to stderr, else stdout), then the output file if open, then fsync. A FATAL means the flusher will not run again. The full-ring gap fallback already holds the finished line from the producer's `format_line`, so it just writes those bytes (no fsync).
- Never touch the ring (immediate). `ano_log_immediate` must not reset or consume buffered records, preserving single-consumer.
- Init error path: before `g_log` is live, write to stderr only, guarded by an "initialized" flag. Never lock or log through half-constructed state.
- Full ring. On `(pos + need) − head > N` the producer does not drain inline. It backs off (`ano_busywait(128)`) and re-reads `head` until the owned consumer frees room, self-throttling to the drain rate. Draining inline instead serialized every full producer on the drain gate and collapsed throughput at high producer counts; waiting keeps producers off the gate and lets the single consumer run unstarved. The full-check runs before the `tail` CAS, so nothing is reserved while waiting — and that is what makes the gap escape safe. The one case waiting cannot clear is a gap (a producer that reserved but has not published, or died mid-window, §7): if `head` stays frozen for `FULL_STALL_LIMIT` rechecks, the producer concludes the consumer is wedged and writes its line straight through as a last resort, leaking no slot because none was reserved. There is no runtime policy knob: the logger is the integrity corner of the design axis (§14). The lossy `DROP_NEWEST` and the `BLOCK` spin were cut; a lossy variant belongs to `anoptic_profiler.h`. Overwrite-oldest would reclaim an undrained line, so it is excluded.

---

## 11. Output file, timestamps, lifecycle, interface

Output file. A minimal append-only handle in `anoptic_filesystem` (opaque `ano_file`; `ano_fs_open_append` / `_write` / `_sync` / `_close`). This API does not exist yet. `anoptic_filesystem.h` today exposes only `ano_fs_gamepath` / `_userpath` / `_chdir_gamepath` and the `filepath` struct, so the append-handle layer is net-new and a hard prerequisite: the drain pass cannot write until it lands (it gates the lifecycle/drain work, §13). POSIX `open(O_WRONLY|O_CREAT|O_APPEND,0644)` on Linux and macOS; Windows `CreateFileW(FILE_APPEND_DATA)` + `WriteFile`, UTF-8→UTF-16 at the boundary. One handle per process, one batched `write` per drain pass (loop on short writes), fsync only on cleanup / a FATAL write. `ano_log_output_dir(dir)` opens `dir/anoptic_<utc>.log`. With no output file, records still drain to console.

Timestamps. `ano_timestamp_raw()` (monotonic ns) stamped per record at capture, the platform clock behind the abstraction (`clock_gettime(CLOCK_MONOTONIC)` on Linux; `mach_absolute_time` / `CNTVCT_EL0` on Apple Silicon). Claim order gives file order, so the timestamp is display only, and the producer leaves it raw in the marker for the flusher to render. Convert with an init-time anchor captured once: `anchor_raw_ns = ano_timestamp_raw()` and `anchor_unix_ns = ano_timestamp_unix() * 1000000000ULL`. `ano_timestamp_unix()` returns whole **seconds** (`time(NULL)`), so the ×10⁹ is required. Then `wall = anchor_unix_ns + (ts − anchor_raw_ns)`: the second is fixed at the anchor, sub-second precision rides the monotonic delta. No per-record syscall, no drift.

Lifecycle. `ano_log_init`: "initialized" false (immediate path stderr-only); allocate the ring with `ano_aligned_malloc(ANO_LOG_RING_BYTES, ANO_LOG_RING_ALIGN)` and zero it; `tail = head = 0`; set `shift = log2(N)` (via `__builtin_ctzll`) for the lap counter; set defaults; capture the timestamp anchor; open the default output file if a directory was set; "initialized" true; then spawn the owned drain thread (`g_drainRun` true, `ano_thread_create(drainer_main)`) last, once the ring and output are live. The ring is sized in **bytes** (`ANO_LOG_RING_BYTES`, a power of two, default 512 KiB, `-D`-overridable), so the byte size is identical on every platform even though the line size is not (64 vs 128); the line count `N = ANO_LOG_RING_BYTES / ANO_CACHE_LINE` derives from it and stays a power of two. The allocation is aligned to the ring size up to a 2 MiB cap (`ANO_LOG_RING_ALIGN`), so it sits in one self-sized region — page/large-folio/Windows-cache-view friendly, and 2 MiB-aligned (hugepage-eligible) at the top end. (One allocation reused in place. No per-logger `mi_heap` is warranted. `ano_aligned_malloc` is the existing abstraction the current logger already uses.) Any earlier failure rolls back without logging through half-built state (including tearing the ring down if the thread spawn fails). `ano_log_cleanup`: clear `g_drainRun` and join the drain thread first, so nothing reads the ring during teardown; one final drain pass (no producers remain by contract); sync + close the output file; `ano_aligned_free` the ring.

Public interface (`include/anoptic_logger.h`):

```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL } log_types_t;

int  ano_log_init(void);        // 0 ok; until 0, only the immediate (stderr) path works
int  ano_log_cleanup(void);

// Buffered enqueue. Formats {level, file, line, fmt, ...} into one line on the calling thread,
// copies it into the ring, publishes with one release store. Never waits on a thread.
int  ano_log_enqueue(log_types_t level, const char *file, int line, const char *fmt, ...)
        __attribute__((format(printf, 4, 5)));      // checks args vs fmt; pair with -Wformat-nonliteral
                                // 0 enqueued; 1 ring was full, waited for the consumer to free room.
void ano_log_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
        __attribute__((format(printf, 4, 5)));
int  ano_log_output_dir(const char *directoryPath);
void ano_log_set_level(log_types_t min);
void ano_log_flush(void);       // one extra synchronous drain on the caller; the owned thread also drains

// Call-site macros: pass level + __FILE_NAME__ + __LINE__ + fmt straight through. The first three
// are compile-time constants; the line is formatted eagerly inside ano_log_enqueue. fmt MUST be a
// string literal.
#define ano_log_info(...)   ano_log_enqueue(LOG_INFO,  __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_warn(...)   ano_log_enqueue(LOG_WARN,  __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_error(...)  ano_log_enqueue(LOG_ERROR, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define ano_log_fatal(...)  ano_log_immediate(LOG_FATAL, __FILE_NAME__, __LINE__, __VA_ARGS__)
// ano_log_debug / _debug_now compile to nothing unless DEBUG_BUILD.
```

The level-name table (`log_strings[]`) lives in `logging_core.c`.

Files: `include/anoptic_logger.h` (public contract), `src/logging/logging_core.c` (enqueue, immediate, flusher, lifecycle, `format_line`), `src/logging/logging_ring.h` (private `log_marker_t`/`log_word_t`/`log_ring_t` + claim/drain inlines; migrates to `anoptic_collections.h` with the lock-free-collections work), and the output file. Only the output file is platform-specific. The ring is portable C23 `<stdatomic.h>`.

---

## 12. Progress and memory ordering

The producer is lock-free in steady state: it formats on its own stack, loads `tail`, CASes it forward (retrying only against another concurrent reservation), copies the text, and does one release store of `tag`. The steady-state path never waits on the consumer. The full-ring path does wait — backing off until the owned consumer frees room — but that wait is bounded by the drain rate, and if the consumer wedges it falls to bounded steps to a syscall (§10) rather than blocking forever.

For record payload, `tag` is the single synchronizing word (`head` separately carries the reuse happens-before, below). The producer writes `timestamp` and the text with plain stores, then release-stores `tag`. The consumer acquire-loads `tag` and, seeing it nonzero, reads the now-visible payload. The release/acquire pair is the publish: it pins the plain writes before the `tag` store and the plain reads after the `tag` load. Without it, `-O2` may reorder those accesses and a weakly-ordered core may see a committed `tag` ahead of its payload. The pair compiles to plain `mov` on x86 (TSO) and to one `stlr` / one `ldar` on ARM.

The consumer runs on its own owned thread, off the producers' hot path, so the only place it waits, at an unpublished slot (§7), costs nothing on that path. Its per-pass `tail` snapshot is a `relaxed` count bound, not a synchronization point: it caps the walk at exact fullness (§3), while record payload still rides each `tag` acquire. Reclaim is the single `head` release alone — no drain-zeroing — and a reused slot's stale tag is inert under the lap check until republished. The ring is one allocation reused in place, so there is no memory to reclaim and no ABA on the 64-bit `head`/`tail` counters; the 32-bit lap `cycle` is likewise ABA-free, since a stale slot is at most one lap behind the consumer.

The bounded loss case. A producer that reserves and then permanently dies before its `tag` release leaves a slot the consumer can never pass (it never becomes committed-for-this-lap). Records committed logically after it are stranded behind the gap until the ring fills and the producers waiting for room hit the stall escape and route new logs to the immediate path (§10). The loss is one gap wide (everything before it, lower claim order, drains normally) because the records live in one shared order. The trigger is a thread dying mid-window while the process keeps running. A fault that takes the process down routes FATAL synchronously through the immediate path, and a preempted producer (§7) recovers within a quantum.

---

## 13. Implementation order and test plan

Build order, each step independently testable:

1. Ring buffer + seam-aware text copy (§8). Test: a line whose text crosses the buffer end writes and reads back byte-identical; one that fits takes the single-copy path.
2. Ring + `format_line`. `logging_ring.h`: reserve/drain inlines; `format_line` in core. Single-thread: enqueue N, drain N, assert order and that each drained line is byte-identical to a direct `snprintf` of the same call; a multi-line record reassembles exactly; drive the ring full and assert the policy fires; fill to exactly full with every entry committed and assert one drain pass emits them all and terminates (the §3 `tail` bound, without which this step hangs).
3. Lifecycle + drain pass. `ano_log_init`/`cleanup` (cleanup drains once at teardown), `ano_log_flush` drains on the caller, timestamp anchor, batched `ano_fs_write`. Prerequisite: the append-handle output file (`ano_fs_open_append` / `_write` / `_sync` / `_close`, §11) must land first. It is not in `anoptic_filesystem.h` yet.
4. Round-trip. Enqueue → flush → read back: level, `file:line`, body survive; N enqueues emit N ordered lines.
5. Immediate/FATAL. No buffer wipe, sync write, guarded init path.
6. Output file, level gate, `ano_log_flush`.
7. Multi-thread soak + latency benchmark under TSan.
8. Wire the header, remove the header-defined `log_strings[]`, retire the mutex internals behind the fallback flag. Build all three configs.

Test plan:

- Round-trip: level, `file:line`, body survive enqueue → flush → file, byte-identical to a direct `snprintf` of the same call.
- Formatting: every conversion (plus width, precision, `%*d`, `%s`) the call site uses lands byte-identical to `snprintf`, since `format_line` delegates to `vsnprintf`; a `%s` whose backing buffer is overwritten after the call still logs its value, because it was formatted before return; an over-long line is clamped at `ANO_LOG_MSG_MAX` so it never overruns the entry.
- Head/tail: drive `tail − head` to exactly `N` and assert the next reservation reports full; assert the single `head` store frees the whole drained range. Exact-full drain: fill to exactly `N` with **every** entry committed (no gap), run one drain pass, and assert it emits exactly those records and terminates. This is the case the `tail` bound exists for (§3); a lap-check-only consumer wedges in an infinite re-read here. Gap: reserve but withhold commit and assert the consumer sees the slot is not committed-for-this-lap and stops; after a lap of reuse, assert a reclaimed slot still carrying the prior lap's tag is rejected by the lap check (not read as live), so no zeroing is needed.
- Seam: a line straddling the buffer end round-trips byte-identical; the head line, `tag`, and timestamp are never split.
- Multi-thread: P producers reserve concurrently; every record is flushed; per-record integrity; clean under TSan. Claim order is file order only for records that traversed the ring. A full-ring write-through (§10) happens out of band and can land ahead of still-buffered records, so assert in-order file output on a run sized not to hit the full path, or restrict the order check to ring-flushed records.
- Full ring: flood past capacity from several producers at once with no intervening flush and assert every record still appears, since a full ring makes producers wait for room rather than dropping. With the owned consumer draining continuously, a single producer cannot overflow the ring, so the flood is concurrent; whether it saturates is timing-dependent (the consumer may keep pace, especially under TSan), so saturation is an observation, not an assertion — the invariant under test is no loss.
- Boundaries: empty message; a message at `ANO_LOG_MSG_MAX`; a reservation landing exactly at capacity; an empty flush.
- Immediate is immediate: a FATAL reaches its stream before any flush.

TSan gates every concurrency test (the `tsan-runner` agent). The lifetime/order tests are headless and fit the CTest baseline.

---

## 14. Open decisions

- Marker layout (§3). 16-byte marker = atomic `tag` + plain `timestamp`. `tag` carries `len`/`level`/`flags` with `ANO_LOG_COMMITTED` set on publish, plus a 32-bit `cycle` (the lap, `pos >> shift`) in its high word. A slot is live iff committed and its `cycle` is the current lap, which lets the drainer reclaim by advancing `head` without zeroing each line (the high 32 bits were formerly idle reserve). The word is punned via `log_word_t` only on thread-local copies. The message, severity name, and `file:line` are baked into the text by the producer, so the marker holds no call-site references.
- Wrap: split copy at the seam (§8). The magic buffer is deferred to the event-bus ring.
- Cache-line constants (decided, in `anoptic_memory.h`): `ANO_CACHE_LINE` is the true coherency line (64 on x86-64 / 128 on Apple Silicon) and sets the reservation grain `ANO_CL` and packing density. `ANO_THREAD_LINE` (128 everywhere) is the false-sharing isolation distance and is what `tail`/`head` pad to. 128 clears both Apple Silicon's 128-byte line and x86's adjacent-line-prefetch buddy pair. The split mirrors C++17's constructive/destructive interference sizes. `render_bridge.h`'s private `ANO_CACHE_LINE 64` was removed in favor of these, and its SPSC cursors now pad to `ANO_THREAD_LINE`.
- Eager vs deferred formatting (decided): the logger formats eagerly (§9). The deferred path (call-site dictionary, two-pass argument codec, offline formatting of `(site id, timestamp, arg blob)`, and the fixed-schema `ANO_PROBE(site, u32, u64)` tracer) belongs to `anoptic_profiler.h`. Reopen only if the logger ever needs out-of-process reconstruction, which is a telemetry concern.
- Mutex fallback (decided): kept as `src/logging/logging_old.c` (namespaced `mtxlog_*`), built only into the optional `anotest_logbench` as the head-to-head baseline, never into `anoptic_core`. The ring is proven under TSan and beats it ~2× on latency and throughput; the baseline stays for regression-tracking, not as a runtime fallback.
- Header name (decided): renamed `anoptic_logging.h` → `anoptic_logger.h`.
- Output-file location: `anoptic_filesystem` (recommended) vs logger-private platform files.
- Full ring (decided): wait-for-room is the behavior — the producer backs off until the owned consumer frees space, no loss and self-throttling to the drain rate. Write-through is only the last-resort escape for a wedged consumer (a dead-producer gap, §7/§10). The runtime policy setter, the `DROP_NEWEST`/`BLOCK` alternatives, and the `dropped` counter were cut as speculative generality. Which policy a caller wants is a deployment decision, not a per-call one, and a lossy ring is the profiler's job, not the logger's. Reopen only if a caller genuinely needs lossy logging in-process.
- Collections convergence: this ring is the ECS event-bus primitive. At the collections port it migrates into `anoptic_collections.h` as the shared lock-free variable-record ring, logger and event bus as its two callers. Build it once.
- Module axis (the through-line). The same variable-record ring backs three callers that pick different policies on one integrity-versus-throughput axis. The logger picks integrity (sparse access, total order, wait-on-full, never silently drop, eager-formatted finished text) because Release compiles most calls out and volume is low enough to afford it. A future `anoptic_profiler.h` picks throughput (high volume, lossy-OK, per-thread to avoid perturbing the measured path, deferred binary schema, Morgan Stanley Binlog as prior art). A crash trace would pick durability (ring `mmap`'d and never zeroed so the tail survives the process). The logger's shared-ring choice is what buys its integrity (a dead producer strands one gap, §12), and it is the choice most likely to need revisiting at the event-bus port: a high-traffic bus may want per-producer rings. If so, "build it once" splits into "one ring layout, several ownership policies," and that should be decided before the collections port.
- Deferred: log rotation.
