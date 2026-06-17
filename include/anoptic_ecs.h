/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_ecs.h
 * @brief Logic-side Entity Component System: generational entities, chunked
 *        sparse-set component storage, two-stage tick with deferred structural
 *        mutation.
 *
 * Design of record: docs/artifacts/ECS.md. This is the authoritative simulation
 * container; it knows nothing about the renderer. Renderable entities are
 * projected to the render world through docs/artifacts/VK_BACKEND_INTEROP.md and
 * anoptic_render_bridge.h, never through this header.
 *
 * Threading model (ECS.md S3): a single logic master thread drives the tick and
 * may fan component work out to worker threads. Read/get/has and single-store
 * iteration are safe to call concurrently from workers. Structural mutation
 * (entity destroy, component remove) is DEFERRED and applied single-threaded in
 * ano_ecs_flush_structural(); component add is append-only and must not race on
 * the same store (call it from a single thread per store, or defer it).
 */

#ifndef ANOPTIC_ECS_H
#define ANOPTIC_ECS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <mimalloc.h>   // mi_heap_t: stores live in a caller-provided scoped heap

// ---------------------------------------------------------------------------
// Entity handles
// ---------------------------------------------------------------------------

// Copy-by-value 64-bit handle. `index` addresses the entity registry; a handle
// validates only while `generation` matches the live slot generation, so stale
// handles (entity destroyed and slot recycled) fail validation deterministically.
typedef struct EcsEntityId
{
    uint32_t index;
    uint32_t generation;
} EcsEntityId;

// Reserved invalid handle. index == UINT32_MAX never names a live slot.
#define ANO_ECS_INVALID_ENTITY ((EcsEntityId){ .index = UINT32_MAX, .generation = 0u })

static inline bool ano_ecs_entity_equal(EcsEntityId a, EcsEntityId b)
{
    return a.index == b.index && a.generation == b.generation;
}

// ---------------------------------------------------------------------------
// Component identity & ownership mask
// ---------------------------------------------------------------------------

#define ANO_ECS_MAX_COMPONENTS 128
#define ANO_ECS_MASK_WORDS     (ANO_ECS_MAX_COMPONENTS / 64)

// Component type id, assigned by the caller at registration time (0 .. MAX-1).
typedef uint8_t EcsComponentId;

// Compile-time static bitset of owned components. O(1) presence test, flat layout,
// no heap. Test: mask.words[id >> 6] & (1ULL << (id & 63)).
typedef struct EcsComponentMask
{
    uint64_t words[ANO_ECS_MASK_WORDS];
} EcsComponentMask;

static inline bool ano_ecs_mask_test(const EcsComponentMask *m, EcsComponentId id)
{
    return (m->words[id >> 6] & (1ULL << (id & 63))) != 0ull;
}

// ---------------------------------------------------------------------------
// World (opaque)
// ---------------------------------------------------------------------------

// The world owns the entity registry, the per-component chunked sparse-set
// stores, and the deferred-structural queues. Internals are private; bulk
// iteration is exposed through EcsColumn below rather than by leaking the layout.
typedef struct EcsWorld EcsWorld;

// in:  heap                  scoped heap all world storage allocates from (chunks grow within it)
//      initial_entity_capacity  starting registry size; grows by chunks as needed
// out: EcsWorld*             new world, or NULL on allocation failure
// inv: the world must be destroyed before `heap` is released.
EcsWorld *ano_ecs_world_create(mi_heap_t *heap, uint32_t initial_entity_capacity);

// Releases all world storage. Does not release the backing heap.
void ano_ecs_world_destroy(EcsWorld *world);

// ---------------------------------------------------------------------------
// Component registration
// ---------------------------------------------------------------------------

// in:  world, id (< ANO_ECS_MAX_COMPONENTS, unused), stride (bytes/component, > 0)
// out: true on success; false if id is out of range or already registered
// inv: stride is fixed for the lifetime of the world; stores are POD.
bool ano_ecs_register_component(EcsWorld *world, EcsComponentId id, size_t stride);

// ---------------------------------------------------------------------------
// Entity lifecycle
// ---------------------------------------------------------------------------

// Allocates an entity (LIFO from the registry free-list, preserving slot
// generation). O(1). Returns ANO_ECS_INVALID_ENTITY on capacity-growth failure.
EcsEntityId ano_ecs_entity_create(EcsWorld *world);

// Queues an entity for destruction; applied in ano_ecs_flush_structural().
// Safe to call from a worker during the update stage. Destroying an entity bumps
// its slot generation (invalidating outstanding handles) and removes all its
// components via swap-and-pop.
void ano_ecs_entity_destroy(EcsWorld *world, EcsEntityId e);

// True iff `e` names a live slot whose generation matches. O(1).
bool ano_ecs_entity_alive(const EcsWorld *world, EcsEntityId e);

// ---------------------------------------------------------------------------
// Component mutation & access
// ---------------------------------------------------------------------------

// Appends a zeroed component of type `id` to entity `e` and returns a pointer to
// it. Append-only: never moves existing elements. O(1).
// inv: not safe to call concurrently for the SAME component id (races `count`);
//      single-thread per store, or route through a deferred add.
// out: pointer to the (zeroed) component payload, or NULL on failure / if `e`
//      already owns `id` / `e` is not alive.
void *ano_ecs_add(EcsWorld *world, EcsEntityId e, EcsComponentId id);

// As ano_ecs_add, but initializes the new component by copying `stride` bytes
// from `src`. `src` must point to at least the registered stride.
void *ano_ecs_add_init(EcsWorld *world, EcsEntityId e, EcsComponentId id, const void *src);

// Queues removal of component `id` from `e`; applied in ano_ecs_flush_structural()
// (removal swap-and-pops and moves another element, so it cannot run mid-iteration).
void ano_ecs_remove(EcsWorld *world, EcsEntityId e, EcsComponentId id);

// in:  world, e, id
// out: pointer to e's component payload of type id, or NULL if absent / e dead.
//      Double-indirection: mask test, sparse lookup, dense base + dense*stride.
// inv: pointer is valid until the next structural mutation of that store.
void *ano_ecs_get(const EcsWorld *world, EcsEntityId e, EcsComponentId id);

// True iff `e` is alive and owns component `id`. O(1).
bool ano_ecs_has(const EcsWorld *world, EcsEntityId e, EcsComponentId id);

// ---------------------------------------------------------------------------
// Bulk iteration
// ---------------------------------------------------------------------------

// A read/write view over one component's dense, gapless storage. The hot path for
// million-entity systems: iterate `data` as a flat array of `count` elements of
// `stride` bytes; `owners[i]` is the entity owning dense element i (use it to
// fetch co-components via ano_ecs_get / ano_ecs_has for multi-component joins).
typedef struct EcsColumn
{
    void              *data;    // dense payloads: count * stride bytes
    const EcsEntityId *owners;  // dense index -> owning entity, parallel to data
    uint32_t           count;   // active element count
    size_t             stride;  // bytes per element
} EcsColumn;

// in:  world, id
// out: dense view of the store. data/owners are NULL and count 0 if `id` is
//      unregistered or empty.
// inv: the view is invalidated by any structural mutation of that store; do not
//      add/remove on `id` while iterating its column.
EcsColumn ano_ecs_column(const EcsWorld *world, EcsComponentId id);

// ---------------------------------------------------------------------------
// Tick boundary
// ---------------------------------------------------------------------------

// Applies all queued destroys and component removals (ECS.md S3 stage 2, run by
// the logic master after the parallel update stage). Call once per tick, before
// the graphics-extract pass, so extract observes a settled layout.
// inv: must NOT run concurrently with any system pass.
void ano_ecs_flush_structural(EcsWorld *world);

#endif // ANOPTIC_ECS_H
