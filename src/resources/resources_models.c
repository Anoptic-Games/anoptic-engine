/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The models, as DATA (M5): the two truthful Stage A scaffolds. "scoped-pool" reproduces
// today's registry routing byte for byte; "global-pool" is the historical group-0 scaffold
// (one immortal root, no lifetime ownership). Neither is a Model A..E claim -- A.4 bullet 1.
//
// TODO(W1, M19): model-a .. model-e, behind the test-only selector.
//
// Every model is a `static const res_model` literal in THIS FILE and nowhere else. No
// registry code, no consumer code, no public header change, no `if (model == ...)` outside
// res_place_route's 30 lines. `git diff model-a model-e -- src/ include/` must touch exactly
// this file: that is the proof that the five models are a contest and not five rewrites.

#include <string.h>

#include "resources_place.h"

// The legacy pooled/direct split: bytes above this leave the multipool. Matches the
// registry's historical RES_POOL_MAX and the multipool's default max_block, so serving
// sizes stay bit-identical to the deleted serving_size().
#define SCAFFOLD_POOL_MAX ((size_t)1 << 20)

// Both scaffolds route EVERY (role, destination) to SMALL: today's registry pools every
// block <= 1 MiB regardless of role, and the router's small_max overflow reproduces the
// legacy oversize split (adoptable -> TRANSFER, everything else -> BULK). Row/column 0 fall
// to RES_ARENA_METADATA, whose backing is RES_BACK_NONE in both scaffolds: a malformed
// plan refuses at route time instead of landing somewhere accidental.
#define SCAFFOLD_ROUTE                                                     \
    {                                                                      \
        [RES_ROLE_REGISTRY ... RES_ROLE_TRANSFER] = {                      \
            [RES_DEST_METADATA ... RES_DEST_STRIPE] = RES_ARENA_SMALL,     \
        },                                                                 \
    }

// The scaffold arena roster. SMALL: the default-config multipool (geometric 16 B .. 1 MiB
// classes -- exactly what ano_mem_multipool_make(parent, NULL) built here yesterday).
// BULK: the root's own mi_heap, direct. TRANSFER: RES_BACK_HEAP with NO root arena --
// res_place_alloc reads that as the CALLING THREAD'S DEFAULT HEAP, which is today's >1 MiB
// placement bug, preserved deliberately at M5 (behavior-identical means bug-identical; it
// dies at M6 when the per-domain transfer_root lands).
#define SCAFFOLD_ARENAS                                                    \
    {                                                                      \
        [RES_ARENA_SMALL]    = { .backing = RES_BACK_MULTIPOOL },          \
        [RES_ARENA_BULK]     = { .backing = RES_BACK_HEAP },               \
        [RES_ARENA_TRANSFER] = { .backing = RES_BACK_HEAP,                 \
                                 .confers = RES_SITE_TRANSFERABLE },       \
    }

// scoped-pool: today's production behavior. One root per lifetime domain, each with its
// own mi_heap and one default-config multipool over it.
static const res_model MODEL_SCOPED_POOL = {
    .name      = "scoped-pool",
    .root_axis = RES_ROOT_LIFETIME,
    .small_max = SCAFFOLD_POOL_MAX,
    .arena     = SCAFFOLD_ARENAS,
    .route     = SCAFFOLD_ROUTE,
};

// global-pool: the historical group-0 scaffold. ONE process root; opening a lifetime
// domain creates no memory ownership, only a stats key; domain wink is a no-op and the
// root dies at shutdown.
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
    // 7.1's env grammar admits the short names; the resolved model logs its truthful one.
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
