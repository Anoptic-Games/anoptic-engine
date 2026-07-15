/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// SlotUpload: DEVICE_LOCAL + per-frame delta staging + entity-buffer growth. Render-thread only.

#ifndef ANO_SLOT_UPLOAD_H
#define ANO_SLOT_UPLOAD_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"   // SlotUpload, RendererState

// Device buffer + N host-visible staging buffers + region arrays. False on failure.
bool slot_upload_create(SlotUpload* b, uint32_t capacity, uint32_t stride, uint32_t stagingCap, bool computeShared);
// Stage one slot's new value into frame f's host-visible delta ring (grows the ring on demand).
void slot_upload_stage(SlotUpload* b, uint32_t f, uint32_t index, const void* value);
// Record frame f's staged deltas as a copy into the device buffer.
void slot_upload_flush(VkCommandBuffer cmd, SlotUpload* b, uint32_t f);
// Grow every slot-indexed GPU buffer to hold >= required slots. False on OOM.
bool ensureEntityCapacity(RendererState* state, uint32_t required, uint32_t frameIndex);
// Apply gfx+compute CONCURRENT sharing to a buffer the async light-cull touches across queue families.
void buffer_share_async_compute(VkBufferCreateInfo* bi, uint32_t fams[2]);

#endif // ANO_SLOT_UPLOAD_H
