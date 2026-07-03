/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_text.h -- module lifecycle over the FreeType backend:
 *   - ano_text_version before init reports all zeros (no backend yet);
 *   - ano_text_init returns 0 and is idempotent (second call also 0);
 *   - the linked FreeType is the vendored submodule generation (2.13+), proving the
 *     static-link chain anoptic_core -> libfreetype.a actually resolves symbols;
 *   - ano_text_shutdown zeroes the version view, double shutdown is safe, and a full
 *     init -> shutdown -> init -> shutdown cycle works (heap + library rebuild cleanly).
 * Exit 0 == pass; failures print what broke. */

#include <stdio.h>

#include "anoptic_text.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static void expect_version_zero(const char *when)
{
    int maj = -1, min = -1, pat = -1;
    ano_text_version(&maj, &min, &pat);
    printf("%s: FreeType version reads %d.%d.%d\n", when, maj, min, pat);
    CHECK(maj == 0 && min == 0 && pat == 0, "version is all zeros without a live backend");
}

int main(void)
{
    expect_version_zero("before init");

    CHECK(ano_text_init() == 0, "ano_text_init succeeds");
    CHECK(ano_text_init() == 0, "second init is an idempotent success");

    int maj = 0, min = 0, pat = 0;
    ano_text_version(&maj, &min, &pat);
    printf("after init: FreeType %d.%d.%d\n", maj, min, pat);
    CHECK(maj == 2, "FreeType major version is 2");
    CHECK(min >= 13, "FreeType minor version is >= 13 (vendored submodule generation)");
    ano_text_version(NULL, NULL, NULL); // NULL outputs are a documented no-op

    ano_text_shutdown();
    expect_version_zero("after shutdown");
    ano_text_shutdown(); // double shutdown must be harmless

    CHECK(ano_text_init() == 0, "re-init after shutdown succeeds");
    ano_text_version(&maj, &min, &pat);
    CHECK(maj == 2 && min >= 13, "re-initialized backend reports the same FreeType");
    ano_text_shutdown();

    if (failures == 0)
        printf("anotest_text: all checks passed\n");
    else
        printf("anotest_text: %d check(s) FAILED\n", failures);
    return failures ? 1 : 0;
}
