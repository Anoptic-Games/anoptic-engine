/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the deferred capture stores the caller's sourceFile as a raw POINTER in the ring
// blob (log_core.c:205) and format_deferred dereferences it at drain time (:268/:277, batch
// drain :555), but the header's lifetime contract is one-sided: anoptic_log.h:53 demands
// "printFormat MUST be a string literal" while sourceFile 〜 the parameter beside it, same
// trust level 〜 carries no lifetime requirement, so a caller passing a stack or heap path
// through the documented entry points ano_log_write/ano_log_vwrite (the vwrite comment
// invites exactly such wrappers) gets a dangling deref on the drain thread, or silently logs
// whatever the buffer holds at drain rather than at call; the implementation's own %s arm
// proves the intended rule by deep-copying every caller-owned string at capture (:247-:256)
// (docs/BUGS.md, Log / Interface-level, log_core.c:205).
// Harness: the real public logger end to end, anotest_logging style 〜 init, output dir, log,
// flush, slurp the session log, strstr. Memory-safe by construction: the trigger buffers stay
// live and are only OVERWRITTEN after each call returns, so pointer-capture shows up as
// drain-time content instead of call-time content 〜 no UAF is needed to prove the deref site.
// CONTROL A: literal sourceFile round-trips with its file:line prefix, so a fix that drops
// the prefix machinery cannot pass. CONTROL B: a mutable buffer left intact round-trips, so a
// reject-non-literal "fix" is visible.
// TRIGGER: a 2000-record FIFO backlog pins the drainer behind the triggers (ring 512 KiB 〜
// no write-through arm), then 16 records each pass their own live buffer as sourceFile and
// the buffer is scribbled immediately after the call returns; the drained lines must carry
// the call-time names. Today every one carries the scribble 〜 capture is by pointer.
// Exit 0 == pass.

#include <anoptic_log.h>
#include <anoptic_filesystem.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
static void make_dir(const char *p)    { _mkdir(p); }
static void remove_dir(const char *p)  { _rmdir(p); }
#else
#include <sys/stat.h>
#include <unistd.h>
static void make_dir(const char *p)    { mkdir(p, 0777); }
static void remove_dir(const char *p)  { rmdir(p); }
#endif

#ifndef ANO_TEST_OUTDIR
#define ANO_TEST_OUTDIR "."
#endif
#define LOG_DIR ANO_TEST_OUTDIR "/anolog_srcguard"

#define BACKLOG_RECORDS 2000
#define TRIGGERS        16

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Session-stamped log path, resolved once in main.
static char LOG_PATH[96];

// Slurp whole file into a NUL-terminated heap buffer. Caller frees. NULL if unreadable.
static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) { fclose(f); return NULL; }
    for (;;) {
        if (len + 1 >= cap) {
            char *nbuf = realloc(buf, cap * 2);
            if (nbuf == NULL) { free(buf); fclose(f); return NULL; }
            buf = nbuf; cap *= 2;
        }
        size_t got = fread(buf + len, 1, cap - len - 1, f);
        len += got;
        if (got == 0) break;
    }
    if (ferror(f)) { free(buf); fclose(f); return NULL; }
    fclose(f);
    buf[len] = '\0';
    return buf;
}

// Drop the current session log so each case reads only its own records.
static void reset_output(void)
{
    remove(LOG_PATH);
    ano_log_output_dir(LOG_DIR);
}

// CONTROL A: string-literal sourceFile round-trips with its file:line prefix.
static void control_literal(void)
{
    reset_output();
    ano_log_write(ANO_INFO, 0, "ctrl_lit.c", 42, "control literal %d", 1);
    ano_log_flush();
    char *c = slurp(LOG_PATH);
    CHECK(c != NULL, "control A: session log readable");
    if (c) {
        CHECK(strstr(c, "ctrl_lit.c:42") != NULL, "control A: literal sourceFile prefix drained intact");
        CHECK(strstr(c, "control literal 1") != NULL, "control A: body drained intact");
        free(c);
    }
}

// CONTROL B: a mutable buffer left intact round-trips 〜 non-literal sourceFile is in-contract.
static void control_intact_buffer(void)
{
    reset_output();
    char keep[32];
    strcpy(keep, "ctrl_keep.c");
    ano_log_write(ANO_INFO, 0, keep, 77, "control intact %d", 2);
    ano_log_flush();
    char *c = slurp(LOG_PATH);
    CHECK(c != NULL, "control B: session log readable");
    if (c) {
        CHECK(strstr(c, "ctrl_keep.c:77") != NULL, "control B: intact caller buffer drained with call-time content");
        free(c);
    }
}

// TRIGGER: sourceFile must be captured by value at call time, like the %s arm one switch-case
// over. Backlog pins the drainer FIFO behind the triggers; each trigger's buffer is scribbled
// the instruction after ano_log_write returns; the drained prefixes must still name the
// call-time files. Buffers stay live 〜 the failure signal is content, not a crash.
static void trigger_calltime_capture(void)
{
    reset_output();

    for (int i = 0; i < BACKLOG_RECORDS; i++)
        ano_log(ANO_INFO, "filler %d", i);

    static char buf[TRIGGERS][24];
    for (unsigned k = 0; k < TRIGGERS; k++) {
        snprintf(buf[k], sizeof buf[k], "live_%02u.c", k);
        ano_log_write(ANO_INFO, 0, buf[k], (int)(4200u + k), "trig %u", k);
        memcpy(buf[k], "gone", 4);   // scribble: same length, call-time name destroyed
    }
    ano_log_flush();

    char *c = slurp(LOG_PATH);
    CHECK(c != NULL, "trigger: session log readable");
    if (c == NULL) return;

    int scribbled = 0;
    for (unsigned k = 0; k < TRIGGERS; k++) {
        char want[32], got[32];
        snprintf(want, sizeof want, "live_%02u.c:%u", k, 4200u + k);
        snprintf(got,  sizeof got,  "gone_%02u.c:%u", k, 4200u + k);
        if (strstr(c, got) != NULL) scribbled++;
        CHECK(strstr(c, want) != NULL, "trigger: sourceFile drained with call-time content, not drain-time");
    }
    if (scribbled > 0)
        printf("  evidence: %d of %d drained records carry the post-call scribble 〜 sourceFile was deref'd at drain\n",
               scribbled, TRIGGERS);
    free(c);
}

int main(void)
{
    // Anchor scratch to the exe dir before any I/O.
    if (!ano_fs_chdir_gamepath()) {
        fprintf(stderr, "chdir to gamepath failed\n");
        return 1;
    }
    snprintf(LOG_PATH, sizeof LOG_PATH, "%s/%s_ano.log", LOG_DIR, ano_fs_session_stamp());

    if (ano_log_init() != 0) {
        fprintf(stderr, "ano_log_init failed\n");
        return 1;
    }
    make_dir(LOG_DIR);

    control_literal();
    printf("  [%s] control_literal\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    int after_a = failures;

    control_intact_buffer();
    printf("  [%s] control_intact_buffer\n", failures == after_a ? "PASS" : "FAIL");
    fflush(stdout);
    int after_b = failures;

    trigger_calltime_capture();
    printf("  [%s] trigger_calltime_capture\n", failures == after_b ? "PASS" : "FAIL");
    fflush(stdout);

    ano_log_cleanup();
    remove(LOG_PATH);
    remove_dir(LOG_DIR);

    if (failures != 0) {
        fprintf(stderr, "anotest_logsrcguard: %d check(s) failed\n", failures);
        return 1;
    }
    printf("anotest_logsrcguard: all checks passed.\n");
    return 0;
}
