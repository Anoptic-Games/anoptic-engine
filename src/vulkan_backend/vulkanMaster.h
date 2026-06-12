/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef VULKANMASTER_H
#define VULKANMASTER_H


#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"

#include "vulkan_backend/instance/instanceInit.h"

#include "vulkan_backend/structs.h"

#include "vulkan_backend/instance/pipeline.h"

#include "vulkan_backend/vulkanConfig.h"

#include "vulkan_backend/texture/texture.h"

#include "render/gltf/ano_GltfParser.h"
#include "render/text/ano_RenderText.h"


// Function interfaces

// Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
bool initVulkan(); // Move to includes

// A celebration
void unInitVulkan(); // Move to includes

// Draws a single frame

void drawFrame(); // Move to includes

// Returns whether the program has been requested to exit

bool anoShouldClose(); // Move to includes

void drawChar(FT_ULong glyph_number);

#endif
