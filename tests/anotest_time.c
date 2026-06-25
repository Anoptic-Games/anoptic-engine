/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */
#include <stdio.h>
#include <inttypes.h> // PRIu64: portable uint64_t format across LP64/LLP64

#include "anoptic_time.h"

uint64_t firshhhahafigits(uint64_t num, uint64_t n) {

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

    printf("nanoseconds: %" PRIu64 "\n", nanoStamp);
    printf("microseconds: %" PRIu64 "\n", microStamp);
    printf("milliseconds: %" PRIu64 "\n", (uint64_t)milliStamp);

    uint64_t first4nano = firshhhahafigits(nanoStamp, 4);
    uint64_t first4micro = firshhhahafigits(microStamp, 4);
    uint64_t first4milli = firshhhahafigits((uint64_t)milliStamp, 4);

    if (!(first4nano == first4micro && first4micro == first4milli)) {
        printf("anoptic_time.h: various resolution timestamps inconsistent.\n");
        return -1;
    }

    return 0;
}

/* Resolution tests: assert the waits actually land near their target across every scale.
 * (These replace the earlier print-only sleep checks, which never failed on a wrong wait.) */

#define SLEEP_SAMPLES   8   // best-of-N: scheduler hiccups inflate the worst case, not the best
#define BUSY_SAMPLES    5

// One ano_sleep resolution case. Asserts no sample ever wakes early (the hard contract), and the
// best sample lands within an overshoot tolerance (the achievable resolution).
//   in:  us (uint64_t) requested sleep, microseconds
//   out: int, 0 pass, 1 fail
static int sleepCase(uint64_t us) {
    uint64_t want = us * 1000ULL;                       // ns
    // Never-early floor: tolerate only clock quantization, not Sleep()-style truncation.
    uint64_t floorSlack = want / 100ULL + 20000ULL;     // 1% + 20us
    // Achievable-resolution ceiling on the best sample: generous so it isn't scheduler-flaky,
    // tight enough that 15.6ms Sleep granularity on a sub-ms request still fails.
    uint64_t ceil = want + (want / 2ULL > 2000000ULL ? want / 2ULL : 2000000ULL);

    uint64_t best = UINT64_MAX;
    int early = 0;
    for (int i = 0; i < SLEEP_SAMPLES; i++) {
        uint64_t t0 = ano_timestamp_raw();
        if (ano_sleep(us) != 0) {
            printf("  [FAIL] ano_sleep(%" PRIu64 "us) returned nonzero\n", us);
            return 1;
        }
        uint64_t el = ano_timestamp_raw() - t0;
        if (el + floorSlack < want)                     // woke meaningfully early
            early = 1;
        if (el < best)
            best = el;
    }

    int ok = !early && best <= ceil;
    printf("  [%s] sleep %8" PRIu64 "us  best=%8" PRIu64 "ns  floor>=%" PRIu64 "ns  ceil<=%" PRIu64 "ns\n",
           ok ? "PASS" : "FAIL", us, best, want > floorSlack ? want - floorSlack : 0, ceil);
    if (early)
        printf("         ^ a sample woke before the requested duration (timer too coarse / truncating)\n");
    return ok ? 0 : 1;
}

// One ano_busywait resolution case. The spin must elapse at least its target (the Windows ano_sleep
// spin-tail relies on this) and not wildly overshoot.
//   in:  ns (uint64_t) requested busy-wait
//   out: int, 0 pass, 1 fail
static int busyCase(uint64_t ns) {
    uint64_t floorSlack = ns / 100ULL + 1000ULL;        // 1% + 1us
    uint64_t ceil = ns + (ns / 10ULL > 100000ULL ? ns / 10ULL : 100000ULL);

    uint64_t best = UINT64_MAX;
    int early = 0;
    for (int i = 0; i < BUSY_SAMPLES; i++) {
        uint64_t t0 = ano_timestamp_raw();
        if (ano_busywait(ns) != 0) {
            printf("  [FAIL] ano_busywait(%" PRIu64 "ns) returned nonzero\n", ns);
            return 1;
        }
        uint64_t el = ano_timestamp_raw() - t0;
        if (el + floorSlack < ns)
            early = 1;
        if (el < best)
            best = el;
    }

    int ok = !early && best <= ceil;
    printf("  [%s] busywait %9" PRIu64 "ns  best=%9" PRIu64 "ns  ceil<=%" PRIu64 "ns\n",
           ok ? "PASS" : "FAIL", ns, best, ceil);
    return ok ? 0 : 1;
}

// Sweep both primitives across sub-ms to 100ms scales and assert resolution at each.
//   out: int, count of failed cases
static int testResolution(void) {
    int fails = 0;

    // Warm up: the first ano_sleep lazily creates the per-thread waitable timer (Windows); keep that
    // one-time cost out of the timed samples.
    ano_sleep(1000);
    ano_busywait(1000);

    printf("\nano_busywait resolution sweep:\n");
    uint64_t busyNs[] = {1000, 10000, 100000, 1000000, 10000000}; // 1us .. 10ms
    for (size_t i = 0; i < sizeof busyNs / sizeof busyNs[0]; i++)
        fails += busyCase(busyNs[i]);

    printf("\nano_sleep resolution sweep:\n");
    // sub-ms (spin-only on Windows), the 1ms boundary, and coarse scales.
    uint64_t sleepUs[] = {50, 100, 250, 500, 1000, 2000, 5000, 10000, 50000, 100000};
    for (size_t i = 0; i < sizeof sleepUs / sizeof sleepUs[0]; i++)
        fails += sleepCase(sleepUs[i]);

    return fails;
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

    /* Resolution Tests: assert ano_busywait/ano_sleep land near their target across all scales */
    int resFails = testResolution();
    if (resFails != 0) {
        printf("\nanoptic_time.h: %d resolution case(s) failed.\n", resFails);
        return -1;
    }

    printf("\nanoptic_time.h: All Tests passed!\n");
    return 0;
}
