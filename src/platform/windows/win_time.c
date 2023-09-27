/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#if defined(_WIN32)
#include "anoptic_time.h"
#include <Windows.h>
#include <stdio.h>

// High resolution relative timestamps from this local machine.
uint64_t ano_timestamp_raw() {
    printf("ano_timestamp_raw\tTest!\n");
}

// return ano_timestamp_raw, but scaled to microseconds.
uint64_t ano_timestamp_us(){
    printf("ano_timestamp_us\tTest!\n");
}

// return ano_timestamp_raw, but truncated to ms.
uint32_t ano_timestamp_ms(){
    printf("ano_timestamp_ms\tTest!\n");
}


// Generic timestamps supporting the current date, plus networking adjustments.

// UTC timestamp.
uint64_t ano_timestamp_utc(){
    printf("ano_timestamp_utc\tTest!\n");
}

// Unix timestamp.
uint64_t ano_timestamp_unix(){
    printf("ano_timestamp_unix\tTest!\n");
}

// Network Time Protocol-adjusted timestamp. NOT guaranteed monotonic.
uint64_t ano_timestamp_ntp(){
    printf("ano_timestamp_ntp\tTest!\n");
}


// Waiting facilities

// Spinlock the current thread for approximately ns nanoseconds.
void ano_busywait(uint64_t ns){
    printf("ano_busywait\tTest!\n");
}

// Use OS time facilities for high-res sleep that DOES give up thread execution. (nanosleep on Unix, MultiMedia Timer on Windows)
// UNIX: if more than one second long, just call usleep repeatedly lol
void ano_sleep(uint64_t us){
    printf("ano_sleep\tTest!\n");
}



#endif