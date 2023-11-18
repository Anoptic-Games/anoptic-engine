/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)
#include "anoptic_time.h"
#include <Windows.h>
#include <time.h>

#if defined(DEBUG_BUILD)
#include <stdio.h>
#endif

// cache the performance frequency (acquired only once)
static uint64_t cached_performance_frequency = 0;

void initialize_performance_frequency() {
    LARGE_INTEGER tmp;
    if (QueryPerformanceFrequency(&tmp) == 0) {
        // Handle error
        printf("Failed to query performance frequency.");
    }
    cached_performance_frequency = tmp.QuadPart;
    printf("\nPerformance Frequency: %llu\n\n", cached_performance_frequency);
}

uint64_t ano_timestamp_raw() {
    uint64_t counter;
    LARGE_INTEGER tmp;

    /* It might be faster to have a separate initializer for this cached value,
     * something like ano_time_init().
     */

    // TODO: Replace with KeQueryPerformanceCounter
    // + Monotonic
    // + Might be higher-resolution
    if(cached_performance_frequency == 0) {
        initialize_performance_frequency();
    }

    if(QueryPerformanceCounter(&tmp) == 0) {
        printf("Error getting Windows performance Counter.");
        return 0;
    }
    counter = tmp.QuadPart;


    uint64_t largePart = counter / cached_performance_frequency;    // Seconds
    uint64_t smallPart = counter % cached_performance_frequency;    // Sub-seconds
    smallPart = smallPart * 1000000000LL / cached_performance_frequency;

    uint64_t timeStamp = smallPart + (largePart * 1000000000LL);

    return timeStamp;

    // previous technique was prone to overflows
    //return (uint64_t)(((double)counter / (double)cached_performance_frequency) * 1e9);
}

// return ano_timestamp_raw, but scaled to microseconds.
uint64_t ano_timestamp_us() {
    uint64_t timestamp_ns = ano_timestamp_raw();
    return timestamp_ns / 1000;  // Convert nanoseconds to microseconds
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms() {
    uint64_t timestamp_ns = ano_timestamp_raw();
    return (uint32_t)((double)timestamp_ns / 1e6);  // Convert nanoseconds to milliseconds
}


// Generic timestamps supporting the current date, plus networking adjustments.

// Unix UTC timestamp.
int64_t ano_timestamp_unix() {
    time_t current_time;
    current_time = time(NULL);

    // Error handling
    if (current_time == (time_t)-1) {
        // TODO: Add verbose error logging
        return INT64_MIN; // Out-of-range sentinel value
    }

    return (int64_t)current_time;
}

// Network Time Protocol-adjusted timestamp. NOT guaranteed monotonic.
int64_t ano_timestamp_ntp(){
    printf("ano_timestamp_ntp\tTest!\n");
    // TODO: Fill with network socket stuff etc
    return 0;
}


// Waiting facilities

// Spinlock the current thread for approximately ns nanoseconds.
void ano_busywait(uint64_t ns) {
    if (ns > MAX_BUSYWAIT_NS) {
        printf("Requested busywait time exceeds maximum limit. Exiting.\n");
        return;
    }

    uint64_t start_time = ano_timestamp_raw();
    uint64_t end_time;

    do {
        end_time = ano_timestamp_raw();
    } while (end_time - start_time < ns && start_time != 0 && end_time != 0);
}

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
void ano_sleep(uint64_t us){
    // Convert microseconds to milliseconds and call windows.h Sleep function
    Sleep((DWORD)(us / 1000));
}

#endif