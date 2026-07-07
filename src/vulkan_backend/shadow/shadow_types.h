/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Shadow frustum / caster-volume / mover types. A domain fragment of structs.h (included after the
// base includes + ANO_SHADOW_* defines); relies on GpuAllocation + AnoMotionDescriptor. Not standalone.

#ifndef ANO_SHADOW_TYPES_H
#define ANO_SHADOW_TYPES_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "vulkan_backend/gpu_alloc.h"


// Swept-bound motion exposure (review finding 8, deferred half). A parametric mover's trajectory is
// a closed form, so ONE world-space sphere bounds its mesh for ALL time; a shadow frustum needs
// re-rendering only while some mover's sphere reaches its light's influence volume (or its light
// itself rides a mover). Movers with no finite bound (LINEAR/STREAMED, degenerate params, or
// ANO_FORCE_NO_SWEPT) count into moverUnboundedCount, which restores the old blanket epoch. All
// bookkeeping is render-thread-only and command-driven — counts change when commands change motion/
// pose/mesh or the caster set, never per frame. See the mover_* / shadow_volume_* helpers.
typedef struct MoverBound
{
    uint32_t slot;              // owning entity slot
    uint32_t unbounded;         // 1 = no finite trajectory bound (counted in moverUnboundedCount)
    float    center[3];         // world sphere containing the slot's mesh over its WHOLE trajectory
    float    radius;
    uint64_t exposeMask;        // bit s = this mover exposes frustum s (kept in sync with shadowExposed)
    AnoMotionDescriptor motion; // retained so a teleport / mesh swap can recompute the bound
} MoverBound;

typedef struct ShadowCasterVolume
{
    uint32_t parentSlot;        // slot driving the light (ANO_RENDER_SLOT_UNMAPPED = no caster here)
    float    offset[3];         // light's localOffset in parent model space (recompute input)
    float    range;             // light range as attached; <= 0 = unbounded
    float    center[3];         // cached world influence sphere (parent BASE pose x offset)
    float    radius;            // < 0 = unbounded volume (directional / range <= 0): any mover exposes
} ShadowCasterVolume;


// --- Dynamic shadows (audit 4.7 follow-on, on the 4.8 multi-frustum cull) -------------------
// Shadow frustums reuse CullView (viewProj + 6 planes): shadowsetup.comp writes them from each
// light's live GPU transform, cull.comp tests entities against the planes, and the depth render +
// fragment sampler use the viewProj. They occupy one slot-0 cull partition each, packed after the
// camera partitions: ANO_VIEW_COUNT*drawSlotCount + s (see ano_draw_partition_count, components.h).

// One per shadow frustum: which light + cube face it renders. Drives shadowsetup.comp's projection
// choice (ortho / perspective / cube face). `active` lets spare/runtime-freed frustums be skipped:
// shadowsetup writes reject-all planes for inactive slots (cull then emits nothing) and the record
// loop skips them, so the runtime headroom slots don't spuriously render light 0. Written through a
// SlotUpload (race-free runtime mutation) + a render-thread CPU mirror for the record-loop gating.
// std430: 4 x u32 = 16 B.
typedef struct ShadowFrustumConfig {
    uint32_t lightIndex;   // index into the light buffer
    uint32_t lightType;    // LightType
    uint32_t faceIndex;    // cube face [0,6) for point lights; 0 otherwise
    uint32_t active;       // 0 = inactive (skip render, reject-all cull), 1 = live
} ShadowFrustumConfig;

// CPU-authored, indexed by light index: where this light's shadow frustums (= array layers) live,
// so the fragment shader knows whether/where to sample. std430: 4 x u32 = 16 B.
typedef struct ShadowLightInfo {
    uint32_t castsShadow;  // 0 = no shadow (skip sampling)
    uint32_t baseFrustum;  // first shadow-frustum index / array layer for this light
    uint32_t frustumCount; // 1 (dir/spot) or 6 (point)
    uint32_t pad;
} ShadowLightInfo;

// Per-frame shadow state: the GPU-written frustum buffer + descriptor sets. The CDF-stats atlas
// itself is a SINGLE shared instance on RendererState (review finding 8): per-frustum content
// persists across frames so clean frustums skip their render+blur; the per-frame sets just bind
// the shared views. frustumBuffer stays per-frame (shadowsetup rewrites it from that frame's light
// data — identical for unchanged lights, so cached layers stay consistent with it).
typedef struct ShadowResources {
    VkBuffer        frustumBuffer;   // CullView[ANO_SHADOW_FRUSTUM_COUNT], written by shadowsetup.comp
    GpuAllocation   frustumAlloc;
    VkBuffer        sampleVPBuffer;  // mat4[CAP] viewProjs + vec4[CAP] depth-linearization params,
    GpuAllocation   sampleVPAlloc;   // shadowsetup-written; fragment sampling + encode read it as a UBO
    VkDescriptorSet setupSet;        // shadowsetup.comp inputs/outputs
    VkDescriptorSet geomSet;         // moment render (flat.mesh / flat.vert) + frag sampling
    VkDescriptorSet blurAtlasSet;    // blur src = atlas array (X pass: atlas -> temp)
    VkDescriptorSet blurTempSet;     // blur src = temp array  (Y pass: temp -> atlas)
} ShadowResources;

#endif // ANO_SHADOW_TYPES_H
