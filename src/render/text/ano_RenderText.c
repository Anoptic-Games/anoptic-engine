#include "ano_RenderText.h"

FT_Library FT_ano_Library;
FT_Face face; //TODO:Make this take multiple fonts.

int ft_init()
{
    if (FT_Init_FreeType(&FT_ano_Library))
    {
        printf("Text library initialization failed!\n");
        return 0;
    }
    
    printf("FreeType successfully initialized!\n");
    return 1;
}

void ft_add_font(char* file_path, int face_index)
{
    FT_Error error = FT_New_Face(FT_ano_Library, file_path, face_index, &face);

    if (error == FT_Err_Unknown_File_Format)
    {
        printf("Tried to parse an unsupported file format!\n");
    }
    else if (error)
    {
        printf("Unknown error when parsing font, try and figure out which error %i is.\n", error);
    }
    else
    {
        printf("Successfully initialized font: %s\n", file_path);
    }
}

int ft_load_glyph_bitmap(FT_ULong glyph)
{
    FT_UInt glyph_index =  FT_Get_Char_Index(face, glyph);

    if (FT_Load_Glyph(face, glyph_index, 0))
    {
        printf("Error when loading glyph with index: %i\n", glyph_index);
        return 0;
    }

    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) //TODO: Store glyph data somewhere.
    {
        printf("Error when rendering glyph with index: %i\n", glyph_index);
        return 0;
    }

    return 1;
}

FT_Bitmap ft_get_current_bitmap()
{
    return face->glyph->bitmap;
}

void ft_upload_current_glyph_to_GPU()
{
    Texture8 texture = {};
    FT_Bitmap bitmap = ft_get_current_bitmap();
    texture.mipLevels = 0;
    texture.texChannels = 1;
    texture.pixels = bitmap.buffer;
    texture.texWidth = bitmap.width;
    texture.texHeight = bitmap.rows;

    //createTextureImageFromCPUMemory();
}