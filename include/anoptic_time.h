/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/// \file
/// \brief Anoptic Time Management API.

#ifndef ANOPTIC_TIME_H
#define ANOPTIC_TIME_H

#include <stdint.h>

/// \defgroup TimeAPI Anoptic Time Management API
/// @{

/// \defgroup Config Time Configuration Parameters
/// @{
/// \brief Do not allow busywait to exceed this max time.
/// Default: 1000000000ULL (1 second)
#define MAX_BUSYWAIT_NS 1000000000ULL
/// @}

/// \defgroup HighResTime High-Resolution Local Time
/// @{
/// \brief Obtain a high-resolution monotonic raw timestamp in nanoseconds.
uint64_t ano_timestamp_raw();

/// \brief Obtain a high-resolution monotonic raw timestamp, scaled to microseconds.
uint64_t ano_timestamp_us();

/// \brief Obtain a high-resolution monotonic raw timestamp, scaled to milliseconds.
uint32_t ano_timestamp_ms();
/// @}

/// \defgroup GenericTime Generic Date/Timestamps
/// @{
/// \brief Get Unix UTC timestamp.
/// \note Timestamps are not guaranteed to be monotonic.
int64_t ano_timestamp_unix();

/// \brief Get Network Time Protocol-adjusted timestamp.
/// \note Timestamps are not guaranteed to be monotonic.
// TODO: Figure out network integration.
int64_t ano_timestamp_ntp();
/// @}

/// \defgroup WaitSleep Wait and Sleep Facilities
/// @{
/// \brief Spinlock the current thread for ns nanoseconds.
/// \param ns The number of nanoseconds to busy-wait.
/// \note This has a max time limit defined by MAX_BUSYWAIT_NS.
/// \remarks Use when you need extremely fine wait intervals.
void ano_busywait(uint64_t ns);

/// \brief Sleep for us microseconds using OS time facilities.
/// \param us The number of microseconds to sleep.
/// \note This method gives up thread execution to the OS scheduler.
void ano_sleep(uint64_t us);
/// @}

/// @}
#endif // ANOPTIC_TIME_H
