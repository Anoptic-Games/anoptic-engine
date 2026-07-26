# The Anoptic Logger

The Anoptic logger lets any thread record a line of text without stalling the caller and without losing a line. You call it like `printf`. It formats your message, hands it to a single background thread that owns the output file, and returns. Under load that thread keeps the pipe drained, so frame work never waits on disk.

It is a singleton, one logger per program, owned by `main`. The whole interface lives in `include/anoptic_log.h`, every symbol prefixed `ano_log_`.

---

## Public API

### Severity and Route

```c
typedef enum { ANO_INFO, ANO_WARN, ANO_ERROR, ANO_FATAL } ano_loglevel_t;

typedef enum {
    ANO_FILE = 1 << 0,              // the output file (terminal when none is open)
    ANO_TERM = 1 << 1,              // the terminal: stdout, ERROR+ to stderr; ANSI-colored on a tty
    ANO_BOTH = ANO_FILE | ANO_TERM,
    ANO_NOW  = 1 << 2,              // synchronous: drain, write, fsync on the calling thread
} ano_logroute_t;
```

Severity says how bad. Route says where the line goes. 

### Usage Macros

```c
ano_log(ANO_INFO, "entity %u spawned at (%d,%d)", id, x, y);     // default route
ano_rlog(ANO_WARN, ANO_BOTH, "texture %s missing", name);        // explicit route
ano_olog(ANO_INFO, "chunk cache rebuilt");                       // + call site (file:line)
ano_rolog(ANO_ERROR, ANO_NOW, "device lost: %s", why);           // explicit route + call site
ano_debug_log(ANO_INFO, "loaded %d chunks in %.2f ms", n, ms);   // GONE outside DEBUG_BUILD
ano_debug_rlog(ANO_ERROR, ANO_NOW, "validation: %s", msg);       // DEBUG_BUILD explicit route
```

The default is a bare message: `ano_log` / `ano_rlog` record no call site. The `o` variants (`ano_olog`, `ano_rolog`, and their `debug` twins) capture the source file and line via `__FILE_NAME__` / `__LINE__` and prefix the message with `file.c:212:`. Sprinkle origin lines where they earn their bytes: one `olog` at the top of a subsystem's work, plain `ano_log` for the fifty lines that follow.

Two things to remember:
- The format string must be a compile-time string literal. The macros carry a `printf` format attribute, so the compiler type-checks your arguments against it. `ano_log(ANO_INFO, "%d", x)` is checked. `ano_log(ANO_INFO, some_char_ptr, x)` will not compile.
- Pass dynamic text as an argument: `"%s", dynamic`.
- `ano_debug_log` / `ano_debug_rlog` expand to `((void)0)` outside a `DEBUG_BUILD`, arguments included, so debug logging costs zero in a release build.

Buffered records ride the lock-free ring; the background thread routes each to its sinks.
`NOW` is synchronous: fatal line on disk before the process may die.

### Lifecycle

```c
int ano_log_init(void);     // start up; 0 on success
int ano_log_cleanup(void);  // shut down; 0 on success
```

Call `ano_log_init()` once at startup. It allocates the ring, captures a timestamp anchor, opens the default output file (`<game-dir>/logs/<session-stamp>_ano.log`; one file per session, stamp from `ano_fs_session_stamp()`, truncated at open), and spawns the background drain thread. Until it returns 0, only `NOW`-routed records work (to `stderr`). `ano_log_crash_init` (`anoptic_log_crash.h`, crash superset) prunes `logs/` at boot: newest 4 of each of `*_ano.log` and `*_CRASH.log` survive; the live session's files always kept.

Call `ano_log_cleanup()` once at shutdown. It stops and joins the drain thread, runs one final drain so nothing buffered is lost, then syncs and closes the file.

### Control

```c
int  ano_log_output_dir(const char *dir);                        // open dir/<stamp>_ano.log; 0 ok, -1 rejected
void ano_log_set_level(ano_loglevel_t min);                      // admit only buffered records >= min
void ano_log_set_route(ano_loglevel_t lvl, ano_logroute_t rt);   // rebind a level's default route
void ano_log_flush(void);                                        // drain synchronously, right now
```

`ano_log_output_dir` redirects output to a different directory. A rejected switch (bad or unopenable path) leaves the current file intact and returns `-1`. 
`ano_log_set_level` is the volume knob, and `NOW` records ignore it. 
`ano_log_set_route` must name at least one sink. With no output file configured, FILE records still drain to the terminal.

`ano_log_flush` you usually do not need; the background thread drains continuously. Use it for durability at a point: once-per-tick checkpoint, or before a risky op. Extra drain on the calling thread; returns when the buffer is empty.

### Raw entry points

The macros expand to one function; call it (or the `va_list` variant, for your own wrappers) directly when the macros don't fit:

```c
int ano_log_write (ano_loglevel_t lvl, ano_logroute_t rt, const char *file, int line, const char *fmt, ...);
int ano_log_vwrite(ano_loglevel_t lvl, ano_logroute_t rt, const char *file, int line, const char *fmt, va_list args);
```

A route of `0` (what `ano_log` passes) inherits the level's default. `file` is nullable: pass `NULL` (and any `line`) to record no call site, which is what `ano_log` / `ano_rlog` do; the origin is neither stored nor printed. The return value is almost always ignored: `0` means written or buffered normally, `1` means a full ring made the call wait for drain room.

---

## Using it correctly

A complete, minimal program shape:

```c
#include <anoptic_log.h>

int main(void) {
    ano_log_init();                                 // once, before any logging
    ano_log_output_dir("mylogs");                   // optional: mylogs/<stamp>_ano.log
    ano_log_set_route(ANO_WARN, ANO_BOTH);          // optional: warnings on the terminal too

    ano_log(ANO_INFO, "engine up, build %s", VERSION);

    while (running) {
        // ... worker threads call ano_log freely ...
        tick();
        ano_log_flush();                            // optional: checkpoint each tick
    }

    // REQUIRED before cleanup: stop your own threads so nothing calls ano_log_* again
    ano_log_cleanup();                              // once, at the end
    return 0;
}
```

The rules, in full:

1. Initialise before use, clean up after the workers are gone. Those are the only ordering constraints.
2. Any thread may call `ano_log_*` concurrently, except `ano_log_cleanup`, which is single-owner and requires the producers stopped.
3. Format strings are literals. Dynamic text goes through `%s`.
4. You rarely call `flush`. Reach for it only when you need durability at a specific instant.
5. Use `ANO_FATAL` (or an explicit `ANO_NOW`) for lines that must survive a crash. Let the ring carry everything else.

Output lines look like this: wall-clock time, level, then your message, with the call site in between when the record came from an `o` macro:

```
14:01:43 INFO  entity 4 spawned at (10,-3)
14:01:44 WARN  world.c:212:  chunk 9 took 40ms to load
```

---

## Underneath: lock-free MPSC ring

MPSC: multi-producer, single-consumer.

Producers are your threads. A worker formats on its own stack (no shared state), reserves ring slots with one atomic on the tail, copies bytes in, publishes with one release store. No lock; common path no wait. Formatting is thread-local; shared work is a couple of atomics.

The consumer is a logger-owned thread. It walks the ring in claim order, gathers a pass of lines, writes them in one `write` (many lines, one syscall). Idle: parks briefly. Under load: stays hot, keeps the ring empty.

One ring, one consumer: claim order is a single total order. Lines from different threads interleave; each appears in issue order; the file is FIFO across the program.

Bounded ring → backpressure. A full ring makes the producer wait for drain (throttle to disk speed). Nothing silently dropped. (Wedged consumer: a stalled producer eventually writes its line straight through.)

Most records enter the ring as finished text (eager format on the producer). Fast path can defer: capture args, render on the drain thread. Disk bytes match `printf` either way.

What you can rely on:

- No loss. Every accepted record reaches the file (or the console, with no file configured).
- Order. Records through the ring appear in issue order. `NOW`-routed lines drain the ring first, then write out of band.
- Non-blocking common path. A normal log call returns without waiting on the consumer or the disk.

What you must hold up your end of:

- Initialise before logging; stop the producers before cleanup.
- Keep format strings literal.

That is the entire contract. Ring sizing, timestamps, batching, draining: logger-owned.
