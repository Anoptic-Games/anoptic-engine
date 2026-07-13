/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* levels: a domain that opens, runs, and returns to baseline
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W10, M17):
 *   - the level's assets array IS its disclosed dependency set, and exactly that set loads;
 *   - the on_load script parameterizes what renders and the ambient audio feeds the sink --
 *     a level whose scripts and audio do nothing is scaffolding wearing a domain's name;
 *   - >= 20 open/retire cycles with live_bytes, live_blocks, chunk_bytes, parent_bytes,
 *     domains_live, retired_pending and stalled_readers ALL back at the pre-open baseline. */

#include <stdio.h>

int main(void)
{
    printf("levels: a domain that opens, runs, and returns to baseline\n");
    printf("  PENDING: no oracle yet -- owned by W10, M17.\n");
    return 0;
}
