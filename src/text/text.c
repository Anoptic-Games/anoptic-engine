/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Text implementation: module lifetime + the FreeType backend it wraps.
// FreeType is created through FT_New_Library with custom FT_Memory hooks so every
// parser allocation lands in the module's mimalloc heap (single-writer: the init
// thread), never the global allocator. Design of record: FONT_RENDER.md.

#include "anoptic_text.h"

#include <errno.h>

#include "anoptic_logging.h"
#include "anoptic_memory.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_SYSTEM_H
#include FT_ERRORS_H

static mi_heap_t           *g_textHeap;  // owns every FreeType allocation
static struct FT_MemoryRec_ g_ftMemory;  // hook table handed to FT_New_Library
static FT_Library           g_ftLibrary; // non-NULL <=> module initialized

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
        ano_log_error("text: FT_New_Library failed: %d (%s)", (int)err, msg ? msg : "?");
        g_ftLibrary = NULL;
        mi_heap_destroy(g_textHeap);
        g_textHeap = NULL;
        return EIO;
    }
    FT_Add_Default_Modules(g_ftLibrary);
    FT_Set_Default_Properties(g_ftLibrary); // honors FREETYPE_PROPERTIES, FT_Init parity

    FT_Int maj = 0, min = 0, pat = 0;
    FT_Library_Version(g_ftLibrary, &maj, &min, &pat);
    ano_log_info("text: FreeType %d.%d.%d ready (module-heap backed)", maj, min, pat);
    return 0;
}

// Destroys the FreeType library first (clean teardown through the hooks), then the
// module heap -- a wholesale page release; any straggler allocation dies with it.
// Must run on the init thread.
void ano_text_shutdown(void)
{
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
