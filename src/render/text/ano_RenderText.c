#include "ano_RenderText.h"

#ifndef FT_FREETYPE_H
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

FT_Library FT_ano_Library;
FT_Face face; //TODO:Make this take multiple fonts.

int ft_init()
{
    if (FT_Init_FreeType(&FT_ano_Library))
    {
        printf("Text library initialization failed!");
    }
}

void ft_add_font(char* file_path, int face_index)
{
    FT_Error error = FT_New_Face(FT_ano_Library, file_path, face_index, &face);

    if (error == FT_Err_Unknown_File_Format)
    {
        printf("Tried to parse an unsupported file format!");
    }
    else if (error)
    {
        printf("Unknown error when parsing font, try and figure out which error %i is.", error);
    }
}

void ft_load_glyph_bitmap(int glyph_index)
{
    if (FT_Load_Glyph(face, glyph_index, 0))
    {
        printf("Error when loading glyph with index: %i", glyph_index);
    }

    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) //TODO: Store glyph data somewhere.
    {
        printf("Error when rendering glyph with index: %i", glyph_index);
    }
}