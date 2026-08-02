/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "vulkan_backend/frame/pass_schema.h"
#include "vulkan_backend/pipeline_registry.h"

static_assert(ano_pipeline_passes_valid(ANO_FRAME_PASS_REGISTRY.values),
    "frame passes must cover every frame-scheduled pipeline with a valid implementation and bind domain");
