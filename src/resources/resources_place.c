/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Placement: the 11-call seam of resources_place.h. STUB.
//
// TODO(W1, M5): the live seam. res_place_plan() is the 30-line pure router of blueprint 2.2
// and the ONLY model-dependent code in the module; res_place_alloc/free dispatch on
// res_site.backing. Until it lands, resources_registry.c still routes its own blocks and
// res_place_name() reports the behavior the registry actually implements.

#include "resources_place.h"

#include "resources_internal.h"
#include "resources_tel.h"

// The scaffold name. TODO(W1, M5): read ANO_RES_PLACEMENT once at res_registry_init, log it
// at INFO, and select the model literal. Today's registry behavior IS "scoped-pool": one
// multipool per lifetime domain, over that domain's own mi_heap.
static const char *g_place_name = "scoped-pool";

int res_place_init(const char *model_name)
{
    if (model_name != NULL && model_name[0] != '\0')
        g_place_name = model_name;
    return 0;
}

void res_place_shutdown(void)
{
    g_place_name = "scoped-pool";
}

const char *res_place_name(void)
{
    return g_place_name;
}

const char *ano_res_placement_name(void)
{
    return res_place_name();
}

int res_place_domain_open(ano_res_lifetime lt)
{
    (void)lt;
    return -1;                                  // TODO(W1, M5)
}

void res_place_domain_wink(ano_res_lifetime lt)
{
    (void)lt;                                   // TODO(W1, M6)
}

mi_heap_t *res_place_transfer_heap(ano_res_lifetime lt)
{
    (void)lt;
    return NULL;                                // TODO(W1, M6)
}

void res_place_transfer_heap_destroy(ano_res_lifetime lt)
{
    (void)lt;                                   // TODO(W1, M6)
}

int res_place_route(const res_place_plan *p, size_t size, res_site *out)
{
    (void)p; (void)size; (void)out;
    return -1;                                  // TODO(W1, M5): blueprint 2.2's 30 lines
}

void *res_place_alloc(const res_site *s, size_t size)
{
    (void)s; (void)size;
    return NULL;                                // TODO(W1, M5)
}

void res_place_free(const res_site *s, void *p, size_t size, res_free_mode m)
{
    (void)s; (void)p; (void)size; (void)m;      // TODO(W1, M5)
}

ano_mem_stats res_place_arena_stats(uint32_t root, res_arena_id arena)
{
    (void)root; (void)arena;
    return (ano_mem_stats){0};                  // TODO(W1, M5)
}

size_t res_place_domain_live_bytes(ano_res_lifetime lt)
{
    (void)lt;
    return 0;                                   // TODO(W1, M6): the post-wink assert reads this
}

void res_place_b_kind_wink(uint16_t dense_kind)
{
    (void)dense_kind;                           // TODO(W1, M19): contest harness only (D12)
}
