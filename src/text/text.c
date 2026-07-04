/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Text implementation: module lifetime + the FreeType backend it wraps.
// FreeType is created through FT_New_Library with custom FT_Memory hooks so every
// parser allocation lands in the module's mimalloc heap (single-writer: the init
// thread), never the global allocator. Design of record: FONT_RENDER.md.

#include "anoptic_text.h"
#include "text/text_internal.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "anoptic_logging.h"
#include "anoptic_memory.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_SYSTEM_H
#include FT_ERRORS_H

#define ANO_TEXT_MAX_FONTS 8u

static mi_heap_t           *g_textHeap;  // owns every FreeType allocation
static struct FT_MemoryRec_ g_ftMemory;  // hook table handed to FT_New_Library
static FT_Library           g_ftLibrary; // non-NULL <=> module initialized
static FT_Face              g_faces[ANO_TEXT_MAX_FONTS]; // slot i <-> AnoFontId i+1

// FT_Alloc_Func: route FreeType's malloc into the module heap.
static void *text_ft_alloc(FT_Memory memory, long size)
{
    return mi_heap_malloc(memory->user, (size_t)size);
}

// FT_Free_Func: mimalloc resolves the owning heap from the block itself.
static void text_ft_free(FT_Memory memory, void *block)
{
    (void)memory;
    mi_free(block);
}

// FT_Realloc_Func: cur_size is FreeType-side bookkeeping; mimalloc tracks sizes itself.
static void *text_ft_realloc(FT_Memory memory, long cur_size, long new_size, void *block)
{
    (void)cur_size;
    return mi_heap_realloc(memory->user, block, (size_t)new_size);
}

// Creates the module heap and a FreeType library routed through it, then registers the
// default font-format modules (FT_New_Library starts empty, unlike FT_Init_FreeType).
// Returns 0 on success, ENOMEM/EIO on failure; idempotent when already initialized.
// Invariant: the calling thread becomes the module's owner thread for all later calls.
int ano_text_init(void)
{
    if (g_ftLibrary != NULL)
        return 0;

    g_textHeap = mi_heap_new();
    if (g_textHeap == NULL)
        return ENOMEM;

    g_ftMemory.user    = g_textHeap;
    g_ftMemory.alloc   = text_ft_alloc;
    g_ftMemory.free    = text_ft_free;
    g_ftMemory.realloc = text_ft_realloc;

    FT_Error err = FT_New_Library(&g_ftMemory, &g_ftLibrary);
    if (err != FT_Err_Ok)
    {
        const char *msg = FT_Error_String(err);
        ano_log(ANO_ERROR, "text: FT_New_Library failed: %d (%s)", (int)err, msg ? msg : "?");
        g_ftLibrary = NULL;
        mi_heap_destroy(g_textHeap);
        g_textHeap = NULL;
        return EIO;
    }
    FT_Add_Default_Modules(g_ftLibrary);
    FT_Set_Default_Properties(g_ftLibrary); // honors FREETYPE_PROPERTIES, FT_Init parity

    FT_Int maj = 0, min = 0, pat = 0;
    FT_Library_Version(g_ftLibrary, &maj, &min, &pat);
    ano_log(ANO_INFO, "text: FreeType %d.%d.%d ready (module-heap backed)", maj, min, pat);
    return 0;
}

// Destroys faces then the FreeType library (clean teardown through the hooks), then
// the module heap -- a wholesale page release; any straggler allocation dies with it.
// Must run on the init thread.
void ano_text_shutdown(void)
{
    for (uint32_t i = 0; i < ANO_TEXT_MAX_FONTS; i++)
    {
        if (g_faces[i] != NULL)
        {
            FT_Done_Face(g_faces[i]);
            g_faces[i] = NULL;
        }
    }
    if (g_ftLibrary != NULL)
    {
        FT_Done_Library(g_ftLibrary);
        g_ftLibrary = NULL;
    }
    if (g_textHeap != NULL)
    {
        mi_heap_destroy(g_textHeap);
        g_textHeap = NULL;
    }
}

// Outputs the linked FreeType version through non-NULL pointers; zeros before init.
void ano_text_version(int *major, int *minor, int *patch)
{
    FT_Int maj = 0, min = 0, pat = 0;
    if (g_ftLibrary != NULL)
        FT_Library_Version(g_ftLibrary, &maj, &min, &pat);
    if (major != NULL)
        *major = maj;
    if (minor != NULL)
        *minor = min;
    if (patch != NULL)
        *patch = pat;
}

// Opens a scalable face into the first free registry slot. Returns its 1-based handle;
// 0 on any failure (module down, bad path, bitmap-only face, registry full).
// Invariant: must run on the init thread (FreeType allocates through the module heap).
AnoFontId ano_text_font_load(const char *path)
{
    if (g_ftLibrary == NULL || path == NULL)
        return 0;

    uint32_t slot = ANO_TEXT_MAX_FONTS;
    for (uint32_t i = 0; i < ANO_TEXT_MAX_FONTS; i++)
    {
        if (g_faces[i] == NULL)
        {
            slot = i;
            break;
        }
    }
    if (slot == ANO_TEXT_MAX_FONTS)
    {
        ano_log(ANO_ERROR, "text: font registry full (%u faces), cannot load '%s'",
                      ANO_TEXT_MAX_FONTS, path);
        return 0;
    }

    FT_Face  face = NULL;
    FT_Error err  = FT_New_Face(g_ftLibrary, path, 0, &face);
    if (err != FT_Err_Ok)
    {
        const char *msg = FT_Error_String(err);
        ano_log(ANO_ERROR, "text: FT_New_Face('%s') failed: %d (%s)", path, (int)err,
                      msg ? msg : "?");
        return 0;
    }
    if (!FT_IS_SCALABLE(face))
    {
        // The bake path consumes outlines only; bitmap strikes have no curves.
        ano_log(ANO_ERROR, "text: '%s' is not a scalable outline face", path);
        FT_Done_Face(face);
        return 0;
    }

    g_faces[slot] = face;
    ano_log(ANO_INFO, "text: loaded '%s' (%ld glyphs, upem %u)", path, (long)face->num_glyphs,
                 (unsigned)face->units_per_EM);
    return slot + 1u;
}

// Internal: the FT_Face behind a handle as an opaque pointer; NULL when invalid.
void *ano_text_face(AnoFontId font)
{
    if (font == 0 || font > ANO_TEXT_MAX_FONTS)
        return NULL;
    return g_faces[font - 1u];
}

// Internal ground truth for the reference rasterizer: FreeType's own smooth AA render
// (linear 256-level coverage, unhinted) copied tightly into buf. Sets pixel sizes on
// the face; the bake path is unaffected (it loads FT_LOAD_NO_SCALE). Module thread.
int ano_text_ref_ft_render(AnoFontId font, uint32_t codepoint, uint32_t pixelsPerEm,
                           uint8_t *buf, uint32_t cap, int *width, int *rows,
                           int *left, int *top)
{
    FT_Face face = ano_text_face(font);
    if (face == NULL || buf == NULL || width == NULL || rows == NULL || left == NULL
        || top == NULL || pixelsPerEm == 0)
        return EINVAL;
    if (FT_Set_Pixel_Sizes(face, 0, pixelsPerEm) != FT_Err_Ok)
        return EIO;
    if (FT_Load_Char(face, codepoint, FT_LOAD_NO_HINTING | FT_LOAD_RENDER) != FT_Err_Ok)
        return EIO;
    FT_GlyphSlot slot = face->glyph;
    if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
        return EIO;

    uint32_t bw = slot->bitmap.width, br = slot->bitmap.rows;
    if ((uint64_t)bw * br > cap)
        return ENOMEM;
    // buffer points at the top-left byte; pitch (either sign) steps one row down.
    for (uint32_t r = 0; r < br; r++)
        memcpy(buf + (size_t)r * bw,
               slot->bitmap.buffer + (ptrdiff_t)r * slot->bitmap.pitch, bw);

    *width = (int)bw;
    *rows  = (int)br;
    *left  = slot->bitmap_left;
    *top   = slot->bitmap_top;
    return 0;
}
