//
// Created by Pyrus on 2023-11-19.
//
// Testing Time
#include <stdio.h>

#include "anoptic_time.h"

uint64_t firstNDigits(uint64_t num, uint64_t n) {

    uint64_t divisor = 1;
    for (uint64_t i = 1; i < n; i++) {
        divisor *= 10;

        if (divisor >= num)
            return num;
    }

    while (num >= divisor) {
        num /= 10;
    }

    return num;
}

int testDate() {
    printf("Testing current date.\n");

    int64_t rawTS = ano_timestamp_unix();
    if (rawTS == INT64_MIN)
        return -1;

    time_t currentTime = (time_t)rawTS;
    printf("Current Date and Time: %s\n", ctime(&currentTime));

    return 0;
}

int testTimeStamps() {
    printf("Testing timestamps across various resolutions\n");

    uint64_t nanoStamp = ano_timestamp_raw();
    if (nanoStamp == UINT64_MAX) {
        printf("anoptic_time.h: failed to retrieve nanosecond timestamp.\n");
        return -1;
    }


    uint64_t microStamp = ano_timestamp_us();
    if (microStamp == UINT64_MAX) {
        printf("anoptic_time.h: failed to retrieve microsecond timestamp.\n");
        return -1;
    }


    uint32_t milliStamp = ano_timestamp_ms();
    if (milliStamp == UINT32_MAX) {
        printf("anoptic_time.h: failed to retrieve millisecond timestamp.\n");
        return -1;
    }

    printf("nanoseconds: %llu\n", nanoStamp);
    printf("microseconds: %llu\n", microStamp);
    printf("milliseconds: %llu\n", (uint64_t)milliStamp);

    uint64_t first4nano = firstNDigits(nanoStamp, 4);
    uint64_t first4micro = firstNDigits(microStamp, 4);
    uint64_t first4milli = firstNDigits((uint64_t)milliStamp, 4);

    if (!(first4nano == first4micro && first4micro == first4milli)) {
        printf("anoptic_time.h: various resolution timestamps inconsistent.\n");
        return -1;
    }

    return 0;
}

int testBusyWait(uint64_t duration) {
    printf("\nTesting ano_busywait for %llu ns\n", duration);

    int status = 0;

    uint64_t start = ano_timestamp_raw();
    status = ano_busywait(duration);
    uint64_t end = ano_timestamp_raw();

    uint64_t elapsed = end - start;
    printf("Expected wait:\t%llu ns\n", duration);
    printf("Actual wait:\t%llu ns\n", elapsed);

    return status;
}

int testOSSleep(uint64_t duration) {
    printf("\nTesting ano_sleep for %llu ns\n", duration);

    int status = 0;

    uint64_t start = ano_timestamp_raw();
    status = ano_sleep(duration / 1000);  // Convert ns to us for ano_sleep
    uint64_t end = ano_timestamp_raw();

    uint64_t elapsed = end - start;
    printf("Expected wait:\t%llu ns\n", duration);
    printf("Actual wait:\t%llu ns\n", elapsed);

    return status;
}

int main() {

    int status = 0;

    /* Unix Datestamp Tests */
    status = testDate();
    if (status != 0) {
        printf("anoptic_time.h: testDate() failed to create valid timestamp.\n");
        return -1;
    }

    /* Precision Timestamp Tests */
    status = testTimeStamps();
    if (status != 0) {
        printf("anoptic_time.h: timestamp error.\n");
        return -1;
    }

    /* Sleep Tests */
    uint64_t durations[] = {100, 500, 1000, 1600, 5000, 10000, 160000,
                            16000000, 100000000, 1000000000}; // in nanoseconds
    uint64_t start, end, elapsed;

    // Test with ano_busywait()
    for (int i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
        status = testBusyWait(durations[i]);
        if (status != 0) {
            printf("anoptic_time.h: ano_busywait() failed with duration=%llu\n", durations[i]);
            return -1;
        }
    }

    // Test with ano_sleep()
    for (int i = 0; i < sizeof(durations) / sizeof(durations[0]); i++) {
        status = testOSSleep(durations[i]);
        if (status != 0) {
            printf("anoptic_time.h: ano_sleep() failed with duration=%llu\n", durations[i]);
            return -1;
        }
    }


    printf("anoptic_time.h: All Tests passed!\n");
    return 0;
}