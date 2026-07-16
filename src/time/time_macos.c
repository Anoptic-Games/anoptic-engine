/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Darwin timing, counterpart of Linux CLOCK_MONOTONIC and Windows QPC.
// mach_absolute_time() reads the hardware timebase counter (no syscall).
// Convert to nanoseconds via the mach_timebase_info() numer/denom ratio.
// Apple Silicon ticks at 24 MHz, so raw ticks are NOT nanoseconds without the ratio applied.
// The ratio is cached atomically and the conversion is overflow-safe.
// macOS libc has no clock_nanosleep; ano_sleep waits on mach_wait_until deadlines with a
// spun tail, since the kernel stretches plain relative sleeps under QoS timer leeway.

#if defined(__APPLE__)
#include "anoptic_time.h"
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdatomic.h>


/* Precision Timestamps */

// Cache the timebase frequency in ticks/second, acquired once.
// ticks/sec = 1e9 * denom / numer.
static _Atomic uint64_t cachedTimebaseFreq = 0;

static int initialize_timebase() {

    mach_timebase_info_data_t tb;
    if (mach_timebase_info(&tb) != KERN_SUCCESS || tb.numer == 0) {
        printf("Failed to query mach timebase.\n");
        return -1;
    }
    cachedTimebaseFreq = (uint64_t)1000000000ULL * tb.denom / tb.numer;

    #ifdef DEBUG_BUILD
    printf("\nTimebase Frequency: %llu (numer=%u denom=%u)\n\n",
           cachedTimebaseFreq, tb.numer, tb.denom);
    #endif

    return 0;
}

// Bare timebase counter, no conversion. mach_absolute_time() is a register read, no syscall, no divides.
uint64_t ano_timestamp_ticks() {
    return mach_absolute_time();
}

// Convert raw mach ticks (value or delta) to nanoseconds, overflow-safe via the cached timebase.
uint64_t ano_ticks_to_ns(uint64_t ticks) {

    // Cache the timebase frequency on first run.
    if (cachedTimebaseFreq == 0) {
        if (initialize_timebase() != 0) {
            printf("Exiting due to error with fetching mach timebase.");
            return UINT64_MAX; // Indicate an error occurred.
        }
    }

    // Split into seconds and sub-seconds to scale without overflow.
    uint64_t largePart = ticks / cachedTimebaseFreq;    // Seconds
    uint64_t smallPart = ticks % cachedTimebaseFreq;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000LL / cachedTimebaseFreq;
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
        perror("time()");
        return INT64_MIN; // Out-of-range sentinel value
    }

    return (int64_t)currentTime;
}

// Convert a Unix timestamp to broken-down local civil time.
ano_datetime ano_localtime(int64_t unix_seconds) {

    time_t t = (time_t)unix_seconds;
    struct tm tm;
    if (localtime_r(&t, &tm) == NULL)
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
        printf("Requested busywait time exceeds maximum limit. Returning.\n");
        return -1; // failure
    }

    uint64_t startTime = ano_timestamp_raw();
    uint64_t endTime;

    do {
        endTime = ano_timestamp_raw();
    } while (endTime - startTime < ns);

    return 0; // success
}

// Convert nanoseconds to raw mach ticks, overflow-safe via the cached timebase.
// Returns UINT64_MAX if the timebase is unavailable.
static uint64_t ano_ns_to_ticks(uint64_t ns) {

    // Cache the timebase frequency on first run.
    if (cachedTimebaseFreq == 0) {
        if (initialize_timebase() != 0)
            return UINT64_MAX;
    }

    // Split into seconds and sub-seconds to scale without overflow.
    uint64_t largePart = ns / 1000000000LL;     // Seconds
    uint64_t smallPart = ns % 1000000000LL;     // Sub-seconds

    return largePart * cachedTimebaseFreq + smallPart * cachedTimebaseFreq / 1000000000LL;
}

// Tail window spun instead of slept: kernel timer leeway cannot land closer than this.
#define ANO_SLEEP_SPIN_NS 500000ULL

// Use OS time facilities for high-res sleep that DOES give up thread execution.
// The kernel stretches relative waits up to ~1.5x under QoS timer leeway — nanosleep and
// mach_wait_until measure identically — so wait on an absolute deadline in half-of-remainder
// steps (immune below 2x stretch), then spin the last ANO_SLEEP_SPIN_NS. The absolute
// deadline also re-arms early wakeups (KERN_ABORTED) without drift.
int ano_sleep(uint64_t us) {

    uint64_t waitTicks = ano_ns_to_ticks(us * 1000ULL);
    if (waitTicks == UINT64_MAX)
        return -1;

    uint64_t deadline = mach_absolute_time() + waitTicks;
    uint64_t spinTicks = ano_ns_to_ticks(ANO_SLEEP_SPIN_NS);

    // Whole wait inside the spin window: a single kernel wait keeps the yield contract.
    if (us * 1000ULL <= ANO_SLEEP_SPIN_NS) {
        while (mach_absolute_time() < deadline)
            mach_wait_until(deadline);
        return 0;
    }

    // Kernel-wait toward the spin window, halving the remainder each pass.
    uint64_t now;
    while ((now = mach_absolute_time()) + spinTicks < deadline) {
        uint64_t half = (deadline - spinTicks - now) / 2;
        mach_wait_until(now + (half > 0 ? half : 1));
    }

    // Spin out the tail.
    while (mach_absolute_time() < deadline)
        ;

    return 0; // success
}

#endif
