/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_fs_userpath squatter guard. The header promises "length == 0 if unresolved or
// mkdir failed. Non-empty result is ready to write into" 〜 but the implementation accepts
// mkdir's EEXIST without checking that the existing entry is a directory (docs/BUGS.md,
// Filesystem / Implementation, filesystem_linux.c:65 + macos/win64/fs_mkdir twins). A regular
// file squatting the per-user game dir therefore yields a non-empty path every write under it
// rejects with ENOTDIR: silent success, dead saves/config. The fs_mkdir twin kills the log
// directory the same way for a file named "logs" beside the exe (not triggered here: other
// tests share that directory). The user-data root is redirected into an exe-anchored scratch
// (HOME on POSIX, APPDATA on win64), so the real per-user dir is never touched. Controls pin
// the fresh-create and legit already-a-directory EEXIST paths, so a reject-everything fix
// cannot pass. Headless, deterministic. Exit 0 == pass.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_filesystem.h"
#include "templates/scratch.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Redirect the platform user-data root env var for this process.
#if defined(_WIN32)
static void set_userdata_root(const char *dir) { _putenv_s("APPDATA", dir); }
#else
static void set_userdata_root(const char *dir) { setenv("HOME", dir, 1); }
#endif

// Probe the header's promise: a non-empty userpath must accept a file create inside it.
static bool userpath_is_writable(const ano_fspath *user)
{
    char probe[MAXPATH + 32];
    snprintf(probe, sizeof probe, "%s%canotest_fsguard_probe.tmp", user->str,
#if defined(_WIN32)
             '\\'
#else
             '/'
#endif
    );
    ano_file *f = ano_fs_open_append(probe);
    if (f == NULL)
        return false;
    ano_fs_close(f);
    remove(probe);
    return true;
}

int main(void)
{
    // scratch anchored to the exe dir; removed on exit
    scratch_anchor_to_exe();
    ano_fspath exe = ano_fs_gamepath();
    CHECK(exe.length > 0, "gamepath resolves (scratch anchor)");
    if (exe.length == 0)
        return 1;

    // fake user-data root inside the scratch, absolute so the redirect survives any chdir
    char homeDir[MAXPATH + 32];
    snprintf(homeDir, sizeof homeDir, "%s/fsguard.home", exe.str);
    scratch_make_dir(homeDir);
    set_userdata_root(homeDir);

    // the game dir ano_fs_userpath will resolve under the redirected root
    char target[MAXPATH + 96];
#if defined(_WIN32)
    snprintf(target, sizeof target, "%s\\%s", homeDir, ANO_GAME_NAME);
#elif defined(__APPLE__)
    char lib[MAXPATH + 64];
    snprintf(lib, sizeof lib, "%s/Library", homeDir);
    scratch_make_dir(lib);
    snprintf(lib, sizeof lib, "%s/Library/Application Support", homeDir);
    scratch_make_dir(lib);
    snprintf(target, sizeof target, "%s/Library/Application Support/%s", homeDir, ANO_GAME_NAME);
#else
    snprintf(target, sizeof target, "%s/.%s", homeDir, ANO_GAME_NAME);
#endif

    // control: absent -> created, resolved into the redirect, and writable
    ano_fspath fresh = ano_fs_userpath();
    CHECK(fresh.length > 0, "fresh root: userpath resolves");
    CHECK(fresh.length > 0 && strcmp(fresh.str, target) == 0,
          "userpath landed in the redirected root");
    CHECK(fresh.length > 0 && userpath_is_writable(&fresh),
          "fresh userpath is ready to write into");

    // control: already a directory -> the legit EEXIST fast path still succeeds
    ano_fspath again = ano_fs_userpath();
    CHECK(again.length > 0, "existing directory: userpath still resolves");

    // trigger: a regular file squats the game dir name; mkdir fails EEXIST, nothing under
    // the path can ever be created, so the contract answer is length == 0
    scratch_remove_dir(target);
    FILE *squatter = fopen(target, "wb");
    CHECK(squatter != NULL, "squatter file created");
    if (squatter != NULL) {
        fputs("not a directory\n", squatter);
        fclose(squatter);

        ano_fspath squat = ano_fs_userpath();
        CHECK(squat.length == 0,
              "file squatting the game dir: userpath must report length 0, not success");

        // the lie in action: if it claimed success, the promised write must work; it cannot
        if (squat.length > 0)
            CHECK(userpath_is_writable(&squat),
                  "a non-empty userpath must be ready to write into");

        remove(target);
    }

    // cleanup
#if defined(__APPLE__)
    snprintf(lib, sizeof lib, "%s/Library/Application Support", homeDir);
    scratch_remove_dir(lib);
    snprintf(lib, sizeof lib, "%s/Library", homeDir);
    scratch_remove_dir(lib);
#endif
    scratch_remove_dir(homeDir);

    if (failures) {
        printf("anotest_fsguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_fsguard: all passed\n");
    return 0;
}
