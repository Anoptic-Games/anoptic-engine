/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Placement seam LIVE. res_place_route is the ONLY model-dependent code. Alloc/free dispatch on res_site.backing.
// Single-owner by construction (registry owner-thread gate). Roots/counters/telemetry are plain stores.
// TRANSFER uses calling-thread default heap. Multipool may under-deliver alignment. No WINKABLE free path yet.

#include "resources_place.h"

#include <anoptic_log.h>

#include <string.h>

#include "resources_ext.h"
#include "resources_internal.h"
#include "resources_tel.h"

static struct {
    const res_model *model;
    res_root         root[RES_ROOT_MAX];
} g_place;

// Inputs: model name from ANO_RES_PLACEMENT (NULL/empty = default). Output: 0, or -1 on unknown name.
// Called exactly once per registry lifetime, before any root opens.
int res_place_init(const char *model_name)
{
    const res_model *m = model_name != NULL && model_name[0] != '\0'
                       ? res_model_by_name(model_name)
                       : res_model_default();
    if (m == NULL) {
        ano_log(ANO_ERROR, "resources: unknown placement scaffold '%s'", model_name);
        return -1;
    }
    memset(&g_place, 0, sizeof g_place);
    g_place.model = m;
    ano_log(ANO_INFO, "resources: placement scaffold '%s'", m->name);
    return 0;
}

const char *res_place_name(void)
{
    return g_place.model != NULL ? g_place.model->name : "none";
}

const char *ano_res_placement_name(void)
{
    return res_place_name();
}

/* Roots */

// Root a plan keys into. RES_ROOT_MAX on an unroutable key.
static uint32_t root_key(const res_model *m, ano_res_lifetime lt, res_role role, uint16_t dk)
{
    switch (m->root_axis) {
    case RES_ROOT_SINGLE:   return 0;
    case RES_ROOT_KIND:     return dk;                      // dense id
    case RES_ROOT_LIFETIME: return lt.owner < RES_ROOT_MAX ? lt.owner : RES_ROOT_MAX;
    case RES_ROOT_ROLE:     return (uint32_t)role;          // role class
    }
    return RES_ROOT_MAX;
}

// Build one root from the model's default roster. Output: 0, or -1 with nothing left allocated.
// Arenas from the root's own heap except TRANSFER (NULL -> calling-thread default heap).
static int root_build(uint32_t key)
{
    const res_model *m = g_place.model;
    res_root *r = &g_place.root[key];
    if (r->live)
        return -1;
    *r = (res_root){ .key = key };
    r->heap = mi_heap_new();
    if (r->heap == NULL)
        return -1;
    for (uint32_t a = 0; a < RES_ARENA_COUNT; a++) {
        const res_arena_spec *spec = &m->arena[a];
        r->backing[a] = spec->backing;
        switch (spec->backing) {
        case RES_BACK_NONE:
            break;
        case RES_BACK_MULTIPOOL: {
            ano_mem_multipool_cfg cfg = {
                .min_block = spec->min_block, .max_block = spec->max_block,
                .classes = spec->classes, .class_count = spec->class_count,
            };
            bool dflt = cfg.min_block == 0 && cfg.max_block == 0 && cfg.classes == NULL;
            r->arena[a] = ano_mem_multipool_make(ano_mem_parent_heap(r->heap),
                                                 dflt ? NULL : &cfg);
            if (r->arena[a] == NULL)
                goto fail;
            break;
        }
        case RES_BACK_HEAP:
            // TRANSFER stays NULL: alloc falls through to calling-thread default heap.
            r->arena[a] = a == RES_ARENA_TRANSFER ? NULL : r->heap;
            break;
        default:
            goto fail;                          // POOL/MONOTONIC/STRIPE not built by scaffolds
        }
    }
    r->live = true;
    return 0;
fail:
    for (uint32_t a = 0; a < RES_ARENA_COUNT; a++)
        if (r->backing[a] == RES_BACK_MULTIPOOL && r->arena[a] != NULL)
            ano_mem_multipool_destroy(r->arena[a]);
    mi_heap_destroy(r->heap);
    *r = (res_root){0};
    return -1;
}

// Destroy one root: every owned arena, then the heap.
static void root_destroy(uint32_t key)
{
    res_root *r = &g_place.root[key];
    if (!r->live)
        return;
    for (uint32_t a = 0; a < RES_ARENA_COUNT; a++)
        if (r->backing[a] == RES_BACK_MULTIPOOL && r->arena[a] != NULL)
            ano_mem_multipool_destroy(r->arena[a]);
    mi_heap_destroy(r->heap);
    *r = (res_root){0};
}

void res_place_shutdown(void)
{
    for (uint32_t i = 0; i < RES_ROOT_MAX; i++)
        root_destroy(i);
    memset(&g_place, 0, sizeof g_place);
}

// Inputs: lifetime whose domain the registry just validated. Output: 0 or -1.
// Under SINGLE-rooted model a non-engine open is only a new stats key.
int res_place_domain_open(ano_res_lifetime lt)
{
    if (g_place.model == NULL)
        return -1;
    switch (g_place.model->root_axis) {
    case RES_ROOT_SINGLE:
        return g_place.root[0].live ? 0 : root_build(0);
    case RES_ROOT_LIFETIME:
        if (lt.owner == 0 || lt.owner >= RES_ROOT_MAX)
            return -1;
        return root_build(lt.owner);
    default:
        return -1;                              // KIND/ROLE root axes unused by scaffolds
    }
}

// mi_heap_destroy(root), or a no-op. Under SINGLE the root dies only with the engine domain.
void res_place_domain_wink(ano_res_lifetime lt)
{
    if (g_place.model == NULL)
        return;
    switch (g_place.model->root_axis) {
    case RES_ROOT_SINGLE:
        if (lt.kind == ANO_RES_LIFETIME_ENGINE)
            root_destroy(0);
        return;
    case RES_ROOT_LIFETIME:
        if (lt.owner != 0 && lt.owner < RES_ROOT_MAX)
            root_destroy(lt.owner);
        return;
    default:
        return;
    }
}

mi_heap_t *res_place_transfer_heap(ano_res_lifetime lt)
{
    (void)lt;
    return NULL;                                // STUB
}

void res_place_transfer_heap_destroy(ano_res_lifetime lt)
{
    (void)lt;                                   // STUB
}

/* The route */

static const res_kind_tune *kind_tune_for(const res_model *m, uint32_t tag)
{
    for (size_t i = 0; i < m->kind_tune_count; i++)
        if (m->kind_tune[i].tag == tag)
            return &m->kind_tune[i];
    return NULL;
}

static const res_arena_spec *spec_of(const res_model *m, const res_kind_tune *t, res_arena_id a)
{
    if (t != NULL && t->arena[a].backing != RES_BACK_NONE)
        return &t->arena[a];
    return &m->arena[a];
}

// What the arena WILL charge for `size` bytes. Multipool replicates class mapping.
static size_t arena_serving(const res_arena_spec *spec, size_t size)
{
    if (spec->backing != RES_BACK_MULTIPOOL)
        return size;
    size_t minb = spec->min_block != 0 ? spec->min_block : 16;
    size_t maxb = spec->max_block != 0 ? spec->max_block : (size_t)1 << 20;
    if (size > maxb)
        return size;                            // parent passthrough, charged at request size
    if (size <= minb)
        return minb;
    size_t n = size - 1;
    n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16;
#if SIZE_MAX > UINT32_MAX
    n |= n >> 32;
#endif
    return n + 1;
}

// What the arena WILL deliver. Multipool aligns to min(class stride, 4096). Heap honors request.
static size_t arena_alignment(const res_arena_spec *spec, size_t serving, size_t want)
{
    if (spec->backing == RES_BACK_MULTIPOOL)
        return serving < 4096 ? serving : 4096;
    return want;
}

// PURE ROUTE: reads the plan, allocates nothing, mutates only the telemetry intern table, returns the site.
int res_place_route(const res_place_plan *p, size_t size, res_site *out)
{
    const res_model *m = g_place.model;
    if (m == NULL || p == NULL || out == NULL || size == 0)
        return -1;
    if (p->role >= RES_ROLE_COUNT || p->destination >= RES_DEST_COUNT)
        return -1;
    uint16_t dk  = res_kind_of(p->tag);
    uint32_t key = root_key(m, p->lifetime, p->role, dk);
    if (key >= RES_ROOT_MAX)
        return -1;
    res_root *r = &g_place.root[key];
    if (!r->live)
        return -1;

    const res_kind_tune *t = kind_tune_for(m, p->tag);      // sparse fourcc lookup, may be NULL
    int8_t a = t != NULL ? t->route_override[p->role] : -1;
    if (a < 0)
        a = (int8_t)m->route[p->role][p->destination];
    if (a == RES_ARENA_SMALL && size > m->small_max) {
        // Oversize LOAD/ADOPT to VARIABLE_PAYLOAD/BULK goes to TRANSFER (calling-thread
        // default heap, transferable). Every other oversize block goes to BULK (domain heap).
        bool adoptable = (p->operation == RES_OP_LOAD || p->operation == RES_OP_ADOPT)
                      && (p->destination == RES_DEST_VARIABLE_PAYLOAD
                          || p->destination == RES_DEST_BULK);
        a = (int8_t)(adoptable ? RES_ARENA_TRANSFER : RES_ARENA_BULK);
    }
    if (a >= (int8_t)RES_ARENA_COUNT)
        return -1;                              // REFUSE, loudly

    const res_arena_spec *spec = spec_of(m, t, (res_arena_id)a);
    if (spec->backing == RES_BACK_NONE)
        return -1;
    // A NULL arena refuses -- except TRANSFER with RES_BACK_HEAP, where NULL means calling
    // thread's default heap.
    if (r->arena[a] == NULL && !(a == RES_ARENA_TRANSFER && spec->backing == RES_BACK_HEAP))
        return -1;

    size_t want    = p->alignment != 0 ? p->alignment : ANO_CACHE_LINE;
    size_t serving = arena_serving(spec, size);
    size_t give    = arena_alignment(spec, serving, want);
    // Multipool may under-deliver alignment. Site reports the delivered alignment.
    *out = (res_site){
        .allocator = r->arena[a],
        .serving   = serving,
        .alignment = give,
        .root      = key,
        .cell      = res_tel_intern(p),
        .arena     = (uint8_t)a,
        .backing   = spec->backing,
        .flags     = spec->confers,             // scaffolds do not grant WINKABLE today
    };
    return 0;
}

/* Alloc / free */

// Dispatch on site backing, charge site cell and root.

void *res_place_alloc(const res_site *s, size_t size)
{
    if (s == NULL || size == 0)
        return NULL;
    void *p = NULL;
    switch (s->backing) {
    case RES_BACK_MULTIPOOL:
        p = ano_mem_multipool_alloc(s->allocator, size);
        break;
    case RES_BACK_HEAP:
        p = s->allocator != NULL
          ? mi_heap_malloc_aligned(s->allocator, size, s->alignment)
          : mi_malloc_aligned(size, s->alignment);   // TRANSFER: calling-thread default heap
        break;
    default:
        return NULL;
    }
    if (p == NULL)
        return NULL;
    if (s->root < RES_ROOT_MAX && g_place.root[s->root].live) {
        g_place.root[s->root].live_bytes += s->serving;
        g_place.root[s->root].live_blocks++;
    }
    res_tel_alloc(s->cell, size, s->serving);
    return p;
}

void res_place_free(const res_site *s, void *p, size_t size, res_free_mode m)
{
    (void)m;    // no site is winkable: every free is retail
    if (s == NULL || p == NULL)
        return;
    switch (s->backing) {
    case RES_BACK_MULTIPOOL:
        ano_mem_multipool_free(s->allocator, p, size);
        break;
    default:
        mi_free(p);                             // heap blocks route home from any heap
        break;
    }
    if (s->root < RES_ROOT_MAX && g_place.root[s->root].live) {
        res_root *r = &g_place.root[s->root];
        r->live_bytes -= s->serving <= r->live_bytes ? s->serving : r->live_bytes;
        if (r->live_blocks > 0)
            r->live_blocks--;
    }
    res_tel_free(s->cell, size, s->serving);
}

/* Accounting probes */

ano_mem_stats res_place_arena_stats(uint32_t root, res_arena_id arena)
{
    if (root >= RES_ROOT_MAX || arena >= RES_ARENA_COUNT || !g_place.root[root].live)
        return (ano_mem_stats){0};
    const res_root *r = &g_place.root[root];
    if (r->backing[arena] == RES_BACK_MULTIPOOL && r->arena[arena] != NULL)
        return ano_mem_multipool_stats(r->arena[arena]);
    return (ano_mem_stats){0};                  // heap arenas not reported
}

size_t res_place_domain_live_bytes(ano_res_lifetime lt)
{
    if (g_place.model == NULL)
        return 0;
    uint32_t key = root_key(g_place.model, lt, 0, 0);
    if (key >= RES_ROOT_MAX || !g_place.root[key].live)
        return 0;
    return g_place.root[key].live_bytes;
}

void res_place_b_kind_wink(uint16_t dense_kind)
{
    (void)dense_kind;                           // STUB
}
