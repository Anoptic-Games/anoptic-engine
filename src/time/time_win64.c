/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)
// Pin API level before Windows headers so CreateWaitableTimerExW / FlsAlloc are visible.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // Win10: hi-res waitable timers
#define WINVER       0x0A00
#endif
#include "anoptic_time.h"
#include <windows.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// rdtsc is x86 only; ARM64 Windows falls back to QPC.
#if defined(__x86_64__) || defined(_M_X64)
#define ANO_TSC_ARCH 1
#include <intrin.h>   // __rdtsc, __cpuid, _mm_lfence
#endif


/* Precision Timestamps */

// Timebase: invariant TSC (rdtsc) when available, else QPC. Chosen once, shared by ticks and ticks_to_ns.

// QPC frequency (counts/s), cached once. TSC calibration reference too.
static _Atomic uint64_t cachedPerfFreq = 0;

// QPC frequency, resolved once. The module's single timebase validation point.
//   out: uint64_t counts/s, never 0
static uint64_t query_perf_freq(void) {
    uint64_t f = atomic_load_explicit(&cachedPerfFreq, memory_order_relaxed);
    if (f)
        return f;
    LARGE_INTEGER tmp;
    if (QueryPerformanceFrequency(&tmp) == 0 || tmp.QuadPart <= 0) {
        printf("No performance timebase: QueryPerformanceFrequency failed.\n");
        abort();   // no timebase, no engine; trips the crash blackbox
    }
    f = (uint64_t)tmp.QuadPart;
    atomic_store_explicit(&cachedPerfFreq, f, memory_order_relaxed);
    return f;
}

// Raw QPC counts. Calibration reference for the TSC mapping, and the timebase itself where no
// invariant TSC exists. Not the re-anchor reference: that is unbiased_now.
//   out: uint64_t counts, same domain as query_perf_freq
static inline uint64_t qpc_now(void) {
    LARGE_INTEGER tmp;
    QueryPerformanceCounter(&tmp);   // cannot fail on WinXP+
    return (uint64_t)tmp.QuadPart;
}

// QueryUnbiasedInterruptTimePrecise lives in realtimeapiset.h (pulled by winbase.h on Win10 SDKs)
// and is exported by KernelBase.dll rather than kernel32, so an SDK whose headers predate it needs
// this declaration and a toolchain whose import libs predate it needs a mincore/onecore link. The
// signature is the documented one, so this is a compatible redeclaration wherever the SDK has it.
WINBASEAPI VOID WINAPI QueryUnbiasedInterruptTimePrecise(PULONGLONG lpUnbiasedInterruptTimePrecise);

// Unbiased interrupt time: interrupt time with the time the system spent suspended subtracted out.
// That is the engine's monotonic semantic on every platform (Linux CLOCK_MONOTONIC, Darwin
// mach_absolute_time), which is why the TSC mapping re-anchors against this and not QPC: how far
// QPC advances across S3/S4 depends on which hardware source the machine picked, so the resume gap
// would vary per machine. The Precise form reads the source rather than the last tick, so it does
// not quantize to the ~15.6ms interrupt period.
//   out: uint64_t 100ns units since boot, suspended time excluded, monotonic non-decreasing
static inline uint64_t unbiased_now(void) {
    ULONGLONG t = 0;
    QueryUnbiasedInterruptTimePrecise(&t);   // void return: cannot fail
    return (uint64_t)t;
}

#ifdef ANO_TSC_ARCH

// Resolved timebase, frozen after resolve_clock.
enum { CLOCK_UNSET = 0, CLOCK_TSC = 1, CLOCK_QPC = 2 };
static _Atomic int      g_clockMode  = CLOCK_UNSET;
static _Atomic int      g_clockElect = 0;    // one-time election guard for resolve_clock
static _Atomic uint64_t cachedTscHz  = 0;    // calibrated invariant-TSC frequency (TSC mode only)

// Calibration sanity band. A floor above 0 is what makes an elected cachedTscHz safe to divide by.
#define ANO_TSC_HZ_MIN 100000000ull      // 100 MHz
#define ANO_TSC_HZ_MAX 100000000000ull   // 100 GHz
static_assert(ANO_TSC_HZ_MIN > 0 && ANO_TSC_HZ_MIN < ANO_TSC_HZ_MAX, "TSC band must exclude 0 and be ordered");

// Invariant TSC: CPUID 0x80000007 EDX[8]. Required for rdtsc-as-clock.
static bool have_invariant_tsc(void) {
    int r[4];
    __cpuid(r, 0x80000000);              // max extended leaf in EAX
    if ((unsigned)r[0] < 0x80000007u)
        return false;
    __cpuid(r, 0x80000007);
    return (r[3] & (1 << 8)) != 0;       // EDX[8] = Invariant TSC
}

// Fenced rdtsc for calibration. Hot path uses plain __rdtsc.
static inline uint64_t rdtsc_fenced(void) {
    _mm_lfence();
    uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

#define ANO_TSC_CAL_MS 4u   // per-sample window vs actual QPC elapsed

// TSC Hz from elapsed TSC / elapsed QPC.
static uint64_t sample_tsc_hz(uint64_t qf) {
    LARGE_INTEGER q0, q1;
    QueryPerformanceCounter(&q0);
    uint64_t t0 = rdtsc_fenced();
    Sleep(ANO_TSC_CAL_MS);
    uint64_t t1 = rdtsc_fenced();
    QueryPerformanceCounter(&q1);

    uint64_t dq = (uint64_t)(q1.QuadPart - q0.QuadPart);
    uint64_t dt = t1 - t0;
    if (dq == 0 || dt == 0)
        return 0;
    // tscHz = dt * qf / dq. 128-bit product avoids overflow.
    return (uint64_t)(((unsigned __int128)dt * qf) / dq);
}

// Median of three TSC Hz samples.
static uint64_t calibrate_tsc_hz(void) {
    uint64_t qf = query_perf_freq();
    uint64_t s[3];
    for (int i = 0; i < 3; i++)
        s[i] = sample_tsc_hz(qf);
    if (s[0] > s[1]) { uint64_t t = s[0]; s[0] = s[1]; s[1] = t; }
    if (s[1] > s[2]) { uint64_t t = s[1]; s[1] = s[2]; s[2] = t; }
    if (s[0] > s[1]) { uint64_t t = s[0]; s[0] = s[1]; s[1] = t; }
    return s[1];
}

/* TSC re-anchor */

// A TSC stamp is __rdtsc() + g_tscBias, valid while the raw count stays at or above g_tscAnchorRaw.
// The pair is republished only when a power transition restarts the counter.
static _Atomic uint64_t g_tscBias      = 0;   // exported - raw; wraps mod 2^64 by design
static _Atomic uint64_t g_tscAnchorRaw = 0;   // raw TSC at the anchor
static _Atomic int      g_tscAnchoring = 0;   // one re-anchor at a time
// Reference pair, written before g_clockMode goes public and thereafter only under g_tscAnchoring.
static uint64_t g_refAnchor      = 0;   // unbiased interrupt time (100ns units) at the anchor
static uint64_t g_exportedAnchor = 0;   // exported ticks at the anchor

// Publish an anchor. Bias stored first: a reader that sees the new raw anchor sees the new bias.
//   in:  raw (uint64_t) TSC just sampled, ref (uint64_t) unbiased interrupt time just sampled,
//        exported (uint64_t) count the next stamp must report
static void tsc_set_anchor(uint64_t raw, uint64_t ref, uint64_t exported) {
    g_refAnchor      = ref;
    g_exportedAnchor = exported;
    atomic_store_explicit(&g_tscBias, exported - raw, memory_order_release);
    atomic_store_explicit(&g_tscAnchorRaw, raw, memory_order_release);
}

// Cold path: the raw TSC fell a millisecond below the anchor, which only a power transition does.
// S3 sleep and S4 hibernate drop the core power domain and firmware restarts the counter near zero;
// the invariant-TSC bit checked at election covers P/C/T states only. Re-derive the mapping so the
// exported count continues where it left off, measuring the gap on unbiased interrupt time: the
// engine's clock excludes suspended time, so the resume advance is the awake interval alone and is
// the same on every machine.
//   out: void. On return this thread republished the anchor, or another thread is republishing it;
//        the caller re-reads either way.
static void tsc_reanchor(void) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&g_tscAnchoring, &expected, 1)) {
        Sleep(0);   // loser: yield, then retry the read
        return;
    }
    uint64_t raw = rdtsc_fenced();
    uint64_t ref = unbiased_now();
    uint64_t adv = 0;
    if (ref > g_refAnchor) {   // a reference below its anchor is outside contract; keeps the gap out of the scale
        uint64_t hz = atomic_load_explicit(&cachedTscHz, memory_order_relaxed);
        adv = (uint64_t)(((unsigned __int128)(ref - g_refAnchor) * hz) / 10000000ull);   // 100ns units/s
        // Round the gap up. cachedTscHz is calibrated against QPC and applied here to an unbiased
        // gap, so the estimate carries both references' drift: tens of ppm either way. A short
        // estimate puts post-resume stamps below held pre-resume ones, exactly the u64 delta wrap
        // this seam exists to prevent. Long is one stretched delta, short is 584 years.
        adv += adv / 1024u + hz / 1000u;
    }
    tsc_set_anchor(raw, ref, g_exportedAnchor + adv);
    atomic_store_explicit(&g_tscAnchoring, 0, memory_order_release);
}

// Elect once. Losers wait. Calibration ~12 ms Sleep, once.
static void resolve_clock(void) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&g_clockElect, &expected, 1)) {
        while (atomic_load_explicit(&g_clockMode, memory_order_acquire) == CLOCK_UNSET)
            Sleep(0);
        return;
    }
    int mode = CLOCK_QPC;
    if (have_invariant_tsc()) {
        uint64_t hz = calibrate_tsc_hz();
        if (hz >= ANO_TSC_HZ_MIN && hz <= ANO_TSC_HZ_MAX) {   // rejects a degenerate sample, incl. 0
            atomic_store_explicit(&cachedTscHz, hz, memory_order_relaxed);
            uint64_t raw = rdtsc_fenced();
            tsc_set_anchor(raw, unbiased_now(), raw);   // bias 0: stamps are the raw count until a restart
            mode = CLOCK_TSC;
        }
    }
    query_perf_freq();   // ensure QPC frequency cached for fallback

    #ifdef DEBUG_BUILD
    printf("\nTimebase: %s", mode == CLOCK_TSC ? "invariant TSC" : "QPC");
    if (mode == CLOCK_TSC)
        printf(" @ %llu Hz", (unsigned long long)atomic_load_explicit(&cachedTscHz, memory_order_relaxed));
    printf(" (QPC %llu Hz)\n\n", (unsigned long long)query_perf_freq());
    #endif

    atomic_store_explicit(&g_clockMode, mode, memory_order_release);   // publishes cachedTscHz too
}

static inline int clock_mode(void) {
    int m = atomic_load_explicit(&g_clockMode, memory_order_acquire);
    if (m == CLOCK_UNSET) {
        resolve_clock();
        m = atomic_load_explicit(&g_clockMode, memory_order_acquire);
    }
    return m;
}

#endif // ANO_TSC_ARCH

// Raw counter: rdtsc against its anchor, or QPC. Divide deferred to ano_ticks_to_ns.
// The anchor test is the whole cost of surviving S3/S4: a live counter is always far above it, so
// the stamp keeps its load-add shape and only a restart pays.
uint64_t ano_timestamp_ticks() {
#ifdef ANO_TSC_ARCH
    if (clock_mode() == CLOCK_TSC) {
        for (;;) {
            uint64_t raw    = __rdtsc();
            uint64_t anchor = atomic_load_explicit(&g_tscAnchorRaw, memory_order_acquire);
            if (raw >= anchor)
                return raw + atomic_load_explicit(&g_tscBias, memory_order_relaxed);
            // Below the anchor: core-to-core skew is a few hundred cycles, a restarted counter is
            // seconds of uptime. Clamp the first to the anchor, re-anchor on the second.
            if (anchor - raw < atomic_load_explicit(&cachedTscHz, memory_order_relaxed) / 1000u)
                return anchor + atomic_load_explicit(&g_tscBias, memory_order_relaxed);
            tsc_reanchor();
        }
    }
#endif
    // No invariant TSC: QPC is the timebase directly, and best-effort on suspend semantics 〜 its
    // advance across S3/S4 is its hardware source's business and there is no second reference here
    // to correct it against.
    return qpc_now();
}

// Convert raw counts (value or delta) to nanoseconds, overflow-safe via the resolved timebase.
uint64_t ano_ticks_to_ns(uint64_t ticks) {

    uint64_t freq;
#ifdef ANO_TSC_ARCH
    freq = (clock_mode() == CLOCK_TSC)
               ? atomic_load_explicit(&cachedTscHz, memory_order_relaxed)
               : query_perf_freq();
#else
    freq = query_perf_freq();
#endif

    // Split into two parts to scale without overflow.
    uint64_t largePart = ticks / freq;    // Seconds
    uint64_t smallPart = ticks % freq;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000ULL / freq;
    return smallPart + (largePart * 1000000000ULL);
}

uint64_t ano_timestamp_raw() {
    return ano_ticks_to_ns(ano_timestamp_ticks());
}

// return ano_timestamp_raw, but scaled to microseconds.
uint64_t ano_timestamp_us() {
    return ano_timestamp_raw() / 1000;  // Convert nanoseconds to microseconds
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms() {
    return (uint32_t)(ano_timestamp_raw() / 1000000LL);  // Convert nanoseconds to milliseconds
}


/* Generic Date-Time Stamps */

// Unix UTC timestamp.
int64_t ano_timestamp_unix() {

    time_t currentTime;
    currentTime = time(NULL);

    // Error handling
    if (currentTime == (time_t)-1) {
        return INT64_MIN; // Out-of-range sentinel value
    }

    return (int64_t)currentTime;
}

// Convert a Unix timestamp to broken-down local civil time.
ano_datetime ano_localtime(int64_t unix_seconds) {

    time_t t = (time_t)unix_seconds;
    struct tm tm;
    if (localtime_s(&tm, &t) != 0)
        return (ano_datetime){0};

    return (ano_datetime){
        .year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday,
        .hour = tm.tm_hour, .minute = tm.tm_min, .second = tm.tm_sec,
    };
}


/* Waiting Facilities */

// Spinlock the current thread for approximately ns nanoseconds.
int ano_busywait(uint64_t ns) {

    if (ns > MAX_BUSYWAIT_NS) {
        printf("Requested busywait time exceeds maximum limit. Exiting.\n");
        return -1; // error
    }

    uint64_t startTime = ano_timestamp_raw();
    uint64_t endTime;

    do {
        endTime = ano_timestamp_raw();
    } while (endTime - startTime < ns);

    return 0; // success
}

// Win10 1803+ hi-res timer flag. Define if SDK headers predate it.
// Target 1803+: hi-res timer always available; no timeBeginPeriod floor.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Spin the last 1ms on ano_busywait; waitable-timer wakeups slop ~0.5-1ms.
#define ANO_SLEEP_SPIN_TAIL_NS 1000000ULL

// Per-thread waitable timer. NULL = unset, INVALID_HANDLE_VALUE = unsupported.
static _Thread_local HANDLE tlSleepTimer = NULL;

// FLS slot: closes timer at thread exit. Allocated once via CAS.
static DWORD       gSleepTimerFls = FLS_OUT_OF_INDEXES;
static _Atomic int gFlsInit = 0;

static void NTAPI ano_sleep_timer_free(PVOID p) {
    if (p != NULL && p != INVALID_HANDLE_VALUE)
        CloseHandle((HANDLE)p);
}

// Lazy hi-res waitable timer. NULL -> caller uses coarse Sleep.
static HANDLE ano_sleep_timer(void) {
    if (tlSleepTimer == INVALID_HANDLE_VALUE)
        return NULL;             // previously determined unsupported
    if (tlSleepTimer != NULL)
        return tlSleepTimer;     // hot path: reuse

    HANDLE h = CreateWaitableTimerExW(NULL, NULL,
                                      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (h == NULL) {
        tlSleepTimer = INVALID_HANDLE_VALUE;   // give up once, not every call
        return NULL;
    }
    tlSleepTimer = h;

    // Register thread-exit destructor to close the handle.
    int expected = 0;
    if (atomic_compare_exchange_strong(&gFlsInit, &expected, 1))
        gSleepTimerFls = FlsAlloc(ano_sleep_timer_free);
    if (gSleepTimerFls != FLS_OUT_OF_INDEXES)
        FlsSetValue(gSleepTimerFls, h);        // fires ano_sleep_timer_free at thread exit
    return h;
}

// High-res sleep: waitable timer + busywait tail. Yields the CPU.
//   in:  us (uint64_t) microseconds to sleep
//   out: int, 0 on success, positive errno-ish on failure (Unix-backend parity)
int ano_sleep(uint64_t us) {

    if (us == 0)
        return 0;   // nothing to wait on, matches a zero-length nanosleep

    uint64_t target_ns = us * 1000ULL;
    uint64_t start = ano_timestamp_raw();

    // Coarse stage: yield the CPU for everything but the spin tail.
    if (target_ns > ANO_SLEEP_SPIN_TAIL_NS) {
        uint64_t coarse_ns = target_ns - ANO_SLEEP_SPIN_TAIL_NS;
        HANDLE timer = ano_sleep_timer();
        bool yielded = false;
        if (timer != NULL) {
            // Relative due time in 100ns units, negative per Win32 ABI.
            // Clamp to INT64_MAX so negation never becomes an absolute due; floor to 1 so a wait never rounds to "signal now".
            uint64_t units = coarse_ns / 100ULL;
            if (units == 0) units = 1;
            if (units > (uint64_t)INT64_MAX) units = (uint64_t)INT64_MAX;

            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)units;
            if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
                if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0)
                    return EIO;
                yielded = true;
            }
        }
        // No timer or SetWaitableTimer failed: coarse Sleep, then spin tail.
        // Chunked: a >=49.7-day stage truncates in a DWORD, and 0xFFFFFFFF ms is INFINITE.
        if (!yielded) {
            uint64_t coarse_ms = coarse_ns / 1000000ULL;
            while (coarse_ms > 0ULL) {
                DWORD chunk = coarse_ms > 0x7FFFFFFFULL ? 0x7FFFFFFFUL : (DWORD)coarse_ms;
                Sleep(chunk);
                coarse_ms -= chunk;
            }
        }
    }

    // Spin stage: remaining time in <=MAX_BUSYWAIT_NS chunks (oversleep / missing coarse must not trip busywait's 1e9ns cap).
    for (;;) {
        uint64_t elapsed = ano_timestamp_raw() - start;
        if (elapsed >= target_ns)
            break;
        uint64_t remaining = target_ns - elapsed;
        ano_busywait(remaining > MAX_BUSYWAIT_NS ? MAX_BUSYWAIT_NS : remaining);
    }

    return 0; // success
}

#endif
