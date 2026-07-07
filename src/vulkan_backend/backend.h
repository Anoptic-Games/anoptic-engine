/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANO_VULKAN_BACKEND_H
#define ANO_VULKAN_BACKEND_H

#include "vulkan_backend/structs.h"

// The single VulkanContext instance, defined in vulkanMaster.c. Promoted from a
// file-static to a plain global so the split backend TUs can reach it directly.
// NOTE: instanceInit.c functions take a `VulkanContext* ctx` parameter that legally
// shadows this global; those TUs never include this header and see only the parameter,
// so the promotion adds no churn there. Do not rename the global or the parameter.
extern VulkanContext ctx;

// Shared capacity constants. Moved here from vulkanMaster.c so the split scene/slot/light/shadow
// TUs that size or grow the slot-indexed GPU buffers see the same values (a #define cannot be
// externed). vulkanMaster.c still consumes them (initVulkan) via this header.
//
// Entity (render-slot) buffer sizing. INITIAL_ENTITY_CAPACITY is just the
// starting slot count, NOT a ceiling: the slot-indexed GPU buffers grow on demand
// (see ensureEntityCapacity) in ENTITY_GROWTH_CHUNK-aligned, geometrically-doubling
// steps. PALETTE_CAPACITY sizes the material/light palettes, which are indexed by
// distinct material/light, not by entity, so they scale on their own axis.
#define INITIAL_ENTITY_CAPACITY 10000u
#define ENTITY_GROWTH_CHUNK      8192u
#define PALETTE_CAPACITY        10000u
#define STREAM_CAPACITY         16384u  // streamed-transform lane; separate axis, not grown in v1
#define SLOT_STAGING_INIT        1024u  // initial per-frame delta budget for a SlotUpload; grows on demand

// Light-palette rows [0, ANO_STATIC_LIGHT_COUNT) are the STATIC region the logic master fills with
// scene light-entities (create-with-light, static shadow budget); the runtime attach registry owns
// [ANO_STATIC_LIGHT_COUNT, PALETTE_CAPACITY). A fixed boundary (vs the old "base = live scene count")
// lets the logic master own the static light_index namespace independently of the runtime registry.
#define ANO_STATIC_LIGHT_COUNT     64u

#endif // ANO_VULKAN_BACKEND_H
