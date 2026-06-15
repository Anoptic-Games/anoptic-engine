/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Darwin timing. The macOS counterpart of the Linux CLOCK_MONOTONIC and the
// Windows QueryPerformanceCounter/QueryPerformanceFrequency path: the hardware
// timebase counter read by mach_absolute_time() (CNTVCT_EL0 on Apple Silicon,
// served from the commpage — no syscall), converted to nanoseconds via the
// mach_timebase_info() numer/denom ratio. On Apple Silicon the counter ticks at
// 24 MHz (numer/denom = 125/3, ~41.67 ns/tick), so the ratio MUST be applied —
// raw ticks are not nanoseconds. The ratio is cached atomically and the
// conversion is overflow-safe (same split technique as the Windows file).
// One divergence from Linux: macOS libc has no clock_nanosleep, so ano_sleep
// uses nanosleep (relative interval, -1/errno convention).

#if defined(__APPLE__)
#include "anoptic_time.h"
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>


/* Precision Timestamps */

// Cache the timebase counter frequency in ticks/second (acquired only once) —
// the mach analog of the Windows QueryPerformanceFrequency. Derived from the
// timebase: 1 tick = numer/denom ns, so ticks/sec = 1e9 * denom / numer
// (24,000,000 on Apple Silicon; 1e9 on Intel, where ticks are already ns).
static _Atomic uint64_t cached_timebase_frequency = 0;

static int initialize_timebase() {

    mach_timebase_info_data_t tb;
    if (mach_timebase_info(&tb) != KERN_SUCCESS || tb.numer == 0) {
        printf("Failed to query mach timebase.\n");
        return -1;
    }
    cached_timebase_frequency = (uint64_t)1000000000ULL * tb.denom / tb.numer;

    #ifdef DEBUG_BUILD
    printf("\nTimebase Frequency: %llu (numer=%u denom=%u)\n\n",
           cached_timebase_frequency, tb.numer, tb.denom);
    #endif

    return 0;
}

uint64_t ano_timestamp_raw() {

    // Cache the timebase frequency the first time we run the program.
    if (cached_timebase_frequency == 0) {
        if (initialize_timebase() != 0) {
            printf("Exiting due to error with fetching mach timebase.");
            return UINT64_MAX; // Indicate an error occurred.
        }
    }

    uint64_t counter = mach_absolute_time();

    // Split the counter into seconds and sub-seconds to scale without overflow.
    uint64_t largePart = counter / cached_timebase_frequency;    // Seconds
    uint64_t smallPart = counter % cached_timebase_frequency;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000LL / cached_timebase_frequency;
    uint64_t timeStamp = smallPart + (largePart * 1000000000LL);

    return timeStamp;
}

// return ano_timestamp_raw, but scaled to microseconds.
uint64_t ano_timestamp_us() {

    uint64_t timestamp_ns = ano_timestamp_raw();
    if (timestamp_ns == UINT64_MAX)
        return UINT64_MAX; // Indicate an error occurred.

    return timestamp_ns / 1000;  // Convert nanoseconds to microseconds
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms() {

    uint64_t timestamp_ns = ano_timestamp_raw();
    if (timestamp_ns == UINT64_MAX)
        return UINT32_MAX; // Indicate an error occurred.

    return (uint32_t)(timestamp_ns / 1000000LL);  // Convert nanoseconds to milliseconds
}


/* Generic Date-Time Stamps */

// Unix UTC timestamp.
int64_t ano_timestamp_unix() {

    time_t current_time;
    current_time = time(NULL);

    // Error handling
    if (current_time == (time_t)-1) {
        perror("time()");
        return INT64_MIN; // Out-of-range sentinel value
    }

    return (int64_t)current_time;
}


/* Waiting Facilities */

// Spinlock the current thread for approximately ns nanoseconds.
int ano_busywait(uint64_t ns) {

    if (ns > MAX_BUSYWAIT_NS) {
        printf("Requested busywait time exceeds maximum limit. Returning.\n");
        return -1; // failure
    }

    uint64_t start_time = ano_timestamp_raw();
    uint64_t end_time;

    do {
        end_time = ano_timestamp_raw();
    } while (end_time - start_time < ns);

    return 0; // success
}

// Use OS time facilities for high-res sleep that DOES give up thread execution.
// macOS has no clock_nanosleep; nanosleep sleeps a relative interval and, on
// interruption, returns -1/EINTR with the unslept remainder in `remaining`.
int ano_sleep(uint64_t us) {

    struct timespec request = {0};
    struct timespec remaining = {0};

    // Convert the sleep time from microseconds to seconds and nanoseconds
    request.tv_sec = us / 1000000LL;
    request.tv_nsec = (us % (uint64_t)1000000LL) * 1000;

    // Sleep for the relative time
    while (nanosleep(&request, &remaining) != 0) {
        if (errno == EINTR) {
            request = remaining;
            printf("Interrupted by signal handler\n");
        } else {
            perror("nanosleep");
            return errno;
        }
    }

    return 0; // success
}

#endif
