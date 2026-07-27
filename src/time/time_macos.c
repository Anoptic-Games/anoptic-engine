/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Darwin: mach_absolute_time() (no syscall). ns via mach_timebase_info numer/denom (AS: 24 MHz, not ns).
// Ratio cached atomically; conversion overflow-safe.
// ano_sleep: mach_wait_until absolute deadlines + spin tail (no clock_nanosleep, QoS leeway stretches relative waits).

#if defined(__APPLE__)
#include "anoptic_time.h"
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>


/* Precision Timestamps */

// Cache timebase frequency in ticks/s, once. ticks/sec = 1e9 * denom / numer.
static _Atomic uint64_t cachedTimebaseFreq = 0;

// Resolve the timebase into cachedTimebaseFreq. The module's single validation point.
//   out: void. cachedTimebaseFreq is nonzero on return.
static void initialize_timebase(void) {

    mach_timebase_info_data_t tb;
    uint64_t freq = 0;
    if (mach_timebase_info(&tb) == KERN_SUCCESS && tb.numer != 0)
        freq = (uint64_t)1000000000ULL * tb.denom / tb.numer;
    if (freq == 0) {
        printf("Failed to query mach timebase.\n");
        abort();   // no timebase, no engine; trips the crash blackbox
    }
    cachedTimebaseFreq = freq;

    #ifdef DEBUG_BUILD
    printf("\nTimebase Frequency: %llu (numer=%u denom=%u)\n\n",
           (unsigned long long)freq, tb.numer, tb.denom);
    #endif
}

// Timebase frequency in ticks/s, resolved on first use.
//   out: uint64_t ticks/s, never 0
static inline uint64_t timebase_freq(void) {
    if (cachedTimebaseFreq == 0)
        initialize_timebase();
    return cachedTimebaseFreq;
}

// Bare timebase counter, no conversion. mach_absolute_time() is a register read.
uint64_t ano_timestamp_ticks() {
    return mach_absolute_time();
}

// Convert raw mach ticks (value or delta) to nanoseconds, overflow-safe via the cached timebase.
uint64_t ano_ticks_to_ns(uint64_t ticks) {

    uint64_t freq = timebase_freq();

    // Split into seconds and sub-seconds to scale without overflow.
    uint64_t largePart = ticks / freq;    // Seconds
    uint64_t smallPart = ticks % freq;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000LL / freq;
    return smallPart + (largePart * 1000000000LL);
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
static uint64_t ano_ns_to_ticks(uint64_t ns) {

    uint64_t freq = timebase_freq();

    // Split into seconds and sub-seconds to scale without overflow.
    uint64_t largePart = ns / 1000000000LL;     // Seconds
    uint64_t smallPart = ns % 1000000000LL;     // Sub-seconds

    return largePart * freq + smallPart * freq / 1000000000LL;
}

// Tail window spun instead of slept: kernel timer leeway cannot land closer than this.
#define ANO_SLEEP_SPIN_NS 500000ULL

// High-res sleep that DOES yield: absolute mach_wait_until in half-remainder steps, spin last ANO_SLEEP_SPIN_NS.
// QoS stretches relative waits ~1.5x (nanosleep and mach_wait_until alike); half-remainder is immune below 2x.
// Absolute deadline re-arms early wakeups (KERN_ABORTED) without drift.
int ano_sleep(uint64_t us) {

    uint64_t waitTicks = ano_ns_to_ticks(us * 1000ULL);
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
