/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Logic-side ECS implementation. Public contract + threading rules: anoptic_ecs.h.
 * Design of record: docs/artifacts/ECS.md.
 *
 * Storage: generational entity slots + per-component chunked sparse sets. Each
 * store keeps a dense, gapless (data, owners) pair for flat iteration and a
 * chunked sparse map (entity.index -> dense index) so a component never pays for
 * a full entity-wide index array it barely uses. Removal is swap-and-pop;
 * structural mutation is deferred to ano_ecs_flush_structural(). */

#include "anoptic_ecs.h"

#include <string.h>

#define ECS_SPARSE_CHUNK 1024u            // entity indices per sparse chunk
#define ECS_NO_DENSE     0xFFFFFFFFu      // "entity not present in this store"

// ---------------------------------------------------------------------------
// Private layout
// ---------------------------------------------------------------------------

typedef struct EcsEntitySlot
{
    EcsComponentMask mask;        // components this entity owns
    uint32_t         generation;  // bumped on destroy; stale handles fail the gen check
    bool             alive;
} EcsEntitySlot;

typedef struct EcsStore
{
    bool         active;       // registered
    size_t       stride;       // bytes per component
    uint32_t     count;        // dense length (active instances)
    uint32_t     capacity;     // dense capacity
    uint8_t     *data;         // dense payloads, capacity*stride, gapless
    EcsEntityId *owners;       // dense index -> entity, parallel to data
    uint32_t   **sparse;       // chunk table: sparse[idx/CHUNK][idx%CHUNK] -> dense
    uint32_t     sparseChunks; // length of the chunk table
} EcsStore;

typedef struct EcsPendingRemove
{
    EcsEntityId    e;
    EcsComponentId comp;
} EcsPendingRemove;

struct EcsWorld
{
    mi_heap_t        *heap;

    EcsEntitySlot    *slots;
    uint32_t          slotCount;     // high-water of allocated indices
    uint32_t          slotCapacity;

    uint32_t         *freeList;      // recycled entity indices (LIFO)
    uint32_t          freeCount;
    uint32_t          freeCapacity;

    EcsStore          stores[ANO_ECS_MAX_COMPONENTS];

    EcsEntityId      *destroyQueue;
    uint32_t          destroyCount;
    uint32_t          destroyCapacity;

    EcsPendingRemove *removeQueue;
    uint32_t          removeCount;
    uint32_t          removeCapacity;
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static inline void mask_set(EcsComponentMask *m, EcsComponentId id)   { m->words[id >> 6] |=  (1ULL << (id & 63)); }
static inline void mask_clear(EcsComponentMask *m, EcsComponentId id) { m->words[id >> 6] &= ~(1ULL << (id & 63)); }

// Geometric growth of a plain element array. Leaves *arr/*cap untouched on OOM.
static bool ensure_cap(mi_heap_t *heap, void **arr, uint32_t *cap, uint32_t need, size_t elem)
{
    if (need <= *cap) return true;
    uint32_t newcap = *cap ? *cap : 8u;
    while (newcap < need) newcap *= 2u;
    void *p = mi_heap_realloc(heap, *arr, (size_t)newcap * elem);
    if (!p) return false;
    *arr = p;
    *cap = newcap;
    return true;
}

// Grows the entity slot array, zeroing the new region (fresh slots start at
// generation 0, not alive).
static bool slots_reserve(EcsWorld *w, uint32_t need)
{
    if (need <= w->slotCapacity) return true;
    uint32_t oldcap = w->slotCapacity;
    uint32_t newcap = oldcap ? oldcap : 64u;
    while (newcap < need) newcap *= 2u;
    EcsEntitySlot *p = mi_heap_realloc(w->heap, w->slots, (size_t)newcap * sizeof(EcsEntitySlot));
    if (!p) return false;
    memset(p + oldcap, 0, (size_t)(newcap - oldcap) * sizeof(EcsEntitySlot));
    w->slots = p;
    w->slotCapacity = newcap;
    return true;
}

// Grows a store's dense (data, owners) pair together.
static bool store_reserve(EcsWorld *w, EcsStore *s, uint32_t need)
{
    if (need <= s->capacity) return true;
    uint32_t newcap = s->capacity ? s->capacity : 64u;
    while (newcap < need) newcap *= 2u;
    uint8_t *nd = mi_heap_realloc(w->heap, s->data, (size_t)newcap * s->stride);
    if (!nd) return false;
    s->data = nd; // valid memory even if owners realloc fails below; capacity not yet bumped
    EcsEntityId *no = mi_heap_realloc(w->heap, s->owners, (size_t)newcap * sizeof(EcsEntityId));
    if (!no) return false;
    s->owners = no;
    s->capacity = newcap;
    return true;
}

// sparse[idx] = dense, allocating the chunk table and chunk on demand.
static bool sparse_set(EcsWorld *w, EcsStore *s, uint32_t index, uint32_t dense)
{
    uint32_t ci = index / ECS_SPARSE_CHUNK;
    if (ci >= s->sparseChunks) {
        uint32_t newn = s->sparseChunks ? s->sparseChunks : 4u;
        while (newn <= ci) newn *= 2u;
        uint32_t **p = mi_heap_realloc(w->heap, s->sparse, (size_t)newn * sizeof(uint32_t *));
        if (!p) return false;
        for (uint32_t i = s->sparseChunks; i < newn; i++) p[i] = NULL;
        s->sparse = p;
        s->sparseChunks = newn;
    }
    if (!s->sparse[ci]) {
        uint32_t *chunk = mi_heap_malloc(w->heap, ECS_SPARSE_CHUNK * sizeof(uint32_t));
        if (!chunk) return false;
        for (uint32_t i = 0; i < ECS_SPARSE_CHUNK; i++) chunk[i] = ECS_NO_DENSE;
        s->sparse[ci] = chunk;
    }
    s->sparse[ci][index % ECS_SPARSE_CHUNK] = dense;
    return true;
}

static uint32_t sparse_get(const EcsStore *s, uint32_t index)
{
    uint32_t ci = index / ECS_SPARSE_CHUNK;
    if (ci >= s->sparseChunks || !s->sparse[ci]) return ECS_NO_DENSE;
    return s->sparse[ci][index % ECS_SPARSE_CHUNK];
}

// Swap-and-pop the component of type `id` owned by `e` from its store. The chunks
// for `e` and for the moved element already exist, so sparse_set cannot fail here.
static void store_remove(EcsWorld *w, EcsComponentId id, EcsEntityId e)
{
    EcsStore *s = &w->stores[id];
    uint32_t dense = sparse_get(s, e.index);
    if (dense == ECS_NO_DENSE) return;

    uint32_t last = s->count - 1u;
    if (dense != last) {
        memcpy(s->data + (size_t)dense * s->stride,
               s->data + (size_t)last * s->stride, s->stride);
        EcsEntityId moved = s->owners[last];
        s->owners[dense] = moved;
        (void)sparse_set(w, s, moved.index, dense);
    }
    s->count = last;
    (void)sparse_set(w, s, e.index, ECS_NO_DENSE);
    mask_clear(&w->slots[e.index].mask, id);
}

// ---------------------------------------------------------------------------
// World lifecycle
// ---------------------------------------------------------------------------

EcsWorld *ano_ecs_world_create(mi_heap_t *heap, uint32_t initial_entity_capacity)
{
    if (!heap) return NULL;
    EcsWorld *w = mi_heap_zalloc(heap, sizeof(EcsWorld));
    if (!w) return NULL;
    w->heap = heap;
    if (initial_entity_capacity && !slots_reserve(w, initial_entity_capacity)) {
        mi_free(w);
        return NULL;
    }
    return w;
}

void ano_ecs_world_destroy(EcsWorld *w)
{
    if (!w) return;
    for (uint32_t id = 0; id < ANO_ECS_MAX_COMPONENTS; id++) {
        EcsStore *s = &w->stores[id];
        if (s->data)   mi_free(s->data);
        if (s->owners) mi_free(s->owners);
        if (s->sparse) {
            for (uint32_t c = 0; c < s->sparseChunks; c++)
                if (s->sparse[c]) mi_free(s->sparse[c]);
            mi_free(s->sparse);
        }
    }
    if (w->slots)        mi_free(w->slots);
    if (w->freeList)     mi_free(w->freeList);
    if (w->destroyQueue) mi_free(w->destroyQueue);
    if (w->removeQueue)  mi_free(w->removeQueue);
    mi_free(w);
}

// ---------------------------------------------------------------------------
// Component registration
// ---------------------------------------------------------------------------

bool ano_ecs_register_component(EcsWorld *w, EcsComponentId id, size_t stride)
{
    if (id >= ANO_ECS_MAX_COMPONENTS || stride == 0) return false;
    EcsStore *s = &w->stores[id];
    if (s->active) return false;
    s->active = true;
    s->stride = stride;
    return true;
}

// ---------------------------------------------------------------------------
// Entity lifecycle
// ---------------------------------------------------------------------------

EcsEntityId ano_ecs_entity_create(EcsWorld *w)
{
    uint32_t index;
    if (w->freeCount > 0) {
        index = w->freeList[--w->freeCount];          // recycle (generation preserved)
    } else {
        if (!slots_reserve(w, w->slotCount + 1u)) return ANO_ECS_INVALID_ENTITY;
        index = w->slotCount++;
    }
    EcsEntitySlot *slot = &w->slots[index];
    slot->alive = true;
    memset(&slot->mask, 0, sizeof(slot->mask));
    return (EcsEntityId){ .index = index, .generation = slot->generation };
}

void ano_ecs_entity_destroy(EcsWorld *w, EcsEntityId e)
{
    if (!ano_ecs_entity_alive(w, e)) return;
    if (!ensure_cap(w->heap, (void **)&w->destroyQueue, &w->destroyCapacity,
                    w->destroyCount + 1u, sizeof(EcsEntityId)))
        return;
    w->destroyQueue[w->destroyCount++] = e;
}

bool ano_ecs_entity_alive(const EcsWorld *w, EcsEntityId e)
{
    return e.index < w->slotCount
        && w->slots[e.index].alive
        && w->slots[e.index].generation == e.generation;
}

// ---------------------------------------------------------------------------
// Component mutation & access
// ---------------------------------------------------------------------------

void *ano_ecs_add(EcsWorld *w, EcsEntityId e, EcsComponentId id)
{
    if (id >= ANO_ECS_MAX_COMPONENTS) return NULL;
    EcsStore *s = &w->stores[id];
    if (!s->active || !ano_ecs_entity_alive(w, e)) return NULL;
    if (ano_ecs_mask_test(&w->slots[e.index].mask, id)) return NULL; // already owns it

    if (!store_reserve(w, s, s->count + 1u)) return NULL;
    if (!sparse_set(w, s, e.index, s->count)) return NULL;

    uint32_t dense = s->count++;
    s->owners[dense] = e;
    void *slot = s->data + (size_t)dense * s->stride;
    memset(slot, 0, s->stride);
    mask_set(&w->slots[e.index].mask, id);
    return slot;
}

void *ano_ecs_add_init(EcsWorld *w, EcsEntityId e, EcsComponentId id, const void *src)
{
    void *p = ano_ecs_add(w, e, id);
    if (p && src) memcpy(p, src, w->stores[id].stride);
    return p;
}

void ano_ecs_remove(EcsWorld *w, EcsEntityId e, EcsComponentId id)
{
    if (id >= ANO_ECS_MAX_COMPONENTS || !ano_ecs_entity_alive(w, e)) return;
    if (!ensure_cap(w->heap, (void **)&w->removeQueue, &w->removeCapacity,
                    w->removeCount + 1u, sizeof(EcsPendingRemove)))
        return;
    w->removeQueue[w->removeCount++] = (EcsPendingRemove){ .e = e, .comp = id };
}

void *ano_ecs_get(const EcsWorld *w, EcsEntityId e, EcsComponentId id)
{
    if (id >= ANO_ECS_MAX_COMPONENTS || !ano_ecs_entity_alive(w, e)) return NULL;
    if (!ano_ecs_mask_test(&w->slots[e.index].mask, id)) return NULL;
    const EcsStore *s = &w->stores[id];
    uint32_t dense = sparse_get(s, e.index);
    if (dense == ECS_NO_DENSE) return NULL;
    return s->data + (size_t)dense * s->stride;
}

bool ano_ecs_has(const EcsWorld *w, EcsEntityId e, EcsComponentId id)
{
    if (id >= ANO_ECS_MAX_COMPONENTS) return false;
    return ano_ecs_entity_alive(w, e) && ano_ecs_mask_test(&w->slots[e.index].mask, id);
}

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

EcsColumn ano_ecs_column(const EcsWorld *w, EcsComponentId id)
{
    EcsColumn col = {0};
    if (id >= ANO_ECS_MAX_COMPONENTS) return col;
    const EcsStore *s = &w->stores[id];
    if (!s->active) return col;
    col.data   = s->data;
    col.owners = s->owners;
    col.count  = s->count;
    col.stride = s->stride;
    return col;
}

// ---------------------------------------------------------------------------
// Tick boundary
// ---------------------------------------------------------------------------

void ano_ecs_flush_structural(EcsWorld *w)
{
    // Removals first: a pending remove for an entity also being destroyed simply
    // finds the component already gone during the destroy pass.
    for (uint32_t i = 0; i < w->removeCount; i++) {
        EcsPendingRemove r = w->removeQueue[i];
        if (ano_ecs_entity_alive(w, r.e) && ano_ecs_mask_test(&w->slots[r.e.index].mask, r.comp))
            store_remove(w, r.comp, r.e);
    }
    w->removeCount = 0;

    for (uint32_t i = 0; i < w->destroyCount; i++) {
        EcsEntityId e = w->destroyQueue[i];
        if (!ano_ecs_entity_alive(w, e)) continue; // duplicate / already gone this tick
        EcsEntitySlot *slot = &w->slots[e.index];

        for (uint32_t word = 0; word < ANO_ECS_MASK_WORDS; word++) {
            uint64_t bits = slot->mask.words[word];
            while (bits) {
                uint32_t b = (uint32_t)__builtin_ctzll(bits);
                bits &= bits - 1u;
                store_remove(w, (EcsComponentId)(word * 64u + b), e);
            }
        }

        slot->alive = false;
        slot->generation++; // invalidate outstanding handles to this slot
        if (ensure_cap(w->heap, (void **)&w->freeList, &w->freeCapacity,
                       w->freeCount + 1u, sizeof(uint32_t)))
            w->freeList[w->freeCount++] = e.index;
        // On free-list OOM the index simply leaks (never recycled); state stays valid.
    }
    w->destroyCount = 0;
}
