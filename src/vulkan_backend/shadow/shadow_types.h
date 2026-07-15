/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Shadow frustum / caster-volume / mover types. Domain fragment of structs.h. Not standalone.

#ifndef ANO_SHADOW_TYPES_H
#define ANO_SHADOW_TYPES_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/gpu_alloc.h"


// Swept-bound motion exposure. One world sphere bounds a mover's mesh for all time. See the
// mover_* / shadow_volume_* helpers.
typedef struct MoverBound
{
    uint32_t slot;              // owning entity slot
    uint32_t unbounded;         // 1 = no finite trajectory bound
    float    center[3];         // world sphere containing the slot's mesh over its whole trajectory
    float    radius;
    uint64_t exposeMask;        // bit s = this mover exposes frustum s
    AnoMotionDescriptor motion; // retained for bound recompute on teleport / mesh swap
} MoverBound;

typedef struct ShadowCasterVolume
{
    uint32_t parentSlot;        // slot driving the light (ANO_RENDER_SLOT_UNMAPPED = no caster)
    float    offset[3];         // light's localOffset in parent model space
    float    range;             // light range as attached; <= 0 = unbounded
    float    center[3];         // cached world influence sphere
    float    radius;            // < 0 = unbounded volume, any mover exposes
} ShadowCasterVolume;


/* Dynamic Shadows */

// Shadow frustums reuse CullView (viewProj + 6 planes); each gets solid + MASKED cull partitions.

// One per shadow frustum: which light + cube face it renders. std430: 4 x u32 = 16 B.
typedef struct ShadowFrustumConfig {
    uint32_t lightIndex;   // index into the light buffer
    uint32_t lightType;    // LightType
    uint32_t faceIndex;    // cube face [0,6) for point lights; 0 otherwise
    uint32_t active;       // 0 = inactive, 1 = live
} ShadowFrustumConfig;

// CPU-authored, indexed by light index: where this light's shadow frustums live. std430: 4 x u32 = 16 B.
typedef struct ShadowLightInfo {
    uint32_t castsShadow;  // 0 = no shadow
    uint32_t baseFrustum;  // first shadow-frustum index / array layer for this light
    uint32_t frustumCount; // 1 (dir/spot) or 6 (point)
    uint32_t pad;
} ShadowLightInfo;

// Per-frame shadow state: the GPU-written frustum buffer + descriptor sets.
typedef struct ShadowResources {
    VkBuffer        frustumBuffer;   // CullView[ANO_SHADOW_FRUSTUM_COUNT], written by shadowsetup.comp
    GpuAllocation   frustumAlloc;
    VkBuffer        sampleVPBuffer;  // mat4[CAP] viewProjs + vec4[CAP] depth-linearization params
    GpuAllocation   sampleVPAlloc;   // shadowsetup-written UBO
    VkDescriptorSet setupSet;        // shadowsetup.comp inputs/outputs
    VkDescriptorSet geomSet;         // moment render + frag sampling
    VkDescriptorSet blurAtlasSet;    // blur src = atlas array (X pass: atlas -> temp)
    VkDescriptorSet blurTempSet;     // blur src = temp array  (Y pass: temp -> atlas)
} ShadowResources;

#endif // ANO_SHADOW_TYPES_H
