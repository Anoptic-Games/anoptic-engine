/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* hot reload: old-complete or new-complete, and the derived cascade
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W5, M15):
 *   - republish is ONE release-store of the new pub over the old, NEVER NULL in between:
 *     a reader must never acquire a sentinel for a fully live resource;
 *   - mtime is a FILTER (it lies on 9P/SMB); a content hash CONFIRMS;
 *   - an unchanged file with a bumped mtime is rejected (reload_rejected_same_content);
 *   - the DERIVED CASCADE: reloading models/x.gltf must not leave the stale conditioned
 *     scene published and serving old geometry forever. */

#include <stdio.h>

int main(void)
{
    printf("hot reload: old-complete or new-complete, and the derived cascade\n");
    printf("  PENDING: no oracle yet -- owned by W5, M15.\n");
    return 0;
}
