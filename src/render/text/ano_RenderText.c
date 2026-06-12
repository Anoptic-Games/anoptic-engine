/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * Text Rendering Module (DISABLED - pending rewrite)
 *
 * Rewrite plan:
 *
 *   1. FreeType init/shutdown with a multi-face font registry.
 *      Previous code used a single global FT_Face.
 *
 *   2. Glyph rasterization with explicit FT_Set_Pixel_Sizes call
 *      (the old code never set a size). Respect bitmap pitch
 *      rather than assuming pitch == width.
 *
 *   3. Atlas packing into a heap-allocated buffer. The old code
 *      used a stack VLA (easily >1MB for 256 glyphs). Use shelf
 *      or skyline packing instead of a uniform grid.
 *
 *   4. GPU upload via a corrected staging path in texture.c.
 *      The removed createTextureImageFromCPUMemory() had these bugs:
 *        - imageSize = w * h * 4 for single-channel R8 data (4x overread)
 *        - stbi_image_free() called on FreeType's internal bitmap buffer
 *        - copyBufferToImage(... texWidth, texWidth) passed width as height
 *        - VkImage created without TRANSFER_SRC, breaking mipmap blits
 *        - VK_FORMAT_R8_SRGB has near-zero device support; use R8_UNORM
 *      When reimplementing, size the staging buffer to w * h * channels,
 *      copy the bitmap yourself (don't hand FT memory to stbi_image_free),
 *      and add TRANSFER_SRC to image usage flags if generating mipmaps.
 *
 *   5. SDF rendering for resolution-independent text. The
 *      feature-render-text branch has prior work on this
 *      (glyph atlas structs, VRAM uploads, SDF pipeline).
 *
 * The FreeType submodule and stb_image_write.h are still in the tree.
 */
