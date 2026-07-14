/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

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
}

void res_ext_reset(void)
{
                                                // TODO(W2, M2)
}

uint16_t res_kind_of(uint32_t tag)
{
    return tag == RES_TAG_BYTES ? 0u : 0u;      // TODO(W2, M2): dense id from the sorted table
}

uint32_t res_tag_of(uint16_t kind)
{
    (void)kind;
    return RES_TAG_BYTES;                       // TODO(W2, M2)
}

bool res_kind_derived(uint16_t kind)
{
    (void)kind;
    return false;                               // TODO(W2, M2)
}

bool res_kind_bakeable(uint16_t kind)
{
    (void)kind;
    return false;                               // TODO(W2, M2)
}

const res_ext *res_ext_of_tag(uint32_t tag)
{
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
    return RES_TAG_BYTES;
}
