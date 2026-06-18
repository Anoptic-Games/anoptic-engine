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

// --- Render master thread (the render world runs on its own thread) ----------
// The logic/ECS master thread (main) spawns anoRenderThreadMain via
// ano_thread_create; that thread owns all Vulkan + GLFW (init, frame loop,
// teardown). The logic thread is the sole command producer and coordinates
// purely through these accessors (lock-free; no shared mutex).

// ano_thread_create entry point: runs initVulkan + the frame loop + unInitVulkan.
void* anoRenderThreadMain(void* arg);

// true once init has completed and the bridge is live; false while still booting.
bool anoRenderIsReady(void);

// true if initVulkan failed and the render thread is exiting.
bool anoRenderInitFailed(void);

// true once the user has requested the window be closed.
bool anoRenderWindowWantsClose(void);

// Ask the render thread to leave its frame loop and tear down. Call only after
// the producer has stopped submitting (so no push races bridge destruction).
void anoRenderRequestStop(void);

// Producer endpoint for submitting RenderCommands. Valid only after anoRenderIsReady().
AnoRenderBridge* anoRenderBridge(void);

// render_id 0's original geometry index (stand-in producer helper). Read after ready.
uint32_t anoRenderEntity0Mesh(void);

void deferred_delete_resource(RendererState* state, DeletionResourceType type, uint32_t handle);
void flush_deletion_queue(VulkanContext* ctx, RendererState* state, uint32_t frameIndex);

extern PFN_vkCmdDrawMeshTasksEXT pfnVkCmdDrawMeshTasksEXT;
extern PFN_vkCmdDrawMeshTasksIndirectEXT pfnVkCmdDrawMeshTasksIndirectEXT;
extern PFN_vkCmdDrawMeshTasksIndirectCountEXT pfnVkCmdDrawMeshTasksIndirectCountEXT;

#endif
