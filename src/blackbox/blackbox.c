/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The blackbox's job, in full:
//   1. Be the thing that can't die when everything else does.
//   2. Write a tiny, reliable "we are going down" record.
//   3. Give the logger one last chance to speak.
// This TU is the calm half: resolve the CRASH.log path once, run the Stage 4 look-back, hand off to
// the per-platform Stage 1 hooks. The turbulent half (the handlers) lives in the platform TUs.

#include <anoptic_blackbox.h>
#include <anoptic_logging.h>
#include <anoptic_filesystem.h>

#include "blackbox/blackbox_internal.h"

#include <stdio.h>

char bb_crashPath[MAXPATH];

// Stage 4, minimal cut: a leftover CRASH.log IS the crash marker -- the record appends, so it survives
// until someone reads it. Announce it on both sinks and leave it in place for the investigation.
static void investigate_previous_flight(void)
{
    FILE *f = fopen(bb_crashPath, "rb");
    if (f == NULL)
        return;     // clean previous flight
    fclose(f);
    ano_rlog(ANO_WARN, ANO_BOTH, "blackbox: %s holds records from a previous crash.", bb_crashPath);
}

int ano_blackbox_init(void)
{
    // Same directory the logger writes anoptic.log into. On resolution failure fall back to the CWD,
    // which main() already pointed at the executable.
    ano_fspath dir = ano_fs_gamepath();
    int n = dir.length > 0
        ? snprintf(bb_crashPath, sizeof bb_crashPath, "%s/CRASH.log", dir.str)
        : snprintf(bb_crashPath, sizeof bb_crashPath, "CRASH.log");
    if (n <= 0 || n >= (int)sizeof bb_crashPath)
        n = snprintf(bb_crashPath, sizeof bb_crashPath, "CRASH.log");
    (void)n;

    investigate_previous_flight();

    return bb_install();
}
