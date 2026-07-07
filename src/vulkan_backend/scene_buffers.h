/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Slot-indexed / palette scene GPU buffers + fallback resources.

#ifndef ANO_SCENE_BUFFERS_H
#define ANO_SCENE_BUFFERS_H

#include <stdbool.h>

#include "vulkan_backend/structs.h"

// Create every slot-indexed / palette scene buffer + shadow resources. false on any failure.
bool ano_vk_create_scene_resources(void);
// Fallback mesh/material/texture bound when a renderable names none.
bool createFallbackResources(VulkanContext* ctx, RendererState* state);

#endif // ANO_SCENE_BUFFERS_H
