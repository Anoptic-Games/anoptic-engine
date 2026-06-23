# Logger — Design (variable-cache-line MPSC ring)

Anoptic Engine uses a MPSC buffered logging system with single-ownership per logger instance. The canonical format would be to have the logger be owned by main.

The ring is variable-length and cache-line-granular (DPDK `rte_ring` family). Each entry is a contiguous run of cache lines: a 16-byte head-line marker — one atomic commit word doubling as the free/uncommitted sentinel, plus a timestamp — followed by the serialized arguments. Call-site constants (level, file, line, format string) live in a site dictionary keyed by an id packed into the commit word. A long record spans more cache lines under the same commit word. The earlier fixed-slot Vyukov draft is archived at `.claude/logger_old.md`.

Scope: the producer enqueue path, the single-consumer flusher, the file sink, and the public interface in `include/anoptic_logging.h`, on Linux, macOS, and Windows.

---

## 1. What the logger must do

`ano_log_*` is called from ECS worker threads during the parallel tick. A logging call must return without waiting on another thread: if a producer waits on a lock or on the consumer, it holds up the tick barrier and serializes the frame. So the producer path is lock-free and bounded in steps, and it never formats. It captures the call-site id and the raw arguments into the ring; the flusher thread does the `vsnprintf`-class work later, off the hot path.

Four further requirements come from the `notes.md` audit: batched output (one syscall per flush interval), a monotonic timestamp per record, a counted full-buffer policy, and a working file sink.

---

## 2. Architecture

One shared bounded ring carries records from many producers to one flusher. A producer reserves a contiguous run of cache lines, copies the captured call into it, and publishes the run with one release store. The flusher walks the ring in claim order, formats each record, and writes the batch with one syscall.

The ring follows the DPDK `rte_ring` family: a `head`/`tail` pair of monotonic counters over a power-of-two array of cache lines. A producer reserves by bumping `tail`; the flusher frees by advancing `head`. Free space is `N − (tail − head)`, and the same comparison guards reuse — a reservation that would pass `head` finds the ring full. Each entry occupies `ceil((HDR + len) / cacheline)` lines and carries one commit word (`tag`) on its first line; a long record is just a longer run under one `tag`, freed with the same single `head` store. Because claim order is one global sequence, the drained stream is already in order and the per-record timestamp is for display only (§11).

The nearest prior art is xtr: one background thread, a bounded ring, all formatting and I/O deferred. xtr runs one ring per sink (SPSC) and reaches its sub-3 ns hot path with C++ templates; this design uses one shared ring and plain C, so the codec is hand-written (§9). See `docs/references/lockfree.md` Part II.

---

## 3. The ring

```c
// The reservation grain is the cache line. ANO_CACHE_LINE is the hardware constant
// (64 today; 128 on Apple Silicon and adjacent-line-prefetch Zen — tracked separately).
#define ANO_CL           ANO_CACHE_LINE
#define ANO_LOG_HDR      16                          // head-line header: the 16-byte marker, below
#define ANO_LOG_MSG_MAX  4096                        // captured-blob cap; an entry is at most
                                                     //   ceil((16 + 4096) / 64) = 65 cache lines

enum { ANO_LOG_TRUNCATED = 1 << 0 };                 // record flags (per-record, runtime)

// An entry's head line begins with this 16-byte marker. The remaining (ANO_CL - 16) bytes of the
// head line, then every following line, are pure blob. Only `tag` is atomic: it is the single
// publish gate for the whole record — `timestamp` and the blob are plain memory whose visibility
// rides tag's release/acquire (§12).
typedef struct {
    _Atomic uint64_t tag;        // commit word. 0 = free/uncommitted sentinel; nonzero = committed.
                                 //   published LAST (release), read FIRST (acquire), zeroed on drain.
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
        uint16_t len;    // arg-blob bytes (<= ANO_LOG_MSG_MAX); entry span = ceil((16 + len) / ANO_CL)
        uint16_t flags;  // ANO_LOG_TRUNCATED, etc. — per-record, runtime
        uint32_t site;   // call-site id, >= 1; 0 is reserved, so tag == 0 is exactly "free/uncommitted"
    };
    uint64_t w;
} log_word_t;
_Static_assert(sizeof(log_word_t) == 8, "commit word is 8 bytes");

typedef struct {
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t tail;  // producer reserve cursor, in cache lines (the hot line)
    _Alignas(ANO_CACHE_LINE) _Atomic uint64_t head;  // consumer drain cursor, in cache lines (release)
    uint64_t          mask;                           // N-1; N = capacity in cache lines, power of two
    char             *buf;                            // N*ANO_CL bytes, cache-line aligned
    _Atomic uint64_t  dropped;                        // DROP_NEWEST drop count; surfaced as a notice
} log_ring_t;
```

The record stores no call-site constants. `level`, `file`, `line`, and `fmt` are invariant for a site; each site registers once into the dictionary (§9) and the record carries only the 32-bit `site` id. The commit marker is `tag`'s nonzero-ness and `len` is its low 16 bits.

Invariants:

- Addressing: counter `c` maps to physical line `c & mask`. `tail`/`head` are monotonic 64-bit counters; `tail − head` is the live (reserved-or-undrained) line count, bounded by `tail − head ≤ N`.
- Commit / visibility: `tag` is the single publish gate. The producer fills `timestamp` and the blob with plain stores, then publishes `tag = {len,flags,site}` with one release store; the consumer acquire-loads `tag` and, seeing it nonzero, reads the now-visible `timestamp` and blob (§12). `0` is the free/uncommitted sentinel: the buffer starts zeroed, the consumer zeroes every drained line (§6), and `site ≥ 1` keeps a committed `tag` nonzero even when `len == 0`. So the test is `tag == 0` (stop) vs `tag != 0` (committed); a reserved-but-uncommitted head line reads exactly `0`. 64-bit, so no ABA.
- Length integrity: `len` is carried inside `tag` and published by that one release store, so it commits in the same atomic event as the gate. The consumer reads `len` only from a nonzero `tag`, so it always reads the exact value the producer committed — there is no torn-`len` state, and `need` always lands on the next record's head line. This is what makes traversal immune to a corrupt blob: the blob is plain bytes that affect only one message's text, while the structure the reader walks lives entirely in the atomic `tail`/`head`/`tag` words. The invariant this rests on: the length must be published by the same store as the commit flag. Widening the blob past what the publish word can carry (moving `len` out of `tag`) reintroduces torn lengths and would require a separate structural backstop; keep `len` in the word.
- Full-detection: the producer reads `head` (acquire) and refuses a reservation that makes `(pos + need) − head > N`.
- Reuse-safety: the same acquire `head` read orders the producer's writes after the consumer's reads on the previous lap, and after its drain-zeroing — so a producer always reserves over an all-zero run disjoint from `[head, tail)`, and only its own release of `tag` makes the head line nonzero.

Entry span is `ceil((ANO_LOG_HDR + len) / ANO_CL)` — `1` for a short record (head line = 16-byte marker + first 48 blob bytes), more for a long one, no per-line header on continuation lines. `tag` is the only synchronized word; everything else rides its release/acquire as plain memory (§12).

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
 drained and free lines are all-zero; "tag word == 0" is the single free/uncommitted marker
```

### 4.2 One entry — the commit word lives on the head line

```
 entry = ceil((16 + len) / 64) cache lines

 head line (byte offsets):
  0                 8                 16                                            64
  +-----------------+-----------------+---------------------------------------------+
  | tag (commit)    | timestamp       | blob[0 .. 47]                               |
  +-----------------+-----------------+---------------------------------------------+
   ^                                   ^ payload begins (ANO_CL - 16 = 48 inline blob bytes)
   |
   tag = { uint16 len; uint16 flags; uint32 site }   (site >= 1)
   published LAST (release), read FIRST (acquire);  == 0  iff  free/uncommitted

 continuation line(s)  (only if blob > 48 bytes):
  +--------------------------------------------------------------+
  | blob[48..111], blob[112..175], ...   pure payload, no header |
  +--------------------------------------------------------------+
```

### 4.3 Producer — reserve → write → publish (lock-free)

```
 capture args -> blob, len            (no formatting; §9)
      |
      v
 need = ceil((16 + len) / 64)         lines this entry needs
      |
      v
 pos = load(tail, relaxed) <-------------------------------+
      |                                                    | CAS lost / stale:
      v                                                    | reload pos
 hd  = load(head, ACQUIRE)   (full-check + reuse-safety)   |
      |                                                    |
      +--- (pos + need) - hd > N ? --yes--> ON_FULL -------|--> immediate path (§10)
      |                                                    |
      no                                                   |
      v                                                    |
 CAS(tail : pos -> pos + need, relaxed) ---- fail ---------+
      |
      success   -- now owns lines [pos, pos+need) --+
      v                                             |
 plain stores: timestamp, then blob  --------------+   (into [pos, pos+need))
      |
      v
 store(tag = {len,flags,site}, RELEASE)   <-- publish: one gate, happens-before for the consumer
      |
      v
    done                              (never waited on another thread)
```

### 4.4 Consumer — single thread, owns head

```
 h = head ;  h0 = head
 +-> t = load( tag@(h & (N-1)), ACQUIRE )
 |        |
 |        +-- t == 0 ? --yes--> STOP            (free/uncommitted = the gap; resume here next pass)
 |        no
 |        v
 |   len  = {t}.len                            (visible via tag's release)
 |   need = ceil((16 + len) / 64)
 |        v
 |   format_record({t}.site, ts, blob over [h, h+need)) -> batch   (§9, dictionary lookup)
 |        v
 |   h += need
 +--------+
   after loop:
        memset([h0, h) lines = 0)   -- one memset, two at the seam: all-zero = free again
        store(head = h, RELEASE)    -- frees [h0, h); the memset happens-before reuse
        one ano_fs_write(batch)     -- single syscall
```

### 4.5 The gap — preempted vs dead producer

```
        head                         tail
         |                            |
         v                            v
 [..D..][  E1  ][ E2 ][ P: reserved ][..free..]
                       ^^^^^^^^^^^^^^
                       tag word == 0  (whole line zeroed on drain; producer writes tag last)

 consumer drains E1, E2, reaches P, reads tag -> 0 -> not committed -> STOP, waits.

 P preempted  : wakes <= 1 quantum, does its release-store, next pass sweeps past.
 P *dies* here: head never passes P -> whole drain wedges, ring fills to immediate path.
                P's window is a bounded copy + one store, so it can be preempted but not
                blocked. One ring = claim order is total order, so the consumer waits on the
                gap instead of skipping it -- no cycle-number machinery (lockfree.md II §6).
```

### 4.6 The seam — a record that crosses the buffer end

```
 a record reserved at line N-2 with need=4 -> physical lines N-2, N-1, 0, 1:

   line N-2 (head): [ tag | ts | blob.. ]      <- marker never splits (one line, in-bounds)
   line N-1       : [ blob ................. ]
   ---- buffer end / wrap ----
   line 0         : [ blob ................. ]   <- remainder continues at the start
   line 1         : [ blob .....            ]

 reservation is one CAS on tail (logically contiguous). the blob copy is <= 2 memcpys:
   [ body .. buffer end ]  then  [ buffer start .. remainder ]
 one memcpy when the record fits before the end (the common case). that's the whole "wrap".
```

---

## 5. The enqueue path

```c
int ano_log_enqueue(uint32_t site, log_types_t level, const char *fmt, ...) {
    if (level < atomic_load_explicit(&g_log.min_level, memory_order_relaxed))
        return 0;                                   // runtime level gate, one relaxed load

    char blob[ANO_LOG_MSG_MAX];                     // serialized args on the stack (thread-local);
    va_list ap, ap2; va_start(ap, fmt);             //   capture raw bytes, format on the flusher
    va_copy(ap2, ap);                               // kept live for the immediate fallbacks below
    uint16_t flags = 0;
    int cap = (site != 0)                           // site 0 == site table exhausted (pathological)
            ? capture_args(blob, sizeof blob, fmt, ap, &flags)   // walk fmt, va_arg each value; %s inline
            : -1;
    va_end(ap);
    if (cap < 0) {                                  // exhausted site OR unhandled specifier → immediate
        log_emit_immediate_v(site, level, fmt, ap2);   //   synchronous vsnprintf via §10 (file:line from dict)
        va_end(ap2);
        return 1;                                   // 1 == written immediately, same as the full path
    }
    va_end(ap2);
    uint16_t len = (uint16_t)cap;
    uint64_t ts  = ano_timestamp_raw();
    uint64_t need = (ANO_LOG_HDR + len + ANO_CL - 1) / ANO_CL;   // cache lines, >= 1

    uint64_t pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);
    for (;;) {
        uint64_t hd = atomic_load_explicit(&g_ring.head, memory_order_acquire); // full + reuse-safety
        if ((pos + need) - hd > g_ring.mask + 1)    // would alias an undrained line → full
            return on_full(site, level, ts, fmt, blob, len, flags);
        if (atomic_compare_exchange_weak_explicit(&g_ring.tail, &pos, pos + need,
                memory_order_relaxed, memory_order_relaxed))
            break;                                  // own lines [pos, pos+need)
        // CAS failed: pos was reloaded with the current tail; retry
    }

    log_marker_t *m = (log_marker_t *)(g_ring.buf + (pos & g_ring.mask) * ANO_CL);
    m->timestamp = ts;                              // plain store, ordered by the release below
    char  *body  = (char *)m + ANO_LOG_HDR;         // blob goes here; the marker never wraps (one line)
    size_t toend = (size_t)(g_ring.buf + (g_ring.mask + 1) * ANO_CL - body); // bytes to the buffer end
    if (len <= toend) memcpy(body, blob, len);      // common: fits before the seam — one copy
    else { memcpy(body, blob, toend);               // straddles the seam — split into two (§8)
           memcpy(g_ring.buf, blob + toend, len - toend); }
    log_word_t v = { .len = len, .flags = flags, .site = site };  // pun on a plain local
    atomic_store_explicit(&m->tag, v.w, memory_order_release);    // publish — one gate, whole record
    return 0;
}
```

Notes.

- Lock-free, bounded steps. The only contended line is `tail`; its CAS retries only under concurrent reservation. The full-check `head` read is an acquire load of a line written once per drain pass.
- One publish. The producer writes the run as one (or seam-split two) `memcpy` and a single release store of `tag` publishes the whole entry. `len` lives in `tag`, so writing it ahead commits early.
- `on_full` (§10) by default formats the captured record to the immediate sink and bumps `flush_request`; only `DROP_NEWEST` bumps `g_ring.dropped` and returns.
- A mutex-based fallback is selectable behind a compile flag as the trivially-correct reference (§14).

---

## 6. The consumer: the flusher thread

One thread, spawned by `ano_log_init` via `ano_thread_create`, joined by `ano_log_cleanup`.

```c
static void *flusher_main(void *unused) {
    while (atomic_load_explicit(&g_log.running, memory_order_acquire)) {
        uint64_t req = atomic_load_explicit(&g_log.flush_request, memory_order_acquire);
        drain_and_emit();
        atomic_store_explicit(&g_log.flush_done, req, memory_order_release);
        ano_sleep(atomic_load_explicit(&g_log.interval_us, memory_order_relaxed));
    }
    drain_and_emit();                               // final drain after stop
    return NULL;
}
```

`drain_and_emit` walks the ring in claim order, stops at the first uncommitted entry, zeroes the whole drained range, and frees it with one `head` store:

```c
uint64_t h0 = g_ring.head;                          // consumer-private until the release store
uint64_t h  = h0;
for (;;) {
    log_marker_t *m = (log_marker_t *)(g_ring.buf + (h & g_ring.mask) * ANO_CL);
    log_word_t v = { .w = atomic_load_explicit(&m->tag, memory_order_acquire) };
    if (v.w == 0)
        break;                                       // tag == 0 (gap/empty) → stop in order
    uint64_t need = (ANO_LOG_HDR + v.len + ANO_CL - 1) / ANO_CL;
    const char *blob = gather_if_seam((char *)m + ANO_LOG_HDR, v.len, scratch, &g_ring); // ≤2 memcpys (§8)
    format_record(v.site, m->timestamp, blob, v.len);   // dictionary lookup + format (§9)
    h += need;
}
if (h != h0) {                                      // zero everything drained (plain; one memset, two at seam)
    uint64_t n = h - h0, a = h0 & g_ring.mask, N = g_ring.mask + 1;
    uint64_t first = (a + n <= N) ? n : (N - a);
    memset(g_ring.buf + a * ANO_CL, 0, first * ANO_CL);           // up to the buffer end
    if (first < n) memset(g_ring.buf, 0, (n - first) * ANO_CL);   // wrap remainder
}
atomic_store_explicit(&g_ring.head, h, memory_order_release); // frees [h0, h); the memset happens-before reuse
// then: one ano_fs_write of the whole batch; append a drop notice if g_ring.dropped advanced.
```

- Order: claim order is a single global serialization, so the batch is already ordered. A producer that reserved but has not published stops the walk; the flusher resumes there next pass (§7).
- Zeroing: after the walk the consumer zeroes the drained range `[h0, h)` — one `memset`, two at the seam — restoring the all-zero free state before the `head` release frees the lines. Whole-line zeroing is branchless and lowers to wide stores / `dc zva` on ARM, keeping "all-zero line = free" as one invariant. The `memset` is plain; the `head` release publishes it.
- Batch: format each record into a reusable consumer-owned buffer (grown geometrically) as `"<wall-time> <LEVEL> file:line: <message>\n"`. This deferred formatting is where the `vsnprintf`-class cost lives — on the flusher, off every producer. A seam-straddling record is gathered into contiguous scratch first (§8). `format_record` slices each conversion out of `fmt` and emits it with one `snprintf` against the matching blob value.
- One write: `ano_fs_write(g_log.sink, batch, len)`, one syscall. No fsync here. Echo to console if configured.
- Drop notice: if `g_ring.dropped` advanced, append a `WARN logger: N messages dropped` line.
- High-watermark assist: if `tail − head` crosses ~75% of `N`, shorten the next sleep. A full event (§10) also bumps `flush_request` — full is "write now."

Cadence is `ano_sleep(interval_us)` between passes; `ano_log_flush` bumps `flush_request` and spins until `flush_done` catches up (the caller never reads the ring, preserving single-consumer). A `pthread_cond_timedwait` wakeup is a later upgrade.

---

## 7. The gap problem

When a producer has reserved its lines but not yet stored `tag`, the consumer hits a zero `tag` mid-ring, stops there, and resumes next pass. That stall is bounded: between reserve and publish the producer does only a bounded blob copy (≤ `ANO_LOG_MSG_MAX`) and one store, so it can be preempted but not blocked, and it finishes within a scheduler quantum. A producer that dies in that window wedges the drain (the consumer stays stopped at its zero `tag` and the ring fills to the immediate path).

One ring means claim order is the total order, so an unpublished slot is a gap to wait on. The logger therefore skips the cycle-number skip LCRQ/SCQ carry and the cross-lane timestamp grace period Quill needs (5 µs, §2). Lineage in `docs/references/lockfree.md` Part II §6.

---

## 8. The seam

The buffer is circular, so a reserved run that starts near the end wraps to line 0. Reservation is unchanged: the `tail` CAS claims the logically contiguous range `[pos, pos+need)`, wrap or no wrap. Only the physical layout differs — a wrapped record sits in two pieces. The head line is always one in-bounds cache line, so `tag` and `timestamp` never cross; only the blob does.

Handling is a split copy: write and read the blob in at most two `memcpy`s, the part to the buffer end and the remainder from the start (producer §5, consumer gathers symmetrically into scratch before `format_record` §6). One copy in the common case. Records are bounded at `ANO_LOG_MSG_MAX` (≤ 65 lines), so a straddling record is small.

A double-mapped "magic" buffer (map the pages twice back-to-back so a wrapping write needs no split) is a throughput micro-optimization costing platform-specific VM mapping; deferred to the shared event-bus ring (§14).

---

## 9. Deferred formatting

The producer captures; the flusher formats. The NanoLog/Quill model in C without templates.

- The site dictionary. Every `ano_log_*` call site is a fixed tuple `{level, file, line, fmt}`. Each site registers once into a process-wide append-only table; the record carries only the 32-bit `site` id (§3). Registration is lazy and lock-free: the macro holds a `static _Atomic uint32_t` cache; on first hit it `fetch_add`s a global counter to claim a slot, fills `g_sites[id]`, and CAS-publishes the id (a first-hit race at worst claims two identical slots — harmless). Ids are 1-based, so `0` stays the free sentinel; on exhaustion (pathological) `ano_log_site` returns 0 and the call routes to the immediate path (§10). A record is self-describing as `(site id, timestamp, arg blob)`, so the raw ring plus the dictionary can be formatted offline (§14).
- `capture_args(blob, cap, fmt, ap, &flags)` walks `fmt`, pulls each argument at its default-promoted type, and appends its raw bytes. Matching the default promotions and length modifiers exactly is mandatory — one misread desyncs every later argument — so the codec is the primary bug surface and the differential test (§13) is the gate. A `%s` is stored as a `uint16_t` length + bytes, copied inline so the record never dereferences a pointer that may have died. Returns the blob length, or `-1` on an unsupported conversion or capture error; on `-1` the enqueue path keeps a `va_copy` and re-formats synchronously (§5, §10). Sets `ANO_LOG_TRUNCATED` if a `%s` or the blob is clamped at `cap`. `fmt` must be a string literal; enforce with `__attribute__((format(printf, 3, 4)))` plus `-Wformat-nonliteral -Werror`.
- `format_record(site, ts, blob, len)` is the inverse on the flusher: look up the site's `fmt` (and level/file/line), walk `fmt`, copy literal runs verbatim, and for each conversion emit one `snprintf` against the blob value (a `%s` reads its inline length + bytes; a `%*` width reads an `int`). Reusing libc per conversion keeps width/precision/flag correctness in the C library. The blob is always contiguous — gathered across the seam first (§6, §8).
- Supported to start: `%d %i %u %x %X %o %c %ld %lld %zu %zd %f %g %e %p %s %%`, with width, precision, and `%*` star width. `%n` and exotic forms are out of scope until needed.

---

## 10. The immediate path and full policy

`ano_log_immediate` (FATAL, `_now`) and `on_full` are the only modes that format on the calling thread; the buffered hot path captures and defers (§9).

- Format synchronously — an immediate write has no flusher pass. `ano_log_immediate` formats from its own live `va_list` and carries `file`/`line` directly (never touches the dictionary); `on_full` formats the captured blob via `format_record`; the unsupported-conversion fallback (§9), sharing the `log_emit_immediate_v` entry point, formats from its preserved `va_list`, taking `file`/`line` from the dictionary (or omitting them when `site == 0`). Then write to console (`>WARN` to stderr, else stdout), then the sink if open, then fsync — a FATAL means the flusher will not run again.
- Never touch the ring. Immediate must not reset or consume buffered records, preserving single-consumer.
- Init error path: before `g_log` is live, write to stderr only, guarded by an "initialized" flag; never lock or log through half-constructed state.
- Full policy. Default IMMEDIATE: on `(pos + need) − head > N` the producer writes that record through the immediate path and bumps `flush_request`, losing nothing and self-throttling to disk speed. Under sustained full, concurrent producers each issue their own `write` and serialize in the kernel (`O_APPEND` keeps appends non-interleaved); a producer waits on the OS here, never on another engine thread. Alternatives: `DROP_NEWEST` (bump `dropped`, no syscall) and `BLOCK` (spin, debug only). Overwrite-oldest would reclaim an undrained line, so it is excluded.

---

## 11. Sink, timestamps, lifecycle, interface

Sink. A minimal append-only handle in `anoptic_filesystem` (opaque `ano_file`; `ano_fs_open_append` / `_write` / `_sync` / `_close`). POSIX `open(O_WRONLY|O_CREAT|O_APPEND,0644)` on Linux and macOS; Windows `CreateFileW(FILE_APPEND_DATA)` + `WriteFile`, UTF-8→UTF-16 at the boundary. One handle per process, one batched `write` per interval, fsync only on cleanup / `ano_log_flush` / a FATAL write. `ano_log_output_dir(dir)` opens `dir/anoptic_<utc>.log`; with no sink, records still drain to console.

Timestamps. `ano_timestamp_raw()` (monotonic ns) stamped per record at capture — the platform clock behind the abstraction (x86 `rdtsc`; Apple Silicon `CNTVCT_EL0` / `mach_absolute_time`). Claim order gives file order, so the timestamp is display only. Convert with an init-time anchor: `wall = anchor_unix_ns + (ts − anchor_raw_ns)` — no per-record syscall, no drift.

Lifecycle. `ano_log_init`: "initialized" false (immediate path stderr-only); `mi_heap_new`; allocate the ring (cache-line aligned, power-of-two line count, zeroed); `tail = head = 0`; clear the site dictionary (id 0 reserved); set defaults; capture the timestamp anchor; open the default sink if a directory was set; `running = 1`; spawn the flusher; "initialized" true. Any earlier failure rolls back without logging through half-built state. `ano_log_cleanup`: `running = 0`; join the flusher (final drain); sync + close the sink; `mi_heap_destroy`. The site dictionary is process-lifetime.

Public interface (`include/anoptic_logging.h`):

```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL } log_types_t;
typedef enum { ANO_LOG_FULL_IMMEDIATE = 0, ANO_LOG_DROP_NEWEST = 1, ANO_LOG_BLOCK = 2 }
    ano_log_full_policy_t;

int  ano_log_init(void);        // 0 ok; until 0, only the immediate (stderr) path works
int  ano_log_cleanup(void);

// Register (once) or return a call-site id for the {level, file, line, fmt} tuple. Lazy and
// lock-free; `cache` is the call site's static slot. Returns 0 only if the site table is exhausted
// (pathological), which routes the call to the immediate path.
uint32_t ano_log_site(_Atomic uint32_t *cache, log_types_t level, const char *file, int line,
                      const char *fmt);

int  ano_log_enqueue(uint32_t site, log_types_t level, const char *fmt, ...)
        __attribute__((format(printf, 3, 4)));      // checks args vs fmt; pair with -Wformat-nonliteral
                                // 0 enqueued; 1 written immediately (full policy, unsupported
                                // conversion, or exhausted site); -1 dropped (DROP_NEWEST).
                                // Never waits on a thread.
void ano_log_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
        __attribute__((format(printf, 4, 5)));
void ano_log_interval(uint32_t ms);
int  ano_log_output_dir(const char *directoryPath);
void ano_log_set_level(log_types_t min);
void ano_log_set_full_policy(ano_log_full_policy_t policy);
void     ano_log_flush(void);
uint64_t ano_log_dropped(void);

// Call-site macros: each site registers its {level, __FILE_NAME__, __LINE__, fmt} once into the
// dictionary (cached in a static id) and enqueues only that id + the captured args. fmt MUST be a
// string literal; a %s argument is copied into the record at capture time.
#define ANO_LOG(lvl, fmt, ...)                                                     \
    do {                                                                           \
        static _Atomic uint32_t site_ = 0;                                         \
        uint32_t s_ = atomic_load_explicit(&site_, memory_order_relaxed);          \
        if (!s_) s_ = ano_log_site(&site_, (lvl), __FILE_NAME__, __LINE__, (fmt)); \
        ano_log_enqueue(s_, (lvl), (fmt) __VA_OPT__(,) __VA_ARGS__);               \
    } while (0)
#define ano_log_info(...)   ANO_LOG(LOG_INFO,  __VA_ARGS__)
#define ano_log_warn(...)   ANO_LOG(LOG_WARN,  __VA_ARGS__)
#define ano_log_error(...)  ANO_LOG(LOG_ERROR, __VA_ARGS__)
#define ano_log_fatal(...)  ano_log_immediate(LOG_FATAL, __FILE_NAME__, __LINE__, __VA_ARGS__)
// ano_log_debug / _debug_now compile to nothing unless DEBUG_BUILD.
```

The level-name table (`log_strings[]`) and the site dictionary live in `logging_core.c`.

Files: `include/anoptic_logging.h` (public contract), `src/logging/logging_core.c` (enqueue, immediate, flusher, site dictionary, lifecycle), `src/logging/logging_ring.h` (private `log_marker_t`/`log_word_t`/`log_ring_t` + claim/drain inlines; migrates to `anoptic_collections.h` at Step 5), and the sink. Only the sink is platform-specific; the ring is portable C23 `<stdatomic.h>`.

---

## 12. Progress and memory ordering

The producer is lock-free in steady state: it loads `tail`, CASes it forward (retrying only against another concurrent reservation), copies the blob, and does one release store of `tag`. The full-ring path is bounded steps to a syscall (§10). Neither waits on the consumer.

`tag` is the only synchronized word. The producer writes `timestamp` and the blob with plain stores, then release-stores `tag`; the consumer acquire-loads `tag` and, seeing it nonzero, reads the now-visible payload. The release/acquire pair is the publish: it pins the plain writes before the `tag` store and the plain reads after the `tag` load. Without it, `-O2` may reorder those accesses and a weakly-ordered core may see a committed `tag` ahead of its payload. The pair compiles to plain `mov` on x86 (TSO) and to one `stlr` / one `ldar` on ARM.

The consumer is a background thread, so the only place it waits — at an unpublished slot (§7) — costs nothing on the hot path. Its drain-zeroing is plain memory, published by the single `head` release. The ring is one allocation reused in place, so there is no memory to reclaim and no ABA on the 64-bit counters.

The bounded loss case. A producer that reserves and then permanently dies before its `tag` release leaves a `0` slot the consumer can never pass; records committed logically after it are stranded behind the gap until the ring fills and new logs route to the immediate path (§10). The loss is one gap wide — everything before it (lower claim order) drains normally — not a whole producer's backlog, because the records live in one shared order rather than per-thread buffers. The trigger is a thread dying mid-window while the process keeps running; a fault that takes the process down instead routes FATAL synchronously through the immediate path, and a preempted producer (§7) recovers within a quantum.

---

## 13. Implementation order and test plan

Build order, each step independently testable:

1. Ring buffer + seam-aware blob copy (§8). Test: a record whose blob crosses the buffer end writes and reads back byte-identical; one that fits takes the single-copy path.
2. Ring + record codec. `logging_ring.h`: reserve/drain inlines, the site dictionary, the codec. Single-thread: enqueue N, drain N, assert order and field integrity; the codec round-trips every supported conversion against `snprintf`, including a `%s` mutated after capture; a multi-line record reassembles exactly; drive the ring full and assert the policy fires.
3. Lifecycle + flusher. `ano_log_init`/`cleanup`, spawn/join, timestamp anchor, batched `ano_fs_write`.
4. Round-trip. Enqueue → flush → read back: level, `file:line`, body survive; N enqueues emit N ordered lines.
5. Immediate/FATAL. No buffer wipe, sync write, guarded init path.
6. Sink, level gate, policy, `ano_log_flush`/`ano_log_dropped`.
7. Multi-thread soak + latency benchmark under TSan.
8. Wire the header, remove the header-defined `log_strings[]`, retire the mutex internals behind the fallback flag. Build all three configs.

Test plan:

- Round-trip: level, `file:line`, body survive enqueue → flush → file.
- Site dictionary: a site registers exactly once and its cached id is stable; concurrent first-hits never corrupt the table and resolve to identical tuples; the flusher reconstructs level/file/line/fmt so the line is byte-identical to an inline-header version.
- Deferred formatting: every supported conversion (plus width, precision, `%*d`) formats byte-identically to `snprintf`; a `%s` whose backing buffer is overwritten between enqueue and flush still logs its captured value; an unsupported conversion makes capture return `-1` and the call is still emitted via the synchronous fallback (§9).
- Head/tail: drive `tail − head` to exactly `N` and assert the next reservation reports full; assert the single `head` store frees the whole drained range. Sentinel: reserve but withhold commit (leave `tag` 0) and assert the consumer reads `0` and stops; assert every drained line is all-zero before reuse.
- Seam: a blob straddling the buffer end round-trips byte-identical; the head line, `tag`, and timestamp are never split.
- Multi-thread: P producers reserve concurrently; every non-dropped record is flushed; per-record integrity; claim order is file order; clean under TSan.
- Full policy: under IMMEDIATE every refused record still appears and `flush_request` advanced; under DROP_NEWEST `ano_log_dropped` counts the rejects exactly and a drop notice appears.
- Boundaries: empty message; a message at `ANO_LOG_MSG_MAX`; a reservation landing exactly at capacity; an empty flush; a table-exhausted call (site 0) routed to immediate.
- Immediate is immediate: a FATAL reaches its stream before any flush.

TSan gates every concurrency test (the `tsan-runner` agent). The lifetime/order tests are headless and fit the CTest baseline.

---

## 14. Open decisions

- Marker and site dictionary (§3, §9). 16-byte marker = atomic `tag` + plain `timestamp`; `level`/`file`/`line`/`fmt` stored once in the dictionary, keyed by a 1-based 32-bit id; `tag`'s nonzero-ness is both commit and free marker; the word is punned via `log_word_t` only on thread-local copies.
- Wrap: split copy at the seam (§8). The magic buffer is deferred to the event-bus ring.
- `ANO_CACHE_LINE` as the reservation grain: make it platform-specific (64 vs 128) in a shared hardware-constants header — it sets packing density and head-line layout.
- Fixed-schema probe (deferred): the same ring + dictionary back a future `ANO_PROBE(site, u32, u64)` tracer — a declared field schema, a compile-time-sized `memcpy`, single-digit-ns capture in plain C, the identical 16-byte marker. The probe drops the format string and its runtime `fmt` walk.
- Offline formatting (deferred): a record is `(site id, timestamp, arg blob)`, so the raw ring + dictionary can be formatted out of process.
- Mutex fallback: keep behind a compile flag or delete once the ring is proven under TSan.
- Header name: keep `anoptic_logging.h` or rename to `anoptic_logger.h`. Mechanical.
- Sink location: `anoptic_filesystem` (recommended) vs logger-private platform files.
- Default full policy: IMMEDIATE (no loss, self-throttling).
- Collections convergence: this ring is the ECS event-bus primitive. At Step 5 it migrates into `anoptic_collections.h` as the shared lock-free variable-record ring, logger and event bus as its two callers. Build it once.
- Module axis (the through-line). The same variable-record ring backs three callers that pick different policies on one integrity-versus-throughput axis: the logger picks integrity (sparse access, total order, immediate-on-full, never silently drop) because Release compiles most calls out and volume is low enough to afford it; a future `anoptic_profiler.h` picks throughput (high volume, lossy-OK, per-thread to avoid perturbing the measured path, binary schema not `fmt` — the §3/§9 fixed-schema probe, Morgan Stanley Binlog as prior art); a crash trace would pick durability (ring `mmap`'d and never zeroed so the tail survives the process). The logger's shared-ring choice is what buys its integrity (a dead producer strands one gap, not a thread's whole buffer — §12), and it is the choice most likely to need revisiting at the event-bus port: a high-traffic bus may want per-producer rings, which is the opposite of the logger's shared ring. If so, "build it once" splits into "one ring layout, two ownership policies," and that should be decided before Step 5, not discovered during it.
- Deferred: log rotation; `pthread_cond_timedwait` for zero-latency flusher wakeup.

