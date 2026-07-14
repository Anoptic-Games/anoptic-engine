/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The one block framing. STUB.
//
// TODO(W5, M11): res_plane_layout (PURE -- the bake, the pack, and the hostile validator all
// depend on exactly this function), res_block_seal, and res_block_open: magic / version /
// layout_id / block_hash / plane extents / grain alignment / count*elem overflow.
//
// res_block_open is the single most fuzz-worthy function in the module. The hostile battery
// (truncated, bad magic, bad version, bad layout_id, hash mismatch, offsets past end,
// count*sizeof overflow, and every INTERIOR index -- prim.material, node.mesh, node.parent,
// child spans, children[i], roots[i], indices[i]) lands with it, under ASan. Today
// ano_resgfx_scene bounds-checks array EXTENTS and validates NOTHING INSIDE THEM: safe only
// because cgltf constructs the indices. A baked block from a pack turns every one of them
// into an out-of-bounds read primitive handed straight to the renderer.

#include "resources_block.h"

#include <string.h>

size_t res_plane_layout(size_t hdr_bytes, const size_t *count, const size_t *elem_size,
                        size_t n_planes, uint64_t *out_off)
{
    (void)hdr_bytes; (void)count; (void)elem_size; (void)n_planes; (void)out_off;
    return 0;                                   // TODO(W5, M11)
}

int res_block_seal(void *base, size_t size, uint32_t magic, uint32_t version,
                   uint64_t layout_id, size_t n_planes,
                   const uint64_t *off, const uint64_t *len)
{
    (void)base; (void)size; (void)magic; (void)version;
    (void)layout_id; (void)n_planes; (void)off; (void)len;
    return -1;                                  // TODO(W5, M11)
}

int res_block_open(const void *bytes, size_t len, uint32_t magic, uint32_t version,
                   uint64_t layout_id, res_block_view *out)
{
    (void)bytes; (void)len; (void)magic; (void)version; (void)layout_id;
    if (out != NULL)
        memset(out, 0, sizeof *out);            // a refusal is always a ZEROED view
    return -1;                                  // TODO(W5, M11)
}
