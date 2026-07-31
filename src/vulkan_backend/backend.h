/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANO_VULKAN_BACKEND_H
#define ANO_VULKAN_BACKEND_H

#include "vulkan_backend/structs.h"

// Single VulkanContext instance, defined in vulkanMaster.c.
extern VulkanContext ctx;

// Slot-indexed GPU capacity. INITIAL_ENTITY_CAPACITY starts (grows); PALETTE_CAPACITY = material/light rows.
#define INITIAL_ENTITY_CAPACITY 10000u
#define ENTITY_GROWTH_CHUNK      8192u
#define PALETTE_CAPACITY        10000u
#define STREAM_CAPACITY         16384u  // streamed-transform lane
#define SLOT_STAGING_INIT        1024u  // initial SlotUpload per-frame delta budget

// Static light-palette [0, ANO_STATIC_LIGHT_COUNT); runtime registry owns the rest.
// Static rows: caller-addressed, scene-lifetime. CREATE/UPDATE overwrites whole. DESTROY releases nothing static.
// Move: recreate same light_index. Retire: intensity 0 + castsShadow 0. Caller error leaves the row rendering.
// Shadow grants CREATE-only. UPDATE: castsShadow 0 revokes, 1 refreshes owned volumes; 0->1 grants nothing. RFIELD_LIGHT ignored; whole payload required.
// Domain gate: gate_light_domain (bridge/apply.c). transformIndex/parentSlot are bare indices (bounds-check only).
#define ANO_STATIC_LIGHT_COUNT     64u

// Static count below ANO_RENDER_NO_LIGHT; light_registry_init takes capacity - ANO_STATIC_LIGHT_COUNT (vulkanMaster.c).
static_assert(ANO_STATIC_LIGHT_COUNT < ANO_RENDER_NO_LIGHT,
               "absent-light sentinel fell inside the static row domain");
static_assert(ANO_STATIC_LIGHT_COUNT < PALETTE_CAPACITY,
               "static region must leave the light palette room for the runtime registry");

// Release static frustum blocks for this palette row; clear ShadowLightInfo. Cursor does not rewind. Command path only.
void unregister_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t frameIndex);

// Refresh owned static caster volumes from restaged range; stale cached layers. Never grants. Command path only.
void refresh_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t parentSlot, float range);

// Decode row caster type from owned block config. *outType == LIGHT_TYPE_COUNT: no block.
[[nodiscard]] bool static_shadow_row_casts(const RendererState* st, uint32_t lightIdx, uint32_t* outType);

// register_static_shadow lives in shadow/shadow.h; all four defined in shadow/shadow_casters.c.
#endif // ANO_VULKAN_BACKEND_H
