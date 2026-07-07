/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Module-private header for the bridge/ domain: the render-side consumer of the ECS<->render bridge
// — render_apply_commands + staging (apply.c) — and the logic-thread producer entry points
// (producer.c, all declared in the public anoptic_render.h). The lock-free transport rings
// themselves stay in src/render_bridge/.

#ifndef ANO_BRIDGE_H
#define ANO_BRIDGE_H

#include <stdint.h>

#include "vulkan_backend/structs.h"

// Drain the command ring and apply each command's changed per-slot fields into `frameIndex`'s delta
// staging; advance slot/light quarantine; publish the light count. Called from drawFrame.
void render_apply_commands(RendererState* state, uint32_t frameIndex);

#endif // ANO_BRIDGE_H
