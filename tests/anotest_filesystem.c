/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_filesystem.h -- the ano_fspath value type and the append-only file API:
 *   - ano_fs_gamepath: resolved, NUL-terminated at length, no trailing separator, and usable
 *     (ano_fs_chdir_gamepath succeeds against it);
 *   - ano_fs_userpath: resolved to <user-data root>/ANO_GAME_NAME (Factorio convention), the
 *     directory exists after the call, and a file can be created inside it (removed after);
 *   - ano_file: open-append/write/sync/close round-trip in the test's scratch dir, with
 *     scratch_count_lines as the oracle (N writes in, N lines out) and append-not-truncate
 *     verified across a close/reopen.
 * The userpath check touches the real per-user directory (the one the engine itself uses);
 * it only adds and removes one probe file there and never deletes the directory.
 * Exit 0 == pass; failures print what broke. */

#include <stdio.h>
#include <string.h>

#include "anoptic_filesystem.h"
#include "templates/scratch.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Shape every resolved ano_fspath must have: length matches the bytes, NUL where promised.
static void check_path_shape(ano_fspath p, const char *label)
{
    printf("%s: \"%s\" (length %u)\n", label, p.str, p.length);
    CHECK(p.length > 0, "path resolved (length > 0)");
    CHECK(p.length < MAXPATH, "length within MAXPATH");
    CHECK(p.str[p.length] == '\0', "NUL exactly at length");
    CHECK(strlen(p.str) == p.length, "no embedded NUL before length");
    CHECK(p.str[p.length - 1] != '/' && p.str[p.length - 1] != '\\',
          "no trailing separator");
}

static void test_gamepath(void)
{
    ano_fspath game = ano_fs_gamepath();
    check_path_shape(game, "gamepath");
    CHECK(ano_fs_chdir_gamepath(), "chdir to gamepath succeeds");

    // Value semantics: two calls resolve independently to the same path.
    ano_fspath again = ano_fs_gamepath();
    CHECK(game.length == again.length && strcmp(game.str, again.str) == 0,
          "gamepath is stable across calls");
}

static void test_userpath(void)
{
    ano_fspath user = ano_fs_userpath();
    check_path_shape(user, "userpath");
    if (user.length == 0)
        return; // shape check already failed loudly; nothing usable to probe

    // Ends with the game name: <root><sep>[.]ANO_GAME_NAME (dot for the Linux convention).
    size_t nameLen = strlen(ANO_GAME_NAME);
    CHECK(user.length > nameLen, "path is longer than the game name");
    CHECK(strcmp(user.str + user.length - nameLen, ANO_GAME_NAME) == 0,
          "path ends with ANO_GAME_NAME");

    // The contract: a non-empty result is ready to write into. Prove it with one probe file.
    char probe[MAXPATH + 32];
    snprintf(probe, sizeof probe, "%s%canotest_userpath_probe.tmp", user.str,
#if defined(_WIN32)
             '\\'
#else
             '/'
#endif
    );
    ano_file *f = ano_fs_open_append(probe);
    CHECK(f != NULL, "can create a file inside userpath");
    if (f != NULL) {
        CHECK(ano_fs_write(f, "probe\n", 6) == 0, "write to userpath probe");
        CHECK(ano_fs_close(f) == 0, "close userpath probe");
    }
    remove(probe);
}

static void test_append_file_api(void)
{
    const char *dir = ANO_TEST_OUTDIR "/anotest_filesystem_scratch";
    char path[512];
    snprintf(path, sizeof path, "%s/append.log", dir);
    scratch_make_dir(dir);

    ano_file *f = ano_fs_open_append(path);
    CHECK(f != NULL, "open_append creates the file");
    if (f != NULL) {
        for (int i = 0; i < 5; i++)
            CHECK(ano_fs_write(f, "line\n", 5) == 0, "write line");
        CHECK(ano_fs_write(f, NULL, 0) == 0, "zero-length write is a no-op success");
        CHECK(ano_fs_sync(f) == 0, "sync flushes");
        CHECK(ano_fs_close(f) == 0, "close succeeds");
    }
    CHECK(scratch_count_lines(path) == 5, "5 writes -> 5 lines");

    // Reopen: append must extend, not truncate.
    f = ano_fs_open_append(path);
    CHECK(f != NULL, "reopen existing file");
    if (f != NULL) {
        CHECK(ano_fs_write(f, "line\n", 5) == 0, "write after reopen");
        CHECK(ano_fs_close(f) == 0, "close after reopen");
    }
    CHECK(scratch_count_lines(path) == 6, "reopen appended, did not truncate");

    CHECK(ano_fs_open_append(NULL) == NULL, "NULL path refused");
    CHECK(ano_fs_write(NULL, "x", 1) == -1, "write on NULL handle refused");
    CHECK(ano_fs_sync(NULL) == -1, "sync on NULL handle refused");
    CHECK(ano_fs_close(NULL) == -1, "close on NULL handle refused");

    remove(path);
    scratch_remove_dir(dir);
}

int main(void)
{
    // Scratch IO first: test_gamepath chdirs away from the launch CWD, and ANO_TEST_OUTDIR
    // is what keeps the scratch anchored regardless -- run in this order to prove that too.
    test_append_file_api();
    test_userpath();
    test_gamepath();

    if (failures == 0) { printf("anotest_filesystem: all checks passed\n"); return 0; }
    printf("anotest_filesystem: %d check(s) failed\n", failures);
    return 1;
}
