/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for the section-5 lifetime-group seam (res_group_begin/end/retire), run
 * under BOTH placement models (ANO_RES_MODEL unset/A and =E -- two ctest
 * registrations, one binary; the header prints the active model):
 *   - loads inside an open scope go sentinel after retire; loads outside (group 0)
 *     survive; re-get after retire reloads the same slot at a fresh generation;
 *   - release-then-retire on a direct row: the handed-off block outlives the group
 *     and there is no double free (ASan is the oracle);
 *   - a gamesave loaded DURING an open scope pins to group 0 and survives the retire;
 *   - stats balance: direct bytes return to baseline after retire under both models;
 *     pool chunk bytes return to baseline under E only (the group pool dies whole),
 *     and stay at high-water under A (its recorded wound) -- asserted via res_model();
 *   - totality: retire of group 0 / unopened / junk ids refuses; the group table
 *     exhausts at RES_GROUP_MAX - 1 concurrent scopes and recovers after retire.
 * Scratch lives next to the exe; saves use the real user root; both cleaned on exit.
 * Exit 0 == pass. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "anoptic_resources.h"
#include "anoptic_log.h"
#include "templates/scratch.h"

#include "resources_internal.h"     // the module-private group hooks under test

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define GRP_DIR   "resources/anotest_grp"
#define BIG_BYTES ((1u << 21) + 100u)           // past the 1 MiB top class: direct

static uint8_t g_big[BIG_BYTES];

static bool write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = len == 0 || fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static bool stage(void)
{
    scratch_make_dir("resources");
    scratch_make_dir(GRP_DIR);
    uint8_t buf[200000];
    for (size_t i = 0; i < sizeof buf; i++)
        buf[i] = (uint8_t)(i * 13 + 5);
    for (size_t i = 0; i < sizeof g_big; i++)
        g_big[i] = (uint8_t)(i * 31 + 7);
    return write_file(GRP_DIR "/p1.bin", buf, 3000)
        && write_file(GRP_DIR "/p2.bin", buf, 40000)
        && write_file(GRP_DIR "/p3.bin", buf, 200000)
        && write_file(GRP_DIR "/big.bin", g_big, sizeof g_big);
}

static void unstage(void)
{
    remove(GRP_DIR "/p1.bin");
    remove(GRP_DIR "/p2.bin");
    remove(GRP_DIR "/p3.bin");
    remove(GRP_DIR "/big.bin");
    scratch_remove_dir(GRP_DIR);
}

// ---------------------------------------------------------------------------------------------
// Scope semantics: in-scope loads retire, group-0 loads survive, re-get reloads.

static void test_scopes(void)
{
    anores_t out = ano_res_get("anotest_grp/p1.bin");
    CHECK(out.gen != 0, "group-0 load");

    int g = res_group_begin();
    CHECK(g >= 1, "scope opens");
    anores_t in = ano_res_get("anotest_grp/p2.bin");
    CHECK(in.gen != 0, "in-scope load");
    res_group_end(g);

    // Ended-but-not-retired: payloads stay live.
    CHECK(anostr_len(ano_res_bytes(in)) == 40000, "ended scope still serves");

    CHECK(res_group_retire(g) == 0, "retire");
    CHECK(anostr_len(ano_res_bytes(in)) == 0, "in-scope handle went sentinel");
    CHECK(anostr_len(ano_res_bytes(out)) == 3000, "group-0 handle survives");
    CHECK(ano_res_unload(in) == -1, "stale unload refuses");

    // Permanent rid binding: the reload lands in the same slot, fresh generation.
    anores_t re = ano_res_get("anotest_grp/p2.bin");
    CHECK(re.slot == in.slot && re.rid == in.rid, "reload keeps the slot binding");
    CHECK(re.gen > in.gen, "reload bumps the generation");
    CHECK(ano_res_unload(re) == 0, "unload reload");
    CHECK(ano_res_unload(out) == 0, "unload group-0 handle");
}

// ---------------------------------------------------------------------------------------------
// Release-then-retire: a handed-off direct block outlives its group; a pooled release
// inside a scope leaves nothing for the sweep to double-free (ASan watches).

static void test_release_then_retire(void)
{
    int g = res_group_begin();
    CHECK(g >= 1, "scope opens");

    anores_t hb = ano_res_get("anotest_grp/big.bin");
    CHECK(hb.gen != 0, "direct load");
    anostr_t bv = ano_res_bytes(hb);
    const char *resident = anostr_bytes(&bv);
    void *blk = NULL; size_t bs = 0;
    CHECK(ano_res_release(hb, &blk, &bs) == 0, "direct release");
    CHECK(blk == (void *)resident && bs == BIG_BYTES, "direct release is zero-copy");

    anores_t hp = ano_res_get("anotest_grp/p3.bin");
    void *pblk = NULL; size_t ps = 0;
    CHECK(ano_res_release(hp, &pblk, &ps) == 0, "pooled release");
    CHECK(ps == 200000, "pooled release size");

    res_group_end(g);
    CHECK(res_group_retire(g) == 0, "retire after releases");

    // The taker's blocks are untouched by the sweep: readable, then freed exactly once.
    CHECK(memcmp(blk, g_big, BIG_BYTES) == 0, "handed-off block intact after retire");
    ((uint8_t *)blk)[0] ^= 0xFF;
    ano_aligned_free(blk);
    CHECK(((const uint8_t *)pblk)[199999] == (uint8_t)(199999 * 13 + 5),
          "pooled copy intact after retire");
    ano_aligned_free(pblk);
}

// ---------------------------------------------------------------------------------------------
// Save pinning: a save loaded during an open scope belongs to group 0, structurally.

static void test_save_pinning(void)
{
    ano_fspath user = ano_fs_userpath();
    char path[MAXPATH + 48];
    for (int i = 1; i <= 200; i++) {
        snprintf(path, sizeof path, "%s/saves/anogrpslot.%d.anosave", user.str, i);
        remove(path);
    }

    uint8_t payload[256];
    memset(payload, 0xA5, sizeof payload);
    CHECK(ano_res_save_commit("anogrpslot", 1u, payload, sizeof payload) == 0,
          "save commit");

    int g = res_group_begin();
    CHECK(g >= 1, "scope opens");
    anores_t hs = ano_res_save_load("anogrpslot", NULL, NULL);
    CHECK(hs.gen != 0, "save loads inside the scope");
    res_group_end(g);
    CHECK(res_group_retire(g) == 0, "retire");

    anostr_t v = ano_res_bytes(hs);
    CHECK(anostr_len(v) == sizeof payload, "save survives the retire (pinned)");
    CHECK(anostr_len(v) != 0 && memcmp(anostr_bytes(&v), payload, sizeof payload) == 0,
          "save bytes intact");
    CHECK(ano_res_unload(hs) == 0, "unload save");

    for (int i = 1; i <= 8; i++) {
        snprintf(path, sizeof path, "%s/saves/anogrpslot.%d.anosave", user.str, i);
        remove(path);
    }
}

// ---------------------------------------------------------------------------------------------
// Stats balance around one scope cycle. Direct bytes rebalance under both models; chunk
// bytes rebalance under E only (A's shared pool keeps its high-water chunks: the wound).

static void test_stats_balance(void)
{
    res_reg_stats base = res_registry_stats();

    int g = res_group_begin();
    CHECK(g >= 1, "scope opens");
    anores_t h1 = ano_res_get("anotest_grp/p1.bin");
    anores_t h2 = ano_res_get("anotest_grp/p2.bin");
    anores_t hb = ano_res_get("anotest_grp/big.bin");
    CHECK(h1.gen != 0 && h2.gen != 0 && hb.gen != 0, "scope loads");

    res_reg_stats mid = res_registry_stats();
    CHECK(mid.direct_blocks == base.direct_blocks + 1, "direct block counted");
    CHECK(mid.direct_bytes > base.direct_bytes, "direct bytes counted");
    CHECK(mid.pools.live_bytes > base.pools.live_bytes, "pooled bytes live");

    res_group_end(g);
    CHECK(res_group_retire(g) == 0, "retire");

    res_reg_stats end = res_registry_stats();
    CHECK(end.direct_bytes == base.direct_bytes, "direct bytes back to baseline");
    CHECK(end.direct_blocks == base.direct_blocks, "direct blocks back to baseline");
    CHECK(end.pools.live_bytes == base.pools.live_bytes, "live bytes back to baseline");
    if (res_model() == RES_MODEL_E)
        CHECK(end.pools.chunk_bytes == base.pools.chunk_bytes,
              "E: group chunks returned whole");
    else
        CHECK(end.pools.chunk_bytes >= base.pools.chunk_bytes,
              "A: shared pool keeps high-water chunks");
}

// ---------------------------------------------------------------------------------------------
// Totality and table exhaustion.

static void test_totality(void)
{
    CHECK(res_group_retire(0) == -1, "group 0 never retires");
    CHECK(res_group_retire(99) == -1, "junk id refuses");
    CHECK(res_group_retire(3) == -1, "unopened id refuses");
    res_group_end(99);                          // hostile end is a no-op

    int ids[16];
    int n = 0;
    for (; n < 16; n++) {
        ids[n] = res_group_begin();
        if (ids[n] < 0)
            break;
    }
    CHECK(n >= 1, "at least one scope available");
    CHECK(n < 16, "the table is bounded");
    for (int i = 0; i < n; i++) {
        res_group_end(ids[i]);
        CHECK(res_group_retire(ids[i]) == 0, "exhaustion drain retires");
    }
    int again = res_group_begin();
    CHECK(again >= 1, "table recovers after retire");
    res_group_end(again);
    CHECK(res_group_retire(again) == 0, "recovered scope retires");
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    scratch_anchor_to_exe();
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    (void)logAlive;

    CHECK(stage(), "stage fixtures");
    CHECK(ano_res_init() == 0, "ano_res_init");
    printf("anotest_resgroups: model %c\n", res_model() == RES_MODEL_E ? 'E' : 'A');

    test_scopes();
    test_release_then_retire();
    test_save_pinning();
    test_stats_balance();
    test_totality();

    unstage();
    if (failures == 0) { printf("anotest_resgroups: all checks passed\n"); return 0; }
    printf("anotest_resgroups: %d check(s) failed\n", failures);
    return 1;
}
