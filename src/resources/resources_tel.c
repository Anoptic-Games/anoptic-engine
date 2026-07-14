/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Five-axis attribution cells. STUB.
//
// TODO(W1, M3): the real interning (packed 19-bit key -> a cell index, in RES_ARENA_PLANE),
// the AoS charge calls, and the copy-out snapshot. res_account_copy/_transfer stop doing
// `(void)plan;` in the same commit.

#include "resources_tel.h"

#include <string.h>

#include "resources_ext.h"

int res_tel_init(void)
{
    return 0;                                   // TODO(W1, M3)
}

void res_tel_shutdown(void)
{
                                                // TODO(W1, M3)
}

uint16_t res_tel_intern(const res_place_plan *p)
{
    (void)p;
    return 0;                                   // TODO(W1, M3): cell 0 is also the overflow bucket
}

void res_tel_alloc(uint16_t cell, size_t requested, size_t serving)
{
    (void)cell; (void)requested; (void)serving;
}

void res_tel_free(uint16_t cell, size_t requested, size_t serving)
{
    (void)cell; (void)requested; (void)serving;
}

void res_tel_copy(uint16_t cell, size_t bytes)
{
    (void)cell; (void)bytes;
}

void res_tel_transfer(uint16_t cell, size_t bytes, bool zero_copy)
{
    (void)cell; (void)bytes; (void)zero_copy;
}

void res_tel_promote(uint16_t dst_cell, size_t bytes, bool copied)
{
    (void)dst_cell; (void)bytes; (void)copied;
}

size_t res_tel_snapshot(res_tel_cell *out, size_t cap)
{
    (void)out; (void)cap;
    return 0;                                   // TODO(W1, M3)
}

size_t res_tel_overflow_hits(void)
{
    return 0;                                   // TODO(W1, M3)
}

// The public cube. res_tel_cell_public mirrors res_tel_cell field for field, so the copy-out
// is one memcpy per cell -- and a reader never touches a live cell.
size_t ano_res_stats_cells(res_tel_cell_public *out, size_t cap)
{
    static_assert(sizeof(res_tel_cell_public) == sizeof(res_tel_cell),
                  "the public cube must mirror the private cell exactly");
    (void)out; (void)cap;
    return 0;                                   // TODO(W1, M3)
}

size_t ano_res_stats_overflow_hits(void)
{
    return res_tel_overflow_hits();
}
