/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* audio: planar PCM ingest and the null sink's FNV oracle
 *
 * PLACEHOLDER. Registered by W0 so the target name, its LABELS and its per-sanitizer
 * TIMEOUT are frozen with the rest of the build, and so no later workstream ever has to
 * touch a CMakeLists.txt. It asserts nothing yet and says so out loud: a stub that printed
 * "OK" would be a green lie.
 *
 * TODO(W8, M16):
 *   - WAV -> one f32 plane PER CHANNEL, every plane grain-aligned inside one block;
 *   - the mixer walks one plane per channel and the null sink's running FNV-1a-64 over
 *     every emitted frame matches an independently computed hash. That hash is the oracle:
 *     a dropped voice, a misread plane, or a stale block changes it;
 *   - hostile RIFF: a lying chunk size, a truncated data chunk, an absurd channel count. */

#include <stdio.h>

int main(void)
{
    printf("audio: planar PCM ingest and the null sink's FNV oracle\n");
    printf("  PENDING: no oracle yet -- owned by W8, M16.\n");
    return 0;
}
