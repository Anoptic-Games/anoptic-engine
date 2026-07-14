/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* the remote-FS floor: 9P and SMB tell the truth about nothing
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W12, M18):
 *   - a short read is NOT EOF: only *got == 0 is, and the read loop must believe only that;
 *   - mtime lies, so hot reload must confirm by content hash;
 *   - fsync failures are real failures and are never retried on the same handle (fsyncgate);
 *   - the write protocol stays old-complete-or-new-complete across a hostile rename. */

#include <stdio.h>

int main(void)
{
    printf("the remote-FS floor: 9P and SMB tell the truth about nothing\n");
    printf("  PENDING: no oracle yet -- owned by W12, M18.\n");
    return 0;
}
