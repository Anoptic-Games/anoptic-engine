/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: drain-batch sizing vs deferred wide rendering (docs/BUGS.md, Log / Implementation).
// g_batch is sized ring bytes + 16 per record on the claim that a record's rendered text fits its
// ring footprint. Deferred records break the claim: "%*d" width 4000 occupies one 64-byte ring
// line yet renders ~4016 batch bytes, so any drain pass over a backlog of ~164+ such records must
// cross g_batchCap 〜 the per-record prefix memcpy and newline are unchecked, and the size_t room
// subtraction then underflows, unbounding every later record into a multi-MB heap overwrite on
// whichever thread runs the pass. Four producers outrun the drainer (~150 ns capture vs ~µs
// render), so the backlog is guaranteed; the buggy build dies in the drain pass. Correct behavior
// asserted here: the burst survives, drops nothing, and renders every wide field at full width.
// The narrow control burst pins the adjacent correct path (rendered ~ stored, bound holds), so a
// fix that drops or truncates records to dodge the overflow still fails. Exit 0 == pass.

#include <anoptic_log.h>
#include <anoptic_threads.h>
#include <anoptic_filesystem.h>

#include "templates/scratch.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DIR_CTRL ANO_TEST_OUTDIR "/anolog_flood_ctrl"
#define DIR_WIDE ANO_TEST_OUTDIR "/anolog_flood_wide"
// Session-stamped paths, resolved in main().
static char PATH_CTRL[96], PATH_WIDE[96];

#define CTRL_RECORDS    4096
#define WIDE_PRODUCERS  4
#define WIDE_PER_THREAD 2048
#define WIDE_TOTAL      (WIDE_PRODUCERS * WIDE_PER_THREAD)
#define WIDE_FIELD      4000    // rendered field width; ring footprint stays one line

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// One thread's slice of the wide burst. Each record: one ring line in, ~4016 batch bytes out.
static void *wide_producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < WIDE_PER_THREAD; i++)
        ano_log(ANO_INFO, "%*d", WIDE_FIELD, i);
    return NULL;
}

// Shortest line in `path` ('\n'-terminated), UINT64_MAX if none.
static uint64_t min_line_len(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return UINT64_MAX;
    uint64_t best = UINT64_MAX, cur = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') { if (cur < best) best = cur; cur = 0; }
        else cur++;
    }
    fclose(f);
    return best;
}

int main(void)
{
    scratch_anchor_to_exe();   // scratch relative to this exe's dir, cross-platform
    scratch_make_dir(DIR_CTRL);
    scratch_make_dir(DIR_WIDE);
    snprintf(PATH_CTRL, sizeof PATH_CTRL, "%s/%s_ano.log", DIR_CTRL, ano_fs_session_stamp());
    snprintf(PATH_WIDE, sizeof PATH_WIDE, "%s/%s_ano.log", DIR_WIDE, ano_fs_session_stamp());
    remove(PATH_CTRL);
    remove(PATH_WIDE);

    CHECK(ano_log_init() == 0, "logger up");
    ano_log_set_level(ANO_INFO);
    CHECK(ano_log_output_dir(DIR_CTRL) == 0, "output -> control scratch");

    // control: narrow deferred burst 〜 rendered ~ stored, the sizing claim holds, nothing lost
    for (int i = 0; i < CTRL_RECORDS; i++)
        ano_log(ANO_INFO, "ctrl %d", i);
    ano_log_flush();
    printf("logflood: control burst drained (%d records)\n", CTRL_RECORDS);
    fflush(stdout);

    // trigger: wide deferred burst 〜 buggy build dies in a drain pass before the joins return
    CHECK(ano_log_output_dir(DIR_WIDE) == 0, "output -> wide scratch");
    anothread_t prod[WIDE_PRODUCERS];
    for (intptr_t i = 0; i < WIDE_PRODUCERS; i++)
        CHECK(ano_thread_create(&prod[i], NULL, wide_producer, (void *)i) == 0, "wide producer up");
    for (int i = 0; i < WIDE_PRODUCERS; i++)
        ano_thread_join(prod[i], NULL);
    ano_log_flush();
    printf("logflood: wide burst drained (%d records)\n", WIDE_TOTAL);
    fflush(stdout);

    ano_log_cleanup();

    // oracles on closed files: no loss, and full-width rendering (kills truncate-to-fit fakes)
    uint64_t ctrlLines = scratch_count_lines(PATH_CTRL);
    uint64_t wideLines = scratch_count_lines(PATH_WIDE);
    uint64_t wideMin   = min_line_len(PATH_WIDE);
    printf("logflood: ctrl=%llu/%d wide=%llu/%d minlen=%llu\n",
           (unsigned long long)ctrlLines, CTRL_RECORDS,
           (unsigned long long)wideLines, WIDE_TOTAL, (unsigned long long)wideMin);
    CHECK(ctrlLines == CTRL_RECORDS, "control: every narrow record is one line");
    CHECK(wideLines == WIDE_TOTAL, "wide: every record survives as one line");
    CHECK(wideMin != UINT64_MAX && wideMin >= WIDE_FIELD, "wide: fields render at full width");

    remove(PATH_CTRL);
    remove(PATH_WIDE);
    scratch_remove_dir(DIR_CTRL);
    scratch_remove_dir(DIR_WIDE);

    if (failures) {
        printf("anotest_logflood: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_logflood: all passed\n");
    return 0;
}
