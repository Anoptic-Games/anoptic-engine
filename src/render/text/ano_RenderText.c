#define STB_IMAGE_WRITE_IMPLEMENTATION


#include "ano_RenderText.h"

#include <stdint.h>

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

FT_Bitmap* ft_get_glyph_bitmap(FT_ULong glyph)
{
    ft_load_glyph_bitmap(glyph);

    return &(face->glyph->bitmap);
}

int ft_debug_save_glyph(char* filename, FT_ULong glyph)
{
    FT_Bitmap* glyph_bitmap = ft_get_glyph_bitmap(glyph);
    uint32_t pixel_count = (glyph_bitmap->rows * glyph_bitmap->width * 3);
    unsigned char expanded_glyph_buffer[pixel_count];
    for (int i = 0; i < pixel_count; i++ )
    {
       expanded_glyph_buffer[i] = (glyph_bitmap->buffer)[i/3];
    }
    
    return stbi_write_bmp(filename, (int)glyph_bitmap->width, (int)glyph_bitmap->rows, 3, expanded_glyph_buffer);
}

int ft_debug_save_glyph_atlas(char* filename, FT_ULong start, FT_ULong end)
{
    uint32_t max_glyph_width = 0, max_glyph_height = 0;
    uint32_t glyph_count = (end - start) + 1;
    uint32_t atlas_dimensions = 1;

    while ((atlas_dimensions * atlas_dimensions) < glyph_count)
    {
        atlas_dimensions++;
    }

    for (int i = 0; i < glyph_count; i ++)
    {
        FT_Bitmap* glyph = ft_get_glyph_bitmap(start + i);
        max_glyph_width = glyph->width > max_glyph_width ? glyph->width: max_glyph_width;
        max_glyph_height = glyph->rows > max_glyph_height ? glyph->rows: max_glyph_height;
    }

    uint32_t atlas_width = atlas_dimensions * max_glyph_width;
    uint32_t atlas_height = atlas_dimensions * max_glyph_height;
    unsigned char atlas_buffer[atlas_width * atlas_height * 3];
    for (int i = 0; i < atlas_width * atlas_height * 3; i++)
    {
        atlas_buffer[i] = 0;
    }

    for (int i = 0; i < glyph_count; i++)
    {
        uint32_t x_pos = (i % atlas_dimensions) * max_glyph_width;
        uint32_t y_pos = (i / atlas_dimensions) * max_glyph_height;
        FT_Bitmap* glyph_bitmap = ft_get_glyph_bitmap(start + i);

        for (int y = 0; y < glyph_bitmap->rows; y++)
        {
            for (int x = 0; x < glyph_bitmap->width; x++)
            {
                uint32_t target_position = (x_pos + x + ((y + y_pos) * atlas_width)) * 3;
                atlas_buffer[target_position] = glyph_bitmap->buffer[x + (y * glyph_bitmap->width)];
                atlas_buffer[target_position+1] = glyph_bitmap->buffer[x + (y * glyph_bitmap->width)];
                atlas_buffer[target_position+2] = glyph_bitmap->buffer[x + (y * glyph_bitmap->width)];
            }
        }
    }

    return stbi_write_bmp(filename, (int)atlas_width, (int)atlas_height, 3, atlas_buffer);
}
