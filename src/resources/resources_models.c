/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The models, as DATA. STUB.
//
// TODO(W1, M5):  the two scaffold literals, "global-pool" and "scoped-pool".
// TODO(W1, M19): model-a .. model-e, behind the test-only selector.
//
// Every model is a `static const res_model` literal in THIS FILE and nowhere else. No
// registry code, no consumer code, no public header change, no `if (model == ...)` outside
// res_place_plan's 30 lines. `git diff model-a model-e -- src/ include/` must touch exactly
// this file: that is the proof that the five models are a contest and not five rewrites.

#include <string.h>

#include "resources_place.h"

static const res_model *const MODELS[] = { NULL };

const res_model *res_model_by_name(const char *name)
{
    if (name == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof MODELS / sizeof *MODELS; i++)
        if (MODELS[i] != NULL && strcmp(MODELS[i]->name, name) == 0)
            return MODELS[i];
    return NULL;
}

const res_model *res_model_default(void)
{
    return res_model_by_name("scoped-pool");
}
