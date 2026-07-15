/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Common TU: session stamp and log directory.

#include "anoptic_filesystem.h"
#include "filesystem/filesystem_internal.h"

#include <anoptic_time.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>


/* Session Stamp */

// One stamp per process, latched by first caller. Racing loser spins on winner.
// Output: "YYYY-MM-DD_XXXXXX", static storage.
const char *ano_fs_session_stamp(void)
{
    static _Atomic int state;   // 0 unset, 1 building, 2 ready
    static char stamp[24];      // "YYYY-MM-DD_XXXXXX" is 17 + NUL
    if (atomic_load_explicit(&state, memory_order_acquire) != 2) {
        int expect = 0;
        if (atomic_compare_exchange_strong_explicit(&state, &expect, 1,
                memory_order_acquire, memory_order_acquire)) {
            ano_datetime d = ano_localtime(ano_timestamp_unix());
            unsigned ctr = (unsigned)(ano_timestamp_ticks() % 1000000u);  // low 6 digits
            snprintf(stamp, sizeof stamp, "%04d-%02d-%02d_%06u", d.year, d.month, d.day, ctr);
            atomic_store_explicit(&state, 2, memory_order_release);
        } else {
            while (atomic_load_explicit(&state, memory_order_acquire) != 2)
                ano_busywait(100);
        }
    }
    return stamp;
}


/* Log Path */

// "<gamepath>/logs", created if absent.
ano_fspath ano_fs_logpath(void)
{
    ano_fspath dir = ano_fs_gamepath();
    if (dir.length == 0 || dir.length + 5 >= MAXPATH)
        return (ano_fspath){0};
    memcpy(dir.str + dir.length, "/logs", 6);   // 6 includes NUL
    dir.length += 5;
    if (fs_mkdir(dir.str) != 0)
        return (ano_fspath){0};
    return dir;
}
