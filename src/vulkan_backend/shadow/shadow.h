/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Module-private header for the shadow/ domain. Cross-file/cross-module surface only.

#ifndef ANO_SHADOW_H
#define ANO_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#include "vulkan_backend/structs.h"   // RendererState, VulkanContext, Shadow* types
#include <anoptic_render.h>           // AnoMotionDescriptor

// --- shadow/shadow_cache.c ---------------------------------------------------
// Invalidate a frustum block's cached atlas layers.
void shadow_layers_invalidate(RendererState* st, uint32_t base, uint32_t count);
// Install/clear a caster's influence volume on its frustum block.
void shadow_volume_set(RendererState* st, uint32_t base, uint32_t count, uint32_t parentSlot,
                       const float off[3], float range);
void shadow_volume_clear(RendererState* st, uint32_t base, uint32_t count);
void shadow_volumes_reparent(RendererState* st, uint32_t slot);
// Swept-exposure mover upkeep for a slot.
void mover_refresh_slot(RendererState* st, uint32_t slot);
void shadow_track_motion(RendererState* st, uint32_t slot, const AnoMotionDescriptor* m);

// --- shadow/shadow_casters.c -------------------------------------------------
// Runtime shadow caster attach/detach + parent-destroy cascade + static-region registration.
void shadow_caster_attach(RendererState* st, uint32_t lightPalIdx, uint32_t regRow,
                          uint32_t lightType, uint32_t frameIndex);
void shadow_caster_detach(RendererState* st, uint32_t regRow, uint32_t frameIndex);
void cascade_detach_lights(RendererState* state, uint32_t parentRid, uint32_t frameIndex);
void register_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t lightType,
                            uint32_t frameIndex, uint32_t parentSlot, float range);

// --- shadow/shadow_record.c --------------------------------------------------
// The shadow region of the record path. Called from recordCommandBuffer.
void ano_shadow_record(VkCommandBuffer cmd, uint32_t entityCount, uint32_t drawSlotCount);

// --- shadow/shadow_resources.c -----------------------------------------------
bool createShadowResources(VulkanContext* ctx, RendererState* state);

#endif // ANO_SHADOW_H
