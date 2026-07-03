/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Anoptic Text API
//
// Font loading, glyph-curve baking, and minimal text shaping for the GPU text stack
// (Scanline Sweeper coverage rasterization -- design of record: FONT_RENDER.md).
// FreeType backs the font parsing internally; no FreeType type crosses this header.
//
// Threading: the module owns a private mimalloc heap (single-writer). ano_text_init,
// all font loading/baking, and ano_text_shutdown must run on the SAME thread. Baked
// blobs and shaped output are plain data, readable from any thread once produced.

#ifndef ANOPTICENGINE_ANOPTIC_TEXT_H
#define ANOPTICENGINE_ANOPTIC_TEXT_H

// Initializes the text module: private allocation heap + font parser backend.
// Returns 0 on success, a positive errno-style code on failure. Idempotent.
int ano_text_init(void);

// Tears down all module state, including every loaded font. Safe when never initialized.
void ano_text_shutdown(void);

// Reports the backing font-parser (FreeType) version for startup logs and tests.
// Outputs: major/minor/patch through any non-NULL pointers; all zeros before init.
void ano_text_version(int *major, int *minor, int *patch);

#endif
