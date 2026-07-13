/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// FROZEN SEAM (blueprint 2.4). ONE framing for every conditioned resource in the engine:
// graphics scene, decoded pixels, GPU binding table, audio PCM (one f32 plane per channel),
// script bytecode, level, font bake, pack TOC. One framing, one validator, one bake path,
// one pack entry type, one hostile-input battery.
//
// res_block_open is the single most fuzz-worthy function in the module and is treated as
// such: concentrated blast radius, chosen deliberately over six validators that are each
// "probably fine" for the sharpest hole in the ledger.
//
// Aliasing, settled: the block is allocated storage with no declared type, so typed access
// through anoresgfx_vertex * is legal under C23 6.5p7, and RES_PLANE_GRAIN-aligned plane
// bases over a cache-line-aligned block base satisfy alignment.

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

// PURE. A deterministic function of its arguments ONLY. The bake, the pack, and the
// hostile validator all depend on exactly this function.
// Inputs: header bytes, per-plane element counts and element sizes, plane count.
// Output: the total block size; out_off[i] receives each plane's grain-aligned base offset.
// 0 on overflow or n_planes > RES_BLOCK_PLANES_MAX.
size_t res_plane_layout(size_t hdr_bytes, const size_t *count, const size_t *elem_size,
                        size_t n_planes, uint64_t *out_off);

// Stamp magic/version/layout_id/plane table into base, then the block_hash over the whole
// block with that field zeroed. 0 / -1.
int res_block_seal(void *base, size_t size, uint32_t magic, uint32_t version,
                   uint64_t layout_id, size_t n_planes,
                   const uint64_t *off, const uint64_t *len);

typedef struct res_block_view {
    const res_block_hdr *hdr;
    const void *plane[RES_BLOCK_PLANES_MAX];
    uint64_t    count[RES_BLOCK_PLANES_MAX];
    size_t      size;
} res_block_view;

// THE ONE hostile-input gate: magic / version / layout_id / block_hash / plane extents /
// grain alignment / count*elem overflow. Extensions add ONE cross-reference pass on top,
// at LOAD/ADOPT time, never per-view. 0 / -1 (out is zeroed on refusal).
int res_block_open(const void *bytes, size_t len, uint32_t magic, uint32_t version,
                   uint64_t layout_id, res_block_view *out);

#endif // ANOPTIC_RESOURCES_BLOCK_H
