/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* anopak: mount, lookup, ranged read, deterministic build
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W4, M14):
 *   - the builder is DETERMINISTIC: the same input tree yields a byte-identical pack;
 *   - loose-shadows-pack is an invariant of the WALK, not of mount order;
 *   - the TOC corruption matrix: bad magic, bad version, unsorted TOC, rid2 mismatch,
 *     data_off past the end, a codec byte naming GDEFLATE -- every one REFUSED;
 *   - res_gfx parse_count reads 0 after loading a BAKED scene. Prose is not evidence. */

#include <stdio.h>

int main(void)
{
    printf("anopak: mount, lookup, ranged read, deterministic build\n");
    printf("  PENDING: no oracle yet -- owned by W4, M14.\n");
    return 0;
}
