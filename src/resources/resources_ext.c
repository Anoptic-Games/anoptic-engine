/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The extension registry: MEANING (blueprint 2.1, M2).
//
// A kind is a FOURCC tag, interned to a dense id by res_ext_freeze(), which SORTS the
// registered kind set by fourcc so the dense id is a deterministic function of the tag SET
// and never of registration order (D17). The core's builtin kind, RES_TAG_BYTES, is dense
// id 0 by construction: it lives outside the sorted table, and 0 doubles as "unknown".
// A dense id never reaches disk, a pack TOC, or a bake header -- fourcc only.
//
// res_tag_from_path is a walk over the registered extensions' classify(): the last suffix
// switch in the core is gone, and adding a resource class edits zero tables here.
//
// Owner-thread-only by construction: registration and freeze happen inside
// res_registry_init, before any consumer can route a plan.

#include "resources_ext.h"

#include <anoptic_log.h>

#include <string.h>

// One interned kind: its tag, its flags, and the extension that owns it.
typedef struct ext_kind_row {
    uint32_t tag;
    bool     derived;
    bool     bakeable;
    uint8_t  ext;           // index into g_ext.exts
} ext_kind_row;

static struct {
    const res_ext *exts[RES_EXT_MAX];
    size_t         ext_count;
    ext_kind_row   kinds[RES_KIND_MAX];    // sorted by tag after freeze; ids are index+1
    size_t         kind_count;
    bool           frozen;
} g_ext;

// Inputs: one extension descriptor with a static-lifetime kind table. Output: 0, or -1 on
// a full roster, a post-freeze call, or a reserved/duplicate tag. Invariant: owner thread,
// before res_ext_freeze().
int res_ext_register(const res_ext *ext)
{
    if (ext == NULL || ext->kinds == NULL || ext->kind_count == 0 || g_ext.frozen)
        return -1;
    if (g_ext.ext_count >= RES_EXT_MAX
        || g_ext.kind_count + ext->kind_count > RES_KIND_MAX - 1)   // id 0 is BYTS
        return -1;
    for (size_t k = 0; k < ext->kind_count; k++) {
        uint32_t tag = ext->kinds[k].tag;
        if (tag == 0 || tag == RES_TAG_BYTES) {
            ano_log(ANO_ERROR, "resources: extension '%s' claims a reserved kind tag",
                    ext->name ? ext->name : "?");
            return -1;
        }
        for (size_t i = 0; i < g_ext.kind_count; i++)
            if (g_ext.kinds[i].tag == tag) {
                ano_log(ANO_ERROR, "resources: duplicate kind tag 0x%08x ('%s')",
                        tag, ext->name ? ext->name : "?");
                return -1;
            }
        for (size_t i = 0; i < k; i++)
            if (ext->kinds[i].tag == tag) {
                ano_log(ANO_ERROR, "resources: extension '%s' repeats kind tag 0x%08x",
                        ext->name ? ext->name : "?", tag);
                return -1;
            }
    }
    uint8_t ei = (uint8_t)g_ext.ext_count;
    g_ext.exts[g_ext.ext_count++] = ext;
    for (size_t k = 0; k < ext->kind_count; k++)
        g_ext.kinds[g_ext.kind_count++] = (ext_kind_row){
            .tag = ext->kinds[k].tag,
            .derived = ext->kinds[k].derived,
            .bakeable = ext->kinds[k].bakeable,
            .ext = ei,
        };
    return 0;
}

// Inputs: none. Output: none. Invariant: after this, dense id k (k >= 1) is the (k-1)th
// smallest registered fourcc -- a function of the tag SET alone (D17).
void res_ext_freeze(void)
{
    // Insertion sort by fourcc: the table is tiny and already-registered order is noise.
    for (size_t i = 1; i < g_ext.kind_count; i++) {
        ext_kind_row row = g_ext.kinds[i];
        size_t j = i;
        while (j > 0 && g_ext.kinds[j - 1].tag > row.tag) {
            g_ext.kinds[j] = g_ext.kinds[j - 1];
            j--;
        }
        g_ext.kinds[j] = row;
    }
    g_ext.frozen = true;
}

void res_ext_reset(void)
{
    g_ext = (typeof(g_ext)){0};
}

// Inputs: a fourcc tag. Output: the dense id, 0 for RES_TAG_BYTES and for any unknown tag.
uint16_t res_kind_of(uint32_t tag)
{
    if (tag == RES_TAG_BYTES)
        return 0;
    size_t lo = 0, hi = g_ext.kind_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_ext.kinds[mid].tag < tag)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < g_ext.kind_count && g_ext.kinds[lo].tag == tag ? (uint16_t)(lo + 1) : 0;
}

uint32_t res_tag_of(uint16_t kind)
{
    if (kind == 0 || kind > g_ext.kind_count)
        return RES_TAG_BYTES;
    return g_ext.kinds[kind - 1].tag;
}

bool res_kind_derived(uint16_t kind)
{
    return kind != 0 && kind <= g_ext.kind_count && g_ext.kinds[kind - 1].derived;
}

bool res_kind_bakeable(uint16_t kind)
{
    return kind != 0 && kind <= g_ext.kind_count && g_ext.kinds[kind - 1].bakeable;
}

const res_ext *res_ext_of_tag(uint32_t tag)
{
    uint16_t kind = res_kind_of(tag);
    return kind == 0 ? NULL : g_ext.exts[g_ext.kinds[kind - 1].ext];
}

// The classify() walk that replaced kind_from_path's suffix switch. First claim wins, in
// registration order; RES_TAG_BYTES when nobody claims it.
uint32_t res_tag_from_path(const char *logical, size_t len)
{
    if (logical == NULL || len == 0)
        return RES_TAG_BYTES;
    for (size_t e = 0; e < g_ext.ext_count; e++) {
        if (g_ext.exts[e]->classify == NULL)
            continue;
        uint32_t tag = g_ext.exts[e]->classify(logical, len);
        if (tag != 0)
            return tag;
    }
    return RES_TAG_BYTES;
}
