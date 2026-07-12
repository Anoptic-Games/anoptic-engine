/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Fault injection for the durable write protocol: the process "dies" (longjmp out, so
 * no cleanup code runs -- the on-disk state is exactly what a crash at that instant
 * leaves) after each protocol step, and the target file must be old-complete or
 * new-complete every time, never torn, never absent.
 *
 * This target compiles its own resources_core.c with ANO_RES_FAULT_INJECT, overriding
 * the anoptic_core copy at link time (same trick as anotest_logbench's log_old.c).
 * The harness only drives ano_res_write: the protocol is shared with save_commit, and
 * jumping out from under save_commit would strand its mutex.
 * Exit 0 == pass. */

#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "anoptic_resources.h"
#include "resources_internal.h"
#include "templates/scratch.h"

#ifndef _WIN32
#include <dirent.h>
#endif

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static jmp_buf        g_jmp;
static res_fault_step g_target;

static void die_at(res_fault_step step)
{
    if (step == g_target)
        longjmp(g_jmp, 1);
}

// The file as bytes, "" if absent. buf holds cap.
static long read_back(const char *path, char *buf, long cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    long n = (long)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

static void purge_temps(const char *dir)
{
#ifndef _WIN32
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n >= 4 && strcmp(e->d_name + n - 4, ".tmp") == 0) {
            char p[MAXPATH + 300];
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            remove(p);
        }
    }
    closedir(d);
#else
    (void)dir;
#endif
}

int main(void)
{
    scratch_anchor_to_exe();
    CHECK(ano_res_init() == 0, "init");

    ano_fspath user = ano_fs_userpath();
    char final[MAXPATH + 48], dir[MAXPATH + 48], got[256];
    snprintf(final, sizeof final, "%s/anotest_fault/fault.bin", user.str);
    snprintf(dir, sizeof dir, "%s/anotest_fault", user.str);

    // Baseline: a committed "old" version.
    const char *old = "OLD-CONTENT-vvvvvvvvvvvvvvvv";
    CHECK(ano_res_write("anotest_fault/fault.bin", old, strlen(old)) == 0, "baseline write");

    const char *expect = old;
    for (int step = RES_FAULT_AFTER_WRITE; step <= RES_FAULT_AFTER_RENAME; step++) {
        char fresh[64];
        snprintf(fresh, sizeof fresh, "NEW-STEP-%d-%s", step,
                 step % 2 ? "x" : "yyyyyyyyyyyyyyyyyyyyyy");   // lengths differ per step

        g_target = (res_fault_step)step;
        res_fault_hook = die_at;
        if (setjmp(g_jmp) == 0) {
            (void)ano_res_write("anotest_fault/fault.bin", fresh, strlen(fresh));
            CHECK(false, "fault hook did not fire");
        }
        res_fault_hook = NULL;

        // The crash-instant contract: old-complete or new-complete, never torn/absent.
        long n = read_back(final, got, sizeof got);
        CHECK(n >= 0, "target file exists after crash");
        bool is_old = n == (long)strlen(expect) && memcmp(got, expect, (size_t)n) == 0;
        bool is_new = n == (long)strlen(fresh)  && memcmp(got, fresh, (size_t)n) == 0;
        CHECK(is_old || is_new, "old-complete or new-complete, never torn");
        if (step >= RES_FAULT_AFTER_RENAME)
            CHECK(is_new, "post-rename crash leaves the new content");
        else
            CHECK(is_old, "pre-rename crash leaves the old content");

        if (is_new) {
            static char keep[64];
            snprintf(keep, sizeof keep, "%s", fresh);
            expect = keep;
        }
        purge_temps(dir);                   // crash artifacts, swept between runs
    }

    // A clean run after all that carnage still lands durably.
    const char *last = "FINAL-CLEAN-WRITE";
    CHECK(ano_res_write("anotest_fault/fault.bin", last, strlen(last)) == 0, "clean write");
    long n = read_back(final, got, sizeof got);
    CHECK(n == (long)strlen(last) && memcmp(got, last, (size_t)n) == 0, "clean readback");

    remove(final);
    purge_temps(dir);
    scratch_remove_dir(dir);

    if (failures == 0) { printf("anotest_resfault: all checks passed\n"); return 0; }
    printf("anotest_resfault: %d check(s) failed\n", failures);
    return 1;
}
