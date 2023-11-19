/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__linux__)
#include "anoptic_time.h"
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

// High resolution relative timestamps from this local machine.
uint64_t ano_timestamp_raw() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == -1) {
        perror("clock_gettime");
        return 0; // Indicate an error occurred
    }
    return (uint64_t)(ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

// return ano_timestamp_raw, but scaled to microseconds.
uint64_t ano_timestamp_us() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == -1) {
        perror("clock_gettime");
        return 0; // Indicate an error occurred
    }
    return (uint64_t)(ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == -1) {
        perror("clock_gettime");
        return 0; // Indicate an error occurred
    }
    return (uint32_t)(ts.tv_sec * 1000) + (ts.tv_nsec / 1000000LL);
}


// Generic timestamps supporting the current date, plus networking adjustments.

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

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix)
void ano_sleep(uint64_t us) {
    struct timespec request = {0};
    struct timespec remaining = {0};

    // Convert the sleep time from microseconds to seconds and nanoseconds
    request.tv_sec = us / 1000000LL;
    request.tv_nsec = (us % (uint64_t)1000000LL) * 1000;

    printf("seconds requested:\t\t%ll\n", request.tv_sec);
    printf("nanoseconds requested:\t\t%ll\n", request.tv_nsec);

    // Sleep for the relative time
    while (clock_nanosleep(CLOCK_MONOTONIC_RAW, 0, &request, &remaining) == -1) {
        if (errno != EINTR) {
            perror("clock_nanosleep");
            break;
        }

        // Might be something wrong with this loop.

        // In case we get interrupted, continue sleeping for the remaining time
        request = remaining;
    }
}

#endif