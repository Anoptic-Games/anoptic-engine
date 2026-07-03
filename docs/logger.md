# The Anoptic Logger

The Anoptic logger lets any thread record a line of text without stalling the caller and without losing a line. You call it like `printf`. It formats your message, hands it to a single background thread that owns the output file, and returns. Under load that thread keeps the pipe drained, so frame work never waits on disk.

It is a singleton, one logger per program, owned by `main`. The whole interface lives in `include/anoptic_logging.h`, every symbol prefixed `ano_log_`.

---

## The interactive surface

### Severity levels

```c
typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL } log_types_t;
```

The levels are ordered. A runtime gate admits everything at or above a chosen minimum, so you can lower the volume in production without touching call sites.

### The call-site macros — what you actually use

Day to day you use these macros:

```c
ano_log_debug("loaded %d chunks in %.2f ms", n, ms);   // compiled out unless DEBUG_BUILD
ano_log_info ("entity %u spawned at (%d,%d)", id, x, y);
ano_log_warn ("texture %s missing, using fallback", name);
ano_log_error("vkAcquireNextImage failed: %d", err);
ano_log_fatal("arena exhausted, %zu bytes requested", want);
```

Each macro captures the source file and line (via `__FILE_NAME__` / `__LINE__`) and forwards the rest. Two things to remember:

- The format string must be a compile-time string literal. The macros carry a `printf` format attribute, so the compiler type-checks your arguments against it. `ano_log_info("%d", x)` is checked. `ano_log_info(some_char_ptr, x)` will not compile. Pass dynamic text as an argument: `ano_log_info("%s", dynamic)`.
- `ano_log_debug` evaluates to nothing outside a `DEBUG_BUILD`, arguments included, so debug logging costs zero in a release build.

`debug`, `info`, `warn`, and `error` are buffered through the lock-free ring and written by the background thread. `fatal` is synchronous (see "immediate" below), because you want a fatal line on disk before the process possibly dies.

### Lifecycle

```c
int ano_log_init(void);     // build up; 0 on success
int ano_log_cleanup(void);  // tear down; 0 on success
```

Call `ano_log_init()` once at startup, before anyone logs. It allocates the ring, captures a timestamp anchor, opens the default output file (`<game-dir>/anoptic.log`), and spawns the background drain thread. Until it returns 0, only the immediate path works, writing to `stderr`.

Call `ano_log_cleanup()` once at shutdown. It stops and joins the drain thread, runs one final drain so nothing buffered is lost, then syncs and closes the file. The one hard rule in the API lives here: every producer thread must have stopped before you call cleanup. Logging concurrently with cleanup is undefined.

### Control

```c
int  ano_log_output_dir(const char *dir);  // open dir/anoptic.log as the output; 0 ok, -1 rejected
void ano_log_set_level(log_types_t min);   // admit only records at or above `min`
void ano_log_flush(void);                  // drain synchronously, on the calling thread, right now
```

`ano_log_output_dir` redirects output to a different directory. A rejected switch (bad or unopenable path) leaves the current file intact and returns `-1`. `ano_log_set_level` is the volume knob. With no output file configured, records still drain to the console.

`ano_log_flush` you usually do not need. The background thread drains the ring continuously. Reach for `flush` when you want everything logged so far on disk now: a once-per-tick checkpoint, or just before a risky operation. It runs an extra drain pass synchronously on the calling thread and returns when the buffer is empty.

### Raw entry points

The macros expand to these; call them directly only if you are building your own wrappers:

```c
int  ano_log_enqueue  (log_types_t lvl, const char *file, int line, const char *fmt, ...);
void ano_log_immediate(log_types_t lvl, const char *file, int line, const char *fmt, ...);
```

`ano_log_enqueue` is the buffered path. Its return value is almost always ignored: `0` means buffered normally, `1` means the ring was full so the call waited for the drain to free room before buffering. It never drops. `ano_log_immediate` formats and writes the line straight through on the calling thread, fsyncs it, and is what `ano_log_fatal` uses.

---

## Using it correctly

A complete, minimal program shape:

```c
#include <anoptic_logging.h>

int main(void) {
    ano_log_init();                       // once, before any logging
    ano_log_output_dir("logs");           // optional: logs/anoptic.log
    ano_log_set_level(LOG_INFO);          // optional: drop DEBUG

    ano_log_info("engine up, build %s", VERSION);

    while (running) {
        // ... worker threads call ano_log_* freely ...
        tick();
        ano_log_flush();                  // optional: checkpoint each tick
    }

    join_all_workers();                   // REQUIRED before cleanup
    ano_log_cleanup();                    // once, at the end
    return 0;
}
```

The rules, in full:

1. Initialise before use, clean up after the workers are gone. Those are the only ordering constraints.
2. Any thread may call `ano_log_*` concurrently, except `ano_log_cleanup`, which is single-owner and requires the producers stopped.
3. Format strings are literals. Dynamic text goes through `%s`.
4. You rarely call `flush`. Reach for it only when you need durability at a specific instant.
5. Use `fatal` for lines that must survive a crash. Use `info`/`warn`/`error` for everything else and let the ring carry them.

Output lines look like this: wall-clock time, level, call site, then your message:

```
14:01:43 INFO  world.c:212:  entity 4 spawned at (10,-3)
```

---

## What it is, underneath: a lock-free MPSC ring

MPSC stands for multi-producer, single-consumer, and that shape is the whole design.

The many producers are your threads. When a worker logs, it formats the line on its own stack, touching no shared state, then reserves a small run of slots in the shared ring with one atomic op on the tail cursor, copies the bytes in, and publishes with one release store. No lock, and on the common path no wait. That is what keeps a logging call from holding up a frame: the expensive part (formatting) is thread-local, the shared part is a couple of atomics.

The single consumer is a thread the logger owns. It walks the ring in claim order, gathers a whole pass of lines, and writes them with one `write` call: many lines, one syscall. With nothing to drain it parks briefly, so an idle logger costs nothing. Under load it stays hot and keeps the ring empty.

Because there is one ring and one consumer, claim order is a single total order. Lines from different threads interleave but each appears in issue order, and the file is FIFO across the whole program.

The ring is bounded, so it can fill. The answer is backpressure: a producer that finds no room waits for the consumer to drain some, self-throttling to disk speed. Nothing is silently dropped. (If the consumer is ever wedged, a stalled producer eventually writes its own line straight through rather than blocking forever, a safety valve.)

Formatting shapes latency. Most records carry finished text into the ring, formatted eagerly by the producer. The fast path can also defer formatting, capturing the arguments and rendering them on the drain thread, which trims the producer's cost further. Either way the result on disk is byte-for-byte what `printf` would produce. The choice is only about where the work happens.

What you can rely on:

- No loss. Every accepted record reaches the file (or the console, with no file configured).
- Order. Records through the ring appear in issue order. `immediate`/`fatal` lines are written out of band and may interleave with still-buffered records.
- Non-blocking common path. A normal log call returns without waiting on the consumer or the disk.

What you must hold up your end of:

- Initialise before logging; stop the producers before cleanup.
- Keep format strings literal.

That is the entire contract. Everything else (ring sizing, timestamps, batching, draining) the logger handles on its own.
