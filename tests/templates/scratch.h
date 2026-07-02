/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Scratch dirs and file oracles for tests that write output. ANO_TEST_OUTDIR anchors scratch to
// the test's own build tree (set per target in tests/CMakeLists.txt), so a by-hand run from the
// repo root leaves no litter in the CWD. Tests delete their scratch on exit.

#ifndef ANOPTIC_TEST_TEMPLATES_SCRATCH_H
#define ANOPTIC_TEST_TEMPLATES_SCRATCH_H

#include <stdint.h>
#include <stdio.h>

#ifndef ANO_TEST_OUTDIR
#define ANO_TEST_OUTDIR "."
#endif

#if defined(_WIN32)
#include <direct.h>
static inline void scratch_make_dir(const char *p)   { _mkdir(p); }
static inline void scratch_remove_dir(const char *p) { _rmdir(p); }
#else
#include <sys/stat.h>
#include <unistd.h>
static inline void scratch_make_dir(const char *p)   { mkdir(p, 0777); }
static inline void scratch_remove_dir(const char *p) { rmdir(p); }
#endif

// '\n' count, 0 if absent. The no-loss oracle: one record in, one line out.
static inline uint64_t scratch_count_lines(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    uint64_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF)
        if (c == '\n') n++;
    fclose(f);
    return n;
}

#endif // ANOPTIC_TEST_TEMPLATES_SCRATCH_H
