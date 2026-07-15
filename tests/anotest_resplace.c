/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* The routing oracle (M5 scope; M19 extends it across the five contest models). Drives the
 * placement seam directly -- no registry -- against the real allocators. Asserts: model
 * resolution and honest refusal of unknown names; the M2 extension registry (sorted dense
 * ids, classify walk, reset/rebuild); M3 telemetry interning and gauge math; the scoped-pool
 * routing table (arena, backing, root, serving, alignment, flags), the preserved M5 oversize
 * split, refusals for malformed and unroutable plans; live-byte gauges returning to zero;
 * wink topology under both scaffolds. Exit 0 == pass. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "resources_ext.h"
#include "resources_internal.h"
#include "resources_place.h"
#include "resources_tel.h"

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define MIB ((size_t)1 << 20)

static ano_res_lifetime lt(uint32_t owner, ano_res_lifetime_kind kind)
{
    return (ano_res_lifetime){ .owner = owner, .generation = 1, .kind = kind };
}

static res_place_plan pl(uint32_t owner, res_role role, res_operation op,
                         res_destination dest, size_t align)
{
    return (res_place_plan){
        .tag = ANO_FOURCC('T','S','T','0'),
        .lifetime = lt(owner, owner == 1 ? ANO_RES_LIFETIME_ENGINE
                                         : ANO_RES_LIFETIME_WORLD_LEVEL),
        .role = role, .operation = op, .destination = dest,
        .provenance = RES_PROVENANCE_NAMESPACE, .alignment = align,
    };
}

// Fake extensions for the M2 section. Registration order deliberately NOT fourcc order.
static uint32_t cls_a(const char *l, size_t len)
{
    (void)len;
    const char *dot = strrchr(l, '.');
    return dot != NULL && strcmp(dot, ".aaa") == 0 ? ANO_FOURCC('A','A','A','A') : 0;
}
static uint32_t cls_b(const char *l, size_t len)
{
    (void)len;
    const char *dot = strrchr(l, '.');
    if (dot != NULL && strcmp(dot, ".bbb") == 0) return ANO_FOURCC('Z','Z','Z','Z');
    if (dot != NULL && strcmp(dot, ".aaa") == 0) return ANO_FOURCC('M','M','M','M');
    return 0;
}
static const res_ext_kind KA[] = {
    { .tag = ANO_FOURCC('Z','Z','Z','Z'), .name = "a.z", .derived = true,  .bakeable = false },
    { .tag = ANO_FOURCC('A','A','A','A'), .name = "a.a", .derived = false, .bakeable = true  },
};
static const res_ext_kind KB[] = {
    { .tag = ANO_FOURCC('M','M','M','M'), .name = "b.m", .derived = false, .bakeable = false },
};
static const res_ext EXT_A = { .name = "exta", .kinds = KA, .kind_count = 2, .classify = cls_a };
static const res_ext EXT_B = { .name = "extb", .kinds = KB, .kind_count = 1, .classify = cls_b };
static const res_ext_kind KDUP[] = {
    { .tag = ANO_FOURCC('D','U','P','1'), .name = "d1" },
    { .tag = ANO_FOURCC('D','U','P','1'), .name = "d2" },
};
static const res_ext EXT_DUP = { .name = "dup", .kinds = KDUP, .kind_count = 2 };
static const res_ext_kind KRSV[] = { { .tag = RES_TAG_BYTES, .name = "r" } };
static const res_ext EXT_RSV = { .name = "rsv", .kinds = KRSV, .kind_count = 1 };

static void test_ext(void)
{
    res_ext_reset();
    CHECK(res_ext_register(NULL) == -1, "null ext refused");
    CHECK(res_ext_register(&EXT_DUP) == -1, "intra-list duplicate tag refused");
    CHECK(res_ext_register(&EXT_RSV) == -1, "reserved BYTS tag refused");
    CHECK(res_ext_register(&EXT_A) == 0, "ext A registers");
    CHECK(res_ext_register(&EXT_B) == 0, "ext B registers");
    res_ext_freeze();
    CHECK(res_ext_register(&EXT_B) == -1, "post-freeze register refused");

    // Sorted dense ids: a pure function of the tag set (D17). AAAA < MMMM < ZZZZ -> 1, 2, 3.
    uint16_t ka = res_kind_of(ANO_FOURCC('A','A','A','A'));
    uint16_t km = res_kind_of(ANO_FOURCC('M','M','M','M'));
    uint16_t kz = res_kind_of(ANO_FOURCC('Z','Z','Z','Z'));
    CHECK(ka == 1 && km == 2 && kz == 3, "dense ids follow fourcc order, not call order");
    CHECK(res_kind_of(RES_TAG_BYTES) == 0, "BYTS is dense id 0");
    CHECK(res_kind_of(ANO_FOURCC('N','O','P','E')) == 0, "unknown tag -> 0");
    CHECK(res_kind_of(ANO_FOURCC('D','U','P','1')) == 0, "refused registration left nothing");
    CHECK(res_tag_of(0) == RES_TAG_BYTES && res_tag_of(4) == RES_TAG_BYTES,
          "tag_of 0 and out-of-range -> BYTS");
    CHECK(res_tag_of(1) == ANO_FOURCC('A','A','A','A'), "tag_of round-trips");
    CHECK(res_kind_derived(kz) && !res_kind_derived(ka), "derived flags");
    CHECK(res_kind_bakeable(ka) && !res_kind_bakeable(km), "bakeable flags");
    CHECK(!res_kind_derived(0) && !res_kind_bakeable(0), "id 0 has no flags");
    CHECK(res_ext_of_tag(ANO_FOURCC('M','M','M','M')) == &EXT_B, "owner of MMMM is ext B");
    CHECK(res_ext_of_tag(RES_TAG_BYTES) == NULL, "BYTS has no extension");

    // Classify walk: registration order, first claim wins; BYTS when nobody claims.
    CHECK(res_tag_from_path("x/y.aaa", 7) == ANO_FOURCC('A','A','A','A'), "first claim wins");
    CHECK(res_tag_from_path("x/y.bbb", 7) == ANO_FOURCC('Z','Z','Z','Z'), "ext B claims .bbb");
    CHECK(res_tag_from_path("x/y.ccc", 7) == RES_TAG_BYTES, "unclaimed -> BYTS");
    CHECK(res_tag_from_path(NULL, 0) == RES_TAG_BYTES, "NULL -> BYTS");

    // Reset then rebuild: ids are again a pure function of the (new) tag set.
    res_ext_reset();
    CHECK(res_kind_of(ANO_FOURCC('A','A','A','A')) == 0, "reset cleared the table");
    CHECK(res_ext_register(&EXT_B) == 0, "re-register after reset");
    res_ext_freeze();
    CHECK(res_kind_of(ANO_FOURCC('M','M','M','M')) == 1, "sole kind is dense id 1");
    res_ext_reset();
}

static void test_tel(void)
{
    CHECK(res_tel_init() == 0, "tel init");
    res_place_plan p = pl(1, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    uint16_t c1 = res_tel_intern(&p);
    CHECK(c1 != 0, "plan interns to a non-overflow cell");
    CHECK(res_tel_intern(&p) == c1, "same plan -> same cell");
    res_place_plan q = pl(1, RES_ROLE_NAME, RES_OP_BIND, RES_DEST_METADATA, 16);
    uint16_t c2 = res_tel_intern(&q);
    CHECK(c2 != 0 && c2 != c1, "distinct key -> distinct cell");

    res_tel_alloc(c1, 100, 128);
    res_tel_alloc(c1, 50, 64);
    res_tel_free(c1, 50, 64);
    res_tel_cell cells[RES_TEL_CELLS];
    size_t n = res_tel_snapshot(cells, RES_TEL_CELLS);
    CHECK(n >= 3, "snapshot sees cell 0 plus both interned cells");
    const res_tel_cell *c = NULL;
    for (size_t i = 0; i < n; i++)
        if (cells[i].key != 0 && cells[i].allocs == 2) c = &cells[i];
    CHECK(c != NULL, "charged cell present in the snapshot");
    if (c != NULL) {
        CHECK(c->requested_bytes == 150 && c->serving_bytes == 192, "cumulative charges");
        CHECK(c->live_bytes == 128 && c->live_blocks == 1, "live gauges after one free");
        CHECK(c->peak_bytes == 192, "peak holds the high-water mark");
        CHECK(c->frees == 1, "free counted");
    }
    CHECK(res_tel_overflow_hits() == 0, "no overflow in this run");

    // Axis outside field width lands in overflow bucket, counted. Masked would charge someone else's cell.
    res_place_plan hostile = pl(1, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    hostile.lifetime.kind = (ano_res_lifetime_kind)9;
    CHECK(res_tel_intern(&hostile) == 0, "out-of-range lifetime kind lands in cell 0");
    hostile = pl(1, RES_ROLE_COUNT, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_tel_intern(&hostile) == 0, "out-of-range role lands in cell 0");
    hostile = pl(1, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_COUNT, 64);
    CHECK(res_tel_intern(&hostile) == 0, "out-of-range destination lands in cell 0");
    CHECK(res_tel_overflow_hits() == 3, "every unattributable plan is counted");
    res_tel_shutdown();
}

static void test_place_scoped(void)
{
    res_site s;
    res_place_plan p = pl(1, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_place_route(&p, 100, &s) != 0, "route before init refuses");
    CHECK(res_place_init("bogus-model") == -1, "unknown model refused, loudly");
    CHECK(res_place_init(NULL) == 0, "NULL name takes the default");
    CHECK(strcmp(res_place_name(), "scoped-pool") == 0, "default is scoped-pool");
    res_place_shutdown();
    CHECK(res_place_init("global") == 0 && strcmp(res_place_name(), "global-pool") == 0,
          "short alias resolves to the truthful name");
    res_place_shutdown();
    CHECK(res_place_init("scoped-pool") == 0, "scoped-pool init");
    CHECK(res_tel_init() == 0, "tel up for route-time interning");

    CHECK(res_place_domain_open(lt(0, ANO_RES_LIFETIME_ENGINE)) == -1, "owner 0 refused");
    CHECK(res_place_domain_open(lt(64, ANO_RES_LIFETIME_WORLD_LEVEL)) == -1, "owner 64 refused");
    CHECK(res_place_domain_open(lt(1, ANO_RES_LIFETIME_ENGINE)) == 0, "engine root opens");
    CHECK(res_place_domain_open(lt(1, ANO_RES_LIFETIME_ENGINE)) == -1, "double open refused");
    CHECK(res_place_domain_open(lt(2, ANO_RES_LIFETIME_WORLD_LEVEL)) == 0, "world root opens");

    // The routing table. Small blocks pool; serving is the pow2 class; alignment is the
    // class stride (D15's silent under-delivery preserved at M5, reported truthfully).
    p = pl(1, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_place_route(&p, 100, &s) == 0, "payload load routes");
    CHECK(s.arena == RES_ARENA_SMALL && s.backing == RES_BACK_MULTIPOOL, "small -> pool");
    CHECK(s.root == 1, "root keyed by owner");
    CHECK(s.serving == 128 && s.alignment == 128, "pow2 class serving and stride");
    CHECK(s.flags == 0, "no site flags at M5 (nothing winkable yet)");
    CHECK(s.cell != 0, "telemetry cell interned at route time");
    res_site s2;
    CHECK(res_place_route(&p, 100, &s2) == 0 && s2.cell == s.cell, "cell stable per plan");
    CHECK(res_place_route(&p, 8, &s2) == 0 && s2.serving == 16 && s2.alignment == 16,
          "tiny block takes the 16-byte class");
    CHECK(res_place_route(&p, MIB, &s2) == 0 && s2.arena == RES_ARENA_SMALL
          && s2.serving == MIB, "exactly 1 MiB still pools");

    // The preserved oversize split: adoptable LOAD/ADOPT payloads -> TRANSFER on the
    // calling thread's default heap (the M5 bug, dies at M6); everything else -> BULK.
    CHECK(res_place_route(&p, MIB + 1, &s2) == 0 && s2.arena == RES_ARENA_TRANSFER
          && s2.backing == RES_BACK_HEAP && s2.allocator == NULL
          && s2.flags == RES_SITE_TRANSFERABLE && s2.serving == MIB + 1,
          "oversize adoptable -> TRANSFER, default heap, transferable");
    res_place_plan q = pl(1, RES_ROLE_PAYLOAD, RES_OP_SAVE_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_place_route(&q, MIB + 1, &s2) == 0 && s2.arena == RES_ARENA_BULK
          && s2.backing == RES_BACK_HEAP && s2.allocator != NULL && s2.flags == 0,
          "oversize non-adoptable -> BULK on the domain heap");

    // Refusals: malformed plans, unopened owners.
    q = pl(1, (res_role)0, RES_OP_BIND, RES_DEST_METADATA, 16);
    CHECK(res_place_route(&q, 100, &s2) != 0, "role 0 refuses (NONE-backed METADATA row)");
    q = pl(1, RES_ROLE_COUNT, RES_OP_BIND, RES_DEST_METADATA, 16);
    CHECK(res_place_route(&q, 100, &s2) != 0, "role out of range refuses");
    q = pl(1, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_COUNT, 16);
    CHECK(res_place_route(&q, 100, &s2) != 0, "dest out of range refuses");
    q = pl(3, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_place_route(&q, 100, &s2) != 0, "unopened owner refuses");
    CHECK(res_place_route(NULL, 100, &s2) != 0 && res_place_route(&p, 0, &s2) != 0,
          "NULL plan and zero size refuse");

    // Alloc / free against the real arenas; gauges track serving and return to zero.
    CHECK(res_place_route(&p, 100, &s) == 0, "route for alloc");
    void *blk = res_place_alloc(&s, 100);
    CHECK(blk != NULL, "pooled alloc lands");
    CHECK(res_place_domain_live_bytes(lt(1, ANO_RES_LIFETIME_ENGINE)) == 128,
          "root gauge tracks serving");
    res_place_free(&s, blk, 100, RES_FREE_RETAIL);
    CHECK(res_place_domain_live_bytes(lt(1, ANO_RES_LIFETIME_ENGINE)) == 0, "gauge to zero");

    CHECK(res_place_route(&p, MIB + 1, &s) == 0, "transfer route");
    void *tb = res_place_alloc(&s, MIB + 1);
    CHECK(tb != NULL, "TRANSFER alloc on the default heap");
    res_place_free(&s, tb, MIB + 1, RES_FREE_RETAIL);
    q = pl(1, RES_ROLE_PAYLOAD, RES_OP_SAVE_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_place_route(&q, MIB + 1, &s) == 0, "bulk route");
    void *bb = res_place_alloc(&s, MIB + 1);
    CHECK(bb != NULL, "BULK alloc on the domain heap");
    res_place_free(&s, bb, MIB + 1, RES_FREE_RETAIL);
    CHECK(res_place_domain_live_bytes(lt(1, ANO_RES_LIFETIME_ENGINE)) == 0,
          "gauges zero after heap blocks too");

    // Arena stats pass through the pool; heap arenas stay invisible until D19 (M6).
    CHECK(res_place_route(&p, 100, &s) == 0 && res_place_alloc(&s, 100) != NULL,
          "pool warm for stats");
    ano_mem_stats ms = res_place_arena_stats(1, RES_ARENA_SMALL);
    CHECK(ms.chunk_count >= 1, "pool arena stats visible");
    ms = res_place_arena_stats(1, RES_ARENA_BULK);
    CHECK(ms.chunk_bytes == 0 && ms.chunk_count == 0, "heap arena stats zero until D19");
    ms = res_place_arena_stats(63, RES_ARENA_SMALL);
    CHECK(ms.chunk_bytes == 0, "dead root stats are zero");

    // Wink: the winked root refuses routes; shutdown reclaims the rest.
    res_place_domain_wink(lt(2, ANO_RES_LIFETIME_WORLD_LEVEL));
    q = pl(2, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_place_route(&q, 100, &s2) != 0, "winked root refuses routes");
    res_place_shutdown();
    res_tel_shutdown();
    CHECK(strcmp(res_place_name(), "none") == 0, "name after shutdown is none");
}

static void test_place_global(void)
{
    CHECK(res_place_init("global-pool") == 0, "global-pool init");
    CHECK(res_tel_init() == 0, "tel up");
    res_site s;
    CHECK(res_place_domain_open(lt(5, ANO_RES_LIFETIME_WORLD_LEVEL)) == 0,
          "first open builds the process root");
    CHECK(res_place_domain_open(lt(6, ANO_RES_LIFETIME_STREAMING)) == 0,
          "later opens are only stats keys");
    res_place_plan p = pl(5, RES_ROLE_PAYLOAD, RES_OP_LOAD, RES_DEST_VARIABLE_PAYLOAD, 64);
    CHECK(res_place_route(&p, 100, &s) == 0 && s.root == 0, "every owner keys root 0");
    res_place_domain_wink(lt(5, ANO_RES_LIFETIME_WORLD_LEVEL));
    CHECK(res_place_route(&p, 100, &s) == 0, "non-engine wink is a no-op under SINGLE");
    res_place_domain_wink(lt(1, ANO_RES_LIFETIME_ENGINE));
    CHECK(res_place_route(&p, 100, &s) != 0, "engine wink kills the process root");
    res_place_shutdown();
    res_tel_shutdown();

    CHECK(res_model_by_name("model-a") == NULL, "no contest model exists before M19");
    CHECK(res_model_default() == res_model_by_name("scoped"), "default is scoped-pool");
}

int main(void)
{
    printf("resource placement: the routing oracle\n");
    test_ext();
    test_tel();
    test_place_scoped();
    test_place_global();
    if (failures != 0) {
        printf("FAILED: %d assertion(s)\n", failures);
        return 1;
    }
    printf("all placement routing assertions passed\n");
    return 0;
}
