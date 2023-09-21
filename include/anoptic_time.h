/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

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

uint64_t ano_timestamp_ntp();   // Network Transfer Protocol-adjusted timestamp.


// Start and operate on a timespan within the a specified thread.
void ano_timespan_start(uint32_t threadIndex);  // Starts a time span from 0 seconds on the specified thread.

uint64_t ano_timespan_get(uint32_t threadIndex); // Get the elapsed span of time.

void ano_timespan_pause(uint32_t threadIndex);  // Pauses/freezes the counter so that get() the moment in time when this was called.

void ano_timestamp_resume(uint32_t threadIndex); // Resumes the counter.

void ano_timespan_stop(uint32_t threadIndex);   // Stops the timespan counter.

#endif // ANOPTIC_TIME_H
// end of include guard, end of file