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

// Static light-palette region [0, ANO_STATIC_LIGHT_COUNT), runtime registry owns the rest.
//
// Static rows are caller-addressed and scene-lifetime: the renderer never allocates or frees one,
// and every CREATE/UPDATE naming light_index overwrites it whole. RCMD_DESTROY of the parent
// releases nothing static-side 〜 cascade_detach_lights is runtime-registry-scoped by design.
// Runtime rows follow their parent entity; static rows follow their author.
//
// So a caller removing or moving a static light mid-scene MUST rewrite its row, before or with
// destroying the parent: re-create on the same light_index to move it (re-registration replaces
// the prior shadow binding, in place when the footprint matches), or rewrite it with intensity 0
// and castsShadow 0 to retire it. Failing to is a caller error, and the row keeps rendering off
// whatever entity is recycled into its slot.
//
// Shadow grants are CREATE-only. CREATE with castsShadow registers the row's frustum block and
// installs its influence volumes (register_static_shadow). UPDATE|RFIELD_LIGHT re-decodes the whole
// row and then follows castsShadow: 0 revokes (unregister_static_shadow 〜 blocks go inactive,
// volumes clear, the budget row returns), 1 refreshes the volumes the row ALREADY owns from the
// restaged range (refresh_static_shadow) and grants nothing. So a castsShadow raised 0 -> 1 on
// UPDATE is dropped, and a LightType the row's block does not hold is ignored by the caster geometry
// 〜 both are reported at the drain seam (bridge/apply.c) and both are fixed by the destroy/recreate
// above, never by a second UPDATE.
//
// RFIELD_LIGHT carries no field mask: ANO_LIGHT_FIELD_* is runtime-registry vocabulary
// (RCMD_LIGHT_UPDATE only) and is ignored on the static path. A static UPDATE naming light_index
// MUST resend the whole light payload; a zero castsShadow in that payload is a revoke, never
// "unchanged".
//
// The row domain is decided once, at the drain seam (gate_light_domain, bridge/apply.c): a
// CREATE/UPDATE naming neither a static row nor ANO_RENDER_NO_LIGHT is refused there and logged,
// and the entity lands unlit. Consumers below the seam keep their bounds tests as backstops.
//
// LightData.transformIndex and ShadowCasterVolume.parentSlot carry a bare slot index with no
// liveness guarantee: consumers bounds-check it and never infer tenancy from it.
#define ANO_STATIC_LIGHT_COUNT     64u

// The refusal spelling must fall outside the domain it refuses, and the domain inside the lane the
// registry splits: light_registry_init takes capacity - ANO_STATIC_LIGHT_COUNT (vulkanMaster.c).
_Static_assert(ANO_STATIC_LIGHT_COUNT < ANO_RENDER_NO_LIGHT,
               "absent-light sentinel fell inside the static row domain");
_Static_assert(ANO_STATIC_LIGHT_COUNT < PALETTE_CAPACITY,
               "static region must leave the light palette room for the runtime registry");

// Revoke half of the static caster lifecycle, per the contract above: release every static frustum
// block owned by this palette row (budget rows return, the monotonic region does not) and clear the
// row's ShadowLightInfo. Command path only. Homed here beside the contract it serves.
void unregister_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t frameIndex);

// UPDATE half of the same contract: refresh the caster volumes a static row already owns from its
// restaged range, and stale their cached layers. Never grants 〜 it walks only blocks the row holds,
// so a block-less row is a no-op by construction and no budget or region state can move.
// Command path only.
void refresh_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t parentSlot, float range);

// Query half of the same contract: what the row's caster mirror holds, decoded from the block's own
// stored config. Total out-param 〜 LIGHT_TYPE_COUNT means the row owns no block. The drain seam
// reads it to report the two edges UPDATE drops: a raised castsShadow with nothing to raise, and a
// payload LightType the owned block does not hold.
[[nodiscard]] bool static_shadow_row_casts(const RendererState* st, uint32_t lightIdx, uint32_t* outType);

// The grant half (register_static_shadow) stays in shadow/shadow.h, beside the runtime caster
// attach/detach it shares a registration path with. All four are defined together in
// shadow/shadow_casters.c and share one ownership decode.

#endif // ANO_VULKAN_BACKEND_H
