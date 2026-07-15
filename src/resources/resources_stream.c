/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Streaming chunks. STUB.

#include "resources_internal.h"

#include "resources_place.h"

void *res_chunk_acquire(ano_res_lifetime lt)
{
    (void)lt;
    return NULL;                                // STUB
}

void res_chunk_release(ano_res_lifetime lt, void *chunk)
{
    (void)lt; (void)chunk;                      // STUB
}
