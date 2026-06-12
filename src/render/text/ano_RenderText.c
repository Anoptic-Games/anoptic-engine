/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * Text Rendering Module (DISABLED - pending rewrite)
 *
 * Plan:
 *   1. FreeType init/shutdown, multi-face font registry
 *   2. Glyph rasterization with explicit FT_Set_Pixel_Sizes,
 *      respecting bitmap pitch (not assuming pitch == width)
 *   3. Tight atlas packing (shelf or skyline, not uniform grid)
 *      into a heap-allocated buffer (not stack VLA)
 *   4. GPU upload via a correct staging path:
 *      - R8_UNORM format (not R8_SRGB, near-zero device support)
 *      - imageSize = width * height * 1 (not * 4)
 *      - image created with TRANSFER_SRC for mipmap blits
 *      - copyBufferToImage height != width
 *      - no stbi_image_free on FreeType-owned memory
 *   5. SDF rendering (see feature-render-text branch for prior work)
 *
 * Previous implementation had 4 bugs in the CPU-to-GPU bridge:
 *   - 4x staging buffer overread (assumed RGBA for single-channel data)
 *   - stbi_image_free called on FreeType's internal bitmap buffer
 *   - height passed as width to copyBufferToImage
 *   - image missing TRANSFER_SRC usage for mipmap generation
 */
