#ifndef FT_FREETYPE_H
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

int ft_init();
void ft_add_font(char* file_path, int face_index);
int ft_load_glyph_bitmap(FT_ULong glyph);
FT_Bitmap* ft_get_glyph_bitmap(FT_ULong glyph);
