/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Explicit lifetime-domain coverage: engine/world/save ownership, reader-pinned retirement,
 * stale handles, same-slot reload, transfer after quiescence, and zero-residue shutdown.
 * The reader-pinned oracle is that bytes borrowed before retirement remain byte-exact until
 * read_end, while fresh resolution is already sentinel. Exit 0 == pass. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "anoptic_log.h"
#include "anoptic_resources.h"
#include "templates/scratch.h"

#define GRP_DIR "resources/anotest_grp"
#define BIG_BYTES ((1u << 21) + 100u)

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

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
    uint8_t small[4096];
    for (size_t i = 0; i < sizeof small; i++) small[i] = (uint8_t)(i * 13 + 5);
    for (size_t i = 0; i < sizeof g_big; i++) g_big[i] = (uint8_t)(i * 31 + 7);
    return write_file(GRP_DIR "/engine.bin", small, 1000)
        && write_file(GRP_DIR "/world.bin", small, sizeof small)
        && write_file(GRP_DIR "/big.bin", g_big, sizeof g_big);
}

static void unstage(void)
{
    remove(GRP_DIR "/engine.bin");
    remove(GRP_DIR "/world.bin");
    remove(GRP_DIR "/big.bin");
    scratch_remove_dir(GRP_DIR);
}

int main(void)
{
    scratch_anchor_to_exe();
    int log_alive ANO_LOG_SCOPE_ATTR = ano_log_init();
    (void)log_alive;
    CHECK(stage(), "stage fixtures");
    CHECK(ano_res_init() == 0, "resource init");

    ano_res_lifetime engine = ano_res_lifetime_engine();
    ano_res_lifetime world = {0};
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_WORLD_LEVEL, &world) == 0, "world domain open");

    anores_t permanent = ano_res_get(engine, "anotest_grp/engine.bin");
    anores_t scoped = ano_res_get(world, "anotest_grp/world.bin");
    CHECK(permanent.gen != 0 && scoped.gen != 0, "explicit loads");

    ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
    ano_res_read read = {0};
    CHECK(ano_res_reader_register(&reader) == 0, "reader register");
    CHECK(ano_res_read_begin(&reader, &read) == 0, "read begin");
    anostr_t pinned = ano_res_bytes(&read, scoped);
    CHECK(anostr_len(pinned) == 4096, "world bytes visible");

    CHECK(ano_res_domain_retire(world) == 0, "world retirement accepted");
    CHECK(anostr_len(ano_res_bytes(&read, scoped)) == 0, "retired publication is sentinel");
    CHECK(anostr_len(pinned) == 4096 && (uint8_t)anostr_bytes(&pinned)[4095] == (uint8_t)(4095 * 13 + 5),
          "pre-retire borrowed bytes remain pinned");
    CHECK(anostr_len(ano_res_bytes(&read, permanent)) == 1000, "engine lifetime survives");
    ano_res_allocator_stats mid = ano_res_stats();
    CHECK(mid.retired_pending >= 1 && mid.stalled_readers >= 1, "stalled reader accounted");

    ano_res_read_end(&read);
    CHECK(ano_res_collect() >= 1, "quiescence reclaims retired payload");
    CHECK(ano_res_stats().retired_pending == 0, "retirement queue drained");

    ano_res_lifetime world2 = {0};
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_WORLD_LEVEL, &world2) == 0, "world domain reopens");
    anores_t reloaded = ano_res_get(world2, "anotest_grp/world.bin");
    CHECK(reloaded.slot == scoped.slot && reloaded.gen > scoped.gen, "same slot, fresh generation");
    CHECK(ano_res_domain_retire(world2) == 0, "second world retires");
    (void)ano_res_collect();

    ano_res_lifetime streaming = {0};
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_STREAMING, &streaming) == 0, "streaming domain");
    anores_t big = ano_res_get(streaming, "anotest_grp/big.bin");
    void *taken = NULL;
    size_t taken_size = 0;
    CHECK(ano_res_release(streaming, big, &taken, &taken_size) == 0, "transfer after quiescence");
    CHECK(taken != NULL && taken_size == BIG_BYTES && memcmp(taken, g_big, BIG_BYTES) == 0,
          "transferred bytes exact");
    ano_aligned_free(taken);
    CHECK(ano_res_domain_retire(streaming) == 0, "streaming domain retires");

    CHECK(ano_res_unload(engine, permanent) == 0, "engine resource unload");
    (void)ano_res_collect();
    CHECK(ano_res_reader_unregister(&reader) == 0, "reader unregister");
    unstage();
    CHECK(ano_res_shutdown() == 0, "zero-residue shutdown");
    ano_res_allocator_stats zero = ano_res_stats();
    CHECK(zero.live_bytes == 0 && zero.live_blocks == 0 && zero.domains_live == 0,
          "shutdown reports no residue");

    if (failures == 0) { printf("anotest_resgroups: all checks passed\n"); return 0; }
    printf("anotest_resgroups: %d check(s) failed\n", failures);
    return 1;
}
