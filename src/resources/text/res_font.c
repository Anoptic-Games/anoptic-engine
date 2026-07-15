/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Font extension. STUB. FONT tag lives under graphics classify today.
// Callers use ano_res_get + held read scope + ano_text_font_load_memory. FreeType BORROWS the blob.

#include "../resources_ext.h"
#include "../resources_internal.h"

uint32_t res_font_tag(void)
{
    return RES_TAG_FONT;
}
