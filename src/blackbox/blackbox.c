/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Calm half of the blackbox: resolve the CRASH.log path once, run the Stage 4 look-back, hand off to the per-platform Stage 1 hooks. The handlers live in the platform TUs.

#include <anoptic_blackbox.h>
#include <anoptic_logging.h>
#include <anoptic_filesystem.h>

#include "blackbox/blackbox_internal.h"

#include <stdio.h>

char bb_crashPath[MAXPATH];

// Stage 4: leftover *_CRASH.log files mark previous crashes. Announce the count on both sinks, then prune each log type to the newest BB_KEEP_LOGS, the live session's files excepted.
static void investigate_previous_flight(const char *dir)
{
    char newest[MAXPATH];
    int n = bb_scan_suffix(dir, "_CRASH.log", newest);
    if (n == 1)
        ano_rlog(ANO_WARN, ANO_BOTH, "blackbox: 1 crash log detected, %s/%s.", dir, newest);
    else if (n > 1)
        ano_rlog(ANO_WARN, ANO_BOTH, "blackbox: %d crash logs detected, newest %s/%s.", n, dir, newest);
    const char *stamp = ano_fs_session_stamp();
    bb_prune_suffix(dir, "_CRASH.log", BB_KEEP_LOGS, stamp);
    bb_prune_suffix(dir, "_ano.log",   BB_KEEP_LOGS, stamp);
}

int ano_blackbox_init(void)
{
    // Resolve <logs>/<stamp>_CRASH.log once at init, handlers only open() it. Fallbacks: <gamedir>, then CWD.
    ano_fspath dir = ano_fs_logpath();
    if (dir.length == 0)
        dir = ano_fs_gamepath();
    const char *stamp = ano_fs_session_stamp();
    int n = dir.length > 0
        ? snprintf(bb_crashPath, sizeof bb_crashPath, "%s/%s_CRASH.log", dir.str, stamp)
        : snprintf(bb_crashPath, sizeof bb_crashPath, "%s_CRASH.log", stamp);
    if (n <= 0 || n >= (int)sizeof bb_crashPath)
        n = snprintf(bb_crashPath, sizeof bb_crashPath, "CRASH.log");
    (void)n;

    investigate_previous_flight(dir.length > 0 ? dir.str : ".");

    return bb_install();
}

// Thin routing to the per-platform arm/release. Contract in the public header.
int ano_blackbox_thread_arm(void)
{
    return bb_thread_arm();
}

void ano_blackbox_thread_disarm(void)
{
    bb_thread_disarm();
}
