//
// Created by Pyrus on 2023-11-19.
//
// Testing Time
#include <stdio.h>

#include "anoptic_time.h"

int testTime() {
    printf("Running in debug mode!\n");

    // Example: Printing current Unix timestamp as a date-time string
    time_t currentTime = (time_t)ano_timestamp_unix();
    printf("Current Date and Time: %s", ctime(&currentTime));

    // Testing durations
    uint64_t durations[] = {100, 500, 1000, 1600, 5000, 10000, 160000,
                            16000000, 100000000, 1000000000}; // in nanoseconds
    uint64_t start, end, elapsed;

    // Test with ano_busywait()
    for (int i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
        printf("\nTesting ano_busywait for %llu ns\n", durations[i]);

        start = ano_timestamp_raw();
        ano_busywait(durations[i]);
        end = ano_timestamp_raw();

        elapsed = end - start;
        printf("Expected wait:\t%llu ns\n", durations[i]);
        printf("Actual wait:\t%llu ns\n", elapsed);
    }

    // Test with ano_sleep()
    for (int i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
        printf("\nTesting ano_sleep for %llu ns\n", durations[i]);

        start = ano_timestamp_raw();
        ano_sleep(durations[i] / 1000);  // Convert ns to us for ano_sleep
        end = ano_timestamp_raw();

        elapsed = end - start;
        printf("Expected wait:\t%llu ns\n", durations[i]);
        printf("Actual wait:\t%llu ns\n", elapsed);
    }

    return 0;
}