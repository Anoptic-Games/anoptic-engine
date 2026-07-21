/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

<<<<<<< HEAD
// Extension registry: MEANING.
// Kind is a FOURCC, interned to dense id by res_ext_freeze() (sorted by fourcc). Dense id never reaches disk.
// RES_TAG_BYTES is dense id 0. Owner-thread-only: register/freeze inside res_registry_init.

#include "resources_ext.h"

#include <anoptic_log.h>

#include <string.h>

// One interned kind: tag, flags, and owning extension.
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

// Inputs: extension descriptor with static-lifetime kind table. Output: 0, or -1 on full/post-freeze/duplicate. Owner thread, before freeze.
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

// After this, dense id k (k >= 1) is the (k-1)th smallest registered fourcc.
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
=======
// The extension registry: MEANING. STUB, except the path classifier, which had to move here
// the moment res_place_plan started carrying a FOURCC tag.
//
// TODO(W2, M2): res_ext_register / res_ext_freeze (SORTING the kind table by fourcc, so a
// dense id is a function of the tag SET and not of call order) / res_kind_of / res_tag_of.
// res_tag_from_path's switch below is then DELETED and replaced by a walk over the
// registered extensions' classify() -- at which point graphics registers itself and adding
// audio edits zero tables.

#include "resources_ext.h"

#include <string.h>

int res_ext_register(const res_ext *ext)
{
    (void)ext;
    return -1;                                  // TODO(W2, M2)
}

void res_ext_freeze(void)
{
                                                // TODO(W2, M2): sort by fourcc (D17)
>>>>>>> block-b1-base
}

void res_ext_reset(void)
{
<<<<<<< HEAD
    g_ext = (typeof(g_ext)){0};
}

// Inputs: fourcc tag. Output: dense id. 0 for RES_TAG_BYTES and unknown.
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
=======
                                                // TODO(W2, M2)
}

uint16_t res_kind_of(uint32_t tag)
{
    return tag == RES_TAG_BYTES ? 0u : 0u;      // TODO(W2, M2): dense id from the sorted table
>>>>>>> block-b1-base
}

uint32_t res_tag_of(uint16_t kind)
{
<<<<<<< HEAD
    if (kind == 0 || kind > g_ext.kind_count)
        return RES_TAG_BYTES;
    return g_ext.kinds[kind - 1].tag;
=======
    (void)kind;
    return RES_TAG_BYTES;                       // TODO(W2, M2)
>>>>>>> block-b1-base
}

bool res_kind_derived(uint16_t kind)
{
<<<<<<< HEAD
    return kind != 0 && kind <= g_ext.kind_count && g_ext.kinds[kind - 1].derived;
=======
    (void)kind;
    return false;                               // TODO(W2, M2)
>>>>>>> block-b1-base
}

bool res_kind_bakeable(uint16_t kind)
{
<<<<<<< HEAD
    return kind != 0 && kind <= g_ext.kind_count && g_ext.kinds[kind - 1].bakeable;
=======
    (void)kind;
    return false;                               // TODO(W2, M2)
>>>>>>> block-b1-base
}

const res_ext *res_ext_of_tag(uint32_t tag)
{
<<<<<<< HEAD
    uint16_t kind = res_kind_of(tag);
    return kind == 0 ? NULL : g_ext.exts[g_ext.kinds[kind - 1].ext];
}

// classify() walk. First claim wins in registration order. RES_TAG_BYTES when nobody claims it.
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
=======
    (void)tag;
    return NULL;                                // TODO(W2, M2)
}

// Suffix classification, verbatim from the registry's deleted kind_from_path switch.
// TODO(W2, M2): DELETE this and walk the registered extensions' classify() instead. It is
// the last place in the core that knows a file extension means anything.
uint32_t res_tag_from_path(const char *logical, size_t len)
{
    (void)len;
    if (logical == NULL)
        return RES_TAG_BYTES;
    const char *dot = strrchr(logical, '.');
    if (dot == NULL)
        return RES_TAG_BYTES;
    if (strcmp(dot, ".spv") == 0 || strcmp(dot, ".vert") == 0 || strcmp(dot, ".frag") == 0)
        return RES_TAG_SHADER;
    if (strcmp(dot, ".ttf") == 0 || strcmp(dot, ".otf") == 0)
        return RES_TAG_FONT;
    if (strcmp(dot, ".png") == 0 || strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return RES_TAG_IMAGE_ENC;
>>>>>>> block-b1-base
    return RES_TAG_BYTES;
}
