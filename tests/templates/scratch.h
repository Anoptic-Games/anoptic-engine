/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Scratch dirs and file oracles for tests that write output. Scratch paths are relative to the
// process's working directory; call scratch_anchor_to_exe() once at the top of main() to point that
// at the executable's own directory (build/<cfg>/tests), so a by-hand run from the repo root leaves
// no litter in the CWD. Tests delete their scratch on exit.

#ifndef ANOPTIC_TEST_TEMPLATES_SCRATCH_H
#define ANOPTIC_TEST_TEMPLATES_SCRATCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <anoptic_filesystem.h>

// Base for the relative scratch paths below; "." means "the CWD", which scratch_anchor_to_exe()
// repoints at the executable's directory. We deliberately do NOT bake a compile-time build path
// here anymore: that was a build-MACHINE path, meaningless when the exe runs on another OS (e.g.
// cross-built for Windows and launched via WSL interop, where /home/... is not a valid path).
#ifndef ANO_TEST_OUTDIR
#define ANO_TEST_OUTDIR "."
#endif

// Anchor the CWD to the executable's own directory via the engine's cross-platform gamepath
// resolver (readlink/GetModuleFileName/_NSGetExecutablePath), so relative scratch paths resolve
// there regardless of launch dir, on every target OS. Call once at the top of main(). Returns
// false if the exe path could not be resolved (scratch then falls back to the launch CWD).
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
