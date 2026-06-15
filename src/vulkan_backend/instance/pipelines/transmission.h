/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#ifndef PIPELINE_TRANSMISSION_H
#define PIPELINE_TRANSMISSION_H

#include <stdbool.h>
#include "vulkan_backend/structs.h"

// Initialize transmission pipeline layout, cache, shaders, and variants (opaque and blended)
bool ano_pipeline_transmission_init(VulkanContext* ctx, RendererState* state, PipelinePrototype* proto);

#endif
