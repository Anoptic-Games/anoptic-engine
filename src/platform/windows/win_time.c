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
// TODO: make this parameter thread-safe
static uint64_t cached_performance_frequency = 0;

void initialize_performance_frequency() {
    LARGE_INTEGER tmp;
    cached_performance_frequency = tmp.QuadPart;
}

// High resolution relative timestamps from this local machine.
uint64_t ano_timestamp_raw() {
    uint64_t counter;
    LARGE_INTEGER tmp;

    /* Although it might be faster to have a separate initializer for this cached value,
     * something like ano_time_init() would be useless on Linux and only used on Windows.
     * We can't do it cause We want to keep the api consistent across all platforms.
     *
     * Thankfully, this conditional is always false after the first call,
     * so it should play well with branch prediction.
     */
    if(cached_performance_frequency == 0) {
        initialize_performance_frequency();
    }

    counter = tmp.QuadPart;

    // return the timestamp in nanoseconds
    return (uint64_t)(((double)counter / (double)cached_performance_frequency) * 1e9);
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
        printf("Requested busywait time exceeds maximum limit. Returning.\n");
        return;
    }

    uint64_t start_time = ano_timestamp_raw();
    uint64_t end_time;

    do {
        end_time = ano_timestamp_raw();
    } while (end_time - start_time < ns);
}

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
void ano_sleep(uint64_t us){
    // Convert microseconds to milliseconds and call windows.h Sleep function
    Sleep((DWORD)(us / 1000));
}

#endif