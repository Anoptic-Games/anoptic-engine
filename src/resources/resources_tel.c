/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

<<<<<<< HEAD
// Five-axis attribution cells. 19-bit key interned ONCE at route time, cached in res_site.cell.
// Charges are plain non-atomic stores, owner-thread-only.
// Cell 0 is overflow: counted and Debug-asserted. Overflow != 0 voids the run.
// STORAGE: static 128 KiB table.

#include "resources_tel.h"

#include <assert.h>
=======
// Five-axis attribution cells. STUB.
//
// TODO(W1, M3): the real interning (packed 19-bit key -> a cell index, in RES_ARENA_PLANE),
// the AoS charge calls, and the copy-out snapshot. res_account_copy/_transfer stop doing
// `(void)plan;` in the same commit.

#include "resources_tel.h"

>>>>>>> block-b1-base
#include <string.h>

#include "resources_ext.h"

<<<<<<< HEAD
static struct {
    res_tel_cell cells[RES_TEL_CELLS];
    bool         used[RES_TEL_CELLS];
    size_t       overflow_hits;
} g_tel;

int res_tel_init(void)
{
    memset(&g_tel, 0, sizeof g_tel);
    g_tel.used[0] = true;                       // cell 0 exists from birth: the overflow bucket
    return 0;
=======
int res_tel_init(void)
{
    return 0;                                   // TODO(W1, M3)
>>>>>>> block-b1-base
}

void res_tel_shutdown(void)
{
<<<<<<< HEAD
    memset(&g_tel, 0, sizeof g_tel);
}

// Inputs: routing plan. Output: cell index, or 0 when full (counted, Debug-asserted). Open addressing over 1..1023. Key stable for the run.
uint16_t res_tel_intern(const res_place_plan *p)
{
    if (p == NULL)
        return 0;
    // An axis outside its field width would MASK onto a valid key and charge someone
    // else's cell. Unattributable plans land in the overflow bucket, counted: overflow voids
    // the run's numbers rather than quietly lying.
    if (p->lifetime.kind > ANO_RES_LIFETIME_SHARED_IMMUTABLE
        || p->role >= RES_ROLE_COUNT
        || p->operation >= RES_OP_COUNT
        || p->destination >= RES_DEST_COUNT) {
        g_tel.overflow_hits++;
        return 0;
    }
    uint32_t key = RES_TEL_KEY(res_kind_of(p->tag), p->lifetime.kind, p->role,
                               p->operation, p->destination);
    uint32_t idx = (key * 2654435761u) % (RES_TEL_CELLS - 1u) + 1u;
    for (uint32_t probe = 0; probe < RES_TEL_CELLS - 1u; probe++) {
        res_tel_cell *c = &g_tel.cells[idx];
        if (g_tel.used[idx] && c->key == key)
            return (uint16_t)idx;
        if (!g_tel.used[idx]) {
            g_tel.used[idx] = true;
            c->key = key;
            return (uint16_t)idx;
        }
        idx = idx % (RES_TEL_CELLS - 1u) + 1u;  // wrap inside 1..1023; cell 0 is reserved
    }
    g_tel.overflow_hits++;
    assert(!"res_tel: cell table overflow; every number from this run is void");
    return 0;
=======
                                                // TODO(W1, M3)
}

uint16_t res_tel_intern(const res_place_plan *p)
{
    (void)p;
    return 0;                                   // TODO(W1, M3): cell 0 is also the overflow bucket
>>>>>>> block-b1-base
}

void res_tel_alloc(uint16_t cell, size_t requested, size_t serving)
{
<<<<<<< HEAD
    if (cell >= RES_TEL_CELLS)
        return;
    res_tel_cell *c = &g_tel.cells[cell];
    c->allocs++;
    c->requested_bytes += requested;
    c->serving_bytes += serving;
    c->live_bytes += serving;
    c->live_blocks++;
    if (c->live_bytes > c->peak_bytes)
        c->peak_bytes = c->live_bytes;
=======
    (void)cell; (void)requested; (void)serving;
>>>>>>> block-b1-base
}

void res_tel_free(uint16_t cell, size_t requested, size_t serving)
{
<<<<<<< HEAD
    (void)requested;                            // frees uncharge SERVING; requested is cumulative
    if (cell >= RES_TEL_CELLS)
        return;
    res_tel_cell *c = &g_tel.cells[cell];
    c->frees++;
    c->live_bytes -= serving <= c->live_bytes ? serving : c->live_bytes;
    if (c->live_blocks > 0)
        c->live_blocks--;
=======
    (void)cell; (void)requested; (void)serving;
>>>>>>> block-b1-base
}

void res_tel_copy(uint16_t cell, size_t bytes)
{
<<<<<<< HEAD
    if (cell >= RES_TEL_CELLS)
        return;
    g_tel.cells[cell].copies++;
    g_tel.cells[cell].bytes_copied += bytes;
=======
    (void)cell; (void)bytes;
>>>>>>> block-b1-base
}

void res_tel_transfer(uint16_t cell, size_t bytes, bool zero_copy)
{
<<<<<<< HEAD
    if (cell >= RES_TEL_CELLS)
        return;
    res_tel_cell *c = &g_tel.cells[cell];
    c->transfers++;
    c->transfer_bytes += bytes;
    if (zero_copy)
        c->releases_zero_copy++;
    else
        c->releases_copied++;
=======
    (void)cell; (void)bytes; (void)zero_copy;
>>>>>>> block-b1-base
}

void res_tel_promote(uint16_t dst_cell, size_t bytes, bool copied)
{
<<<<<<< HEAD
    if (dst_cell >= RES_TEL_CELLS)
        return;
    res_tel_cell *c = &g_tel.cells[dst_cell];
    c->promotions++;
    if (copied) {
        c->copies++;
        c->bytes_copied += bytes;
    }
}

// Copy-out: reader never touches a live cell. Returns populated count. Fills out[0..min(returned,cap)).
size_t res_tel_snapshot(res_tel_cell *out, size_t cap)
{
    size_t n = 0;
    for (uint32_t i = 0; i < RES_TEL_CELLS; i++) {
        if (!g_tel.used[i])
            continue;
        if (out != NULL && n < cap)
            out[n] = g_tel.cells[i];
        n++;
    }
    return n;
=======
    (void)dst_cell; (void)bytes; (void)copied;
}

size_t res_tel_snapshot(res_tel_cell *out, size_t cap)
{
    (void)out; (void)cap;
    return 0;                                   // TODO(W1, M3)
>>>>>>> block-b1-base
}

size_t res_tel_overflow_hits(void)
{
<<<<<<< HEAD
    return g_tel.overflow_hits;
}

// Public cube. res_tel_cell_public mirrors res_tel_cell field for field. One memcpy per cell.
=======
    return 0;                                   // TODO(W1, M3)
}

// The public cube. res_tel_cell_public mirrors res_tel_cell field for field, so the copy-out
// is one memcpy per cell -- and a reader never touches a live cell.
>>>>>>> block-b1-base
size_t ano_res_stats_cells(res_tel_cell_public *out, size_t cap)
{
    static_assert(sizeof(res_tel_cell_public) == sizeof(res_tel_cell),
                  "the public cube must mirror the private cell exactly");
<<<<<<< HEAD
    size_t n = 0;
    for (uint32_t i = 0; i < RES_TEL_CELLS; i++) {
        if (!g_tel.used[i])
            continue;
        if (out != NULL && n < cap)
            memcpy(&out[n], &g_tel.cells[i], sizeof(res_tel_cell_public));
        n++;
    }
    return n;
=======
    (void)out; (void)cap;
    return 0;                                   // TODO(W1, M3)
>>>>>>> block-b1-base
}

size_t ano_res_stats_overflow_hits(void)
{
    return res_tel_overflow_hits();
}
