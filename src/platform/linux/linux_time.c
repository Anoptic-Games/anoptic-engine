/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(__linux__)
#include <unistd.h>
#include "anoptic_time.h"
#include <stdio.h>

// High resolution relative timestamps from this local machine.
uint64_t ano_timestamp_raw() {
    printf("Test!\n");
}

// return ano_timestamp_raw, but scaled to microseconds.
uint64_t ano_timestamp_us(){
    printf("Test!\n");
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms(){
    printf("Test!\n");
}


// Generic timestamps supporting the current date, plus networking adjustments.

// UTC timestamp.
uint64_t ano_timestamp_utc(){
    printf("Test!\n");
}

// Unix timestamp.
uint64_t ano_timestamp_unix(){
    printf("Test!\n");
}

// Network Time Protocol-adjusted timestamp. NOT guaranteed monotonic.
uint64_t ano_timestamp_ntp(){
    printf("Test!\n");
}

// Waiting facilities

// Spinlock the current thread for approximately ns nanoseconds.
void ano_busywait(uint64_t ns){
    printf("Test!\n");
}

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
// UNIX: if more than one second long, just call usleep repeatedly lol
void ano_sleep(uint64_t us){
    printf("Test!\n");
}



#endif