/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANO_RENDERTEXT_H
#define ANO_RENDERTEXT_H

/* Text rendering interface (DISABLED - pending rewrite)
 *
 * Inputs:  font file path, glyph codepoint(s), pixel size
 * Outputs: GPU-resident texture atlas, glyph metric lookup table
 *
 * Expected interface (approximate):
 *
 *   int ano_text_init(void);
 *   void ano_text_shutdown(void);
 *   int ano_text_add_font(const char* path, int face_index, uint32_t pixel_size);
 *   int ano_text_build_atlas(FT_ULong first, FT_ULong last);
 *
 * GPU upload will need a corrected createTextureImageFromCPUMemory()
 * back in texture.c. See ano_RenderText.c for the full rewrite plan.
 *
 * See also: feature-render-text branch for SDF rendering prior work.
 */

#endif
