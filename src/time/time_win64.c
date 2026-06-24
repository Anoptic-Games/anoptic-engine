/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)
#include "anoptic_time.h"
#include <Windows.h>
#include <time.h>
#include <stdatomic.h>
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

uint64_t ano_timestamp_raw() {

    uint64_t counter;
    LARGE_INTEGER tmp;

    // Cache the performance frequency on first run.
    if(cachedPerfFreq == 0) {
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

    // Split into two parts to scale without overflow.
    uint64_t largePart = counter / cachedPerfFreq;    // Seconds
    uint64_t smallPart = counter % cachedPerfFreq;    // Sub-seconds

    // Recombine the two parts.
    smallPart = smallPart * 1000000000LL / cachedPerfFreq;
    uint64_t timeStamp = smallPart + (largePart * 1000000000LL);

    return timeStamp;
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

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
int ano_sleep(uint64_t us) {

    // Convert microseconds to milliseconds and call windows.h Sleep function
    Sleep((DWORD)(us / 1000));

    return 0; // success
}

#endif