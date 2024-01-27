/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Provides structures and function interfaces for loading glTF assets */

#ifndef FT_FREETYPE_H
#include <ft2build.h>
#include <stb_image_write.h>
#include "vulkan_backend/structs.h"
#include FT_FREETYPE_H


// Types

typedef struct BMPtexel
{
	unsigned char r;
	unsigned char g;
	unsigned char b;
} BMPtexel;

// To be expanded with kerning information, etc.
typedef struct CharPattern
{
	uint16_t startWidth;
	uint16_t startHeight;
	uint16_t endWidth;
	uint16_t endHeight;
} CharPattern;

// Holds all data pertinent to atlas access
typedef struct CharAtlas
{
	BMPtexel* texels;
	uint32_t width;
	uint32_t height;
	CharPattern* patterns;
	uint32_t patternCount; // This also doubles as a counter for the number of glyphs in the atlas
} CharAtlas;


// Functions

int ft_init();
void ft_add_font(char* file_path, int face_index);
int ft_load_glyph_bitmap(FT_ULong glyph);
FT_Bitmap* ft_get_glyph_bitmap(FT_ULong glyph);
int ft_render_glyph_atlas(CharAtlas* atlas_buffer, FT_ULong start, FT_ULong end);

int ft_debug_save_glyph(char* filename, FT_ULong glyph);
int ft_debug_save_glyph_atlas(char* filename, FT_ULong start, FT_ULong end);
#endif
