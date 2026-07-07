/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Public render API surface implemented in render_api.c: the loaded-asset registry + anoRenderAsset*
// query API, anoRenderTextBake, and the runtime knob setters/getters (all declared in the public
// anoptic_render.h). Plus the render-internal scene-asset loader hoisted from initVulkan.

#ifndef ANO_RENDER_API_H
#define ANO_RENDER_API_H

#include <stdbool.h>

// Parse + register the scene's glTF assets into the loaded-asset registry (hoisted from initVulkan;
// uses the file-global ctx). false on a fatal asset-parse failure (having already run unInitVulkan).
bool ano_render_load_scene_assets(void);

#endif // ANO_RENDER_API_H
