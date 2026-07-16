# Lock-free references — Shared-Memory Synchronization (Scott & Brown) and the fetch-and-add queue lineage

Brief notes from Michael L. Scott and Trevor Brown, *Shared-Memory Synchronization* (2nd ed., Synthesis Lectures on Computer Architecture, Springer 2024).

---

# Part I : Book Summary

### 1. Definitions

- Wait-free: every operation completes in a bounded number of its own steps. Strongest; starvation-free. (The bound need not be statically known.)
- Lock-free: some thread is guaranteed to complete in a bounded number of steps. Livelock-free; an individual thread may starve.
- Obstruction-free: a thread completes in bounded steps if no other thread takes steps meanwhile. Weakest; admits livelock.

Treiber's stack and the M&S queue are both lock-free (p. 47). Most wait-free algorithms use helping: a thread "gets a stalled peer out of the way" so it can proceed (p. 48). This is the fast-path/slow-path machinery behind WFqueue/wCQ. A telling small example (p. 48): an increment-only counter is wait-free with a single FAI, but only lock-free if FAI is emulated with a CAS loop — the primitive choice decides the progress class.

The consensus hierarchy (Herlihy, p. 51): primitives are ranked by how many threads they can get to agree wait-free. TAS, swap, FAA, FAI have consensus number 2, they cannot solve wait-free consensus for three or more threads. CAS and LL/SC are universal (consensus number ∞). This is the formal reason FAA alone cannot build arbitrary structures (a stack needs CAS's conditionality), and why a bounded ring that must conditionally refuse-on-full reaches for CAS.

### 2. FAA vs CAS, from the source (§2.3.2, p. 33; footnote p. 48)

The O(n) vs O(n²) argument in the book's own words (p. 33): under contention, n threads doing CAS-emulated increments may force the hardware to serialize O(n²) attempts (each retry is a wasted round). In contrast, n threads doing FAA complete in O(n) because every access succeeds. "Clearly justifies the continued inclusion of FAA in the x86 instruction set," with a pointer to Morrison & Afek 2013 (LCRQ) as the compelling queue application.

### 3. The array-ring vs linked-structure divide, and Safe Memory Reclamation (§8.7, p. 170)

The book spends ~9 pages on SMR because it is the dominant source of complexity and bugs in nonblocking node-based structures: once a node is unlinked, when is it safe to free, given other threads may still hold pointers into it? The techniques, in escalating sophistication:

- Hazard pointers (Michael 2004, p. 171): each thread publishes the nodes it is about to access; a node is freed only when no hazard pointer references it. Bounded garbage, per-access cost.
- Epoch-based reclamation (EBR, p. 174): threads announce an epoch; a node retired in epoch e is freed once all threads have advanced past e. Cheap, but a single stalled thread pins all garbage.
- Interval-based (IBR, Wen et al. 2018, p. 177): nodes carry birth/retire epochs; a thread protects a bounded time interval, so a stall pins only bounded garbage — IBR is the Scott-lab successor that gets HP's bound with EBR's speed.
- NBR (p. 177), Free Access / VBR (p. 178): further points on the bounded-garbage vs overhead vs programmer-effort trade.

All of this exists only because nodes are dynamically allocated and freed. An array-based structure that reuses slots in place doesn't suffer from this problem.

The book covers one possible resolution explicitly (p. 149): Morrison & Afek 2013 structure the queue as a linked list of array-based ring buffers, which "dramatically" reduces both memory-management overhead and failed-CAS contention, enqueuing/dequeuing in most cases with fetch-and-increment.

### 4. The M&S queue internals (§8.3.1, p. 147)

- A permanent dummy node sits at the head; the first real item follows it. Dequeue reclaims the old dummy and promotes the dequeued node to the new dummy (p. 147).
- Enqueue is a two-step update: CAS the last node's `next` to the new node (the linearization point), then swing `tail` (cleanup, performable by any thread — the helping). Dequeue linearizes at the CAS that moves `head` (p. 148).
- Consistent snapshot: enqueue re-reads `tail` after reading `tail.next` and relies on the counted-pointer increment to prove the two reads were simultaneously valid — the same "check, then let the CAS validate" shape the logger's multi-slot claim uses (p. 149).
- ABA is handled with counted pointers (a sequence number packed beside the pointer), needing double-width CAS on x86 (`cmpxchg16b`) (p. 31, p. 148). Overlapping enqueues/dequeues may linearize in arbitrary order (Hoffman's "baskets," p. 149) — concurrent operations have no inherent order to violate.

### 5. Dual data structures — condition synchronization without losing nonblocking progress (§8.8, p. 179)

A nonblocking operation must be total (defined on any state), so a dequeue on empty returns ⊥. But often a thread genuinely needs to wait for a precondition — an empty queue to fill, or a full bounded queue to drain (the book lists "a full bounded container" explicitly, p. 179). The naive answer, spin `repeat v := remove() until v ≠ ⊥`, wastes cycles and — worse — lets the scheduler pick who gets the next datum (p. 179).

A dual data structure holds reservations alongside data: an operation that finds its precondition unmet inserts a reservation, and a later operation fulfills it, all linearizable and nonblocking, with any spinning confined to a single reservation slot (p. 179). Scherer & Scott built dual versions of the Treiber stack and M&S queue; Izraelevitz & Scott (2017) did the LCRQ. Java 6's Executor replaced Java 5's lock-based task pools with dualstacks/dualqueues for a 2–10× thread-dispatch throughput gain (p. 180). This is the principled frame for both of the logger's "what happens at the boundary" questions.

### 6. Spin locks: local spinning, and the preemption tax (§4.2.2–4.3.4, p. 67; §4.5.1, p. 80)

- Ticket lock (p. 67): FAA a ticket, spin on `now_serving`. Fair and constant-space, but every waiter polls the one `now_serving` line, so contention traffic is O(threads).
- MCS lock (p. 69): each waiter spins on its own `qnode` flag (a local cache line); the holder signals exactly its successor. Constant remote-access cost to pass the lock, FIFO, scalable — the canonical "local spinning" structure. Joins the queue wait-free via swap.
- Which to use (p. 78): for single-digit thread counts, a TAS-with-backoff or ticket lock is fine; reach for a queued lock (MCS/CLH) only when contention is a real bottleneck.
- The preemption tax (p. 78 table; §7.5.2): every fair/queued lock rates poorly on preemption tolerance. A descheduled lock-holder — or a descheduled thread in the middle of the wait queue — stalls its successors. It is inherent to lock-based mutual exclusion under preemption.
- Locality-conscious / cohort locks (p. 80): pass a lock to a nearby core when possible to keep the protected cache lines local; NUMA-aware. Background for the job system.

### 7. Combining and elimination (§5.4, p. 100; §8.9, p. 180)

- Flat combining (Hendler et al. 2010, cited p. 149): under high contention, let one thread apply many pending operations, trading parallelism for locality and far fewer atomics. The batching half of the engine's cache-line-stripe idea.
- Elimination (p. 180): operations that cancel — a push and a pop — meet in an elimination array and annihilate without touching the main structure, an adaptive contention back-off. FIFO elimination (Moir et al. 2005) augments M&S nodes with monotonically increasing serial numbers so an enqueue can eliminate with a dequeue once "sufficiently old" — the same monotonic-ticket device the ring uses, repurposed.

---
## References (book sections)

- §2.3.1 ABA, Treiber counted pointers — p. 30
- §2.3.2 The value of FAA (O(n) vs O(n²); LOCK-add returns no value) — p. 33
- §3.2.1 Nonblocking progress (wait/lock/obstruction-free; helping); FAA/xadd footnote — p. 47
- §3.3 The consensus hierarchy (CAS universal; FAA consensus number 2) — p. 51
- §4.2.2 Ticket lock; §4.3.1 MCS lock; §4.3.4 which lock (preemption table) — p. 67, 69, 78
- §4.5.1 Locality-conscious / cohort locking — p. 80
- §5.4 Combining; §8.9 elimination, FIFO elimination — p. 100, 180
- §6.2 Sequence locks; §6.3 RCU and grace periods — p. 111, 113
- §8.3.1 The M&S queue (dummy node, two-step enqueue, consistent snapshot) — p. 147
- §8.7 Safe memory reclamation (HP, EBR, IBR, NBR, Free Access/VBR) — p. 170
- §8.8 Dual data structures (reservations; Java Executor 2–10×) — p. 179

Full text: `Michael L. Scott, Trevor Brown - Shared-Memory Synchronization (2024, Springer)`

# Part II : Fetch-and-Add

## Fetch-and-Add (FAA) — the primitive the modern queue lineage runs on

Why this doc exists: the lock-free queue literature moved off compare-and-swap (CAS) and onto fetch-and-add (FAA) as its core synchronization primitive, and that shift is the single fact that explains why the post-2013 queues (LCRQ, SCQ, wCQ) scale where Michael & Scott (1996) does not. `docs/notes.md` already reaches for it — "reserve with `fetch_add` on the tail" — without naming the lineage. This is the named lineage, the hardware truth underneath it, and the map onto Anoptic's logger, event bus, and the cache-line-stripe idea.

Goes with `docs/logger.md` §2 (the per-thread-lanes vs shared-ring decision) and `docs/notes.md` §2.

---

### 1. The primitive

```
fetch_add(addr, v):   atomically  { old = *addr; *addr = old + v; return old; }
```

It returns the previous value, and that is the whole point: two threads that `fetch_add(&c, 1)` concurrently get two distinct results. FAA is a **ticket dispenser**: every caller leaves with a unique, monotonically increasing number, and none of them had to agree with anyone else to get it. That uniqueness is what a ring buffer, a queue, or a sequence counter is built on: "give me the next index" is exactly `fetch_add(&tail, 1)`.

FAA is one of a family of **unconditional** read-modify-write (RMW) operations — alongside `exchange` (XCHG, atomic swap), `fetch_or`, `fetch_and`. They all share the property that matters: they always succeed. Contrast CAS, the **conditional** RMW, which writes only if the location still holds an expected value and otherwise fails and reports back.

#### Hardware

| ISA | Instruction | Notes |
|-----|-------------|-------|
| x86-64 | `LOCK XADD` | single locked instruction; returns old value. Same cost class as `LOCK CMPXCHG` in isolation. |
| ARMv8.1-A (LSE) | `LDADD` / `LDADDA` / `LDADDAL` | true single-instruction atomic add with optional acquire/release. |
| ARMv8.0 (no LSE) | `LDAXR`/`STLXR` loop | there is no atomic-add instruction; the compiler emits an LL/SC retry loop, so "FAA" is *secretly a CAS-class loop here* and loses its contention advantage. |

The ARM caveat matters for this engine's targets. Apple Silicon (M1 and later) implements LSE, so FAA is a real single instruction on the macOS target. On generic aarch64, Clang/GCC default to "outline atomics" that runtime-detect LSE; for the Apple target build with `-mcpu=apple-m1` (or `-march=armv8.4-a`) so FAA inlines to `LDADD` instead of an LL/SC loop. On the Ryzen x86-64 target it is always `LOCK XADD`. Verify this in the disassembly before trusting any FAA-based scalability claim on ARM — the difference is invisible in C source and total in the generated code.

---

### 2. FAA vs CAS — the difference is entirely under contention

In isolation, on x86, `lock xadd` and `lock cmpxchg` cost about the same. The divergence appears only when N threads hit the same location at once.

```
CAS loop (e.g. claim a slot, M&S tail-swing):
    do {
        old = load(addr);
        new = f(old);
    } while (!compare_exchange(addr, &old, new));   // <-- retries on every lost race

FAA (claim the next index):
    ticket = fetch_add(addr, 1);                     // <-- no loop, ever
```

Under contention the CAS loop **retry-storms**: each thread reads, computes, attempts to write, discovers another thread changed the location, and starts over. With N contenders the worst case is O(N) attempts per thread — O(N²) total wasted work, and it gets worse as cores increase. FAA cannot retry: each `fetch_add` completes in a bounded number of steps no matter how many threads are present.

| | CAS | FAA |
|---|---|---|
| Conditionality | conditional — may fail | unconditional — always succeeds |
| Loop on the hot path | yes (retry) | no |
| Progress guarantee of one op | lock-free (the *system* progresses; an individual thread can starve) | wait-free (bounded steps, no starvation) |
| Behavior under heavy contention | throughput collapses (retry storm, superlinear waste) | throughput degrades gracefully (linear serialization) |
| ABA exposure | yes — value A→B→A defeats the compare; needs tags / hazard pointers | none on a monotonic counter (it never returns to a prior value, modulo sized wraparound) |
| Expresses "update only if unchanged" | yes — its whole job | no — cannot express conditionality |

The wait-free row is why FAA underlies the wait-free queue results: a bare FAA is already wait-free, so a queue whose common path is a single FAA is wait-free on that path for free.

---

### 3. The contention truth — FAA avoids *wasted* traffic

The most common misconception is that it sidesteps cache-coherency cost. It does not. The cache line holding the counter still has exactly one owner at a time under MESI; it still ping-pongs between cores, one acquisition per `fetch_add`. A single hot FAA counter is still a serialization point and still a scalability bottleneck.

The precise statement: FAA does *useful work on every line acquisition* (the core that owns the line completes its increment and leaves with a ticket), whereas a CAS loop *wastes line acquisitions* (a core acquires the line, reads, computes, tries to write, finds it stale, fails, and must acquire it again). FAA converts the unavoidable serialization into linear, productive serialization; CAS turns it into superlinear waste.

This is the direct motivation for the engine's cache-line-stripe idea (`notes.md` §2, the cache-line-stripe design). If one FAA = one unavoidable line bounce, then the way to scale is to make each bounce carry more payload: claim a 64-byte stripe with one FAA, fill it with N plain (non-atomic) stores, publish the whole stripe with one release. The per-item coherency cost falls to ~1/N. Striping amortizes against the coherency protocol. Same move as a sharded/striped counter (`LongAdder`-style) for the pure-counting case: when one FAA line is too hot, split it into per-core lines and sum on read.

---

### 4. What FAA is for

Use FAA (or another unconditional RMW) on the hot path when the operation is "take the next slot / the next ticket / set this bit":

- ring-buffer reserve (`fetch_add` the head/tail index) — queues, log lanes, event rings;
- ticket allocation — entity IDs, generational-handle indices, sequence numbers;
- ticket lock acquisition (§5);
- one-shot flags via `fetch_or` (claim-once semantics without a loop);
- producer pointer-swap via `exchange` (Vyukov's MPSC enqueues with a single XCHG on the tail — unconditional, wait-free for that step).

Do not reach for FAA when the operation is inherently conditional — "swing this pointer only if it still points where I last saw it":

- LIFO / Treiber stack — needs CAS to swing the head conditionally; FAA cannot express it;
- M&S linked-queue tail/head pointer updates — CAS;
- lock-free maps, trees, priority queues — CAS or stronger;
- any "compare X, update only if unchanged" — CAS by definition.

The decision heuristic: if the hot operation can be phrased as "atomically take a unique number," it is an FAA; if it must be phrased as "atomically replace A with B only if it is still A," it is a CAS. The modern queue designs (§5) are largely the art of *reframing* a queue so the hot path becomes the first kind and the rare slow path is the only place the second kind appears.

---

### 5. The queue lineage

Each step's one-line contribution. The through-line: push CAS off the common path and make the common path an FAA index into an array.

| Structure | Year | Core idea | What it fixed | Cite |
|-----------|------|-----------|---------------|------|
| Ticket lock | folklore | `t = FAA(&next,1); spin until serving==t`; release `serving++` | fair FIFO locking with one FAA, no CAS | — |
| M&S queue | 1996 | lock-free linked list, CAS on head and tail | the foundational lock-free FIFO | Michael & Scott, PODC'96 |
| Vyukov bounded MPMC | ~2010 | array ring, per-slot sequence number, CAS on position | bounded, no node reclamation; practitioner staple | Vyukov (1024cores) |
| LCRQ | 2013 | ring of cells; enqueue/dequeue **FAA** a counter to pick a cell, then a near-always-uncontended CAS on that cell; link rings with M&S at the rare boundary | moved the hot path from contended CAS to FAA; big scalability jump on x86 | Morrison & Afek, PPoPP'13 |
| WFqueue | 2016 | wait-free queue whose fast path is basically an FAA, with fast-path/slow-path helping | "wait-free as fast as fetch-and-add" — wait-freedom at FAA cost | Yang & Mellor-Crummey, PPoPP'16 |
| SCQ | 2019 | scalable circular queue; an indirection (a queue of indices) so **single-word** CAS suffices; livelock-free | killed LCRQ's double-width CAS (`cmpxchg16b`) portability dependence and its starvation cases; memory-efficient | Nikolaev, DISC'19 |
| wCQ | 2022 | SCQ + fast-path/slow-path helping for wait-freedom, bounded memory | wait-free with bounded memory, still FAA-cored | Nikolaev & Ravindran, SPAA'22 |

Two practitioner notes. LCRQ needs a 128-bit CAS (`cmpxchg16b` on x86) to swap a {value, index} cell atomically — fine on x86-64, a portability hazard elsewhere; SCQ's single-word-CAS reframing is the reason to prefer it for a cross-platform engine. And the wait-free entries (WFqueue, wCQ) get their wait-freedom from the Kogan-Petrank fast-path/ slow-path + helping methodology (PPoPP'11): run the cheap lock-free path normally, fall to a cooperative helping protocol only when a thread is detected to be starving, so the wait-free machinery almost never engages.

---

### 6. The empty-cell subtlety (and the dead-producer case)

FAA-reserve introduces a hazard: a thread can FAA a cell index and then stall — or die — before depositing its value, leaving a reserved-but-empty cell in the middle of the ring. A naive consumer that walks forward and stops at the first unfilled cell then wedges behind it forever, unable to distinguish a slow producer from a dead one. This is the "gap problem" `notes.md` flags.

LCRQ/SCQ solve it with **cycle numbers** (a generation tag per cell): the dequeuer reaching a cell that holds an older cycle than expected can mark it unsafe and advance past it. The record in that cell is lost, but the queue keeps flowing — no wedge. That correction is required for any ring whose producers can stall on real work between reserve and commit — the event bus. The logger (`docs/logger.md` §7) deliberately does *not* carry cycle numbers: its reserve→publish window holds no syscall, lock, or allocation — only a bounded copy and one release store — so a producer cannot block there, only be preempted, and that self-heals within a scheduler quantum. A genuinely dead producer would wedge the drain, but death in a window with nothing that can block is not a real failure mode, so the cheaper deterministic gap-stop (stop at the first uncommitted entry, resume next pass) suffices. Both designs still lose a record whose producer died before publishing it; neither async design can save it.

---

### 7. Mapping onto Anoptic

- `notes.md` §2 (the logger) — "reserve with `fetch_add` on the tail, publish with a per-slot commit marker." That instinct is the FAA-ring family, but the settled logger refines it differently from SCQ: it reserves a *variable* run of cache lines, so the reserve is a CAS-of-`need` (which can refuse-on-full), and it publishes a whole entry with one commit word. It needs no cycle numbers (§6) — the deterministic gap-stop suffices. The logger is where the discipline (relaxed-or-CAS reserve + release publish + acquire drain) is first exercised.
- The logger (`docs/logger.md`) — per-thread SPSC lanes need no shared FAA at all (each lane has one producer; the index is uncontended), but the logger chose the single shared ring anyway: claim order gives global order for free and it is one allocation, and a logger's contention on the one hot `tail` (a CAS) is rare and bounded. The two trade contention-freedom against strict global order — lanes win only where contention actually dominates (the event bus, below).
- The event bus — this is where a shared, ordered, high-throughput MPSC/MPMC actually pays, and therefore where SCQ/wCQ belong. Spend the FAA-queue complexity budget here.
- The cache-line-stripe structure (`notes.md` §2, the cache-line-stripe design) — §3 above is its justification: one FAA per stripe, N plain stores inside, one release publish; amortize the unavoidable line bounce over N items. The architect's novel structure is an FAA-batching design in the LCRQ/CRQ cell lineage, generalized to the coherency unit. The logger is the natural first exercise of this shape: each record reserves a run of cache lines, fills it with plain stores, and publishes with one release store — the stripe idea with a variable run length and a CAS reserve.
- Ticket allocation — entity IDs and generational-handle indices (`anoptic_ecs`) are FAA ticket dispensers; the same primitive, the easy case (allocation only, no dequeue, no gap).

---

### 8. C23 spelling

```c
#include <stdatomic.h>

// Pure index/ticket allocation: relaxed is enough — you need atomicity and uniqueness.
// Ordering is established separately at publication.
uint64_t ticket = atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed);

// Ring reserve + publish (SCQ-style single-cell shape; the logger reserves a variable run with CAS — docs/logger.md §5):
uint64_t i = atomic_fetch_add_explicit(&head, 1, memory_order_relaxed); // (1) claim index
slot_t *s = &ring[i & mask];
s->payload = ...;                                                        // (2) plain stores
atomic_store_explicit(&s->commit, i + 1, memory_order_release);         // (3) publish
// consumer pairs an acquire load of s->commit with (2): release/acquire = happens-before.

// Ticket lock, the canonical FAA structure:
uint64_t t = atomic_fetch_add_explicit(&next_ticket, 1, memory_order_relaxed);
while (atomic_load_explicit(&now_serving, memory_order_acquire) != t)
    cpu_relax();                       // PAUSE / YIELD
// critical section ...
atomic_store_explicit(&now_serving, t + 1, memory_order_release);
```

Memory-order discipline: the FAA that allocates an index can be `relaxed` — it carries no data, only a unique number. Visibility of the payload you write into the claimed slot is a *separate* contract, carried by a release store at publication and an acquire load at consumption (exactly the logger's `tail`/`commit` release and the consumer's acquire). Do not conflate "the index is unique" (FAA) with "the data is visible" (release/acquire); they are orthogonal and both required.

A single hot FAA line is still a bottleneck (§3). If profiling shows the counter line bouncing, the fix is structural — stripe it (per-core counters summed on read) or batch it (one FAA per stripe of N items).

---

### References

- Michael & Scott, "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms," PODC 1996. — the foundational lock-free FIFO (CAS-based).
- Kogan & Petrank, "Wait-Free Queues With Multiple Enqueuers and Dequeuers," PPoPP 2011. — the fast-path/slow-path + helping methodology behind the wait-free queues.
- Morrison & Afek, "Fast Concurrent Queues for x86 Processors," PPoPP 2013. — LCRQ/CRQ; the FAA-cored ring that started the modern lineage.
- Yang & Mellor-Crummey, "A Wait-free Queue as Fast as Fetch-and-Add," PPoPP 2016. — WFqueue.
- Nikolaev, "A Scalable, Portable, and Memory-Efficient Lock-Free FIFO Queue," DISC 2019. — SCQ; single-word-CAS, livelock-free, portable.
- Nikolaev & Ravindran, "wCQ: A Fast Wait-Free Queue with Bounded Memory Usage," SPAA 2022. — wait-free SCQ with bounded memory.
- Michael, "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects," IEEE TPDS 2004. — the reclamation problem that array/ring designs (Vyukov, SCQ) sidestep.
- Vyukov, bounded MPMC and intrusive MPSC queues, 1024cores.net. — the canonical practitioner designs `notes.md` already cites.
- Herlihy & Shavit, *The Art of Multiprocessor Programming*, 2nd ed., 2020. — the textbook treatment of RMW primitives, progress guarantees, and the universality of consensus number.

---

### Reading list / Papers Mentioned

Suggested order: it is the dependency order. Each builds on the last; read M&S first so the FAA pivot in LCRQ lands, then SCQ/wCQ as its refinements.

1. Michael & Scott, "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms," PODC 1996. The baseline — the CAS-based linked queue everything else is measured against. Read for: the two-CAS enqueue/dequeue and why a node-based queue drags the reclamation problem behind it. PDF: https://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
2. Vyukov, "Bounded MPMC queue," 1024cores (no paper; the canonical practitioner writeup). The array ring with per-slot sequence numbers — the shape SCQ formalizes. Read for: the per-cell sequence-number protocol, and Vyukov's own honesty that it is "not lock-free in the strict sense" (a stalled producer blocks its cell — the empty-cell hazard of §6). URL: https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
3. Morrison & Afek, "Fast Concurrent Queues for x86 Processors," PPoPP 2013. The FAA pivot — LCRQ/CRQ. Read for: FAA to pick a cell + an almost-always-uncontended per-cell CAS, rings linked by M&S at the rare boundary. This is the paper that introduces the FAA pivot. PDF: https://www.cs.tau.ac.il/~mad/publications/ppopp2013-x86queues.pdf
4. Kogan & Petrank, "Wait-Free Queues With Multiple Enqueuers and Dequeuers," PPoPP 2011. The helping methodology (fast-path/slow-path) that turns a lock-free queue wait-free cheaply. Read for: priority-based helping — faster threads complete slower threads' pending ops. PDF (mirror): https://scispace.com/pdf/wait-free-queues-with-multiple-enqueuers-and-dequeuers-2um6q9fmxh.pdf · metadata: https://dblp.org/rec/conf/ppopp/KoganP11.html
5. Yang & Mellor-Crummey, "A Wait-free Queue as Fast as Fetch-and-Add," PPoPP 2016. WFqueue — the title is the thesis. FAA-cored, wait-free via Kogan-Petrank helping, with a segmented linked list. Read for: how the wait-free machinery stays off the common path. PDF: https://chaoran.me/assets/pdf/wfq-ppopp16.pdf
6. Nikolaev, "A Scalable, Portable, and Memory-Efficient Lock-Free FIFO Queue," DISC 2019. SCQ — the one to actually implement for the event bus. Read for: the index-indirection that drops LCRQ's double-width CAS down to single-word CAS, ABA-safety without external reclamation, and the cycle-number skip that defeats the empty-cell wedge. PDF: https://rusnikola.github.io/files/ringpaper-disc.pdf · arXiv: https://arxiv.org/abs/1908.04511
7. Nikolaev & Ravindran, "wCQ: A Fast Wait-Free Queue with Bounded Memory Usage," SPAA 2022. SCQ made wait-free with bounded memory. Read for: the bounded-memory wait-free construction if hard progress guarantees ever matter (real-time tick). arXiv: https://arxiv.org/abs/2201.02179
8. Romanov & Koval, "The State-of-the-Art LCRQ Concurrent Queue Algorithm Does NOT Require CAS2," PPoPP 2023. LPRQ. Updates §5 of this doc: the "LCRQ needs 128-bit CAS" claim is true of the original 2013 design but no longer of the lineage — LPRQ matches LCRQ using only single-word CAS + FAA. So portability is no longer SCQ's exclusive advantage; pick between SCQ and LPRQ on benchmark. DOI: https://doi.org/10.1145/3572848.3577485
9. Michael, "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects," IEEE TPDS 2004. The reclamation problem that node-based queues (M&S, WFqueue) carry and that array/ring designs (Vyukov, SCQ) sidestep. Read it to understand *why* the field drifted to rings. PDF (mirror): https://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf

### Reference implementations — read the code

FAA queues in C (closest to what we will write):

- chaoran/fast-wait-free-queue — the single best repo to read. A benchmark harness containing WFqueue (`wfqueue.c`/`.h`), LCRQ (`lcrq.h`), M&S (`msqueue.c`), and CC-Queue side by side in plain C, all behind one interface. Start here. https://github.com/chaoran/fast-wait-free-queue
- rusnikola/lfqueue — SCQ, the author's own reference. The portable single-word-CAS design from paper 6. https://github.com/rusnikola/lfqueue
- rusnikola/wfqueue — wCQ, the author's own reference (paper 7). https://github.com/rusnikola/wfqueue
- cksystemsgroup/scal — a research harness with many queues incl. `lcrq.h`; useful for cross-comparing implementations of the same algorithm. https://github.com/cksystemsgroup/scal

Production bounded rings (how shipping systems actually use FAA / ticketing):

- facebook/folly `MPMCQueue.h` — a production ticket-based bounded MPMC: an atomic-increment ticket dispenser (FAA), exactly the §2 argument in shipping form. Its `ProducerConsumerQueue.h` is the SPSC analog of our render-bridge ring. https://github.com/facebook/folly/blob/main/folly/MPMCQueue.h
- DPDK `rte_ring` — the canonical high-throughput bounded ring in networking; prod/cons head-tail couples, reserve-then-commit. The design our slot-reserve mirrors at scale. https://github.com/DPDK/dpdk/blob/main/lib/ring/rte_ring.h · guide: https://doc.dpdk.org/guides/prog_guide/ring_lib.html
- crossbeam-rs `ArrayQueue` (Rust) — a clean, heavily-commented Vyukov bounded-MPMC port; the most readable version of paper 2's algorithm. Note the documented "not strictly lock-free" caveat — the empty-cell hazard, in production. https://github.com/crossbeam-rs/crossbeam/blob/master/crossbeam-queue/src/array_queue.rs
- couchbase/phosphor `mpmc_bounded_queue.h` and grivet/mpsc-queue — compact C/C++ ports of Vyukov's bounded MPMC and intrusive MPSC if you want them standalone. https://github.com/grivet/mpsc-queue

Teaching repos:

- pramalhe/ConcurrencyFreaks (Pedro Ramalhete) — Kogan-Petrank queue, M&S, hazard pointers and hazard eras, dozens of structures with blog writeups. The best place to read reclamation code. https://github.com/pramalhe/ConcurrencyFreaks

Logger designs (ties back to docs/logger.md — the lanes-vs-single-ring decision, and prior art to revisit for the ECS event streams and other live-system queues):

- choll/xtr — the closest single prior art to `docs/logger.md`: delegate all formatting and I/O to one background thread, minimize call-site work, deferred-format over a bounded ring. It diverges exactly where we did — per-sink SPSC lanes (the Quill side of the §2 decision) — and rests on two C++ tricks that do not port to C: a per-call-site function pointer the compiler generates from the argument types (so a no-arg log is "one pointer to the ring"), and fmtlib. Its one borrowable idea is the double-mapped "magic" ring buffer (`mirrored_memory_mapping`: map the buffer twice back-to-back so a wrapping variable-length record is contiguous, deleting spill/reassembly) — SPSC-coupled, so revisit it alongside the lanes question for the ECS/event streams. ~2 ns no-arg, C++20 + fmtlib, Linux/x86-leaning (TSC, optional io_uring). https://github.com/choll/xtr
- odygrd/quill — the per-thread SPSC lanes + single backend thread architecture, in production C++; the canonical realization of the lanes model the logger considered and rejected (§2) in favour of a single MPSC ring (order + simplicity over contention-freedom). Read `quill/core/` for the lane and the backend drain — the "lanes are an MPSC decomposed into wait-free SPSC" claim, shipping, and the design to reach for if a live system ever does need per-producer contention-freedom. https://github.com/odygrd/quill
- PlatformLab/NanoLog — Stanford's deferred-format logger: capture format-string id + raw args on the hot path, format on the consumer. This is the model `docs/logger.md` §5/§6 adopts — the buffered producer captures and the flusher formats; the C realization serializes args by walking `fmt` (no `va_list` survives the call) and re-emits per conversion on the backend. https://github.com/PlatformLab/NanoLog
- cameron314/concurrentqueue (moodycamel) — a widely-used C++ MPMC built from per-producer sub-queues: the lanes idea generalized to a queue. The logger chose a single ring instead (it wants global order and one allocation), but per-producer sharding is the production answer when contention and throughput dominate — i.e. the ECS event bus. https://github.com/cameron314/concurrentqueue
