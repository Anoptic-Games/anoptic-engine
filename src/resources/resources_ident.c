/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Identity. A file resource is named by its logical path; a DERIVED or DUPLICATE resource
// has NO STRING KEY AT ALL -- its rid is seeded from the source's rid over a different
// basis, so it cannot be resolved from the filesystem and typed views refuse a kind
// mismatch before dereferencing a byte.
//
// That is what lets the public path alphabet stay untouched: nothing is reserved, no '#'
// and no '@' are banned, and derived-key type confusion is unrepresentable rather than
// merely discouraged (D8).
//
// rid and rid2 are two independent bases over the same bytes: {rid, rid2} is a 128-bit
// identity, and collision refusal at bind time compares {rid, rid2, name_len} without ever
// touching a retired domain's name text (M7).

#include <stdint.h>
#include <string.h>

#include "resources_internal.h"

#define FNV_PRIME  UINT64_C(0x100000001b3)
#define FNV_BASIS  UINT64_C(0xcbf29ce484222325)   // basis A: the canonical FNV-1a-64 offset
#define FNV_BASIS2 UINT64_C(0x9e3779b97f4a7c15)   // basis B
#define FNV_DERIV  UINT64_C(0x2545f4914f6cdd1d)   // basis D: derived identities
#define FNV_DUP    UINT64_C(0x8ebc6af09c88c6e3)   // basis U: duplicate identities

static uint64_t fnv_bytes(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

static uint64_t fnv_u64(uint64_t h, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        h ^= (uint8_t)(v >> (8 * i));
        h *= FNV_PRIME;
    }
    return h;
}

// A rid is never 0: 0 is the sentinel handle's rid and must not be mintable.
static uint64_t nonzero(uint64_t h) { return h ? h : 1u; }

uint64_t res_rid_file(const char *logical, size_t len)
{
    return nonzero(fnv_bytes(FNV_BASIS, logical, len));
}

uint64_t res_rid_file2(const char *logical, size_t len)
{
    return nonzero(fnv_bytes(FNV_BASIS2, logical, len));
}

uint64_t res_rid_derived(uint64_t src_rid, uint32_t kind_tag)
{
    return nonzero(fnv_u64(fnv_u64(FNV_DERIV, src_rid), kind_tag));
}

uint64_t res_rid_duplicate(uint64_t src_rid, uint32_t owner_index)
{
    return nonzero(fnv_u64(fnv_u64(FNV_DUP, src_rid), owner_index));
}
