/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef PIPELINE_FLAT_H
#define PIPELINE_FLAT_H

#include <stdbool.h>
#include "vulkan_backend/structs.h"

// Initialize flat pipeline layout, cache, shaders, and variants (opaque and blended)
bool ano_pipeline_flat_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

// Clean up flat pipeline resources
void ano_pipeline_flat_cleanup(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

#endif
