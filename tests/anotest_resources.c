/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for the anoptic_resources.h core (Phase A surface): namespace + mounts +
 * shadow order, the logical-path grammar, slurp read contract, durable writes,
 * quarantine, and gamesave commits.
 *   - pre-init calls hit the sentinel;
 *   - hostile paths (absolute, dotted, empty segments, backslash, colon, control bytes,
 *     overlong) refuse everywhere, plus a randomized no-crash fuzz;
 *   - shadow order: write root > newest mount > older mount > base; prefix grafts scope
 *     a mount to a logical subtree; the table freezes at first read;
 *   - slurp: byte-identical content (small, inline-tiny, and multi-chunk 1.5 MiB),
 *     guard NUL, ANO_CACHE_LINE alignment, absent -> empty;
 *   - write: durable replace visible to the namespace, overwrite works, no temp litter;
 *     quarantine renames to .broken and refuses when absent;
 *   - save frames: five commits keep exactly ANO_RES_SAVE_KEEP generations; the newest
 *     is validated with an INDEPENDENT FNV/layout oracle in this file.
 * Scratch lives next to the exe (scratch_anchor_to_exe) and in the real user root
 * (that is ano_res_write's actual contract); both are cleaned on exit.
 * Exit 0 == pass. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "anoptic_resources.h"
#include "anoptic_log.h"
#include "templates/scratch.h"
#include "templates/rng.h"

#ifndef _WIN32
#include <dirent.h>
#endif

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// ---------------------------------------------------------------------------------------------
// Helpers.

static ano_fspath fspath_of(const char *fmtdir)   // absolute path under the exe dir
{
    ano_fspath game = ano_fs_gamepath();
    ano_fspath r = {0};
    int w = snprintf(r.str, sizeof r.str, "%s/%s", game.str, fmtdir);
    if (w > 0 && w < (int)sizeof r.str)
        r.length = (uint16_t)w;
    return r;
}

static bool write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = len == 0 || fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static bool file_exists_stdio(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

static bool slurp_equals(mi_heap_t *heap, const char *logical, const void *data, size_t len)
{
    anostr_t v = ano_res_slurp(heap, logical);
    return anostr_len(v) == len
        && (len == 0 || memcmp(anostr_bytes(&v), data, len) == 0);
}

// Independent save-frame oracle: little-endian readers + a local FNV-1a-64.
static uint64_t oracle_fnv(const uint8_t *p, size_t n)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= UINT64_C(0x100000001b3); }
    return h;
}
static uint64_t oracle_le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static uint32_t oracle_le32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

// ---------------------------------------------------------------------------------------------

static void test_before_init(void)
{
    CHECK(ano_res_resolve("shaders/x.spv").length == 0, "pre-init resolve is empty");
    CHECK(ano_res_exists("shaders/x.spv") == false, "pre-init exists is false");
    CHECK(ano_res_mount("", ano_fs_gamepath()) == -1, "pre-init mount refuses");
    CHECK(ano_res_write("a/b.bin", "x", 1) == -1, "pre-init write refuses");
    CHECK(ano_res_save_commit("s", 1, "x", 1) == -1, "pre-init save refuses");
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    if (heap)
        CHECK(anostr_len(ano_res_slurp(heap, "a/b.bin")) == 0, "pre-init slurp is empty");
}

static void test_hostile_paths(mi_heap_t *heap)
{
    static const char *bad[] = {
        "", "/", "/abs/path", "a//b", "a/", "./a", "a/./b", "..", "../up", "a/../b",
        "a\\b", "C:/win", "a:b", "a/b/", ".", "sneaky/..", "\x01ctl", "a/\x7f",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(ano_res_resolve(bad[i]).length == 0, "hostile path: resolve empty");
        CHECK(ano_res_exists(bad[i]) == false, "hostile path: exists false");
        CHECK(ano_res_resolve_write(bad[i]).length == 0, "hostile path: resolve_write empty");
        CHECK(anostr_len(ano_res_slurp(heap, bad[i])) == 0, "hostile path: slurp empty");
        CHECK(ano_res_write(bad[i], "x", 1) == -1, "hostile path: write refuses");
        CHECK(ano_res_quarantine(bad[i]) == -1, "hostile path: quarantine refuses");
    }
    CHECK(ano_res_resolve(NULL).length == 0, "NULL path: resolve empty");
    CHECK(ano_res_exists(NULL) == false, "NULL path: exists false");
    CHECK(anostr_len(ano_res_slurp(heap, NULL)) == 0, "NULL path: slurp empty");
    CHECK(anostr_len(ano_res_slurp(NULL, "ok/path")) == 0, "NULL heap: slurp empty");
    CHECK(ano_res_write("ok/path.bin", NULL, 4) == -1, "NULL data + size: write refuses");

    char longpath[400];
    memset(longpath, 'a', sizeof longpath - 1);
    longpath[sizeof longpath - 1] = '\0';
    CHECK(ano_res_resolve(longpath).length == 0, "overlong path: resolve empty");
    CHECK(ano_res_write(longpath, "x", 1) == -1, "overlong path: write refuses");

    // Randomized no-crash fuzz across every entry point; validity is incidental.
    test_rng rng = rng_make(0x51DEu);
    char fuzz[64];
    static const char alphabet[] = "ab/.\\:xy_0-";
    for (int it = 0; it < 2000; it++) {
        uint32_t n = 1 + rng_below(&rng, sizeof fuzz - 2);
        for (uint32_t i = 0; i < n; i++)
            fuzz[i] = alphabet[rng_below(&rng, sizeof alphabet - 1)];
        fuzz[n] = '\0';
        (void)ano_res_resolve(fuzz);
        (void)ano_res_exists(fuzz);
        ano_fspath base = fspath_of("anores_mnt1");
        (void)ano_res_subpath(base, fuzz);
    }

    // Save-slot names are single segments.
    CHECK(ano_res_save_commit("a/b", 1, "x", 1) == -1, "slotted slash refuses");
    CHECK(ano_res_save_commit("..", 1, "x", 1) == -1, "dotted slot refuses");
    CHECK(ano_res_save_commit("", 1, "x", 1) == -1, "empty slot refuses");
    CHECK(ano_res_save_commit(NULL, 1, "x", 1) == -1, "NULL slot refuses");
}

static void test_subpath(void)
{
    ano_fspath base = fspath_of("anores_mnt1");
    ano_fspath j = ano_res_subpath(base, "tex/albedo.png");
    CHECK(j.length == base.length + 1 + 14, "subpath joins");
    CHECK(j.length > 0 && strstr(j.str, "anores_mnt1/tex/albedo.png") != NULL,
          "subpath content");
    CHECK(ano_res_subpath(base, "../escape").length == 0, "subpath rejects dotdot");
    CHECK(ano_res_subpath(base, "/abs").length == 0, "subpath rejects absolute");
    CHECK(ano_res_subpath((ano_fspath){0}, "x").length == 0, "subpath rejects empty base");
    char longrel[300];
    memset(longrel, 'b', sizeof longrel - 1);
    longrel[sizeof longrel - 1] = '\0';
    CHECK(ano_res_subpath(base, longrel).length == 0, "subpath rejects overflow");
}

static void test_shadow_and_mounts(mi_heap_t *heap)
{
    // Stage: base file in <exe>/resources, two mounts, one grafted mount.
    scratch_make_dir("resources");
    scratch_make_dir("resources/anotest_res");
    scratch_make_dir("anores_mnt1");
    scratch_make_dir("anores_mnt1/anotest_res");
    scratch_make_dir("anores_mnt2");
    scratch_make_dir("anores_mnt2/anotest_res");
    scratch_make_dir("anores_graft");
    CHECK(write_file("resources/anotest_res/shadow.txt", "base-content", 12), "stage base");
    CHECK(write_file("anores_mnt1/anotest_res/shadow.txt", "mnt1-content", 12), "stage mnt1");
    CHECK(write_file("anores_mnt2/anotest_res/shadow.txt", "mnt2-content", 12), "stage mnt2");
    CHECK(write_file("resources/anotest_res/base_only.txt", "only-in-base", 12), "stage base-only");
    CHECK(write_file("anores_graft/inner.txt", "grafted-bytes", 13), "stage graft");

    // Invalid mounts consume no slots.
    CHECK(ano_res_mount("", (ano_fspath){0}) == -1, "empty root refuses");
    CHECK(ano_res_mount("../bad", fspath_of("anores_mnt1")) == -1, "bad prefix refuses");
    CHECK(ano_res_mount(NULL, fspath_of("anores_mnt1")) == -1, "NULL prefix refuses");

    // Real mounts: mnt1 then mnt2 (newer), then the graft.
    CHECK(ano_res_mount("", fspath_of("anores_mnt1")) == 0, "mount mnt1");
    CHECK(ano_res_mount("", fspath_of("anores_mnt2")) == 0, "mount mnt2");
    CHECK(ano_res_mount("graft/", fspath_of("anores_graft")) == 0, "mount graft");

    // Fill the table (scoped away so they never resolve anything), then overflow.
    int duds = 0;
    while (ano_res_mount("zzz_never/", fspath_of("anores_mnt1")) == 0)
        duds++;
    CHECK(duds == ANO_RES_MAX_MOUNTS - 3, "table filled to ANO_RES_MAX_MOUNTS");
    CHECK(ano_res_mount("", fspath_of("anores_mnt1")) == -1, "9th mount refuses");

    // First read freezes the table. Newest mount wins over older and base.
    CHECK(slurp_equals(heap, "anotest_res/shadow.txt", "mnt2-content", 12),
          "newest mount shadows older and base");
    CHECK(ano_res_mount("", fspath_of("anores_mnt1")) == -1, "mount after freeze refuses");

    // Base answers when no mount has it.
    CHECK(slurp_equals(heap, "anotest_res/base_only.txt", "only-in-base", 12),
          "base serves unshadowed files");

    // The graft: logical prefix maps into the mount root; ungrafted name stays unknown.
    CHECK(slurp_equals(heap, "graft/inner.txt", "grafted-bytes", 13), "grafted mount serves");
    CHECK(ano_res_exists("inner.txt") == false, "graft does not leak to the root namespace");
    CHECK(ano_res_exists("graft/inner.txt") == true, "exists sees the graft");

    // resolve points at the winning root.
    ano_fspath r = ano_res_resolve("anotest_res/shadow.txt");
    CHECK(r.length > 0 && strstr(r.str, "anores_mnt2") != NULL, "resolve names the winner");
    CHECK(ano_res_resolve("anotest_res/nowhere.bin").length == 0, "resolve miss is empty");

    // Write root beats every mount: land a shadow via the durable write path.
    CHECK(ano_res_write("anotest_res/shadow.txt", "user-content", 12) == 0,
          "write into the write root");
    CHECK(slurp_equals(heap, "anotest_res/shadow.txt", "user-content", 12),
          "write root shadows all mounts");
    r = ano_res_resolve("anotest_res/shadow.txt");
    CHECK(r.length > 0 && strstr(r.str, "anores_mnt2") == NULL, "resolve moved to write root");
}

static void test_read_contract(mi_heap_t *heap)
{
    // Small file: byte-identical, aligned, guard NUL.
    uint8_t blob[100];
    test_rng rng = rng_make(0xC0FFEEu);
    for (size_t i = 0; i < sizeof blob; i++)
        blob[i] = (uint8_t)rng_next(&rng);
    blob[13] = 0;                                       // embedded NUL must survive
    CHECK(write_file("resources/anotest_res/blob.bin", blob, sizeof blob), "stage blob");
    anostr_t v = ano_res_slurp(heap, "anotest_res/blob.bin");
    CHECK(anostr_len(v) == sizeof blob, "slurp size == bytes written");
    CHECK(memcmp(anostr_bytes(&v), blob, sizeof blob) == 0, "slurp byte-identical");
    CHECK(((uintptr_t)anostr_bytes(&v) & (ANO_CACHE_LINE - 1)) == 0,
          "slurp buffer is cache-line aligned");
    CHECK(anostr_bytes(&v)[sizeof blob] == 0, "guard NUL past the end");

    // Tiny file (<= 12 bytes): inline value, still byte-exact.
    CHECK(write_file("resources/anotest_res/tiny.txt", "tiny-body", 9), "stage tiny");
    anostr_t t = ano_res_slurp(heap, "anotest_res/tiny.txt");
    CHECK(anostr_len(t) == 9 && anostr_is_inline(t), "tiny slurp is inline");
    CHECK(memcmp(anostr_bytes(&t), "tiny-body", 9) == 0, "tiny slurp byte-identical");

    // Empty file: the empty string (documented ambiguity with failure).
    CHECK(write_file("resources/anotest_res/empty.bin", "", 0), "stage empty");
    CHECK(anostr_len(ano_res_slurp(heap, "anotest_res/empty.bin")) == 0, "empty file slurps empty");

    // Multi-chunk: 1.5 MiB spans three 512 KiB reads; byte-identical end to end.
    size_t bigLen = 3u * 512u * 1024u / 2u + 17u;
    uint8_t *big = mi_heap_malloc(heap, bigLen);
    CHECK(big != NULL, "big staging buffer");
    if (big) {
        test_rng brng = rng_make(0xB16u);
        for (size_t i = 0; i < bigLen; i++)
            big[i] = (uint8_t)rng_next(&brng);
        CHECK(write_file("resources/anotest_res/big.bin", big, bigLen), "stage big");
        anostr_t bv = ano_res_slurp(heap, "anotest_res/big.bin");
        CHECK(anostr_len(bv) == bigLen, "big slurp size");
        CHECK(anostr_len(bv) == bigLen
              && memcmp(anostr_bytes(&bv), big, bigLen) == 0, "big slurp byte-identical");
        CHECK(anostr_len(bv) == bigLen && anostr_bytes(&bv)[bigLen] == 0, "big guard NUL");
    }

    // Absent file: empty, one log line, no UB.
    CHECK(anostr_len(ano_res_slurp(heap, "anotest_res/absent.bin")) == 0, "absent slurps empty");
}

static void test_write_and_quarantine(mi_heap_t *heap)
{
    // Durable replace + overwrite.
    uint8_t v1[256], v2[300];
    memset(v1, 0xA1, sizeof v1);
    memset(v2, 0xB2, sizeof v2);
    CHECK(ano_res_write("anotest_res/cfg.bin", v1, sizeof v1) == 0, "first write");
    CHECK(slurp_equals(heap, "anotest_res/cfg.bin", v1, sizeof v1), "first write readback");
    CHECK(ano_res_write("anotest_res/cfg.bin", v2, sizeof v2) == 0, "overwrite");
    CHECK(slurp_equals(heap, "anotest_res/cfg.bin", v2, sizeof v2), "overwrite readback");

    // resolve_write creates parents ready to open.
    ano_fspath deep = ano_res_resolve_write("anotest_res/deep/a/b/c.txt");
    CHECK(deep.length > 0, "resolve_write non-empty");
    if (deep.length)
        CHECK(write_file(deep.str, "d", 1), "resolve_write parents exist");

    // Quarantine: file moves aside as .broken, namespace no longer sees it.
    CHECK(ano_res_quarantine("anotest_res/cfg.bin") == 0, "quarantine");
    CHECK(ano_res_exists("anotest_res/cfg.bin") == false, "quarantined file gone");
    ano_fspath user = ano_fs_userpath();
    char broken[MAXPATH + 16];
    snprintf(broken, sizeof broken, "%s/anotest_res/cfg.bin.broken", user.str);
    CHECK(file_exists_stdio(broken), ".broken evidence exists");
    CHECK(ano_res_quarantine("anotest_res/cfg.bin") == -1, "re-quarantine refuses (absent)");

#ifndef _WIN32
    // No temp litter in the write directory.
    char wdir[MAXPATH + 16];
    snprintf(wdir, sizeof wdir, "%s/anotest_res", user.str);
    DIR *d = opendir(wdir);
    CHECK(d != NULL, "write dir opens");
    if (d) {
        struct dirent *e;
        int tmps = 0;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n >= 4 && strcmp(e->d_name + n - 4, ".tmp") == 0)
                tmps++;
        }
        closedir(d);
        CHECK(tmps == 0, "no .tmp litter after writes");
    }
#endif
}

static void test_saves(void)
{
    ano_fspath user = ano_fs_userpath();
    char path[MAXPATH + 48];

    // Pre-clean generations from previous runs so seq starts at 1.
    for (int i = 1; i <= 200; i++) {
        snprintf(path, sizeof path, "%s/saves/anotestslot.%d.anosave", user.str, i);
        remove(path);
    }

    // Five commits; each payload distinct; all succeed.
    uint8_t payload[512];
    for (int gen = 1; gen <= 5; gen++) {
        memset(payload, gen, sizeof payload);
        payload[0] = (uint8_t)gen;
        CHECK(ano_res_save_commit("anotestslot", 7u, payload, sizeof payload) == 0,
              "save commit succeeds");
    }

    // Exactly ANO_RES_SAVE_KEEP newest generations remain: 3, 4, 5.
    for (int i = 1; i <= 5; i++) {
        snprintf(path, sizeof path, "%s/saves/anotestslot.%d.anosave", user.str, i);
        bool present = file_exists_stdio(path);
        if (i <= 5 - ANO_RES_SAVE_KEEP)
            CHECK(!present, "old generation pruned");
        else
            CHECK(present, "recent generation kept");
    }

    // Independent oracle over the newest file: layout, hashes, payload, seq.
    snprintf(path, sizeof path, "%s/saves/anotestslot.5.anosave", user.str);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "newest save opens");
    if (f) {
        uint8_t buf[48 + 512 + 16];
        size_t got = fread(buf, 1, sizeof buf, f);
        CHECK(fgetc(f) == EOF, "save file has exact frame length");
        fclose(f);
        CHECK(got == sizeof buf, "save file size = 48 + payload + 16");
        if (got == sizeof buf) {
            CHECK(memcmp(buf, "ANOS", 4) == 0, "header magic");
            CHECK(buf[4] == 1 && buf[5] == 0, "container_version 1 LE");
            CHECK(buf[6] == 1, "hash_id FNV-1a-64");
            CHECK(buf[7] == 0, "flags 0");
            CHECK(oracle_le32(buf + 8) == 7u, "format_version echoed");
            CHECK(oracle_le32(buf + 12) == 7u, "min_reader_version");
            CHECK(oracle_le64(buf + 16) == 512u, "payload_len");
            CHECK(oracle_le64(buf + 24) == 5u, "seq echoes the filename");
            CHECK(oracle_le64(buf + 32) == oracle_fnv(buf, 32), "header hash (oracle)");
            CHECK(oracle_le64(buf + 40) == 0u, "reserved 0");
            memset(payload, 5, sizeof payload);
            payload[0] = 5;
            CHECK(memcmp(buf + 48, payload, 512) == 0, "payload byte-identical");
            CHECK(oracle_le64(buf + 48 + 512) == oracle_fnv(buf + 48, 512),
                  "payload hash (oracle)");
            CHECK(memcmp(buf + 48 + 512 + 8, "ANOSDONE", 8) == 0, "footer magic");
        }
    }

    // Cleanup this run's generations.
    for (int i = 1; i <= 8; i++) {
        snprintf(path, sizeof path, "%s/saves/anotestslot.%d.anosave", user.str, i);
        remove(path);
    }
}

// ---------------------------------------------------------------------------------------------

static void cleanup(void)
{
    // Exe-side scratch.
    remove("resources/anotest_res/shadow.txt");
    remove("resources/anotest_res/base_only.txt");
    remove("resources/anotest_res/blob.bin");
    remove("resources/anotest_res/tiny.txt");
    remove("resources/anotest_res/empty.bin");
    remove("resources/anotest_res/big.bin");
    scratch_remove_dir("resources/anotest_res");
    remove("anores_mnt1/anotest_res/shadow.txt");
    scratch_remove_dir("anores_mnt1/anotest_res");
    scratch_remove_dir("anores_mnt1");
    remove("anores_mnt2/anotest_res/shadow.txt");
    scratch_remove_dir("anores_mnt2/anotest_res");
    scratch_remove_dir("anores_mnt2");
    remove("anores_graft/inner.txt");
    scratch_remove_dir("anores_graft");

    // Write-root scratch.
    ano_fspath user = ano_fs_userpath();
    char p[MAXPATH + 48];
    snprintf(p, sizeof p, "%s/anotest_res/shadow.txt", user.str);        remove(p);
    snprintf(p, sizeof p, "%s/anotest_res/cfg.bin", user.str);           remove(p);
    snprintf(p, sizeof p, "%s/anotest_res/cfg.bin.broken", user.str);    remove(p);
    snprintf(p, sizeof p, "%s/anotest_res/deep/a/b/c.txt", user.str);    remove(p);
    snprintf(p, sizeof p, "%s/anotest_res/deep/a/b", user.str);          scratch_remove_dir(p);
    snprintf(p, sizeof p, "%s/anotest_res/deep/a", user.str);            scratch_remove_dir(p);
    snprintf(p, sizeof p, "%s/anotest_res/deep", user.str);              scratch_remove_dir(p);
    snprintf(p, sizeof p, "%s/anotest_res", user.str);                   scratch_remove_dir(p);
}

int main(void)
{
    scratch_anchor_to_exe();
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    (void)logAlive;

    test_before_init();

    CHECK(ano_res_init() == 0, "ano_res_init");
    CHECK(ano_res_init() == 0, "repeat init is a no-op success");

    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    CHECK(heap != NULL, "test heap");
    if (heap) {
        test_shadow_and_mounts(heap);       // mounts BEFORE the grammar fuzz freezes reads
        test_hostile_paths(heap);
        test_subpath();
        test_read_contract(heap);
        test_write_and_quarantine(heap);
        test_saves();
    }
    cleanup();

    if (failures == 0) { printf("anotest_resources: all checks passed\n"); return 0; }
    printf("anotest_resources: %d check(s) failed\n", failures);
    return 1;
}
