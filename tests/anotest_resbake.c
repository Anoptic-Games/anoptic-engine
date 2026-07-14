/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* block framing: the hostile-input battery
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W5, M11):
 *   - res_plane_layout is PURE and its offsets are RES_PLANE_GRAIN-aligned;
 *   - res_block_open refuses: truncated, bad magic, bad version, bad layout_id, hash
 *     mismatch, offsets past end, count*sizeof overflow;
 *   - and every INTERIOR index: prim.material, node.mesh, node.parent, child spans,
 *     children[i], roots[i], indices[i]. ano_resgfx_scene checks array EXTENTS today and
 *     validates NOTHING INSIDE THEM -- a baked block from a pack turns each of those into an
 *     out-of-bounds read primitive handed straight to the renderer. Runs under ASan. */

#include <stdio.h>

int main(void)
{
    printf("block framing: the hostile-input battery\n");
    printf("  PENDING: no oracle yet -- owned by W5, M11.\n");
    return 0;
}
