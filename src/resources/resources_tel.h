/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Five-axis attribution (kind, lifetime, role, operation, destination).
// Alloc-hot fields live in line 0. Charges are plain non-atomic stores, owner-thread-only.
// Overflow lands in cell 0, is COUNTED, and Debug-asserts. Overflow != 0 voids the run's numbers.

#ifndef ANOPTIC_RESOURCES_TEL_H
#define ANOPTIC_RESOURCES_TEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "resources_internal.h"

#define RES_TEL_CELLS 1024u

typedef struct res_tel_cell {
    /* line 0 -- the ALLOCATION HOT PATH touches this line and no other */
    uint32_t key;                 // packed kind:5 | lifetime:3 | role:3 | op:4 | dest:4
    uint32_t _pad0;
    uint64_t allocs, frees;
    uint64_t requested_bytes, serving_bytes;
    uint64_t live_bytes, peak_bytes;
    uint64_t live_blocks;
    /* line 1 -- copy / transfer / promote / release / retire */
    uint64_t copies, bytes_copied;
    uint64_t transfers, transfer_bytes;
    uint64_t promotions, duplications;
    uint64_t releases_zero_copy, releases_copied;
} res_tel_cell;
static_assert(sizeof(res_tel_cell) == 128, "two lines; alloc touches only line 0");
static_assert(offsetof(res_tel_cell, copies) == 64, "alloc-hot fields must fit line 0");

// 19-bit packed key. Interned ONCE in res_place_plan, cached in res_site.cell.
#define RES_TEL_KEY(kind, lifetime, role, op, dest)                     \
    (((uint32_t)(kind)     & 0x1Fu)                                     \
   | (((uint32_t)(lifetime) & 0x07u) << 5)                              \
   | (((uint32_t)(role)     & 0x07u) << 8)                              \
   | (((uint32_t)(op)       & 0x0Fu) << 11)                             \
   | (((uint32_t)(dest)     & 0x0Fu) << 15))

int      res_tel_init(void);
void     res_tel_shutdown(void);

uint16_t res_tel_intern(const res_place_plan *);   // called ONCE, inside res_place_plan
void res_tel_alloc   (uint16_t cell, size_t requested, size_t serving);
void res_tel_free    (uint16_t cell, size_t requested, size_t serving);
void res_tel_copy    (uint16_t cell, size_t bytes);
void res_tel_transfer(uint16_t cell, size_t bytes, bool zero_copy);
void res_tel_promote (uint16_t dst_cell, size_t bytes, bool copied);   // ALWAYS the DESTINATION
size_t res_tel_snapshot(res_tel_cell *out, size_t cap);                // copy-out; readers never touch live cells
size_t res_tel_overflow_hits(void);   // != 0 invalidates every number from that run

#endif // ANOPTIC_RESOURCES_TEL_H
