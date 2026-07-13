/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Historical resource-placement workloads retained behind the explicit lifetime-domain
 * grammar. This no longer selects or claims an allocator-contest model. The scenarios are:
 *   (a) steady-state get/unload churn over a 256-file synthetic streaming-size mix;
 *   (b) 50 scoped cycles over the available real glTF assets, including wall time,
 *       retire latency, and residual allocator footprint;
 *   (c) direct-class pointer-equality release plus conditioned-ingest throughput.
 * These workloads preserve the Phase D evidence but do not represent the complete
 * allocator hierarchy contest or establish any named contestant. NO FIGURE FROM THIS FILE MAY
 * ENTER A CONTEST TABLE -- see the deleted res_registry_stats() shim below. DISABLED in ctest;
 * historical numbers live in docs/resourcemgr/RESOURCE_MANAGER_IMPL.md. Exit 0 == pass. */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "anoptic_res_graphics.h"
#include "anoptic_resources.h"
#include "anoptic_log.h"
#include "templates/bench.h"
#include "templates/rng.h"
#include "templates/scratch.h"

#include "resources_internal.h"     // neutral placement vocabulary

static ano_res_lifetime g_engine_lifetime;
static ano_res_lifetime g_current_lifetime;
static ano_res_reader g_reader = { .lane = ANO_RES_READER_NONE };
static ano_res_read g_read;
static bool g_read_active;

/* DELETED: the res_registry_stats() shim. It aliased ONE field to two names --
 * direct_bytes = pools.live_bytes = ano_res_stats().live_bytes -- and the file then compared
 * them, printed their difference as a "direct" figure, and summed them into a "residual". The
 * manager exposes no direct-vs-pooled split today (that arrives with the telemetry cells), so
 * every number the shim produced was a tautology wearing a measurement's name. It is gone, and
 * NO FIGURE FROM THIS FILE MAY ENTER A CONTEST TABLE: ano_res_stats().live_bytes is the total
 * of pooled AND direct live bytes, chunk_bytes is what the arenas hold from the parent, and the
 * two OVERLAP -- they are reported separately below and are never added together. */

static void bench_read_begin(void)
{
    if (!g_read_active && ano_res_read_begin(&g_reader, &g_read) == 0)
        g_read_active = true;
}
static void bench_read_end(void)
{
    if (g_read_active) {
        ano_res_read_end(&g_read);
        g_read_active = false;
    }
}
static int bench_domain_begin(void)
{
    ano_res_lifetime scope = {0};
    if (ano_res_domain_open(ANO_RES_LIFETIME_WORLD_LEVEL, &scope) != 0)
        return -1;
    g_current_lifetime = scope;
    return (int)scope.owner;
}
static void bench_domain_end(int g) { (void)g; }
static int bench_domain_retire(int g)
{
    (void)g;
    bench_read_end();
    int rc = ano_res_domain_retire(g_current_lifetime);
    (void)ano_res_collect();
    g_current_lifetime = g_engine_lifetime;
    bench_read_begin();
    return rc;
}
static int bench_unload(anores_t h)
{
    bench_read_end();
    int rc = ano_res_unload(g_current_lifetime, h);
    (void)ano_res_collect();
    bench_read_begin();
    return rc;
}
static int bench_release(anores_t h, void **data, size_t *size)
{
    bench_read_end();
    int rc = ano_res_release(g_current_lifetime, h, data, size);
    bench_read_begin();
    return rc;
}
#define ano_res_get(path) ano_res_get(g_current_lifetime, (path))
#define ano_res_bytes(handle) ano_res_bytes(&g_read, (handle))
#define ano_res_unload(handle) bench_unload((handle))
#define ano_res_release(handle, data, size) bench_release((handle), (data), (size))
#define ano_resgfx_model(handle) ano_resgfx_model(g_current_lifetime, &g_read, (handle))
#define ano_resgfx_scene(handle) ano_resgfx_scene(&g_read, (handle))

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RB_DIR    "resources/anotest_rb"
#define RB_FILES  256u
#define RB_OPS    100000u
#define RB_HITS   20000u
#define RB_CYCLES 50u
#define RB_REPS   200u

// Streaming size mix: five pooled classes; every 32nd file is direct-class (2 MiB).
static const uint32_t RB_SIZES[] = { 1024, 4096, 16384, 65536, 262144 };
#define RB_DIRECT_EVERY 32u
#define RB_DIRECT_BYTES ((1u << 21) + 64u)

static uint64_t g_sink;             // defeat dead-code elimination

static bool write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = len == 0 || fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static void rb_path(char *out, size_t cap, uint32_t i, bool logical)
{
    snprintf(out, cap, "%sanotest_rb/f%03u.bin", logical ? "" : "resources/", i);
}

static uint32_t rb_size(uint32_t i)
{
    if (i % RB_DIRECT_EVERY == 0)
        return RB_DIRECT_BYTES;
    return RB_SIZES[i % (sizeof RB_SIZES / sizeof RB_SIZES[0])];
}

static bool stage(void)
{
    static uint8_t buf[RB_DIRECT_BYTES];
    for (size_t i = 0; i < sizeof buf; i++)
        buf[i] = (uint8_t)(i * 17 + 11);
    scratch_make_dir("resources");
    scratch_make_dir(RB_DIR);
    char p[MAXPATH];
    for (uint32_t i = 0; i < RB_FILES; i++) {
        rb_path(p, sizeof p, i, false);
        if (!write_file(p, buf, rb_size(i)))
            return false;
    }
    return true;
}

static void unstage(void)
{
    char p[MAXPATH];
    for (uint32_t i = 0; i < RB_FILES; i++) {
        rb_path(p, sizeof p, i, false);
        remove(p);
    }
    scratch_remove_dir(RB_DIR);
}

// ---------------------------------------------------------------------------------------------
// (a) Steady-state churn. One warm pass loads every file once (page cache + rows
// bound), then RB_OPS toggle ops with per-op latency split into load/unload series,
// then RB_HITS pure cache-hit gets. Runs twice: group 0, then inside an open scope.

static void run_churn(const char *tag, bool in_scope)
{
    static anores_t handle[RB_FILES];
    static uint64_t loadTicks[RB_OPS], unloadTicks[RB_OPS], hitTicks[RB_HITS];
    char p[MAXPATH];

    int g = -1;
    if (in_scope) {
        g = bench_domain_begin();
        CHECK(g >= 1, "churn scope opens");
    }

    // Warm pass: rows bound, files hot, then everything unloaded.
    for (uint32_t i = 0; i < RB_FILES; i++) {
        rb_path(p, sizeof p, i, true);
        anores_t h = ano_res_get(p);
        CHECK(h.gen != 0, "warm load");
        handle[i] = h;
    }
    for (uint32_t i = 0; i < RB_FILES; i++)
        CHECK(ano_res_unload(handle[i]) == 0, "warm unload");
    memset(handle, 0, sizeof handle);

    bench_lat load, unload, hit;
    bench_lat_init(&load, loadTicks, RB_OPS);
    bench_lat_init(&unload, unloadTicks, RB_OPS);
    bench_lat_init(&hit, hitTicks, RB_HITS);
    test_rng rng = rng_make(0x5EED0 + (in_scope ? 1 : 0));

    uint64_t w0 = bench_begin();
    for (uint32_t it = 0; it < RB_OPS; it++) {
        uint32_t i = rng_below(&rng, RB_FILES);
        if (handle[i].gen != 0) {
            uint64_t t0 = bench_begin();
            int rc = ano_res_unload(handle[i]);
            bench_lat_add(&unload, bench_end(t0));
            CHECK(rc == 0, "churn unload");
            handle[i] = (anores_t){0};
        } else {
            rb_path(p, sizeof p, i, true);
            uint64_t t0 = bench_begin();
            anores_t h = ano_res_get(p);
            bench_lat_add(&load, bench_end(t0));
            CHECK(h.gen != 0, "churn load");
            handle[i] = h;
            g_sink += h.rid;
        }
    }
    uint64_t wall = bench_end(w0);

    // Pure cache hits over whatever is loaded (load the first file if nothing is).
    if (handle[0].gen == 0) {
        rb_path(p, sizeof p, 0, true);
        handle[0] = ano_res_get(p);
    }
    for (uint32_t it = 0; it < RB_HITS; it++) {
        uint32_t i = rng_below(&rng, RB_FILES);
        if (handle[i].gen == 0)
            i = 0;
        rb_path(p, sizeof p, i, true);
        uint64_t t0 = bench_begin();
        anores_t h = ano_res_get(p);
        bench_lat_add(&hit, bench_end(t0));
        g_sink += h.gen;
    }

    for (uint32_t i = 0; i < RB_FILES; i++)
        if (handle[i].gen != 0)
            ano_res_unload(handle[i]);
    memset(handle, 0, sizeof handle);

    char label[64];
    snprintf(label, sizeof label, "%s get(load)", tag);
    bench_lat_row(label, bench_lat_stats(&load));
    snprintf(label, sizeof label, "%s unload", tag);
    bench_lat_row(label, bench_lat_stats(&unload));
    snprintf(label, sizeof label, "%s get(hit)", tag);
    bench_lat_row(label, bench_lat_stats(&hit));
    printf("%-28s   wall %.1f ms, %.2f Mops/s\n", "",
           (double)ano_ticks_to_ns(wall) / 1e6,
           bench_ops_per_sec(RB_OPS, ano_ticks_to_ns(wall)) / 1e6);

    if (in_scope) {
        bench_domain_end(g);
        CHECK(bench_domain_retire(g) == 0, "churn scope retires");
    }
}

// ---------------------------------------------------------------------------------------------
// (b) Level cycles over the real assets. The set is built once from what exists.

static const char *ASSET_GLTFS[] = {
    "assets_real/viking_room.gltf",
    "assets_real/GlassHurricaneCandleHolder.gltf",
    "assets_real/sponza/2.0/Sponza/glTF/Sponza.gltf",
};
#define N_GLTFS (sizeof ASSET_GLTFS / sizeof ASSET_GLTFS[0])

static void run_cycles(bool have_assets)
{
    if (!have_assets) {
        printf("(b) level cycles skipped: assets tree not mounted\n");
        return;
    }
    static uint64_t cycleTicks[RB_CYCLES], retireTicks[RB_CYCLES];
    bench_lat cyc, ret;
    bench_lat_init(&cyc, cycleTicks, RB_CYCLES);
    bench_lat_init(&ret, retireTicks, RB_CYCLES);

    ano_res_allocator_stats base = ano_res_stats();
    size_t liveResidual1 = 0, chunkResidual1 = 0;
    double mbPerCycle = 0.0;

    for (uint32_t c = 0; c < RB_CYCLES; c++) {
        uint64_t t0 = bench_begin();
        int g = bench_domain_begin();
        CHECK(g >= 1, "cycle scope opens");
        size_t loaded = 0;
        for (size_t m = 0; m < N_GLTFS; m++) {
            anores_t src = ano_res_get(ASSET_GLTFS[m]);
            if (src.gen == 0)
                continue;                       // absent asset: skipped, counted below
            anostr_t sv = ano_res_bytes(src);
            loaded += anostr_len(sv);
            anores_t scene = ano_resgfx_model(src);
            CHECK(scene.gen != 0, "ingest");
            anoresgfx_scene view = ano_resgfx_scene(scene);
            loaded += (size_t)view.vertex_count * sizeof(anoresgfx_vertex);
            g_sink += view.vertex_count;
        }
        bench_domain_end(g);
        uint64_t r0 = bench_begin();
        CHECK(bench_domain_retire(g) == 0, "cycle retire");
        bench_lat_add(&ret, bench_end(r0));
        bench_lat_add(&cyc, bench_end(t0));

        // Live and chunk deltas OVERLAP (pooled live bytes are served out of chunks). Reported
        // side by side, never summed: the old shim's "residual" double-counted them.
        ano_res_allocator_stats now = ano_res_stats();
        size_t liveResidual  = now.live_bytes  - base.live_bytes;
        size_t chunkResidual = now.chunk_bytes - base.chunk_bytes;
        if (c == 0) {
            liveResidual1  = liveResidual;
            chunkResidual1 = chunkResidual;
            mbPerCycle     = (double)loaded / (1024.0 * 1024.0);
        }
        if (c == 0 || c == RB_CYCLES / 2 - 1 || c == RB_CYCLES - 1)
            printf("(b) cycle %2u: live +%zu B, chunks +%zu B (over baseline; NOT summed)\n",
                   c + 1, liveResidual, chunkResidual);
    }

    bench_lat_row("level cycle", bench_lat_stats(&cyc));
    bench_lat_row("retire only", bench_lat_stats(&ret));
    printf("(b) ~%.1f MiB ingested per cycle; after cycle 1: live +%zu B, chunks +%zu B\n",
           mbPerCycle, liveResidual1, chunkResidual1);

    // Scoped placement returns group chunks to baseline; global placement may retain
    // high-water chunks (reported above, not asserted).
    ano_res_allocator_stats end = ano_res_stats();
    CHECK(end.live_bytes == base.live_bytes, "no live leak across cycles");
    CHECK(end.chunk_bytes == base.chunk_bytes,
          "retired domains return chunk bytes to baseline");
}

// ---------------------------------------------------------------------------------------------
// (c) Direct-class hand-off: zero-copy release, every rep, pointer-proven.

static void run_handoff(bool have_assets)
{
    static uint64_t relTicks[RB_REPS];
    bench_lat rel;
    bench_lat_init(&rel, relTicks, RB_REPS);
    uint32_t zerocopy = 0;

    uint64_t w0 = bench_begin();
    for (uint32_t r = 0; r < RB_REPS; r++) {
        anores_t h = ano_res_get("anotest_rb/f000.bin");    // 2 MiB: direct-class
        CHECK(h.gen != 0, "hand-off get");
        anostr_t v = ano_res_bytes(h);
        const char *resident = anostr_bytes(&v);
        void *blk = NULL;
        size_t bs = 0;
        uint64_t t0 = bench_begin();
        int rc = ano_res_release(h, &blk, &bs);
        bench_lat_add(&rel, bench_end(t0));
        CHECK(rc == 0, "hand-off release");
        if (blk == (const void *)resident && bs == RB_DIRECT_BYTES)
            zerocopy++;
        g_sink += ((uint8_t *)blk)[bs - 1];
        ano_aligned_free(blk);
    }
    uint64_t wall = bench_end(w0);

    bench_lat_row("direct release", bench_lat_stats(&rel));
    printf("(c) %u/%u zero-copy hand-offs, %.0f hand-offs/s (get+release+free)\n",
           zerocopy, RB_REPS,
           bench_ops_per_sec(RB_REPS, ano_ticks_to_ns(wall)));
    CHECK(zerocopy == RB_REPS, "every direct release is zero-copy");

    // Ingest throughput: the biggest present glTF, 3 reps, best. Staging machinery is
    // identical in both placement scaffolds.
    if (have_assets) {
        const char *best = NULL;
        size_t bestLen = 0;
        for (size_t m = 0; m < N_GLTFS; m++) {
            anores_t src = ano_res_get(ASSET_GLTFS[m]);
            if (src.gen == 0)
                continue;
            size_t len = anostr_len(ano_res_bytes(src));
            if (len > bestLen) { bestLen = len; best = ASSET_GLTFS[m]; }
        }
        if (best != NULL) {
            double bestMs = 1e30;
            size_t bytes = 0;
            for (int rep = 0; rep < 3; rep++) {
                int g = bench_domain_begin();
                anores_t src = ano_res_get(best);
                uint64_t t0 = bench_begin();
                anores_t scene = ano_resgfx_model(src);
                uint64_t ns = ano_ticks_to_ns(bench_end(t0));
                CHECK(scene.gen != 0, "ingest throughput parse");
                anoresgfx_scene view = ano_resgfx_scene(scene);
                bytes = anostr_len(ano_res_bytes(src))
                      + (size_t)view.vertex_count * sizeof(anoresgfx_vertex)
                      + (size_t)view.index_count * 4u;
                if ((double)ns / 1e6 < bestMs)
                    bestMs = (double)ns / 1e6;
                bench_domain_end(g);
                bench_domain_retire(g);
            }
            printf("(c) ingest %s: best %.2f ms, %.1f MB/s conditioned\n",
                   best, bestMs, (double)bytes / (1024.0 * 1024.0) / (bestMs / 1e3));
        }
    }
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    scratch_anchor_to_exe();
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    (void)logAlive;

    CHECK(stage(), "stage synthetic tree");
    CHECK(ano_res_init() == 0, "ano_res_init");

    bool have_assets = false;
#ifdef ANO_TEST_ASSETS
    {
        ano_fspath assets = {0};
        int w = snprintf(assets.str, sizeof assets.str, "%s", ANO_TEST_ASSETS);
        if (w > 0 && w < (int)sizeof assets.str) {
            assets.length = (uint16_t)w;
            have_assets = ano_res_mount("assets_real/", assets) == 0;
        }
    }
#endif
    g_engine_lifetime = ano_res_lifetime_engine();
    g_current_lifetime = g_engine_lifetime;
    CHECK(ano_res_reader_register(&g_reader) == 0, "reader register");
    bench_read_begin();

    printf("anotest_resbench: explicit lifetime domains%s\n",
           have_assets ? "" : " (no assets tree: scenario (b) reduced)");
    bench_lat_header();

    run_churn("churn/g0", false);
    run_churn("churn/scope", true);
    run_cycles(have_assets);
    run_handoff(have_assets);

    unstage();
    bench_read_end();
    CHECK(ano_res_reader_unregister(&g_reader) == 0, "reader unregister");
    (void)ano_res_collect();
    CHECK(ano_res_shutdown() == 0, "resource shutdown");
    printf("sink %llu\n", (unsigned long long)g_sink);
    if (failures == 0) { printf("anotest_resbench: all checks passed\n"); return 0; }
    printf("anotest_resbench: %d check(s) failed\n", failures);
    return 1;
}
