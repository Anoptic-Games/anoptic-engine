/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)
// Pin the API level before any Windows header so CreateWaitableTimerExW / FlsAlloc prototypes are
// visible on every toolchain, not just those defaulting to a modern target.
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

// rdtsc is x86 only; on ARM64 Windows this whole timebase falls back to QPC.
#if defined(__x86_64__) || defined(_M_X64)
#define ANO_TSC_ARCH 1
#include <intrin.h>   // __rdtsc, __cpuid, _mm_lfence
#endif


/* Precision Timestamps */

// Two monotonic timebases on Windows. QPC is portable but coarse: on many hosts it ticks at 10 MHz, a
// 100 ns step, so records stamped inside the same 100 ns window can't be ordered -- coarser than
// Linux/macOS. On x86-64 with an invariant TSC (constant rate across P-states, idle, and cores) rdtsc
// is a sub-nanosecond register read, giving the logger fine cross-thread ordering. We use rdtsc when
// available and fall back to QPC otherwise (no invariant TSC, or a non-x86 Windows build). The choice
// is resolved once and frozen, so ano_timestamp_ticks and ano_ticks_to_ns always share one timebase.

// QPC frequency (counts/second), cached once. The QPC path uses it directly; the TSC path uses it as
// the calibration reference.
static _Atomic uint64_t cachedPerfFreq = 0;

static uint64_t query_perf_freq(void) {
    uint64_t f = atomic_load_explicit(&cachedPerfFreq, memory_order_relaxed);
    if (f)
        return f;
    LARGE_INTEGER tmp;
    if (QueryPerformanceFrequency(&tmp) == 0 || tmp.QuadPart <= 0)
        return 0;   // never fails on WinXP+; callers treat 0 as a hard clock error
    f = (uint64_t)tmp.QuadPart;
    atomic_store_explicit(&cachedPerfFreq, f, memory_order_relaxed);
    return f;
}

#ifdef ANO_TSC_ARCH

// Resolved timebase, decided once by resolve_clock and never changed.
enum { CLOCK_UNSET = 0, CLOCK_TSC = 1, CLOCK_QPC = 2 };
static _Atomic int      g_clockMode  = CLOCK_UNSET;
static _Atomic int      g_clockElect = 0;    // one-time election guard for resolve_clock
static _Atomic uint64_t cachedTscHz  = 0;    // calibrated invariant-TSC frequency (TSC mode only)

// Invariant TSC: CPUID leaf 0x80000007, EDX bit 8. Only then does rdtsc advance at a constant rate
// independent of frequency scaling and idle, i.e. is usable as a clock.
static bool have_invariant_tsc(void) {
    int r[4];
    __cpuid(r, 0x80000000);              // max extended leaf in EAX
    if ((unsigned)r[0] < 0x80000007u)
        return false;
    __cpuid(r, 0x80000007);
    return (r[3] & (1 << 8)) != 0;       // EDX[8] = Invariant TSC
}

// rdtsc bracketed by lfence so the read can't drift past neighbouring instructions during
// calibration. The hot path (ano_timestamp_ticks) uses a plain rdtsc: a few instructions of skew
// there is far below the ordering grain we care about, and the fences aren't free.
static inline uint64_t rdtsc_fenced(void) {
    _mm_lfence();
    uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

#define ANO_TSC_CAL_MS 4u   // per-sample window; the ratio is measured against ACTUAL elapsed QPC, so
                            // Sleep jitter and mid-window preemption don't bias it (both clocks advance
                            // in real time regardless)

// One TSC-frequency measurement: elapsed TSC over elapsed QPC, scaled by the QPC frequency.
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
    // tscHz = dt / (dq / qf) = dt * qf / dq. 128-bit product so a fast CPU or wide window can't overflow.
    return (uint64_t)(((unsigned __int128)dt * qf) / dq);
}

// Calibrate the TSC frequency: median of three samples rejects a one-off preemption outlier.
static uint64_t calibrate_tsc_hz(void) {
    uint64_t qf = query_perf_freq();
    if (qf == 0)
        return 0;
    uint64_t s[3];
    for (int i = 0; i < 3; i++)
        s[i] = sample_tsc_hz(qf);
    if (s[0] > s[1]) { uint64_t t = s[0]; s[0] = s[1]; s[1] = t; }
    if (s[1] > s[2]) { uint64_t t = s[1]; s[1] = s[2]; s[2] = t; }
    if (s[0] > s[1]) { uint64_t t = s[0]; s[0] = s[1]; s[1] = t; }
    return s[1];
}

// Decide the timebase once. One thread wins the election and resolves; concurrent callers wait for
// the published mode. Calibration costs ~12 ms of Sleep, paid once at the first timestamp.
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
        if (hz >= 100000000ull && hz <= 100000000000ull) {   // 100 MHz .. 100 GHz sanity band
            atomic_store_explicit(&cachedTscHz, hz, memory_order_relaxed);
            mode = CLOCK_TSC;
        }
    }
    query_perf_freq();   // ensure the QPC frequency is cached for the fallback path

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

// Bare monotonic counter, no conversion. rdtsc (a register read) when the TSC is usable, else the raw
// QPC count. The frequency divide is deferred to ano_ticks_to_ns.
uint64_t ano_timestamp_ticks() {
#ifdef ANO_TSC_ARCH
    if (clock_mode() == CLOCK_TSC)
        return __rdtsc();
#endif
    LARGE_INTEGER tmp;
    if (QueryPerformanceCounter(&tmp) == 0) {
        printf("Error getting Windows performance Counter.");
        return UINT64_MAX; // Indicate an error occurred.
    }
    return (uint64_t)tmp.QuadPart;
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
    if (freq == 0)
        return UINT64_MAX;   // clock error

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

    uint64_t timestampNs = ano_timestamp_raw();
    if (timestampNs == UINT64_MAX)
        return UINT64_MAX; // Indicate an error occurred.

    return timestampNs / 1000;  // Convert nanoseconds to microseconds
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms() {

    uint64_t timestampNs = ano_timestamp_raw();
    if (timestampNs == UINT64_MAX)
        return UINT32_MAX; // Indicate an error occurred.

    return (uint32_t)(timestampNs / 1000000LL);  // Convert nanoseconds to milliseconds
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
    } while (endTime - startTime < ns && startTime != 0 && endTime != 0);

    return 0; // success
}

// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is Win10 1803+ (2018); define it if the SDK headers predate
// it so the source still compiles. We target 1803+, so the hi-res timer is always available and needs
// no timeBeginPeriod floor.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Spin the last 1ms on ano_busywait; waitable-timer wakeups slop ~0.5-1ms.
#define ANO_SLEEP_SPIN_TAIL_NS 1000000ULL

// Per-thread cached waitable timer, created lazily and reused for the thread's life. Thread-local so
// no locks. NULL = not yet attempted, INVALID_HANDLE_VALUE = attempted and unsupported.
static _Thread_local HANDLE tlSleepTimer = NULL;

// FLS slot whose per-thread destructor closes the cached timer at thread exit, so a churn of
// transient threads doesn't leak one kernel timer each. Allocated once via CAS.
static DWORD       gSleepTimerFls = FLS_OUT_OF_INDEXES;
static _Atomic int gFlsInit = 0;

static void NTAPI ano_sleep_timer_free(PVOID p) {
    if (p != NULL && p != INVALID_HANDLE_VALUE)
        CloseHandle((HANDLE)p);
}

// This thread's hi-res waitable timer, created lazily. Returns NULL on hard failure (caller degrades
// to a coarse Sleep).
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

    // Register a thread-exit destructor so the handle is closed when this thread dies.
    int expected = 0;
    if (atomic_compare_exchange_strong(&gFlsInit, &expected, 1))
        gSleepTimerFls = FlsAlloc(ano_sleep_timer_free);
    if (gSleepTimerFls != FLS_OUT_OF_INDEXES)
        FlsSetValue(gSleepTimerFls, h);        // fires ano_sleep_timer_free at thread exit
    return h;
}

// Use OS time facilities for a high-res sleep that DOES give up thread execution. (clock_nanosleep on
// Unix, waitable timer + busywait tail on Windows.) The timer yields the CPU for the bulk of the wait,
// then a sub-millisecond busywait tail recovers the accuracy the scheduler slops away.
//   in:  us (uint64_t) microseconds to sleep
//   out: int, 0 on success, positive errno-ish on failure (Unix-backend parity)
int ano_sleep(uint64_t us) {

    if (us == 0)
        return 0;   // nothing to wait on, matches a zero-length nanosleep

    uint64_t target_ns = us * 1000ULL;
    uint64_t start = ano_timestamp_raw();
    if (start == 0 || start == UINT64_MAX)
        return EIO;   // clock broken, refuse rather than spin on a garbage delta

    // Coarse stage: yield the CPU for everything but the spin tail.
    if (target_ns > ANO_SLEEP_SPIN_TAIL_NS) {
        uint64_t coarse_ns = target_ns - ANO_SLEEP_SPIN_TAIL_NS;
        HANDLE timer = ano_sleep_timer();
        bool yielded = false;
        if (timer != NULL) {
            // Relative due time in 100ns units, negative per the Win32 ABI. Clamp the magnitude to
            // INT64_MAX so the negation can't overflow into an absolute (positive) due time on a
            // pathologically long sleep; floor to 1 so a real wait never rounds to "signal now".
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
        // No timer, or SetWaitableTimer failed: yield coarsely via Sleep rather than spinning the
        // whole interval on a core. The spin tail below still recovers the sub-ms accuracy.
        if (!yielded)
            Sleep((DWORD)(coarse_ns / 1000000ULL));
    }

    // Spin stage: busy-wait whatever remains, in <=MAX_BUSYWAIT_NS chunks so a long oversleep (or a
    // missing coarse stage) can't trip ano_busywait's 1e9ns cap.
    for (;;) {
        uint64_t now = ano_timestamp_raw();
        if (now == 0 || now == UINT64_MAX)
            return EIO;
        uint64_t elapsed = now - start;
        if (elapsed >= target_ns)
            break;
        uint64_t remaining = target_ns - elapsed;
        ano_busywait(remaining > MAX_BUSYWAIT_NS ? MAX_BUSYWAIT_NS : remaining);
    }

    return 0; // success
}

#endif