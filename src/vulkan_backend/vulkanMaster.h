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


// Function interfaces

extern RendererState rendererState;

extern uint32_t g_ValidationErrors;

// Initializes Vulkan, returns a pointer to VulkanComponents, or NULL on failure
bool initVulkan(); // Move to includes

// A celebration
void unInitVulkan(); // Move to includes

// Draws a single frame

void drawFrame(); // Move to includes

// Returns whether the program has been requested to exit
bool anoShouldClose();

// --- Render world / logic producer ------------------------------------------
// GLFW pins window + event handling to the process main thread (mandatory on
// macOS), so the render world (all Vulkan + GLFW: initVulkan, the frame loop,
// unInitVulkan) runs directly on the main thread — see main(). The logic/ECS
// master runs on its own thread as the sole command producer and coordinates
// purely through the lock-free bridge below (no shared mutex).

// Producer endpoint for submitting RenderCommands. Valid once initVulkan() has returned.
AnoRenderBridge* anoRenderBridge(void);

// render_id 0's original geometry index (stand-in producer helper). Read after init.
uint32_t anoRenderEntity0Mesh(void);

void deferred_delete_resource(RendererState* state, DeletionResourceType type, uint32_t handle);
void flush_deletion_queue(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);

extern PFN_vkCmdDrawMeshTasksEXT pfnVkCmdDrawMeshTasksEXT;
extern PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT;
extern PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT;

#endif
