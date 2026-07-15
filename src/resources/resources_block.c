/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// One block framing.
// res_plane_layout is PURE. Seal enforces the same rules open admits.
// res_block_open validates in order that never reads an unbounded byte. Zero reads past len under ANY input.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "resources_block.h"

#define BLK_FNV_BASIS UINT64_C(0xcbf29ce484222325)
#define BLK_FNV_PRIME UINT64_C(0x00000100000001b3)

// PURE. Deterministic from arguments only.
// In: hdr_bytes, per-plane counts and elem sizes, plane count.
// Out: total block size. out_off[i] gets grain-aligned base. 0 on overflow or n_planes > max.
// Zero-count plane occupies zero bytes and shares the next plane's offset.
size_t res_plane_layout(size_t hdr_bytes, const size_t *count, const size_t *elem_size,
                        size_t n_planes, uint64_t *out_off)
{
    const size_t grain = (size_t)RES_PLANE_GRAIN;
    const size_t pad = grain - 1u;
    size_t total;

    if (n_planes > RES_BLOCK_PLANES_MAX)
        return 0;
    if (n_planes > 0 && (count == NULL || elem_size == NULL || out_off == NULL))
        return 0;
    if (hdr_bytes > SIZE_MAX - pad)
        return 0;

    total = hdr_bytes + pad;
    total &= ~pad;                              // grain is a power of two

    for (size_t i = 0; i < n_planes; i++) {
        size_t bytes;
        size_t rounded;

        if (count[i] != 0 && elem_size[i] > SIZE_MAX / count[i])
            return 0;                           // count*elem wraps
        bytes = count[i] * elem_size[i];
        if (bytes > SIZE_MAX - pad)
            return 0;                           // grain round-up wraps
        rounded = bytes + pad;
        rounded &= ~pad;
        if (total > SIZE_MAX - rounded)
            return 0;                           // offset accumulation wraps

        out_off[i] = (uint64_t)total;
        total += rounded;
    }

    return total;
}

// Stamp magic/version/layout_id/plane table, zero pad/unused, then block_hash over [base,base+size) with hash field zeroed.
// Plane table validated against open's rules first. 0 / -1.
int res_block_seal(void *base, size_t size, uint32_t magic, uint32_t version,
                   uint64_t layout_id, size_t n_planes,
                   const uint64_t *off, const uint64_t *len)
{
    if (base == NULL || ((uintptr_t)base & (_Alignof(res_block_hdr) - 1)) != 0
        || size < sizeof(res_block_hdr) || n_planes > RES_BLOCK_PLANES_MAX
        || (n_planes > 0 && (off == NULL || len == NULL)))
        return -1;

    for (size_t i = 0; i < n_planes; i++) {
        if (off[i] % RES_PLANE_GRAIN != 0
            || off[i] < sizeof(res_block_hdr)
            || off[i] > size
            || (i > 0 && off[i] < off[i - 1]))
            return -1;
    }

    for (size_t i = 0; i < n_planes; i++) {
        uint64_t limit = (i + 1 < n_planes) ? off[i + 1] : (uint64_t)size;
        if (len[i] > limit - off[i])
            return -1;
    }

    res_block_hdr hdr = {0};                     // _pad and unused entries: zero, always
    hdr.magic = magic;
    hdr.version = version;
    hdr.layout_id = layout_id;
    hdr.plane_count = (uint32_t)n_planes;
    for (size_t i = 0; i < n_planes; i++) {
        hdr.off[i] = off[i];
        hdr.len[i] = len[i];
    }
    memcpy(base, &hdr, sizeof hdr);              // base carries no declared type

    uint64_t hash = BLK_FNV_BASIS;
    const uint8_t *bytes = (const uint8_t *)base;
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = (i >= offsetof(res_block_hdr, block_hash)
                        && i < offsetof(res_block_hdr, block_hash) + sizeof hdr.block_hash)
                           ? 0
                           : bytes[i];
        hash ^= byte;
        hash *= BLK_FNV_PRIME;
    }
    memcpy((uint8_t *)base + offsetof(res_block_hdr, block_hash), &hash, sizeof hash);
    return 0;
}

// THE ONE hostile-input gate. In: untrusted bytes, expected identity, out view.
// Never reads outside [bytes, bytes+len). Out: 0 and filled view, or -1 and ZEROED view.
int res_block_open(const void *bytes, size_t len, uint32_t magic, uint32_t version,
                   uint64_t layout_id, res_block_view *out)
{
    if (out != NULL)
        memset(out, 0, sizeof *out);            // a refusal is always a ZEROED view
    // The base must carry the header's own alignment: out->hdr is a TYPED view of these
    // bytes and a skewed base makes every later dereference UB. ABSOLUTE plane alignment is
    // the allocation's contract (manager blocks land on the cache line); the off[] grain
    // rule below is relative to base, exactly as the frozen header states.
    if (out == NULL || bytes == NULL
        || ((uintptr_t)bytes & (_Alignof(res_block_hdr) - 1)) != 0
        || len < sizeof(res_block_hdr))
        return -1;

    res_block_hdr hdr;
    memcpy(&hdr, bytes, sizeof hdr);            // hostile bytes owe no alignment

    if (hdr.magic != magic || hdr.version != version || hdr.layout_id != layout_id)
        return -1;
    if (hdr.plane_count > RES_BLOCK_PLANES_MAX)
        return -1;

    for (uint32_t i = 0; i < hdr.plane_count; i++) {
        if (hdr.off[i] % RES_PLANE_GRAIN != 0
            || hdr.off[i] < sizeof(res_block_hdr)
            || hdr.off[i] > len
            || (i > 0 && hdr.off[i] < hdr.off[i - 1]))
            return -1;
    }

    for (uint32_t i = 0; i < hdr.plane_count; i++) {
        uint64_t limit = (i + 1 < hdr.plane_count) ? hdr.off[i + 1] : (uint64_t)len;
        if (hdr.len[i] > limit - hdr.off[i])    // limit >= off[i] (monotone), cannot wrap
            return -1;
    }

    uint64_t hash = BLK_FNV_BASIS;
    const uint8_t *data = (const uint8_t *)bytes;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = (i >= offsetof(res_block_hdr, block_hash)
                        && i < offsetof(res_block_hdr, block_hash) + sizeof hdr.block_hash)
                           ? 0
                           : data[i];
        hash ^= byte;
        hash *= BLK_FNV_PRIME;
    }
    if (hash != hdr.block_hash)
        return -1;

    out->hdr = (const res_block_hdr *)bytes;
    out->size = len;
    for (uint32_t i = 0; i < hdr.plane_count; i++) {
        out->plane[i] = data + (size_t)hdr.off[i];
        out->count[i] = hdr.len[i];
    }
    return 0;
}
