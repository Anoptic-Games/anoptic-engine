/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* resource ranges: partial reads that never lie
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W4, M14):
 *   - res_read_range returns 0 / RES_RANGE_EOF / -1 -- never a silent partial;
 *   - a short read from a 9P/SMB-shaped source loops rather than reporting EOF;
 *   - rmos_read_at is positional and the caller's loop, not the seam's, owns termination. */

#include <stdio.h>

int main(void)
{
    printf("resource ranges: partial reads that never lie\n");
    printf("  PENDING: no oracle yet -- owned by W4, M14.\n");
    return 0;
}
