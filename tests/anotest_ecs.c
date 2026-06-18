/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Unit coverage for anoptic_ecs.h: registration, add/get/has, dense iteration,
 * swap-and-pop integrity after destroy, deferred structural flush, and
 * generational handle invalidation + slot recycling. Exit 0 == pass. */

#include <stdio.h>
#include <mimalloc.h>
#include "anoptic_ecs.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

typedef struct { uint32_t tag; float x, y, z; } Position; // tag = owning entity index, for swap checks
typedef struct { float dx, dy; }                 Velocity;

#define COMP_POSITION 0
#define COMP_VELOCITY 1

int main(void)
{
    mi_heap_t *heap = mi_heap_new();
    CHECK(heap != NULL, "heap creation");

    EcsWorld *w = ano_ecs_world_create(heap, 16);
    CHECK(w != NULL, "world creation");
    if (!w) { mi_heap_destroy(heap); return 1; }

    CHECK(ano_ecs_register_component(w, COMP_POSITION, sizeof(Position)), "register Position");
    CHECK(ano_ecs_register_component(w, COMP_VELOCITY, sizeof(Velocity)), "register Velocity");
    CHECK(!ano_ecs_register_component(w, COMP_POSITION, sizeof(Position)), "double-register rejected");

    // --- create three entities with Position, one also with Velocity ---
    EcsEntityId e1 = ano_ecs_entity_create(w);
    EcsEntityId e2 = ano_ecs_entity_create(w);
    EcsEntityId e3 = ano_ecs_entity_create(w);
    CHECK(ano_ecs_entity_alive(w, e1) && ano_ecs_entity_alive(w, e2) && ano_ecs_entity_alive(w, e3),
          "entities alive");

    for (int i = 0; i < 3; i++) {
        EcsEntityId e = (i == 0) ? e1 : (i == 1) ? e2 : e3;
        Position p = { .tag = e.index, .x = (float)e.index, .y = 0.0f, .z = 0.0f };
        Position *stored = ano_ecs_add_init(w, e, COMP_POSITION, &p);
        CHECK(stored != NULL && stored->tag == e.index, "add_init Position");
    }

    Velocity v = { .dx = 1.5f, .dy = -2.0f };
    CHECK(!ano_ecs_has(w, e1, COMP_VELOCITY), "no Velocity before add");
    CHECK(ano_ecs_add_init(w, e1, COMP_VELOCITY, &v) != NULL, "add Velocity to e1");
    CHECK(ano_ecs_has(w, e1, COMP_VELOCITY), "has Velocity after add");
    CHECK(ano_ecs_add(w, e1, COMP_VELOCITY) == NULL, "duplicate add rejected");

    // --- get returns the right payload ---
    Position *p2 = ano_ecs_get(w, e2, COMP_POSITION);
    CHECK(p2 != NULL && p2->tag == e2.index, "get e2 Position");

    // --- dense iteration sees all three ---
    EcsColumn col = ano_ecs_column(w, COMP_POSITION);
    CHECK(col.count == 3, "Position column count == 3");
    uint32_t seen = 0;
    for (uint32_t i = 0; i < col.count; i++) {
        Position *pp = (Position *)((uint8_t *)col.data + (size_t)i * col.stride);
        CHECK(pp->tag == col.owners[i].index, "column owner matches payload tag");
        seen |= (1u << pp->tag);
    }
    CHECK(seen == ((1u << e1.index) | (1u << e2.index) | (1u << e3.index)), "iterated all owners");

    // --- deferred remove: not applied until flush ---
    ano_ecs_remove(w, e1, COMP_VELOCITY);
    CHECK(ano_ecs_has(w, e1, COMP_VELOCITY), "Velocity still present pre-flush");
    ano_ecs_flush_structural(w);
    CHECK(!ano_ecs_has(w, e1, COMP_VELOCITY), "Velocity gone post-flush");
    CHECK(ano_ecs_get(w, e1, COMP_POSITION) != NULL, "e1 keeps Position after Velocity removal");

    // --- deferred destroy + swap-and-pop integrity ---
    ano_ecs_entity_destroy(w, e2);
    CHECK(ano_ecs_entity_alive(w, e2), "e2 alive pre-flush (deferred)");
    ano_ecs_flush_structural(w);
    CHECK(!ano_ecs_entity_alive(w, e2), "e2 dead post-flush");
    CHECK(ano_ecs_get(w, e2, COMP_POSITION) == NULL, "stale e2 get == NULL");

    col = ano_ecs_column(w, COMP_POSITION);
    CHECK(col.count == 2, "Position column count == 2 after destroy");
    // e1 and e3 data must survive the swap intact, addressable by handle
    Position *p1 = ano_ecs_get(w, e1, COMP_POSITION);
    Position *p3 = ano_ecs_get(w, e3, COMP_POSITION);
    CHECK(p1 != NULL && p1->tag == e1.index, "e1 Position intact after swap");
    CHECK(p3 != NULL && p3->tag == e3.index, "e3 Position intact after swap");

    // --- slot recycling bumps generation; old handle stays invalid ---
    EcsEntityId e4 = ano_ecs_entity_create(w);
    CHECK(e4.index == e2.index, "e4 recycles e2's slot index");
    CHECK(e4.generation != e2.generation, "recycled slot bumped generation");
    CHECK(ano_ecs_entity_alive(w, e4), "e4 alive");
    CHECK(!ano_ecs_entity_alive(w, e2), "old e2 handle still invalid after recycle");

    ano_ecs_world_destroy(w);
    mi_heap_destroy(heap);

    if (failures == 0) { printf("anotest_ecs: all checks passed\n"); return 0; }
    printf("anotest_ecs: %d check(s) failed\n", failures);
    return 1;
}
