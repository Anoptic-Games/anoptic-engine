/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Lock-free MPSC ring logger. A producer formats on its stack, reserves cache lines with one CAS on
// `tail`, copies the text, and publishes with one release store of `tag`. The common path never waits.
// The logger owns one consumer thread that drains the ring continuously. ano_log_flush also drains inline
// for callers needing records on disk now. Cold paths take small locks: a drain mutex and an output-file
// mutex. FATAL writes straight through. A full ring makes the producer wait for room, never dropping.
// Stop all producers before ano_log_cleanup.

#include "logging/logging_ring.h"

#include <anoptic_threads.h>
#include <anoptic_filesystem.h>
#include <anoptic_time.h>

#include <mimalloc.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>


/* Internal state. Only the ring is producer-shared, the rest is cold. */

// Level names padded to 5, so the prefix is one fixed-length copy with no strlen or pad loop. Index by
// log_types_t. Out-of-range falls back to "?????".
static const char   logPad[5][8] = {"DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};

static log_ring_t   g_ring;         // the shared MPSC ring (producers: tail, consumer: head)
static atomic_bool  g_initialized;  // immediate-path liveness (cold path only)
// Severity gate and liveness in one relaxed load on enqueue. INT_MAX until init, back to INT_MAX at
// cleanup, so a not-live logger gates every enqueue.
static atomic_int   g_minLevel = INT_MAX;

static anothread_mutex_t g_drainMtx;   // gates the single consumer: one drainer at a time
static anothread_mutex_t g_outFileMtx; // guards the output file handle's lifecycle and writes
static ano_file    *g_outFile;         // the open output file, or NULL

// The owned consumer thread. Drains the ring continuously so producers stay lock-free.
// g_drainRun gates its loop, cleared at cleanup before the join.
static anothread_t  g_drainThread;
static atomic_bool  g_drainRun;

// Park/wake for the owned consumer. After an empty pass the drainer parks on g_wakeCv and producers
// signal it on the empty->nonempty transition, so the ring drains at wake latency instead of a fixed
// poll period. g_drainerParked keeps the producer's check to one relaxed load when the drainer is
// awake (the common case under load). The park is a timedwait capped at DRAIN_PARK_US: a producer's
// tag publish (release store) and its g_drainerParked load can reorder against the drainer's
// parked-store + head-recheck (the classic store-buffering interleaving), losing one wakeup. The cap
// bounds that loss; it is an emission-latency bound, not a correctness gate, and ano_log_flush stays
// synchronous regardless.
static anothread_mutex_t g_wakeMtx;
static anothread_cond_t  g_wakeCv;
static atomic_bool       g_drainerParked;
#define DRAIN_PARK_US     1000u     // park cap: worst-case emission delay on a lost wakeup

// Full-ring producer backoff: escalate the spin between head rechecks from MIN to MAX (doubling),
// snapping back to MIN whenever head advances. Short stalls stay responsive, long stalls get off
// the consumer's cache lines. FULL_STALL_LIMIT counts rechecks with head frozen before declaring
// the consumer wedged; at the capped spin that is tens of ms of zero progress, a catastrophic
// fallback only (a producer died mid-publish and the drainer cannot pass its gap).
#define FULL_BACKOFF_MIN_NS 64u
#define FULL_BACKOFF_MAX_NS 8192u
#define FULL_STALL_LIMIT    4096u

// Writer set, bound once when the file opens or closes, so the write path never tests g_outFile.
// g_persist writes a buffer (file else console). g_syncOut fsyncs or no-ops. g_haveFile gates the echo.
static void (*g_persist)(const void *data, size_t len);
static void (*g_syncOut)(void);
static bool         g_haveFile;

// Timestamp anchor, a ticks/unix-ns pair captured once at init. The producer stamps bare ticks. The
// drainer adds the anchor and converts to a wall-clock second, so the per-record division stays off the
// hot path.
static uint64_t     g_anchorTicks;
static uint64_t     g_anchorUnixNs;

// Drainer-private, touched only under g_drainMtx. g_scratch gathers a seam-straddling record. g_batch
// holds a whole drain pass for one write. g_drainHMS caches "HH:MM:SS" so civil-time conversion runs
// once per second.
static char         g_scratch[ANO_LOG_MSG_MAX];
static char        *g_batch;
static size_t       g_batchCap;
static uint64_t     g_drainSec;
static char         g_drainHMS[8];
static bool         g_drainHMSValid;

/* Formatting (eager, on the producer) */

// Decimal of v at p, advance. No printf machinery.
static inline char *put_u32(char *p, uint32_t v)
{
    char tmp[10];
    int i = 0;
    do { tmp[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) *p++ = tmp[--i];
    return p;
}

static const char digLo[] = "0123456789abcdef";
static const char digUp[] = "0123456789ABCDEF";

// Unsigned v in base (8/10/16) via digits, into [p,end). NULL if it would overrun.
static char *put_base(char *p, char *end, unsigned long long v, unsigned base, const char *digits)
{
    char tmp[24];
    int  i = 0;
    do { tmp[i++] = digits[v % base]; v /= base; } while (v);
    if (p + i > end) return NULL;
    while (i) *p++ = tmp[--i];
    return p;
}

// Hand-rolled formatter for the flagless common conversions (d i u x X o c s, l/ll/z/t/h length mods).
// Returns bytes written, or -1 to fall back to vsnprintf for anything else (flags, width, precision,
// floats, %p, unknown, overrun). Matches printf byte-for-byte for what it accepts.
static int fast_format(char *out, int cap, const char *fmt, va_list ap)
{
    char *p = out, *end = out + cap;
    const char *f = fmt;
    while (*f) {
        if (*f != '%') {                                   // literal run: one memcpy to the next %
            const char *s = f;
            do { ++f; } while (*f && *f != '%');
            size_t n = (size_t)(f - s);
            if (p + n > end) return -1;
            memcpy(p, s, n); p += n;
            continue;
        }
        char c = *++f;
        if (c == '%') { if (p >= end) return -1; *p++ = '%'; ++f; continue; }
        int lng = 0;                                       // 0 int, 1 long, 2 long long
        while (c == 'l') { ++lng; c = *++f; }
        if (c == 'z' || c == 't') { lng = sizeof(size_t) == sizeof(long long) ? 2 : 1; c = *++f; }
        else while (c == 'h') c = *++f;                    // sub-int args promote to int anyway
        switch (c) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long)
                        : lng == 1 ? va_arg(ap, long) : (long long)va_arg(ap, int);
            if (v < 0) { if (p >= end) return -1; *p++ = '-'; }
            unsigned long long m = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
            p = put_base(p, end, m, 10, digLo);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long)
                                 : lng == 1 ? va_arg(ap, unsigned long)
                                            : (unsigned long long)va_arg(ap, unsigned int);
            unsigned base = c == 'o' ? 8u : c == 'u' ? 10u : 16u;
            p = put_base(p, end, v, base, c == 'X' ? digUp : digLo);
            break;
        }
        case 'c': { int ch = va_arg(ap, int); if (p >= end) return -1; *p++ = (char)ch; break; }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            while (*s) { if (p >= end) return -1; *p++ = *s++; }
            break;
        }
        default: return -1;                                // flag/width/precision/float/%p/unknown
        }
        if (p == NULL) return -1;                          // put_base overran
        ++f;                                               // past the conversion char
    }
    return (int)(p - out);
}

// Compose "<LEVEL> <file>:<line>:  <message>" into out, no time prefix, no newline. The flusher adds the
// time at emit. Returns the length clamped to cap-1 so an over-long line never overruns the entry. The
// prefix is hand-rolled. The message goes through fast_format, falling back to vsnprintf for conversions
// it cannot handle. format(printf, 6, 0) marks a printf forwarder so the vsnprintf passes -Wformat-nonliteral.
__attribute__((format(printf, 6, 0)))
static int format_line(char *out, int cap, log_types_t level,
                       const char *file, int line, const char *fmt, va_list ap)
{
    char *p = out;
    memcpy(p, (unsigned)level <= LOG_FATAL ? logPad[level] : "?????", 5);  // fixed 5, no strlen/pad
    p += 5;
    *p++ = ' ';
    size_t fl = strnlen(file, 256);         // bounded scan: a freak file name can't crowd out the message
    memcpy(p, file, fl); p += fl;
    *p++ = ':';
    p = put_u32(p, (uint32_t)(line < 0 ? 0 : line));
    *p++ = ':'; *p++ = ' '; *p++ = ' ';
    int head = (int)(p - out);              // bounded <= 5+1+256+1+10+3, far under cap
    va_list apc; va_copy(apc, ap);
    int body = fast_format(out + head, cap - head, fmt, ap);            // common conversions, no libc
    if (body < 0)                                                       // flags/width/float/overrun:
        body = vsnprintf(out + head, (size_t)(cap - head), fmt, apc);   //   fall back, forwarded
    va_end(apc);
    if (body < 0) body = 0;
    int total = head + body;
    return total < cap ? total : cap - 1;   // clamp: never overrun the entry
}


/* Deferred formatting prototype: capture at the call site, render at drain. */

// Capture blob: [const char *file][int line][const char *fmt][args...]. file and fmt are string literals
// stored as pointers. Each conversion's args are captured by type parsed from fmt: any '*' width or
// precision as an int, then the value (int/long/long long, unsigned forms, double, char, void*, or a
// NUL-terminated string copy). Returns blob length, or -1 to bail to eager for %n, a long-double 'L', or
// a wide %lc/%ls. Stores raw values only, the actual formatting happens at drain.
static int capture_deferred(char *out, int cap, const char *file, int line, const char *fmt, va_list ap)
{
    char *p = out, *end = out + cap;
    memcpy(p, &file, sizeof file); p += sizeof file;
    memcpy(p, &line, sizeof line); p += sizeof line;
    memcpy(p, &fmt,  sizeof fmt);  p += sizeof fmt;
    for (const char *f = fmt; *f; ++f) {
        if (*f != '%') continue;
        ++f;
        if (*f == '%') continue;
        int prec = -1;  // %s must honor precision AT CAPTURE: %.*s bytes need not be NUL-terminated
        while (*f == '-' || *f == '+' || *f == ' ' || *f == '#' || *f == '0') ++f;   // flags
        if (*f == '*') { int w = va_arg(ap, int); if (p + 4 > end) return -1; memcpy(p, &w, 4); p += 4; ++f; }
        else while (*f >= '0' && *f <= '9') ++f;                                       // width
        if (*f == '.') {                                                              // precision
            ++f;
            if (*f == '*') {
                int pr = va_arg(ap, int); if (p + 4 > end) return -1; memcpy(p, &pr, 4); p += 4; ++f;
                if (pr >= 0) prec = pr;   // negative = as if omitted (C11 7.21.6.1)
            } else {
                prec = 0;
                while (*f >= '0' && *f <= '9') { prec = prec * 10 + (*f - '0'); ++f; }
            }
        }
        int lng = 0;                                                                 // length modifier
        if (*f == 'L') return -1;                                                     // long double: eager
        while (*f == 'l') { ++lng; ++f; }
        if (*f == 'z' || *f == 't' || *f == 'j') { lng = 2; ++f; }
        while (*f == 'h') ++f;
        switch (*f) {
        case 'd': case 'i':
            if (lng >= 2)      { long long v = va_arg(ap, long long); if (p+8>end) return -1; memcpy(p,&v,8); p+=8; }
            else if (lng == 1) { long long v = va_arg(ap, long);      if (p+8>end) return -1; memcpy(p,&v,8); p+=8; }
            else               { int v = va_arg(ap, int);             if (p+4>end) return -1; memcpy(p,&v,4); p+=4; }
            break;
        case 'u': case 'o': case 'x': case 'X':
            if (lng >= 2)      { unsigned long long v = va_arg(ap, unsigned long long); if (p+8>end) return -1; memcpy(p,&v,8); p+=8; }
            else if (lng == 1) { unsigned long long v = va_arg(ap, unsigned long);      if (p+8>end) return -1; memcpy(p,&v,8); p+=8; }
            else               { unsigned v = va_arg(ap, unsigned);                     if (p+4>end) return -1; memcpy(p,&v,4); p+=4; }
            break;
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
            double v = va_arg(ap, double); if (p+8>end) return -1; memcpy(p,&v,8); p+=8; break;
        }
        case 'c': { int v = va_arg(ap, int);   if (lng) return -1; if (p+4>end) return -1; memcpy(p,&v,4); p+=4; break; }
        case 'p': { void *v = va_arg(ap, void*); if (p+8>end) return -1; memcpy(p,&v,8); p+=8; break; }
        case 's': {
            if (lng) return -1;                                                       // wide string: eager
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            size_t sl = prec >= 0 ? strnlen(s, (size_t)prec) : strlen(s);
            if (sl > 0xffffu) sl = 0xffffu;
            if (p + 2 + (ptrdiff_t)sl + 1 > end) return -1;
            uint16_t sl16 = (uint16_t)sl; memcpy(p,&sl16,2); p += 2;
            memcpy(p, s, sl); p += sl; *p++ = '\0';                                    // NUL for the drain snprintf
            break;
        }
        default: return -1;   // %n or unknown: eager fallback
        }
    }
    return (int)(p - out);
}

// Render a capture blob at drain: prefix (level/file/line) then the message. Re-parses fmt, rebuilds each
// conversion's spec with any '*' resolved from the captured ints, and lets snprintf do the actual format
// from the captured value. Byte-for-byte printf for everything capture_deferred accepts. Returns bytes.
static int format_deferred(char *out, int cap, log_types_t level, const char *blob)
{
    const char *b = blob;
    const char *file; memcpy(&file, b, sizeof file); b += sizeof file;
    int line;         memcpy(&line, b, sizeof line); b += sizeof line;
    const char *fmt;  memcpy(&fmt,  b, sizeof fmt);  b += sizeof fmt;

    char *p = out, *end = out + cap;
    memcpy(p, (unsigned)level <= LOG_FATAL ? logPad[level] : "?????", 5); p += 5;
    *p++ = ' ';
    size_t fl = strnlen(file, 256); memcpy(p, file, fl); p += fl;
    *p++ = ':';
    p = put_u32(p, (uint32_t)(line < 0 ? 0 : line));
    *p++ = ':'; *p++ = ' '; *p++ = ' ';

    for (const char *f = fmt; *f; ++f) {
        if (*f != '%') { if (p < end) *p++ = *f; continue; }
        ++f;
        if (*f == '%') { if (p < end) *p++ = '%'; continue; }
        // Fast plain path: % [l|ll|z|t|j](d i u o x X c s), no flags/width/precision/h. Hand-rolled, no
        // spec build, no libc. The common case, what keeps the drain fast.
        {
            const char *g = f;
            int plng = 0;
            while (*g == 'l') { ++plng; ++g; }
            if (*g == 'z' || *g == 't' || *g == 'j') { plng = 2; ++g; }
            char pc = *g;
            if ((pc=='d'||pc=='i'||pc=='u'||pc=='o'||pc=='x'||pc=='X'||pc=='c'||pc=='s') && end - p > 1) {
                if (pc == 'd' || pc == 'i') {
                    long long v;
                    if (plng >= 1) { memcpy(&v, b, 8); b += 8; } else { int iv; memcpy(&iv, b, 4); b += 4; v = iv; }
                    if (v < 0 && p < end) *p++ = '-';
                    unsigned long long m = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
                    char *q = put_base(p, end, m, 10, digLo); if (q) p = q;
                } else if (pc == 'c') {
                    int iv; memcpy(&iv, b, 4); b += 4; if (p < end) *p++ = (char)iv;
                } else if (pc == 's') {
                    uint16_t sl; memcpy(&sl, b, 2); b += 2;
                    if (p + sl <= end) { memcpy(p, b, sl); p += sl; } b += (size_t)sl + 1;
                } else {
                    unsigned long long v;
                    if (plng >= 1) { memcpy(&v, b, 8); b += 8; } else { unsigned uv; memcpy(&uv, b, 4); b += 4; v = uv; }
                    unsigned base = pc == 'o' ? 8u : pc == 'u' ? 10u : 16u;
                    char *q = put_base(p, end, v, base, pc == 'X' ? digUp : digLo); if (q) p = q;
                }
                f = g;
                continue;
            }
        }

        // Fancy: flags/width/precision/'*'/h/float/%p. Rebuild the spec with '*' resolved, then snprintf.
        char spec[48];
        int  si = 0;
        spec[si++] = '%';
#define SPEC_PUT(ch) do { if (si < (int)sizeof spec - 2) spec[si++] = (ch); } while (0)
        while (*f=='-'||*f=='+'||*f==' '||*f=='#'||*f=='0') SPEC_PUT(*f++);
        if (*f == '*') {                                                             // width from arg
            int w; memcpy(&w, b, 4); b += 4; ++f;
            if (w < 0) { SPEC_PUT('-'); w = -w; }                                     // negative = left justify
            if (w != 0) { char *sp = put_u32(spec + si, (uint32_t)w); si = (int)(sp - spec); }  // 0 = no width
        } else while (*f >= '0' && *f <= '9') SPEC_PUT(*f++);
        if (*f == '.') {                                                            // precision
            ++f;
            if (*f == '*') {
                int pr; memcpy(&pr, b, 4); b += 4; ++f;
                if (pr >= 0) { SPEC_PUT('.'); char *sp = put_u32(spec + si, (uint32_t)pr); si = (int)(sp - spec); }
            } else { SPEC_PUT('.'); while (*f >= '0' && *f <= '9') SPEC_PUT(*f++); }
        }
        int lng = 0;
        while (*f == 'l') { ++lng; SPEC_PUT(*f++); }
        if (*f == 'z' || *f == 't' || *f == 'j') { lng = 2; SPEC_PUT(*f++); }
        while (*f == 'h') SPEC_PUT(*f++);
        char c = *f; SPEC_PUT(c); spec[si] = '\0';
#undef SPEC_PUT
        int rem = (int)(end - p);
        if (rem <= 1) break;

        int wrote = 0;
// GCC spelling: honored by both gcc and clang (clang aliases GCC diagnostic
// pragmas); the clang spelling is invisible to gcc, whose -Werror build breaks.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
        switch (c) {
        case 'd': case 'i':
            if (lng >= 2)      { long long v; memcpy(&v, b, 8); b += 8; wrote = snprintf(p, (size_t)rem, spec, v); }
            else if (lng == 1) { long long v; memcpy(&v, b, 8); b += 8; wrote = snprintf(p, (size_t)rem, spec, (long)v); }
            else               { int v; memcpy(&v, b, 4); b += 4;       wrote = snprintf(p, (size_t)rem, spec, v); }
            break;
        case 'u': case 'o': case 'x': case 'X':
            if (lng >= 2)      { unsigned long long v; memcpy(&v, b, 8); b += 8; wrote = snprintf(p, (size_t)rem, spec, v); }
            else if (lng == 1) { unsigned long long v; memcpy(&v, b, 8); b += 8; wrote = snprintf(p, (size_t)rem, spec, (unsigned long)v); }
            else               { unsigned v; memcpy(&v, b, 4); b += 4;          wrote = snprintf(p, (size_t)rem, spec, v); }
            break;
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
            { double v; memcpy(&v, b, 8); b += 8; wrote = snprintf(p, (size_t)rem, spec, v); } break;
        case 'c': { int v; memcpy(&v, b, 4); b += 4; wrote = snprintf(p, (size_t)rem, spec, v); } break;
        case 'p': { void *v; memcpy(&v, b, 8); b += 8; wrote = snprintf(p, (size_t)rem, spec, v); } break;
        case 's': { uint16_t sl; memcpy(&sl, b, 2); b += 2; const char *s = b; b += (size_t)sl + 1;
                    wrote = snprintf(p, (size_t)rem, spec, s); } break;
        default: break;
        }
#pragma GCC diagnostic pop
        if (wrote < 0) wrote = 0;
        if (wrote > rem - 1) wrote = rem - 1;   // snprintf truncated to rem-1 chars plus its NUL
        p += wrote;
    }
    return (int)(p - out);
}

// Two digits (00-99) at p, advance. No printf machinery.
static inline char *put2(char *p, int v)
{
    *p++ = (char)('0' + (v / 10) % 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

// Render "HH:MM:SS" (8 bytes) for wall-clock second `sec` into out8. Hand-rolled, no snprintf.
static void render_hms(char *out8, uint64_t sec)
{
    ano_datetime t = ano_localtime((int64_t)sec);
    char *p = put2(out8, t.hour);   *p++ = ':';
    p      = put2(p, t.minute);     *p++ = ':';
    (void)   put2(p, t.second);
}

// Wall-clock second for a record's ticks, through the init anchor. The ticks->ns division is deferred
// to here, off the producer's hot path.
static inline uint64_t wall_second(uint64_t ticks)
{
    return (g_anchorUnixNs + ano_ticks_to_ns(ticks - g_anchorTicks)) / 1000000000ull;
}

// Render the 8-byte "HH:MM:SS" prefix for a record's raw ticks. Immediate path only. The drain path
// memoizes per second instead (see drain_and_emit).
static int render_walltime(char *out, uint64_t ticks)
{
    render_hms(out, wall_second(ticks));
    return 8;
}


/* Output file (under g_outFileMtx) */

// Open <dir>/anoptic.log for append, NULL on failure.
static ano_file *open_log(const char *dir)
{
    char path[MAXPATH];
    int n = snprintf(path, sizeof path, "%s/%s", dir, ANO_LOG_FILENAME);
    return (n > 0 && n < (int)sizeof path) ? ano_fs_open_append(path) : NULL;
}

// The two persist targets, bound by select_output() when the file opens or closes. The console one
// only fires when the default file failed to open.
static void persist_file(const void *data, size_t len)
{
    if (ano_fs_write(g_outFile, data, len) != 0)
        fwrite(data, 1, len, stderr);   // write failed, fall back to stderr
}
static void persist_console(const void *data, size_t len) { fwrite(data, 1, len, stdout); }
static void sync_file(void) { ano_fs_sync(g_outFile); }
static void sync_none(void) { }

// Bind the writer set to the current output file (or the console when none). Caller holds g_outFileMtx.
static void select_output(void)
{
    g_haveFile = (g_outFile != NULL);
    g_persist  = g_haveFile ? persist_file : persist_console;
    g_syncOut  = g_haveFile ? sync_file    : sync_none;
}

// Echo a finished line to the console by severity (>WARN to stderr, else stdout). Immediate path only.
static inline void echo_console(const char *line, size_t len, log_types_t level)
{
    fwrite(line, 1, len, (level > LOG_WARN) ? stderr : stdout);
}

// Write a finished batch to the chosen output, one locked call, no per-write target test.
static void write_batch(const char *data, size_t len)
{
    if (len == 0)
        return;
    ano_mutex_lock(&g_outFileMtx);
    g_persist(data, len);
    ano_mutex_unlock(&g_outFileMtx);
}

// Write one finished record straight through on the calling thread (immediate or full-ring fallback).
// Assembles "<walltime> <text>\n" once so the append is atomic. `console` echoes by severity, off with
// no file. `sync` fsyncs after the write for FATAL durability.
static void emit_one(log_types_t level, uint64_t raw_ts, const char *text, uint16_t len,
                     bool console, bool sync)
{
    char out[ANO_LOG_MSG_MAX + ANO_LOG_TIME_RESV + 2];
    int  p  = render_walltime(out, raw_ts);
    out[p++] = ' ';
    memcpy(out + p, text, len); p += len;
    out[p++] = '\n';

    ano_mutex_lock(&g_outFileMtx);
    if (console && g_haveFile)              // no file: the persist below already hits the console
        echo_console(out, (size_t)p, level);
    g_persist(out, (size_t)p);
    if (sync) g_syncOut();
    ano_mutex_unlock(&g_outFileMtx);
}


/* The consumer: one single-active drain pass, run by the owned drain thread */

// Drain every committed record up to the tail bound into one batch write. Returns lines reclaimed,
// so the caller parks when a pass finds nothing.
static uint64_t drain_and_emit(void)
{
    // Not one linearization point: a batch drain is N consume events, each linearizing at its own `tag`
    // acquire below. head is drainer-private. The tail load is a relaxed count bound. Reclaim linearizes
    // at the head release-store at the end.
    uint64_t h0  = atomic_load_explicit(&g_ring.head, memory_order_relaxed); // drainer-private
    uint64_t h   = h0;
    uint64_t cap = atomic_load_explicit(&g_ring.tail, memory_order_relaxed); // reserved frontier, a count bound
    size_t   blen = 0;

    for (;;) {  // bounded walk: stops at the tail bound or the first uncommitted gap
        if (h == cap)
            break;  // caught up to tail, the only stop at exact fullness
        log_marker_t *m = log_marker_at(&g_ring, h);
        log_word_t v = { .w = atomic_load_explicit(&m->tag, memory_order_acquire) };
        if (!(v.flags & ANO_LOG_COMMITTED) || v.cycle != log_cycle(&g_ring, h))
            break;  // gap: free, reserved-but-unpublished, or a stale prior-lap tag. Resume here next pass
        uint64_t need = log_span(v.len);
        const char *body = log_gather(&g_ring, h, v.len, g_scratch);   // <= 2 memcpys

        uint64_t sec = wall_second(m->timestamp);   // deferred ticks->wall conversion, off the producer
        if (!g_drainHMSValid || sec != g_drainSec) {   // civil-time conversion once per second
            render_hms(g_drainHMS, sec);
            g_drainSec = sec;
            g_drainHMSValid = true;
        }
        memcpy(g_batch + blen, g_drainHMS, 8); blen += 8;
        g_batch[blen++] = ' ';
        if (v.flags & ANO_LOG_DEFERRED) {   // render the capture blob now, else copy finished text
            size_t room = g_batchCap - blen;
            int dcap = room > ANO_LOG_MSG_MAX ? (int)ANO_LOG_MSG_MAX : (int)room;   // clamp the line like eager
            blen += (size_t)format_deferred(g_batch + blen, dcap, (log_types_t)v.level, body);
        }
        else {
            memcpy(g_batch + blen, body, v.len); blen += v.len;
        }
        g_batch[blen++] = '\n';
        h += need;
    }

    // No zeroing: a reused slot carries last lap's tag until republished, and the cycle check above
    // rejects it. Reclaim is just the head advance, halving the drainer's memory traffic.
    atomic_store_explicit(&g_ring.head, h, memory_order_release);   // frees [h0,h) for reuse

    write_batch(g_batch, blen); // one syscall for the whole pass
    return h - h0;
}

// One drain pass, serialized so exactly one thread drains at a time. The owned thread runs it in a loop.
// ano_log_flush runs it inline for a synchronous guarantee. A mutex gates it so an inline flush and the
// owned drainer never overlap, and neither burns a core waiting.
static uint64_t drain(void)
{
    ano_mutex_lock(&g_drainMtx);
    uint64_t n = drain_and_emit();
    ano_mutex_unlock(&g_drainMtx);
    return n;
}

// Signal the parked drainer. Producers reach here only after seeing g_drainerParked, so the mutex
// is uncontended except against the drainer's own park/unpark transitions.
static void wake_drainer(void)
{
    ano_mutex_lock(&g_wakeMtx);
    ano_thread_cond_signal(&g_wakeCv);
    ano_mutex_unlock(&g_wakeMtx);
}

// Park after an empty pass. The parked flag goes up first, then the ring is rechecked under it:
// a producer that published before the flag went up is caught by the recheck, one that publishes
// after it sees the flag and signals. The seq_cst store/load pair keeps that window to the
// store-buffering interleaving, which the DRAIN_PARK_US timedwait cap bounds.
static void drainer_park(void)
{
    ano_mutex_lock(&g_wakeMtx);
    atomic_store_explicit(&g_drainerParked, true, memory_order_seq_cst);
    uint64_t t = atomic_load_explicit(&g_ring.tail, memory_order_seq_cst);
    uint64_t h = atomic_load_explicit(&g_ring.head, memory_order_relaxed);  // drainer-private
    if (t == h && atomic_load_explicit(&g_drainRun, memory_order_relaxed)) {
        struct timespec ts;
        timespec_get(&ts, TIME_UTC);    // CLOCK_REALTIME base, what cond_timedwait expects
        uint64_t ns = (uint64_t)ts.tv_nsec + (uint64_t)DRAIN_PARK_US * 1000u;
        ts.tv_sec  += (time_t)(ns / 1000000000u);
        ts.tv_nsec  = (long)(ns % 1000000000u);
        ano_thread_cond_timedwait(&g_wakeCv, &g_wakeMtx, &ts);
    }
    atomic_store_explicit(&g_drainerParked, false, memory_order_relaxed);
    ano_mutex_unlock(&g_wakeMtx);
}

// The owned consumer. Drains continuously while there is work, so the ring stays empty
// under load and producers never drain themselves. Parks on an empty pass until a producer's
// wake (or the park cap), so an idle logger costs nothing and the first record after idle is
// drained at wake latency. ano_log_flush still drains inline for callers that need it now.
static void *drainer_main(void *arg)
{
    (void)arg;
    while (atomic_load_explicit(&g_drainRun, memory_order_relaxed)) {
        if (drain() == 0)
            drainer_park();     // empty pass: park until woken, else stay hot and loop
    }
    return NULL;
}


/* Public interface */

int ano_log_enqueue(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    if ((int)level < atomic_load_explicit(&g_minLevel, memory_order_relaxed))
        return 0;   // one relaxed load gates both severity and liveness (INT_MAX until init)

    uint64_t ts = ano_timestamp_ticks();   // stamp at the call site, bare counter, convert at drain

    char blob[ANO_LOG_MSG_MAX]; // capture blob (deferred) or finished line (eager fallback), off-ring
    va_list ap; va_start(ap, fmt);
    va_list apc; va_copy(apc, ap);
    int n = capture_deferred(blob, (int)ANO_LOG_MSG_MAX, file, line, fmt, ap);  // defer formatting to drain
    bool deferred = (n >= 0);
    if (!deferred)                                                              // fancy conversion: format now
        n = format_line(blob, (int)ANO_LOG_MSG_MAX, level, file, line, fmt, apc);
    va_end(apc); va_end(ap);
    uint16_t len  = (uint16_t)n;
    uint64_t need = log_span(len);

    // No log_entry_t: an "entry" is a marker (tag + timestamp) plus inline text, laid straight into the
    // ring's reserved cache lines below. The ring is the storage.
    uint64_t cap = log_lines(&g_ring);
    uint64_t pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);
    uint64_t lastHead = 0;
    uint64_t backoff = FULL_BACKOFF_MIN_NS;
    uint32_t stall = 0;
    bool waited = false;
    for (;;) {
        uint64_t hd = atomic_load_explicit(&g_ring.head, memory_order_acquire); // full and reuse-safety
        if ((pos + need) - hd > cap) {   // would alias undrained: ring full
            // Don't drain inline (that serialized every full producer on g_drainMtx and collapsed @8).
            // Back off and let the owned consumer free space, self-throttling to the drain rate. While
            // head advances this is plain backpressure. If it stalls (a producer died mid-publish, leaving
            // an unpassable gap) write this line through so a wedge degrades to direct output. The
            // reservation isn't claimed yet (no CAS), so bailing leaks no slot.
            waited = true;
            if (hd != lastHead) { lastHead = hd; stall = 0; backoff = FULL_BACKOFF_MIN_NS; }
            else if (++stall > FULL_STALL_LIMIT) {
                if (deferred) {   // render the capture blob to text for the direct write
                    char txt[ANO_LOG_MSG_MAX];
                    int tn = format_deferred(txt, (int)sizeof txt, level, blob);
                    emit_one(level, ts, txt, (uint16_t)tn, false, false);
                } else {
                    emit_one(level, ts, blob, len, false, false);   // cold escape: the call-site ticks
                }
                return 1;
            }
            // A full ring normally means the drainer is awake and busy; if it is parked (a lost
            // wakeup during the fill), signal it rather than spinning out its park cap.
            if (atomic_load_explicit(&g_drainerParked, memory_order_seq_cst))
                wake_drainer();
            ano_busywait(backoff);  // escalating, off the consumer's cache line between rechecks
            if (backoff < FULL_BACKOFF_MAX_NS) backoff <<= 1;
            pos = atomic_load_explicit(&g_ring.tail, memory_order_relaxed);     // re-snapshot, retry
            continue;
        }
        if (atomic_compare_exchange_weak_explicit(&g_ring.tail, &pos, pos + need,
                memory_order_relaxed, memory_order_relaxed))
            break;  // own lines [pos, pos+need)
        // CAS failed: pos reloaded with the current tail, retry.
    }

    log_marker_t *m = log_marker_at(&g_ring, pos);
    m->timestamp = ts;  // plain store, ordered by the release below
    log_write_body(&g_ring, pos, blob, len);    // <= 2 memcpys
    log_word_t v = { .len = len, .level = (uint8_t)level,
                     .flags = (uint8_t)(ANO_LOG_COMMITTED | (deferred ? ANO_LOG_DEFERRED : 0)),
                     .cycle = log_cycle(&g_ring, pos) };
    atomic_store_explicit(&m->tag, v.w, memory_order_release);  // publish: one gate, whole record

    // Empty->nonempty wake: one relaxed-cost load when the drainer is awake (the common case under
    // load). A parked drainer is signaled so the record drains at wake latency, not the park cap.
    if (atomic_load_explicit(&g_drainerParked, memory_order_seq_cst))
        wake_drainer();
    return waited ? 1 : 0;   // 1: the ring was full, so we waited for the consumer to make room
}

void ano_log_immediate(log_types_t level, const char *file, int line, const char *fmt, ...)
{
    char blob[ANO_LOG_MSG_MAX];
    va_list ap; va_start(ap, fmt);
    int n = format_line(blob, (int)ANO_LOG_MSG_MAX,
                        level, file, line, fmt, ap);
    va_end(ap);

    if (!atomic_load_explicit(&g_initialized, memory_order_relaxed)) {
        fprintf(stderr, "%.*s\n", n, blob); // pre-init: stderr only, no anchor yet
        return;
    }
    drain();    // flush buffered records first to keep order
    emit_one(level, ano_timestamp_ticks(), blob, (uint16_t)n, /*console*/true, /*sync*/true);
}

int ano_log_output_dir(const char *directoryPath)
{
    if (directoryPath == NULL || directoryPath[0] == '\0'
        || !atomic_load_explicit(&g_initialized, memory_order_relaxed))
        return -1;

    ano_file *newOut = open_log(directoryPath);
    if (newOut == NULL)
        return -1;  // open failed, keep current output file

    ano_mutex_lock(&g_outFileMtx);
    if (g_outFile != NULL) {
        ano_fs_sync(g_outFile);
        ano_fs_close(g_outFile);
    }
    g_outFile = newOut;
    select_output();    // rebind the writer set to the new file
    ano_mutex_unlock(&g_outFileMtx);
    return 0;
}

void ano_log_set_level(log_types_t min)
{
    if (atomic_load_explicit(&g_initialized, memory_order_relaxed))
        atomic_store_explicit(&g_minLevel, (int)min, memory_order_relaxed);
}

void ano_log_flush(void)
{
    if (atomic_load_explicit(&g_initialized, memory_order_relaxed))
        drain();
}

int ano_log_init(void)
{
    if (atomic_load_explicit(&g_initialized, memory_order_relaxed))
        return 0;
    if (ano_mutex_init(&g_outFileMtx, NULL) != 0)
        return -1;
    if (ano_mutex_init(&g_drainMtx, NULL) != 0) { ano_mutex_destroy(&g_outFileMtx); return -1; }
    if (ano_mutex_init(&g_wakeMtx, NULL) != 0) {
        ano_mutex_destroy(&g_drainMtx); ano_mutex_destroy(&g_outFileMtx);
        return -1;
    }
    if (ano_thread_cond_init(&g_wakeCv, NULL) != 0) {
        ano_mutex_destroy(&g_wakeMtx); ano_mutex_destroy(&g_drainMtx); ano_mutex_destroy(&g_outFileMtx);
        return -1;
    }
    atomic_store_explicit(&g_drainerParked, false, memory_order_relaxed);

    g_ring.mask  = ANO_LOG_RING_LINES - 1;
    g_ring.shift = (uint32_t)__builtin_ctzll(ANO_LOG_RING_LINES);   // log2(N) for the lap counter
    g_ring.buf   = ano_aligned_malloc(ANO_LOG_RING_BYTES, ANO_LOG_RING_ALIGN);
    if (g_ring.buf == NULL) {
        ano_thread_cond_destroy(&g_wakeCv); ano_mutex_destroy(&g_wakeMtx);
        ano_mutex_destroy(&g_drainMtx); ano_mutex_destroy(&g_outFileMtx);
        return -1;
    }
    memset(g_ring.buf, 0, ANO_LOG_RING_BYTES);
    atomic_store(&g_ring.tail, 0);
    atomic_store(&g_ring.head, 0);

    // Batch upper bound: all drained text (<= N*ANO_CL) plus a <= 16-byte prefix per record for at
    // most N records.
    g_batchCap = (size_t)ANO_LOG_RING_LINES * ANO_CL + (size_t)ANO_LOG_RING_LINES * 16 + 256;
    g_batch = mi_malloc(g_batchCap);
    if (g_batch == NULL) {
        ano_aligned_free(g_ring.buf);
        ano_thread_cond_destroy(&g_wakeCv); ano_mutex_destroy(&g_wakeMtx);
        ano_mutex_destroy(&g_drainMtx); ano_mutex_destroy(&g_outFileMtx);
        return -1;
    }
    g_drainHMSValid = false;
    g_outFile = NULL;

    g_anchorTicks  = ano_timestamp_ticks();
    g_anchorUnixNs = (uint64_t)ano_timestamp_unix() * 1000000000ull;

    // Default output file is in the game directory.
    ano_fspath dir = ano_fs_gamepath();
    if (dir.length > 0)
        g_outFile = open_log(dir.str);
    select_output();    // bind g_persist/g_syncOut/g_haveFile to the chosen output

    atomic_store_explicit(&g_initialized, true, memory_order_release);

    // Spawn the owned consumer last, once the ring and output are live.
    atomic_store_explicit(&g_drainRun, true, memory_order_relaxed);
    if (ano_thread_create(&g_drainThread, NULL, drainer_main, NULL) != 0) {
        atomic_store_explicit(&g_drainRun, false, memory_order_relaxed);
        atomic_store_explicit(&g_initialized, false, memory_order_release);
        ano_aligned_free(g_ring.buf); g_ring.buf = NULL;
        mi_free(g_batch);             g_batch = NULL;
        ano_thread_cond_destroy(&g_wakeCv); ano_mutex_destroy(&g_wakeMtx);
        ano_mutex_destroy(&g_drainMtx); ano_mutex_destroy(&g_outFileMtx);
        return -1;
    }

    // Open the gate last: enqueues are admitted (and liveness reads true) only now everything is up.
    atomic_store_explicit(&g_minLevel, LOG_DEBUG, memory_order_relaxed);
    return 0;
}

int ano_log_cleanup(void)
{
    if (!atomic_load_explicit(&g_initialized, memory_order_relaxed))
        return 0;

    atomic_store_explicit(&g_initialized, false, memory_order_release);
    atomic_store_explicit(&g_minLevel, INT_MAX, memory_order_relaxed);   // close the gate: enqueues now no-op

    // Stop the owned consumer before tearing down the ring it reads. The signal cuts a parked
    // drainer's timedwait short so the join doesn't ride out the park cap.
    atomic_store_explicit(&g_drainRun, false, memory_order_relaxed);
    wake_drainer();
    ano_thread_join(g_drainThread, NULL);

    drain();    // one final drain, no producers remain by contract

    ano_mutex_lock(&g_outFileMtx);
    if (g_outFile != NULL) {
        ano_fs_sync(g_outFile);
        ano_fs_close(g_outFile);
        g_outFile = NULL;
    }
    ano_mutex_unlock(&g_outFileMtx);

    ano_aligned_free(g_ring.buf); g_ring.buf = NULL;
    mi_free(g_batch);             g_batch = NULL;
    ano_thread_cond_destroy(&g_wakeCv);
    ano_mutex_destroy(&g_wakeMtx);
    ano_mutex_destroy(&g_drainMtx);
    ano_mutex_destroy(&g_outFileMtx);
    return 0;
}