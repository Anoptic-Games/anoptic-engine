/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef VULKANMASTER_H
#define VULKANMASTER_H


#include <vulkan/vulkan.h>

#include <anoptic_render.h> // public engine<->renderer contract (lifecycle + command protocol)

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

// --- Render world / logic producer ------------------------------------------
// The public engine<->renderer contract — initVulkan / unInitVulkan ("a celebration") /
// drawFrame / anoShouldClose, the opaque AnoRenderBridge, anoRenderBridge(), the asset-query API
// (anoRenderAssetPrimitives etc.) and ano_render_submit() — is declared in <anoptic_render.h>
// (included above), and defined here in vulkanMaster.c.
// GLFW pins window + event handling to the process main thread (mandatory on macOS),
// so the render world (all Vulkan + GLFW: initVulkan, the frame loop, unInitVulkan)
// runs directly on the main thread — see main(). The logic/ECS master runs on its own
// thread as the sole command producer and coordinates purely through the lock-free
// bridge (no shared mutex).

void deferred_delete_resource(RendererState* state, DeletionResourceType type, uint32_t handle);
void flush_deletion_queue(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);

extern PFN_vkCmdDrawMeshTasksEXT pfnVkCmdDrawMeshTasksEXT;
extern PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT;
extern PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT;

#endif
