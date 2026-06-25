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
#include <Windows.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>


/* Precision Timestamps */

// cache the performance frequency (acquired only once)
static _Atomic uint64_t cachedPerfFreq = 0;

int initialize_performance_frequency() {

    LARGE_INTEGER tmp;
    if (QueryPerformanceFrequency(&tmp) == 0) {
        // Handle error
        printf("Failed to query performance frequency.");
        return -1;
    }
    cachedPerfFreq = tmp.QuadPart;

    #ifdef DEBUG_BUILD
    printf("\nPerformance Frequency: %llu\n\n", cachedPerfFreq);
    #endif

    return 0;
}

// Bare QPC counter, no conversion. The frequency divide is deferred to ano_ticks_to_ns.
uint64_t ano_timestamp_ticks() {

    LARGE_INTEGER tmp;
    if(QueryPerformanceCounter(&tmp) == 0) {
        printf("Error getting Windows performance Counter.");
        return UINT64_MAX; // Indicate an error occurred.
    }
    return (uint64_t)tmp.QuadPart;
}

// Convert raw QPC counts (value or delta) to nanoseconds, overflow-safe via the cached frequency.
uint64_t ano_ticks_to_ns(uint64_t ticks) {

    // Cache the performance frequency on first run.
    if(cachedPerfFreq == 0) {
        if (initialize_performance_frequency() != 0) {
            printf("Exiting due to error with fetching performance frequency.");
            return UINT64_MAX; // Indicate an error occurred
        }
    }

    // Split into two parts to scale without overflow.
    uint64_t largePart = ticks / cachedPerfFreq;    // Seconds
    uint64_t smallPart = ticks % cachedPerfFreq;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000LL / cachedPerfFreq;
    return smallPart + (largePart * 1000000000LL);
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