/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Models as DATA. Scaffolds: "scoped-pool" (production routing) and "global-pool" (one immortal root).
// Every model is a `static const res_model` literal in THIS FILE only.

#include <string.h>

#include "resources_place.h"

// Legacy pooled/direct split. Bytes above this leave the multipool.
#define SCAFFOLD_POOL_MAX ((size_t)1 << 20)

// Both scaffolds route every (role, destination) to SMALL. Oversize: adoptable -> TRANSFER, else BULK. Row/column 0 -> RES_ARENA_METADATA (RES_BACK_NONE refuses malformed plans).
#define SCAFFOLD_ROUTE                                                     \
    {                                                                      \
        [RES_ROLE_REGISTRY ... RES_ROLE_TRANSFER] = {                      \
            [RES_DEST_METADATA ... RES_DEST_STRIPE] = RES_ARENA_SMALL,     \
        },                                                                 \
    }

// Arena roster. SMALL: default multipool. BULK: root mi_heap. TRANSFER: RES_BACK_HEAP with no root arena (calling-thread default heap).
#define SCAFFOLD_ARENAS                                                    \
    {                                                                      \
        [RES_ARENA_SMALL]    = { .backing = RES_BACK_MULTIPOOL },          \
        [RES_ARENA_BULK]     = { .backing = RES_BACK_HEAP },               \
        [RES_ARENA_TRANSFER] = { .backing = RES_BACK_HEAP,                 \
                                 .confers = RES_SITE_TRANSFERABLE },       \
    }

// scoped-pool: one root per lifetime domain, each with its own mi_heap and multipool.
static const res_model MODEL_SCOPED_POOL = {
    .name      = "scoped-pool",
    .root_axis = RES_ROOT_LIFETIME,
    .small_max = SCAFFOLD_POOL_MAX,
    .arena     = SCAFFOLD_ARENAS,
    .route     = SCAFFOLD_ROUTE,
};

// global-pool: one process root. Domain open creates a stats key only. Root dies at shutdown.
static const res_model MODEL_GLOBAL_POOL = {
    .name      = "global-pool",
    .root_axis = RES_ROOT_SINGLE,
    .small_max = SCAFFOLD_POOL_MAX,
    .arena     = SCAFFOLD_ARENAS,
    .route     = SCAFFOLD_ROUTE,
};

static const res_model *const MODELS[] = { &MODEL_SCOPED_POOL, &MODEL_GLOBAL_POOL };

const res_model *res_model_by_name(const char *name)
{
    if (name == NULL)
        return NULL;
    // Short env names map to the truthful model name.
    if (strcmp(name, "scoped") == 0)
        name = "scoped-pool";
    else if (strcmp(name, "global") == 0)
        name = "global-pool";
    for (size_t i = 0; i < sizeof MODELS / sizeof *MODELS; i++)
        if (strcmp(MODELS[i]->name, name) == 0)
            return MODELS[i];
    return NULL;
}

const res_model *res_model_default(void)
{
    return res_model_by_name("scoped-pool");
}
