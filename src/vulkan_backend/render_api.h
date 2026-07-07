/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Public render API surface implemented in render_api.c, plus the render-internal scene-asset loader.

#ifndef ANO_RENDER_API_H
#define ANO_RENDER_API_H

#include <stdbool.h>

// Parse + register the scene's glTF assets into the loaded-asset registry. false on fatal parse failure.
bool ano_render_load_scene_assets(void);

#endif // ANO_RENDER_API_H
