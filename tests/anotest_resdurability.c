/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Subprocess durability coverage for the fixed-name resource protocol. A child process is
 * terminated after each write/sync/close/rename boundary. Parent oracle: the logical resource
 * is byte-exact old-complete or new-complete, never torn. File scratch is removed on exit.
 * No args = parent; argv[1] = child fault step. Exit 0 == pass. */

#include <anoptic_filesystem.h>
#include <anoptic_log.h>
#include <anoptic_resources.h>

#include "resources/resources_internal.h"
#include "templates/scratch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define DURABLE_PATH "config/anotest-process-durable.bin"

static int failures;
static int g_child_step;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL (%d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)

static void terminate_at_step(res_fault_step step)
{
    if ((int)step != g_child_step)
        return;
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), (UINT)(80 + g_child_step));
#else
    _exit(80 + g_child_step);
#endif
}

static int child_main(int step)
{
    g_child_step = step;
    if (ano_log_init() != 0 || ano_res_init() != 0)
        return 40;
    res_fault_hook = terminate_at_step;
    static const char fresh[] = "new-complete-payload";
    (void)ano_res_write(DURABLE_PATH, fresh, sizeof fresh - 1);
    return 41;
}

static int spawn_child(const char *exe, int step)
{
#if defined(_WIN32)
    char command[MAXPATH * 2];
    snprintf(command, sizeof command, "\"%s\" %d", exe, step);
    STARTUPINFOA startup = { .cb = sizeof startup };
    PROCESS_INFORMATION process;
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL,
                        &startup, &process))
        return -1;
    WaitForSingleObject(process.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)code;
#else
    pid_t pid = fork();
    if (pid == 0) {
        char arg[16];
        snprintf(arg, sizeof arg, "%d", step);
        execl(exe, exe, arg, (char *)NULL);
        _exit(39);
    }
    if (pid < 0)
        return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
#endif
}

static bool payload_is(mi_heap_t *heap, const char *text)
{
    anostr_t bytes = ano_res_slurp(heap, DURABLE_PATH);
    size_t len = strlen(text);
    return anostr_len(bytes) == len && memcmp(anostr_bytes(&bytes), text, len) == 0;
}

int main(int argc, char **argv)
{
    scratch_anchor_to_exe();
    if (argc > 1)
        return child_main(atoi(argv[1]));

    int log_scope ANO_LOG_SCOPE_ATTR = ano_log_init();
    CHECK(log_scope == 0, "log init");
    CHECK(ano_res_init() == 0, "resource init");
    ano_fspath game = ano_fs_gamepath();
    char exe[MAXPATH * 2];
#if defined(_WIN32)
    int n = snprintf(exe, sizeof exe, "%s/anotest_resdurability.exe", game.str);
#else
    int n = snprintf(exe, sizeof exe, "%s/anotest_resdurability", game.str);
#endif
    CHECK(n > 0 && n < (int)sizeof exe, "child executable path");

    static const char old[] = "old-complete-payload";
    static const char fresh[] = "new-complete-payload";
    for (int step = RES_FAULT_AFTER_WRITE; step <= RES_FAULT_AFTER_RENAME; step++) {
        CHECK(ano_res_write(DURABLE_PATH, old, sizeof old - 1) == 0,
              "seed old durable payload");
        int code = spawn_child(exe, step);
        CHECK(code == 80 + step, "child terminated at protocol step %d (code %d)", step, code);
        mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
        CHECK(heap != NULL, "read heap");
        bool old_ok = heap != NULL && payload_is(heap, old);
        bool new_ok = heap != NULL && payload_is(heap, fresh);
        CHECK(old_ok || new_ok, "step %d left old-complete or new-complete", step);
    }

    ano_fspath file = ano_res_resolve_write(DURABLE_PATH);
    if (file.length != 0)
        (void)remove(file.str);
    CHECK(ano_res_shutdown() == 0, "resource shutdown");
    if (failures == 0)
        printf("PASS: subprocess resource durability\n");
    return failures == 0 ? 0 : 1;
}
