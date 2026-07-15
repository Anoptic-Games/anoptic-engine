/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Placement: WHERE a byte lives. Model is DATA in resources_models.c.
// res_place_route() is the only routing code. Consumers build a res_place_plan and call res_owned_alloc().

#ifndef ANOPTIC_RESOURCES_PLACE_H
#define ANOPTIC_RESOURCES_PLACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <anoptic_memory.h>
#include <anoptic_memory_pools.h>
#include <anoptic_resources.h>

#define RES_ROOT_MAX    64u
#define RES_PLANE_GRAIN 64u   // COMPILE-TIME. Not ANO_CACHE_LINE: baked plane offsets are ABI.

// Routing input. Defined in resources_internal.h. Only pointers cross this header.
typedef struct res_place_plan res_place_plan;

/* Placement vocabulary */

// role / operation / destination / provenance. Each _COUNT bounds a route-plane axis.

typedef enum res_role {
    RES_ROLE_REGISTRY = 1,
    RES_ROLE_NAME,
    RES_ROLE_DEPENDENCY,
    RES_ROLE_PAYLOAD,
    RES_ROLE_DERIVED,
    RES_ROLE_STAGING,
    RES_ROLE_TRANSFER,
    RES_ROLE_COUNT,
} res_role;

typedef enum res_operation {
    RES_OP_BIND = 1,
    RES_OP_LOAD,
    RES_OP_ADOPT,
    RES_OP_DECODE,
    RES_OP_SAVE_LOAD,
    RES_OP_PROMOTE,
    RES_OP_DUPLICATE,
    RES_OP_TRANSFER,
    RES_OP_RETIRE,
    RES_OP_COUNT,
} res_operation;

typedef enum res_destination {
    RES_DEST_METADATA = 1,
    RES_DEST_VARIABLE_PAYLOAD,
    RES_DEST_BULK,
    RES_DEST_STAGING,
    RES_DEST_EXTERNAL_TRANSFER,
    RES_DEST_CHUNK,             // fixed RMOS_CHUNK_MAX streaming chunk
    RES_DEST_STRIPE,            // lane-isolated SoA planes
    RES_DEST_COUNT,
} res_destination;

typedef enum res_provenance {
    RES_PROVENANCE_NAMESPACE = 1,
    RES_PROVENANCE_CONDITIONED,
    RES_PROVENANCE_DECODED,
    RES_PROVENANCE_SAVE_FRAME,
    RES_PROVENANCE_TOOL,
    RES_PROVENANCE_PACK,        // from a pack: validate() is MANDATORY on adopt
    RES_PROVENANCE_COUNT,
} res_provenance;

/* Arenas, backings, sites */

typedef enum res_arena_id {
    RES_ARENA_METADATA = 0,  // identity shard, names, dep/pub/retire/bind records
    RES_ARENA_SMALL,         // variable payloads <= model->small_max
    RES_ARENA_BULK,          // large payloads
    RES_ARENA_CHUNK,         // fixed RMOS_CHUNK_MAX streaming chunks (ano_mem_pool)
    RES_ARENA_PLANE,         // lane-isolated SoA planes (ano_mem_stripe)
    RES_ARENA_STAGING,       // monotonic ingest/decode/compile staging
    RES_ARENA_TRANSFER,      // blocks that may leave the manager. NEVER winkable
    RES_ARENA_COUNT          // also the REFUSE sentinel in route[][]
} res_arena_id;

typedef enum res_backing {
    RES_BACK_NONE = 0, RES_BACK_MULTIPOOL, RES_BACK_POOL,
    RES_BACK_MONOTONIC, RES_BACK_STRIPE, RES_BACK_HEAP
} res_backing;

enum {
    RES_SITE_TRANSFERABLE = 1u << 0,  // may leave the manager: no copy, no interior free
    RES_SITE_DIRECT_LAND  = 1u << 1,  // IO/decoder may write straight into it
    RES_SITE_WINKABLE     = 1u << 2,  // reclaimed en masse by wink(). free(WINK) is accounting-only
};

// Routing decision. PURE OUTPUT of res_place_route. Allocates nothing, mutates nothing.
typedef struct res_site {
    void    *allocator;   // ano_mem_multipool* / _pool* / _monotonic* / _stripe* / mi_heap_t*
    size_t   serving;     // what the arena WILL charge. Authoritative.
    size_t   alignment;   // what the arena WILL deliver. plan() REFUSES rather than under-deliver.
    uint32_t root;        // index into res_placement.root[]
    uint16_t cell;        // telemetry cell, interned ONCE at plan time
    uint8_t  arena;       // res_arena_id
    uint8_t  backing;     // res_backing
    uint8_t  flags;       // RES_SITE_*
    uint8_t  _pad[7];
} res_site;

typedef enum res_free_mode { RES_FREE_RETAIL = 0, RES_FREE_WINK = 1 } res_free_mode;

/* The model is DATA */

typedef enum res_root_axis {
    RES_ROOT_SINGLE = 0,   // one process root
    RES_ROOT_KIND,         // one root per dense kind id
    RES_ROOT_LIFETIME,     // one root per lifetime domain
    RES_ROOT_ROLE,         // one root per role class
} res_root_axis;

typedef struct res_arena_spec {
    uint8_t       backing;        // res_backing. RES_BACK_NONE -> plan() REFUSES
    uint8_t       confers;        // RES_SITE_* this arena grants
    uint32_t      min_block, max_block;             // multipool (geometric)
    const size_t *classes; size_t class_count;      // multipool (explicit histogram)
    uint32_t      block_size, max_blocks;           // pool
    uint32_t      lanes, grain;                     // stripe (grain 0 = ANO_THREAD_LINE)
    uint32_t      first_slab;                       // monotonic
} res_arena_spec;

// SPARSE, keyed by FOURCC. Adding a resource class NEVER edits this table.
typedef struct res_kind_tune {
    uint32_t       tag;
    res_arena_spec arena[RES_ARENA_COUNT];          // backing==RES_BACK_NONE -> use the default
    int8_t         route_override[RES_ROLE_COUNT];  // -1 = fall through to model->route
} res_kind_tune;

typedef struct res_model {
    const char    *name;          // "global-pool" | "scoped-pool" | "model-a".."model-e"
    res_root_axis  root_axis;
    size_t         small_max;     // SMALL/BULK split. SIZE_MAX = one class serves all
    res_arena_spec arena[RES_ARENA_COUNT];                    // the default roster
    const res_kind_tune *kind_tune; size_t kind_tune_count;   // B and E only
    uint8_t        route[RES_ROLE_COUNT][RES_DEST_COUNT];     // RES_ARENA_COUNT = REFUSE
} res_model;                                                  // NO plan_hook. Ever.

typedef struct res_root {
    mi_heap_t *heap;
    void      *arena[RES_ARENA_COUNT];
    uint8_t    backing[RES_ARENA_COUNT];
    uint32_t   key;                    // dense kind | lifetime.owner | role class | 0
    size_t     live_bytes, live_blocks;
    bool       live;
} res_root;

// Model roster by name (ANO_RES_PLACEMENT). NULL when unknown.
const res_model *res_model_by_name(const char *name);
const res_model *res_model_default(void);

/* The whole seam */

// 11 calls.

int   res_place_init(const char *model_name);   // once, at res_registry_init
void  res_place_shutdown(void);
const char *res_place_name(void);               // logged at INFO. reported in stats

int   res_place_domain_open (ano_res_lifetime lt);   // new root, or just a new stats key
void  res_place_domain_wink (ano_res_lifetime lt);   // mi_heap_destroy(root), or a no-op
mi_heap_t *res_place_transfer_heap(ano_res_lifetime lt);  // per-domain. OUTLIVES root
void  res_place_transfer_heap_destroy(ano_res_lifetime lt);

// PURE ROUTE: reads the plan, allocates nothing, mutates nothing, returns the site.
// Verb is res_place_route() because res_place_plan is the plan TYPE.
int   res_place_route(const res_place_plan *p, size_t size, res_site *out);
void *res_place_alloc(const res_site *s, size_t size);
void  res_place_free (const res_site *s, void *p, size_t size, res_free_mode m);

ano_mem_stats res_place_arena_stats(uint32_t root, res_arena_id arena);
size_t        res_place_domain_live_bytes(ano_res_lifetime lt);   // post-wink assert reads this

// Contest harness only. STUB no-op.
void  res_place_b_kind_wink(uint16_t dense_kind);

#endif // ANOPTIC_RESOURCES_PLACE_H
