/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Platform time: monotonic clocks, civil time, busywait, and yielding sleep.

#ifndef ANOPTIC_TIME_H
#define ANOPTIC_TIME_H

#include <stdint.h>
#include <time.h>


/* Timestamps */

// Busywait hard cap (ns). Default: 1000000000ULL (1 second).
#define MAX_BUSYWAIT_NS 1000000000ULL

// High-resolution monotonic timestamp in nanoseconds.
uint64_t ano_timestamp_raw();

// Raw monotonic hardware counter, no unit conversion 〜 cheapest stamp.
// Units: mach ticks (Darwin), TSC or QPC counts (Windows), nanoseconds (Linux). Value or delta; convert via ano_ticks_to_ns (hot path stamp, cold path convert).
uint64_t ano_timestamp_ticks();

// Convert a raw counter value or delta from ano_timestamp_ticks to nanoseconds.
uint64_t ano_ticks_to_ns(uint64_t ticks);

// High-resolution monotonic timestamp, scaled to microseconds.
uint64_t ano_timestamp_us();

// High-resolution monotonic timestamp, scaled to milliseconds.
uint32_t ano_timestamp_ms();

// Unix UTC timestamp (seconds). Not guaranteed monotonic.
int64_t ano_timestamp_unix();


/* Civil Time */

// Platform-agnostic broken-down local civil time.
// The platform layer wraps localtime_r / localtime_s.
typedef struct {
    int year;    // full year, e.g. 2026
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-60 (60 on a leap second)
} ano_datetime;

// Unix seconds (ano_timestamp_unix) to local civil time. All-zero on failure.
ano_datetime ano_localtime(int64_t unix_seconds);


/* Sleep */

// Spin the calling thread for ns nanoseconds. Cap is MAX_BUSYWAIT_NS.
int ano_busywait(uint64_t ns);

// Sleep for us microseconds via OS facilities. Yields to the scheduler.
int ano_sleep(uint64_t us);


#endif // ANOPTIC_TIME_H
