/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Scratch dirs and file oracles for tests that write output.
// Paths relative to CWD. Call scratch_anchor_to_exe() once at top of main() to point CWD at the exe dir (build/<cfg>/tests).
// Tests delete their scratch on exit.

#ifndef ANOPTIC_TEST_TEMPLATES_SCRATCH_H
#define ANOPTIC_TEST_TEMPLATES_SCRATCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <anoptic_filesystem.h>

// Base for relative scratch paths. "." = CWD (repointed by scratch_anchor_to_exe()).
#ifndef ANO_TEST_OUTDIR
#define ANO_TEST_OUTDIR "."
#endif

// chdir to the executable's directory. Call once at top of main().
// Returns false if unresolved (scratch stays at launch CWD).
static inline bool scratch_anchor_to_exe(void) { return ano_fs_chdir_gamepath(); }

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
