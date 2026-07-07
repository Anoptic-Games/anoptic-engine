/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Runtime light registry owning light-palette region [base, base+cap) and LightData param helpers.

#ifndef ANO_LIGHT_REGISTRY_H
#define ANO_LIGHT_REGISTRY_H

#include <stdint.h>

#include "vulkan_backend/structs.h"   // LightRegistry, LightData, LIGHT_ROW_* enum
#include <anoptic_render.h>           // RenderLightParams, ANO_LIGHT_FIELD_*

void     light_registry_init(LightRegistry* r, uint32_t base, uint32_t capacity, uint32_t framesInFlight);
void     light_registry_destroy(LightRegistry* r);
uint32_t light_registry_alloc(LightRegistry* r, uint32_t light_id, uint32_t parentRid);
uint32_t light_registry_resolve(const LightRegistry* r, uint32_t light_id);
uint32_t light_registry_parent_of(const LightRegistry* r, uint32_t light_id);
uint32_t light_registry_detach(LightRegistry* r, uint32_t light_id, uint64_t currentFrame);
uint32_t light_registry_detach_children(LightRegistry* r, uint32_t parentRid,
                                        uint64_t currentFrame, uint32_t* out_rows, uint32_t max);
void     light_registry_collect(LightRegistry* r, uint64_t currentFrame);
uint32_t light_registry_compact(LightRegistry* r);

// Build a GPU LightData from bridge params + a resolved parent slot (transformIndex) + local offset.
LightData light_data_from_params(const RenderLightParams* p, uint32_t transformIndex, const float off[3]);
// Merge only the producer fields named in `fields` into a mirror LightData.
void     light_apply_fields(LightData* dst, const RenderLightParams* p, const float off[3], uint32_t fields);

#endif // ANO_LIGHT_REGISTRY_H
