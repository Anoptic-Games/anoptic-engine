/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// ONE framing for every conditioned resource.
// res_block_open is the single hostile-input gate.
// Block is allocated storage with no declared type. Typed access is legal under C23 6.5p7.

#ifndef ANOPTIC_RESOURCES_BLOCK_H
#define ANOPTIC_RESOURCES_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "resources_place.h"   // RES_PLANE_GRAIN

#define RES_BLOCK_PLANES_MAX 32u

typedef struct res_block_hdr {
    uint32_t magic;        // == the kind's FOURCC
    uint32_t version;
    uint64_t layout_id;    // compile-time FNV over sizeof/alignof of every struct in the block
    uint64_t block_hash;   // FNV-1a-64 over the whole block with this field zeroed
    uint32_t plane_count;
    uint32_t _pad;
    uint64_t off[RES_BLOCK_PLANES_MAX];   // byte offsets from base, RES_PLANE_GRAIN-aligned
    uint64_t len[RES_BLOCK_PLANES_MAX];   // element counts
} res_block_hdr;

// PURE. Deterministic from arguments only.
// Inputs: header bytes, per-plane counts and elem sizes, plane count.
// Output: total block size. out_off[i] gets each plane's grain-aligned base. 0 on overflow or n_planes > max.
size_t res_plane_layout(size_t hdr_bytes, const size_t *count, const size_t *elem_size,
                        size_t n_planes, uint64_t *out_off);

// Stamp magic/version/layout_id/plane table, then block_hash over the whole block with that field zeroed. 0 / -1.
int res_block_seal(void *base, size_t size, uint32_t magic, uint32_t version,
                   uint64_t layout_id, size_t n_planes,
                   const uint64_t *off, const uint64_t *len);

typedef struct res_block_view {
    const res_block_hdr *hdr;
    const void *plane[RES_BLOCK_PLANES_MAX];
    uint64_t    count[RES_BLOCK_PLANES_MAX];
    size_t      size;
} res_block_view;

// THE ONE hostile-input gate: magic / version / layout_id / block_hash / plane extents / grain / overflow. 0 / -1 (out zeroed on refusal).
int res_block_open(const void *bytes, size_t len, uint32_t magic, uint32_t version,
                   uint64_t layout_id, res_block_view *out);

#endif // ANOPTIC_RESOURCES_BLOCK_H
