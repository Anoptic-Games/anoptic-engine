/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* resource placement: the routing oracle
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W1, M5 + M19):
 *   - for every model x every (tag, lifetime, role, operation, destination, size): the
 *     expected arena, root, serving, alignment and flags -- and REFUSAL for an unroutable plan;
 *   - the teardown-shape oracle: a winkable model frees NOTHING at RETAIL-in-WINK, a
 *     non-winkable model frees everything;
 *   - the post-wink assertion, for all five models: domain live_bytes == 0. */

#include <stdio.h>

int main(void)
{
    printf("resource placement: the routing oracle\n");
    printf("  PENDING: no oracle yet -- owned by W1, M5 + M19.\n");
    return 0;
}
