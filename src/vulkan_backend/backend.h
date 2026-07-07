/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef ANO_VULKAN_BACKEND_H
#define ANO_VULKAN_BACKEND_H

#include "vulkan_backend/structs.h"

// Single VulkanContext instance, defined in vulkanMaster.c.
extern VulkanContext ctx;

// Shared slot-indexed GPU buffer capacity constants.
// INITIAL_ENTITY_CAPACITY is a starting slot count, not a ceiling (see ensureEntityCapacity).
// PALETTE_CAPACITY sizes the material/light palettes, indexed by distinct material/light.
#define INITIAL_ENTITY_CAPACITY 10000u
#define ENTITY_GROWTH_CHUNK      8192u
#define PALETTE_CAPACITY        10000u
#define STREAM_CAPACITY         16384u  // streamed-transform lane
#define SLOT_STAGING_INIT        1024u  // initial SlotUpload per-frame delta budget

// Static light-palette region [0, ANO_STATIC_LIGHT_COUNT), runtime registry owns the rest.
#define ANO_STATIC_LIGHT_COUNT     64u

#endif // ANO_VULKAN_BACKEND_H
