/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// The font extension. STUB.
//
// TODO(W11, M18): the font bake becomes a plane-set block, and FT_New_Face disappears from
// src/ -- ano_text_font_load / _lit are DELETED and anotest_text.c is rewritten onto
// ano_res_get + a HELD read scope + ano_text_font_load_memory IN THE SAME COMMIT. FreeType
// BORROWS the blob, so an early read_end presents as a FreeType crash rather than a resource
// error: ASan/TSan are mandatory afterwards.

#include "../resources_ext.h"
#include "../resources_internal.h"

// The 'FONT' extension descriptor. TODO(W11, M18): register it, and give it a validate().
uint32_t res_font_tag(void)
{
    return RES_TAG_FONT;
}
