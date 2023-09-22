/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTIC_TIME_H
#define ANOPTIC_TIME_H

#include <stdint.h>

//TODO: Doxygen + Finalize

// High resolution relative timestamps from this local machine.
uint64_t ano_timestamp_raw();   // return a high-resolution monotonic raw timestamp in nanoseconds

uint64_t ano_timestamp_us();    // return ano_timestamp_raw, but scaled to microseconds.
 
uint32_t ano_timestamp_ms();    // return ano_timestamp_raw, but truncated to ms.


// Generic timestamps supporting the current date, plus networking adjustments.
uint64_t ano_timestamp_utc();   // UTC timestamp.

uint64_t ano_timestamp_unix();  // Unix timestamp.

uint64_t ano_timestamp_ntp();   // Network Time Protocol-adjusted timestamp. NOT guaranteed monotonic.


// Start and operate on a timespan within the calling thread.

void ano_timespan_start(uint32_t spanIndex);  // Starts a time span from 0 seconds on the calling thread.

uint64_t ano_timespan_get(uint32_t spanIndex); // Get the elapsed span of time within the calling thread.

void ano_timespan_stop();   // Stops the thread's timespan counters and frees its data.


// Waiting facilities
void ano_busywait(uint64_t ns); // Spinlock the current thread for approximately ns nanoseconds.

void ano_sleep(uint64_t us);     // Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
// UNIX: if more than one second long, just call usleep repeatedly lol

#endif // ANOPTIC_TIME_H
// end of include guard, end of file