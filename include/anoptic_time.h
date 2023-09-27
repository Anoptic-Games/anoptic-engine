/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANOPTIC_TIME_H
#define ANOPTIC_TIME_H

#include <stdint.h>

//TODO: Doxygen + Finalize


/*--- Local High-Resolution Timestamps ---*/

// return a high-resolution monotonic raw timestamp in nanoseconds.
uint64_t ano_timestamp_raw();

// return raw timestamp, but scaled to microseconds.
uint64_t ano_timestamp_us();

// return raw timestamp, but scaled to milliseconds.
uint32_t ano_timestamp_ms();


/*--- Generic datestamps ---*/
/* not guaranteed monotonic */

// UTC timestamp.
uint64_t ano_timestamp_utc();

// Unix timestamp.
uint64_t ano_timestamp_unix();

// Network Time Protocol-adjusted timestamp.
uint64_t ano_timestamp_ntp();


/*--- Wait/Sleep Facilities ---*/

// Spinlock the current thread for approximately ns nanoseconds.
void ano_busywait(uint64_t ns);

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
// UNIX NOTE: if more than one second long, will need to call usleep repeatedly
void ano_sleep(uint64_t us);

#endif // ANOPTIC_TIME_H