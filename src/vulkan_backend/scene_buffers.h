/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Scene buffer creation: the slot-indexed / palette GPU buffers (material, light, motion, instance,
// stream, transform, indirect, cluster, culling) + fallback resources. See scene_buffers.c.

#ifndef ANO_SCENE_BUFFERS_H
#define ANO_SCENE_BUFFERS_H

#include <stdbool.h>

#include "vulkan_backend/structs.h"

// Create every slot-indexed / palette scene buffer + the shadow resources in one shot (hoisted from
// initVulkan; uses the file-global ctx / rendererState). false on any failure.
bool ano_vk_create_scene_resources(void);
// Fallback mesh/material/texture (bound when a renderable names none). Called from initVulkan.
bool createFallbackResources(VulkanContext* ctx, RendererState* state);

#endif // ANO_SCENE_BUFFERS_H
