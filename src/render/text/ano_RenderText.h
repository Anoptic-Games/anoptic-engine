#ifndef FT_FREETYPE_H
#include <ft2build.h>
#include <stb_image_write.h>
#include FT_FREETYPE_H
#endif

#define max_glyph_width 14
#define max_glyph_height 18
#define glyph_per_atlas_row 16

int ft_init();
void ft_add_font(char* file_path, int face_index);
int ft_load_glyph_bitmap(FT_ULong glyph);
FT_Bitmap* ft_get_glyph_bitmap(FT_ULong glyph);
#ifdef DEBUG_BUILD

int ft_debug_save_glyph(char* filename, FT_ULong glyph);
int ft_debug_save_glyph_atlas(char* filename, FT_ULong start, FT_ULong end);
#endif
