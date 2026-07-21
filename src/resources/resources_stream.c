/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Streaming chunks. STUB.
<<<<<<< HEAD
=======
//
// TODO(W4, M14): the chunk is CONTESTED, not a shared primitive (D13). This file must NEVER
// hardcode an ano_mem_pool: it asks the active placement for a chunk
// (role = RES_ROLE_STAGING, destination = RES_DEST_CHUNK) and lets the model decide whether
// that is a bounded pool, one multipool, or a stripe. A hardcoded pool here would hand every
// model the same chunk allocator and delete B.6's home ground.
>>>>>>> block-b1-base

#include "resources_internal.h"

#include "resources_place.h"

void *res_chunk_acquire(ano_res_lifetime lt)
{
    (void)lt;
<<<<<<< HEAD
    return NULL;                                // STUB
=======
    return NULL;                                // TODO(W4, M14): route through res_place_plan
>>>>>>> block-b1-base
}

void res_chunk_release(ano_res_lifetime lt, void *chunk)
{
<<<<<<< HEAD
    (void)lt; (void)chunk;                      // STUB
=======
    (void)lt; (void)chunk;                      // TODO(W4, M14)
>>>>>>> block-b1-base
}
