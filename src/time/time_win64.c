/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)
#include "anoptic_time.h"
#include <Windows.h>
#include <time.h>
#include <stdatomic.h>

#if defined(DEBUG_BUILD)
#include <stdio.h>
#endif


/* Precision Timestamps */

// cache the performance frequency (acquired only once)
static _Atomic uint64_t cached_performance_frequency = 0;

int initialize_performance_frequency() {

    LARGE_INTEGER tmp;
    if (QueryPerformanceFrequency(&tmp) == 0) {
        // Handle error
        printf("Failed to query performance frequency.");
        return -1;
    }
    cached_performance_frequency = tmp.QuadPart;
    printf("\nPerformance Frequency: %llu\n\n", cached_performance_frequency);

    return 0;
}

uint64_t ano_timestamp_raw() {

    uint64_t counter;
    LARGE_INTEGER tmp;

    // Cache the performance frequency the first time we run the program
    if(cached_performance_frequency == 0) {
        if (initialize_performance_frequency() != 0) {
            printf("Exiting due to error with fetching performance frequency.");
            return UINT64_MAX; // Indicate an error occurred
        }
    }

    if(QueryPerformanceCounter(&tmp) == 0) {
        printf("Error getting Windows performance Counter.");
        return UINT64_MAX; // Indicate an error occurred.
    }
    counter = tmp.QuadPart;

    // Get the queried amount into two separate buffers for computing while avoiding overflow.
    uint64_t largePart = counter / cached_performance_frequency;    // Seconds
    uint64_t smallPart = counter % cached_performance_frequency;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000LL / cached_performance_frequency;
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
        return INT64_MIN; // Out-of-range sentinel value
    }

    return (int64_t)current_time;
}


/* Waiting Facilities */

// Spinlock the current thread for approximately ns nanoseconds.
int ano_busywait(uint64_t ns) {

    if (ns > MAX_BUSYWAIT_NS) {
        printf("Requested busywait time exceeds maximum limit. Exiting.\n");
        return -1; // error
    }

    uint64_t start_time = ano_timestamp_raw();
    uint64_t end_time;

    do {
        end_time = ano_timestamp_raw();
    } while (end_time - start_time < ns && start_time != 0 && end_time != 0);

    return 0; // success
}

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
int ano_sleep(uint64_t us) {

    // Convert microseconds to milliseconds and call windows.h Sleep function
    Sleep((DWORD)(us / 1000));

    return 0; // success
}

#endif