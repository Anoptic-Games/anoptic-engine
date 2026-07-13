/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* script: compile to a bytecode block, and a VM that always terminates
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W9, M16):
 *   - a hostile script MUST terminate: fuel exhaustion is ANO_SCRIPT_OUT_OF_FUEL, never a hang;
 *   - the VM re-checks the stack bound the compiler proved, because a bytecode block can
 *     arrive from a pack and the proof did not come with it;
 *   - every result is checked against an independent reference evaluator;
 *   - hostile bytecode: bad opcode, out-of-range constant/local/symbol, a jump past the end. */

#include <stdio.h>

int main(void)
{
    printf("script: compile to a bytecode block, and a VM that always terminates\n");
    printf("  PENDING: no oracle yet -- owned by W9, M16.\n");
    return 0;
}
