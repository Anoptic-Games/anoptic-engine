/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Persistence/settings coverage.
 * Strict config and keybinding defaults. Exact round trips. Corruption quarantine. V1 migration.
 * Complete save fallback past 64 corrupt generations. Orphan recovery past eight temps.
 * Min-reader/world-save migration. Concurrent save sequencing.
 * Concurrency oracle: different slots enter exact-slot lanes together. Same-slot ops never overlap. Gen count equals accepted commits.
 * File fixtures use resource logical names and durable writes. Exit 0 == pass. */

#include <anoptic_config.h>
#include <anoptic_keybindings.h>
#include <anoptic_log.h>
#include <anoptic_res_world.h>
#include <anoptic_resources.h>
#include <anoptic_threads.h>
#include <anoptic_time.h>

#include "resources/resources_internal.h"
#include "templates/scratch.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static ano_res_lifetime g_persist;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL (%d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)

static uint64_t fnv64(const uint8_t *p, size_t n)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= UINT64_C(0x100000001b3); }
    return h;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void putf(uint8_t *p, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    put32(p, bits);
}

static size_t make_frame(uint8_t *out, uint32_t format, uint32_t min_reader,
                         uint64_t seq, const void *payload, size_t payload_len)
{
    memcpy(out, "ANOS", 4);
    out[4] = 1; out[5] = 0; out[6] = 1; out[7] = 0;
    put32(out + 8, format);
    put32(out + 12, min_reader);
    put64(out + 16, payload_len);
    put64(out + 24, seq);
    put64(out + 32, fnv64(out, 32));
    put64(out + 40, 0);
    memcpy(out + 48, payload, payload_len);
    put64(out + 48 + payload_len, fnv64(payload, payload_len));
    memcpy(out + 56 + payload_len, "ANOSDONE", 8);
    return 64 + payload_len;
}

static void remove_logical(const char *logical)
{
    ano_fspath p = ano_res_resolve_write(logical);
    if (p.length != 0)
        (void)remove(p.str);
}

static void clean_slot(const char *slot, uint64_t max_seq)
{
    for (uint64_t seq = 1; seq <= max_seq; seq++)
        (void)ano_res_save_delete(slot, seq);
}

static bool config_equal(const ano_config *a, const ano_config *b)
{
    return memcmp(&a->camera_move_speed, &b->camera_move_speed, sizeof(float)) == 0
        && memcmp(&a->camera_look_sensitivity, &b->camera_look_sensitivity, sizeof(float)) == 0
        && a->menu_at_start == b->menu_at_start;
}

static void test_config(void)
{
    remove_logical(ANO_CONFIG_PATH);
    remove_logical(ANO_CONFIG_PATH ".broken");
    ano_config config;
    CHECK(ano_config_load(g_persist, &config) == ANO_CONFIG_DEFAULTED,
          "missing config regenerates defaults");
    ano_config expected;
    ano_config_defaults(&expected);
    CHECK(config_equal(&config, &expected), "default config values");

    config.camera_move_speed = 7.25f;
    config.camera_look_sensitivity = 0.001234567f;
    config.menu_at_start = true;
    CHECK(ano_config_save(&config) == 0, "config save");
    ano_config round = {0};
    CHECK(ano_config_load(g_persist, &round) == ANO_CONFIG_LOADED, "config reload");
    CHECK(config_equal(&config, &round), "config exact round trip");

    static const char v1[] =
        "{\"schema\":\"anoptic.settings\",\"version\":1,\"move_speed\":3.75,"
        "\"look_sensitivity\":0.0025,\"menu_at_start\":false}\n";
    CHECK(ano_res_write(ANO_CONFIG_PATH, v1, sizeof v1 - 1) == 0, "write config v1");
    CHECK(ano_config_load(g_persist, &round) == ANO_CONFIG_MIGRATED, "config v1 migrates");
    CHECK(round.camera_move_speed == 3.75f && round.camera_look_sensitivity == 0.0025f
          && !round.menu_at_start, "config migration values");
    CHECK(ano_config_load(g_persist, &round) == ANO_CONFIG_LOADED,
          "migrated config persisted as current schema");

    static const char broken[] = "{\"schema\":\"anoptic.settings\",\"version\":2,\"camera\":";
    CHECK(ano_res_write(ANO_CONFIG_PATH, broken, sizeof broken - 1) == 0, "write corrupt config");
    CHECK(ano_config_load(g_persist, &round) == ANO_CONFIG_DEFAULTED,
          "corrupt config quarantines and defaults");
    CHECK(ano_res_exists(ANO_CONFIG_PATH ".broken"), "corrupt config evidence preserved");
}

static ano_keybinding *binding(ano_keybindings *bindings, anostr_sid action)
{
    for (uint32_t i = 0; i < bindings->count; i++)
        if (bindings->entries[i].action == action)
            return &bindings->entries[i];
    return NULL;
}

static void test_keybindings(void)
{
    remove_logical(ANO_KEYBINDINGS_PATH);
    remove_logical(ANO_KEYBINDINGS_PATH ".broken");
    ano_keybindings keys = {0};
    CHECK(ano_keybindings_load(g_persist, &keys) == ANO_KEYBINDINGS_DEFAULTED,
          "missing keybindings regenerate defaults");
    CHECK(ano_keybindings_action(&keys, ANO_KEY_W, 0) == ANO_ACTION_MOVE_FORWARD,
          "default movement binding");
    binding(&keys, ANO_ACTION_MOVE_FORWARD)->key = 90;
    CHECK(ano_keybindings_save(&keys) == 0, "keybindings save");
    ano_keybindings round = {0};
    CHECK(ano_keybindings_load(g_persist, &round) == ANO_KEYBINDINGS_LOADED,
          "keybindings reload");
    CHECK(memcmp(&keys, &round, sizeof keys) == 0, "keybindings exact round trip");
    CHECK(ano_keybindings_action(&round, 90, 0) == ANO_ACTION_MOVE_FORWARD,
          "rebound action survives reload");

    static const char v1[] =
        "{\"schema\":\"anoptic.keybindings\",\"version\":1,\"bindings\":{"
        "\"move.forward\":87,\"move.backward\":83,\"move.left\":65,\"move.right\":68,"
        "\"move.up\":32,\"move.down\":341,\"menu.toggle\":77,"
        "\"render.lighting_cycle\":76,\"render.lod_finer\":91,\"render.lod_coarser\":93,"
        "\"render.shadow_lod_finer\":59,\"render.shadow_lod_coarser\":39,"
        "\"render.hiz_toggle\":72}}\n";
    CHECK(ano_res_write(ANO_KEYBINDINGS_PATH, v1, sizeof v1 - 1) == 0,
          "write keybindings v1");
    CHECK(ano_keybindings_load(g_persist, &round) == ANO_KEYBINDINGS_MIGRATED,
          "keybindings v1 migrates");
    CHECK(ano_keybindings_action(&round, ANO_KEY_H, 0) == ANO_ACTION_HIZ_TOGGLE,
          "keybinding migration values");
    CHECK(ano_keybindings_load(g_persist, &round) == ANO_KEYBINDINGS_LOADED,
          "migrated keybindings persisted as current schema");

    static const char broken[] =
        "{\"schema\":\"anoptic.keybindings\",\"version\":2,\"bindings\":{}}\n";
    CHECK(ano_res_write(ANO_KEYBINDINGS_PATH, broken, sizeof broken - 1) == 0,
          "write corrupt keybindings");
    CHECK(ano_keybindings_load(g_persist, &round) == ANO_KEYBINDINGS_DEFAULTED,
          "corrupt keybindings quarantine and default");
    CHECK(ano_res_exists(ANO_KEYBINDINGS_PATH ".broken"),
          "corrupt keybinding evidence preserved");
}

static void test_deep_fallback(void)
{
    const char *slot = "persist_deep";
    clean_slot(slot, 100);
    CHECK(ano_res_save_commit_ex(slot, 1, 1, "old-valid", 9) == 0, "commit old valid");
    for (uint64_t seq = 2; seq <= 70; seq++) {
        char logical[MAXPATH];
        snprintf(logical, sizeof logical, "saves/%s.%llu.anosave", slot,
                 (unsigned long long)seq);
        CHECK(ano_res_write(logical, "damaged", 7) == 0, "plant corrupt recent generation");
    }
    ano_res_save_result result = {0};
    CHECK(ano_res_save_load_ex(g_persist, slot, 1, &result) == ANO_RES_SAVE_OK,
          "older valid save survives more than 64 corrupt recent generations");
    CHECK(result.seq == 1, "deep fallback selected seq 1");
    ano_res_reader reader = { .lane = ANO_RES_READER_NONE };
    ano_res_read read = {0};
    CHECK(ano_res_reader_register(&reader) == 0 && ano_res_read_begin(&reader, &read) == 0,
          "deep fallback read scope");
    anostr_t payload = ano_res_bytes(&read, result.resource);
    CHECK(anostr_len(payload) == 9 && memcmp(anostr_bytes(&payload), "old-valid", 9) == 0,
          "deep fallback payload");
    ano_res_read_end(&read);
    CHECK(ano_res_reader_unregister(&reader) == 0, "deep fallback reader unregister");
    CHECK(ano_res_unload(g_persist, result.resource) == 0, "deep fallback unload");
    uint32_t generations = 0;
    CHECK(ano_res_save_stats(slot, &generations, NULL) == 0 && generations == 70,
          "corrupt generations are preserved");
    clean_slot(slot, 100);
}

static void test_many_temps(void)
{
    const char *slot = "persist_temps";
    clean_slot(slot, 120);
    for (int i = 0; i < 12; i++) {
        char logical[MAXPATH];
        snprintf(logical, sizeof logical, "saves/%s.%d.anosave.%08x.tmp", slot, i + 1, i);
        CHECK(ano_res_write(logical, "bad", 3) == 0, "plant orphan temp");
    }
    uint8_t frame[128];
    size_t frame_len = make_frame(frame, 1, 1, 99, "temp-valid", 10);
    char logical[MAXPATH];
    snprintf(logical, sizeof logical, "saves/%s.99.anosave.ffffffff.tmp", slot);
    CHECK(ano_res_write(logical, frame, frame_len) == 0, "plant valid temp after first eight");
    ano_res_save_result result = {0};
    CHECK(ano_res_save_load_ex(g_persist, slot, 1, &result) == ANO_RES_SAVE_OK,
          "valid temp beyond eight recovered");
    CHECK(result.seq == 99, "temp recovery sequence");
    CHECK(ano_res_unload(g_persist, result.resource) == 0, "temp recovery unload");
    uint32_t generations = 0;
    CHECK(ano_res_save_stats(slot, &generations, NULL) == 0 && generations == 1,
          "recovered temp became one canonical generation");
    clean_slot(slot, 120);
}

static void make_world_v1(uint8_t out[36], float x, float y, float z)
{
    memcpy(out, "ANOW", 4);
    put32(out + 4, 1);
    put64(out + 8, 55);
    put64(out + 16, 77);
    putf(out + 24, x); putf(out + 28, y); putf(out + 32, z);
}

static void test_world_migration(void)
{
    anoresworld_state state = {
        .simulation_tick = 123, .world_seed = 456,
        .camera_position = { 1.0f, 2.0f, 3.0f },
        .camera_yaw = 0.75f, .camera_pitch = -0.25f, .flags = 9,
    };
    clean_slot("persist_world", 10);
    CHECK(ano_resworld_save_commit("persist_world", &state) == 0, "world save commit");
    anoresworld_state loaded = {0};
    anoresworld_save_info info = {0};
    CHECK(ano_resworld_save_load(g_persist, "persist_world", &loaded, &info) == ANO_RESWORLD_OK,
          "world save load");
    CHECK(memcmp(&state, &loaded, sizeof state) == 0, "world save exact round trip");

    clean_slot("persist_world_v1", 10);
    uint8_t old[36];
    make_world_v1(old, 4.0f, 5.0f, 6.0f);
    CHECK(ano_res_save_commit_ex("persist_world_v1", 1, 1, old, sizeof old) == 0,
          "world v1 commit");
    CHECK(ano_resworld_save_load(g_persist, "persist_world_v1", &loaded, &info)
          == ANO_RESWORLD_MIGRATED, "world v1 migration");
    uint32_t generations = 0;
    CHECK(ano_res_save_stats("persist_world_v1", &generations, NULL) == 0 && generations == 2,
          "world migration preserves source and adds generation");
    CHECK(loaded.camera_position[0] == 4.0f && loaded.camera_pitch == -0.211f,
          "world migration values");

    clean_slot("persist_world_bad", 10);
    CHECK(ano_res_save_commit_ex("persist_world_bad", 1, 1, "not-world", 9) == 0,
          "invalid old world frame commit");
    CHECK(ano_resworld_save_load(g_persist, "persist_world_bad", &loaded, &info)
          == ANO_RESWORLD_MIGRATION_FAILED, "migration failure exact status");
    CHECK(ano_res_save_stats("persist_world_bad", &generations, NULL) == 0 && generations == 1,
          "migration failure preserves source generation");

    clean_slot("persist_world_new", 10);
    CHECK(ano_res_save_commit_ex("persist_world_new", 3, 3, "future", 6) == 0,
          "future min-reader save commit");
    CHECK(ano_resworld_save_load(g_persist, "persist_world_new", &loaded, &info)
          == ANO_RESWORLD_READER_TOO_OLD, "min-reader refusal exact status");
    CHECK(info.source_version == 3 && info.source_min_reader_version == 3,
          "min-reader refusal reports source version");

    clean_slot("persist_world_unsupported", 10);
    CHECK(ano_res_save_commit_ex("persist_world_unsupported", 3, 2, "future", 6) == 0,
          "future compatible-reader frame commit");
    CHECK(ano_resworld_save_load(g_persist, "persist_world_unsupported", &loaded, &info)
          == ANO_RESWORLD_UNSUPPORTED_VERSION, "unsupported format exact status");

    clean_slot("persist_world", 10);
    clean_slot("persist_world_v1", 10);
    clean_slot("persist_world_bad", 10);
    clean_slot("persist_world_new", 10);
    clean_slot("persist_world_unsupported", 10);
}

static anothread_barrier_t g_lane_barrier;
static atomic_int g_hook_mode;
static atomic_int g_active;
static atomic_int g_max_active;

static void save_lane_hook(const char *slot)
{
    (void)slot;
    int active = atomic_fetch_add(&g_active, 1) + 1;
    int max = atomic_load(&g_max_active);
    while (active > max && !atomic_compare_exchange_weak(&g_max_active, &max, active)) {}
    if (atomic_load(&g_hook_mode) == 1)
        (void)ano_thread_barrier_wait(&g_lane_barrier);
    else
        ano_sleep(20000);
    atomic_fetch_sub(&g_active, 1);
}

typedef struct commit_arg {
    const char *slot;
    int rc;
} commit_arg;

static void *commit_thread(void *ctx)
{
    commit_arg *arg = ctx;
    arg->rc = ano_res_save_commit_ex(arg->slot, 1, 1, arg->slot, strlen(arg->slot));
    return NULL;
}

static void run_commit_pair(const char *a, const char *b, int mode, int expected_max)
{
    clean_slot(a, 10);
    if (strcmp(a, b) != 0) clean_slot(b, 10);
    atomic_store(&g_hook_mode, mode);
    atomic_store(&g_active, 0);
    atomic_store(&g_max_active, 0);
    if (mode == 1)
        CHECK(ano_thread_barrier_init(&g_lane_barrier, NULL, 2) == 0, "save hook barrier init");
    res_test_save_enter_hook = save_lane_hook;
    commit_arg args[2] = { { .slot = a, .rc = -1 }, { .slot = b, .rc = -1 } };
    anothread_t threads[2];
    CHECK(ano_thread_create(&threads[0], NULL, commit_thread, &args[0]) == 0,
          "save thread 0 create");
    CHECK(ano_thread_create(&threads[1], NULL, commit_thread, &args[1]) == 0,
          "save thread 1 create");
    ano_thread_join(threads[0], NULL);
    ano_thread_join(threads[1], NULL);
    res_test_save_enter_hook = NULL;
    if (mode == 1)
        (void)ano_thread_barrier_destroy(&g_lane_barrier);
    CHECK(args[0].rc == 0 && args[1].rc == 0, "concurrent commits succeed");
    CHECK(atomic_load(&g_max_active) == expected_max, "save lane overlap oracle");
    uint32_t count = 0;
    CHECK(ano_res_save_stats(a, &count, NULL) == 0
          && count == (strcmp(a, b) == 0 ? 2u : 1u), "slot A generation conservation");
    if (strcmp(a, b) != 0)
        CHECK(ano_res_save_stats(b, &count, NULL) == 0 && count == 1,
              "slot B generation conservation");
    clean_slot(a, 10);
    if (strcmp(a, b) != 0) clean_slot(b, 10);
}

static void test_slot_concurrency(void)
{
    run_commit_pair("persist_parallel_a", "persist_parallel_b", 1, 2);
    run_commit_pair("persist_ordered", "persist_ordered", 2, 1);
}

int main(void)
{
    scratch_anchor_to_exe();
    int log_scope ANO_LOG_SCOPE_ATTR = ano_log_init();
    CHECK(log_scope == 0, "log init");
    CHECK(ano_res_init() == 0, "resource init");
    CHECK(ano_res_domain_open(ANO_RES_LIFETIME_SAVE_CONFIG, &g_persist) == 0,
          "save/config lifetime open");

    test_config();
    test_keybindings();
    test_deep_fallback();
    test_many_temps();
    test_world_migration();
    test_slot_concurrency();

    remove_logical(ANO_CONFIG_PATH);
    remove_logical(ANO_CONFIG_PATH ".broken");
    remove_logical(ANO_KEYBINDINGS_PATH);
    remove_logical(ANO_KEYBINDINGS_PATH ".broken");
    CHECK(ano_res_domain_retire(g_persist) == 0, "save/config lifetime retire");
    CHECK(ano_res_shutdown() == 0, "resource shutdown");
    if (failures == 0)
        printf("PASS: persistence/settings\n");
    return failures == 0 ? 0 : 1;
}
