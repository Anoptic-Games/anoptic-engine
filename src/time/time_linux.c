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

/* Precision Timestamps */

// High resolution relative timestamps from this local machine.
uint64_t ano_timestamp_raw() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        return UINT64_MAX; // Indicate an error occurred.
    }
    return (uint64_t)(ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

// return ano_timestamp_raw, but scaled to microseconds.
uint64_t ano_timestamp_us() {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        return UINT64_MAX; // Indicate an error occurred
    }
    return (uint64_t)(ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms() {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        return UINT32_MAX; // Indicate an error occurred.
    }
    return (uint32_t)(ts.tv_sec * 1000) + (ts.tv_nsec / 1000000LL);
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

// Network Time Protocol-adjusted timestamp. NOT guaranteed monotonic.
int64_t ano_timestamp_ntp(){
    printf("ano_timestamp_ntp\tTest!\n");
    // TODO: Fill with network socket stuff etc
    return 0;
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

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix)
int ano_sleep(uint64_t us) {
    struct timespec request = {0};
    struct timespec remaining = {0};

    // Convert the sleep time from microseconds to seconds and nanoseconds
    request.tv_sec = us / 1000000LL;
    request.tv_nsec = (us % (uint64_t)1000000LL) * 1000;

    // Sleep for the relative time
    int sleepStatus;
    while ((sleepStatus = clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remaining)) != 0) {
        if (sleepStatus == EINTR) {
            request = remaining;
            printf("Interrupted by signal handler\n");
        } else {
            perror("clock_nanosleep");
            printf("clock_nanosleep error with status: %d\n", sleepStatus);
            return errno;
        }
    }

    return 0; // success
}

#endif
