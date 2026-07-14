/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Stage A ownership foundation: immutable publication races, stale handles, non-moving row
 * growth, copy accounting, promotion/duplication/transfer rules, narrow row/owner wrap
 * refusal, and zero-residue shutdown. Publication oracle: every scoped lookup is either the
 * empty stale sentinel or the complete byte-exact 4096-byte fixture; no mixed descriptor or
 * payload is accepted. argv[1] scales the fixed reload loop. Exit 0 == pass. */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_log.h"
#include "anoptic_resources.h"
#include "anoptic_threads.h"
#include "resources_ext.h"
#include "resources_internal.h"
#include "templates/scratch.h"

#define TEST_DIR "resources/anotest_owner"
#define TEST_BYTES 4096u
#define READER_COUNT 4u

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static uint8_t g_fixture[TEST_BYTES];
static _Atomic bool g_stop;
static _Atomic uint64_t g_handle_rid;
static _Atomic uint32_t g_handle_slot;
static _Atomic uint32_t g_handle_gen;
static _Atomic uint64_t g_good;
static _Atomic uint64_t g_stale;
static _Atomic uint64_t g_bad;

static bool write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

static void publish_handle(anores_t h)
{
    atomic_store_explicit(&g_handle_gen, 0, memory_order_release);
    atomic_store_explicit(&g_handle_rid, h.rid, memory_order_relaxed);
    atomic_store_explicit(&g_handle_slot, h.slot, memory_order_relaxed);
    atomic_store_explicit(&g_handle_gen, h.gen, memory_order_release);
}

static anores_t sample_handle(void)
{
    for (;;) {
        uint32_t a = atomic_load_explicit(&g_handle_gen, memory_order_acquire);
        if (a == 0)
            continue;
        anores_t h = {
            .rid = atomic_load_explicit(&g_handle_rid, memory_order_relaxed),
            .slot = atomic_load_explicit(&g_handle_slot, memory_order_relaxed),
            .gen = a,
        };
        uint32_t b = atomic_load_explicit(&g_handle_gen, memory_order_acquire);
        if (a == b)
            return h;
    }
}

static void *reader_main(void *unused)
{
    (void)unused;
    ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
    if (ano_res_reader_register(&reader) != 0) {
        atomic_fetch_add(&g_bad, 1);
        return NULL;
    }
    while (!atomic_load_explicit(&g_stop, memory_order_acquire)) {
        ano_res_read read = {0};
        if (ano_res_read_begin(&reader, &read) != 0) {
            atomic_fetch_add(&g_bad, 1);
            break;
        }
        anores_t h = sample_handle();
        anostr_t v = ano_res_bytes(&read, h);
        size_t n = anostr_len(v);
        if (n == 0) {
            atomic_fetch_add_explicit(&g_stale, 1, memory_order_relaxed);
        } else if (n == TEST_BYTES && memcmp(anostr_bytes(&v), g_fixture, TEST_BYTES) == 0) {
            atomic_fetch_add_explicit(&g_good, 1, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&g_bad, 1, memory_order_relaxed);
        }
        ano_res_read_end(&read);
    }
    if (ano_res_reader_unregister(&reader) != 0)
        atomic_fetch_add(&g_bad, 1);
    return NULL;
}

static void test_publication_race(ano_res_lifetime engine, uint32_t iterations)
{
    anores_t h = ano_res_get(engine, "anotest_owner/race.bin");
    CHECK(h.gen != 0, "initial publication");
    publish_handle(h);
    anothread_t readers[READER_COUNT];
    atomic_store(&g_stop, false);
    for (uint32_t i = 0; i < READER_COUNT; i++)
        CHECK(ano_thread_create(&readers[i], NULL, reader_main, NULL) == 0, "reader thread create");

    for (uint32_t i = 0; i < iterations; i++) {
        CHECK(ano_res_unload(engine, h) == 0, "race unload");
        h = ano_res_get(engine, "anotest_owner/race.bin");
        CHECK(h.gen != 0, "race reload");
        publish_handle(h);
        if ((i & 15u) == 0)
            (void)ano_res_collect();
    }
    atomic_store_explicit(&g_stop, true, memory_order_release);
    for (uint32_t i = 0; i < READER_COUNT; i++)
        CHECK(ano_thread_join(readers[i], NULL) == 0, "reader thread join");
    CHECK(atomic_load(&g_bad) == 0, "publication never mixed descriptor or bytes");
    CHECK(atomic_load(&g_good) > 0, "publication readers observed complete payloads");
    CHECK(ano_res_unload(engine, h) == 0, "final race unload");
    (void)ano_res_collect();
}

static void test_nonmoving_growth(ano_res_lifetime engine)
{
    anores_t handles[192] = {0};
    const void *first = NULL;
    ano_res_allocator_stats before = ano_res_stats();
    for (uint32_t i = 0; i < 192; i++) {
        char logical[64];
        snprintf(logical, sizeof logical, "anotest_owner/adopt%03u.bin", i);
        res_place_plan plan = {
            .tag = RES_TAG_BYTES, .lifetime = engine,
            .role = RES_ROLE_PAYLOAD, .operation = RES_OP_ADOPT,
            .destination = RES_DEST_VARIABLE_PAYLOAD,
            .provenance = RES_PROVENANCE_TOOL,
            .alignment = ANO_CACHE_LINE,
        };
        res_owned_block block = {0};
        CHECK(res_owned_alloc(&plan, 31, &block) == 0, "growth block allocation");
        if (block.data == NULL)
            break;
        memset(block.data, (int)i, 31);
        ((uint8_t *)block.data)[31] = 0;
        handles[i] = res_registry_adopt(logical, &block, NULL, 0);
        CHECK(handles[i].gen != 0, "growth adoption");
        if (i == 0)
            first = res_test_row_address(handles[i].slot);
    }
    CHECK(first != NULL && first == res_test_row_address(handles[0].slot),
          "row address remains stable across chunk and map growth");
    CHECK(ano_res_stats().rows_bound >= before.rows_bound + 192, "growth bound every row");
    for (uint32_t i = 0; i < 192; i++)
        if (handles[i].gen != 0)
            CHECK(ano_res_unload(engine, handles[i]) == 0, "growth unload");
    (void)ano_res_collect();
}

static void test_accounting_and_rules(ano_res_lifetime engine)
{
    ano_res_allocator_stats before = ano_res_stats();
    anores_t h = ano_res_get(engine, "anotest_owner/race.bin");
    ano_res_allocator_stats after = ano_res_stats();
    CHECK(after.copies > before.copies && after.bytes_copied >= before.bytes_copied + TEST_BYTES,
          "load copy is visible in accounting");
    CHECK(ano_res_unload(engine, h) == 0, "accounting unload");
    (void)ano_res_collect();

    ano_res_lifetime transient = {0}, tool = {0}, world = {0}, streaming = {0};
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_TRANSIENT_IMPORT, &transient) == 0, "transient open");
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_TOOL_IMPORT, &tool) == 0, "tool open");
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_WORLD_LEVEL, &world) == 0, "world open");
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_STREAMING, &streaming) == 0, "streaming open");
    CHECK(res_disposition_allowed(transient, engine, RES_DISPOSITION_PROMOTE, false) == 0,
          "transient may promote to engine");
    CHECK(res_disposition_allowed(transient, tool, RES_DISPOSITION_PROMOTE, false) == 0,
          "transient may promote to tool/import");
    CHECK(res_disposition_allowed(world, streaming, RES_DISPOSITION_PROMOTE, false) == -1,
          "world may not silently promote into streaming");
    CHECK(res_disposition_allowed(world, streaming, RES_DISPOSITION_DUPLICATE, false) == 0,
          "cross-lifetime duplication is explicit");
    CHECK(res_disposition_allowed(world, streaming, RES_DISPOSITION_TRANSFER, false) == -1,
          "interior placement cannot transfer");
    CHECK(res_disposition_allowed(world, streaming, RES_DISPOSITION_TRANSFER, true) == 0,
          "transfer-compatible placement may transfer");
    CHECK(ano_res_domain_retire(transient) == 0, "transient retire");
    CHECK(ano_res_domain_retire(tool) == 0, "tool retire");
    CHECK(ano_res_domain_retire(world) == 0, "world retire");
    CHECK(ano_res_domain_retire(streaming) == 0, "streaming retire");
    (void)ano_res_collect();
}

static void test_wrap_refusal(ano_res_lifetime engine)
{
    anores_t h = ano_res_get(engine, "anotest_owner/race.bin");
    CHECK(h.gen != 0, "wrap row load");
    CHECK(res_test_set_generation(h, UINT32_MAX - 1) == 0, "narrow row generation injection");
    anores_t narrow = { .rid = h.rid, .slot = h.slot, .gen = UINT32_MAX - 1 };
    CHECK(ano_res_unload(engine, narrow) == 0, "last representable row generation retires");
    CHECK(ano_res_get(engine, "anotest_owner/race.bin").gen == 0,
          "row generation wrap refuses reload");
    (void)ano_res_collect();

    ano_res_lifetime narrow_owner = {0};
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_TRANSIENT_IMPORT, &narrow_owner) == 0,
          "owner wrap domain open");
    CHECK(res_test_set_owner_generation(narrow_owner, UINT32_MAX - 1) == 0,
          "narrow owner generation injection");
    narrow_owner.generation = UINT32_MAX - 1;
    CHECK(ano_res_domain_retire(narrow_owner) == 0, "owner max-minus-one retires");
    (void)ano_res_collect();
    ano_res_lifetime last = {0};
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_TRANSIENT_IMPORT, &last) == 0
          && last.generation == UINT32_MAX, "owner reaches final generation once");
    CHECK(ano_res_domain_retire(last) == 0, "final owner generation retires");
    (void)ano_res_collect();
    ano_res_lifetime refused = {0};
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_TRANSIENT_IMPORT, &refused) == -1,
          "owner generation wrap refuses reuse");
}

int main(int argc, char **argv)
{
    uint32_t iterations = 1000;
    if (argc > 1) {
        unsigned long n = strtoul(argv[1], NULL, 10);
        if (n > 0 && n <= 1000000) iterations = (uint32_t)n;
    }
    scratch_anchor_to_exe();
    int log_alive ANO_LOG_SCOPE_ATTR = ano_log_init();
    (void)log_alive;
    scratch_make_dir("resources");
    scratch_make_dir(TEST_DIR);
    for (size_t i = 0; i < TEST_BYTES; i++) g_fixture[i] = (uint8_t)(i * 29 + 17);
    CHECK(write_file(TEST_DIR "/race.bin", g_fixture, sizeof g_fixture), "stage race fixture");
    CHECK(ano_res_init() == 0, "resource init");
    ano_res_lifetime engine = ano_res_lifetime_engine();

    test_publication_race(engine, iterations);
    test_nonmoving_growth(engine);
    test_accounting_and_rules(engine);
    test_wrap_refusal(engine);

    remove(TEST_DIR "/race.bin");
    scratch_remove_dir(TEST_DIR);
    CHECK(ano_res_shutdown() == 0, "zero-residue shutdown");
    ano_res_allocator_stats zero = ano_res_stats();
    CHECK(zero.live_bytes == 0 && zero.live_blocks == 0 && zero.retired_pending == 0,
          "shutdown accounting is zero");

    printf("publication: complete=%llu stale=%llu iterations=%u\n",
           (unsigned long long)atomic_load(&g_good),
           (unsigned long long)atomic_load(&g_stale), iterations);
    if (failures == 0) { printf("anotest_resownership: all checks passed\n"); return 0; }
    printf("anotest_resownership: %d check(s) failed\n", failures);
    return 1;
}
