/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/slot_upload.h"
#include "vulkan_backend/light_registry.h"
#include "vulkan_backend/shadow/shadow.h"

/* Runtime Shadow-Frustum Pools */

// Alloc a runtime frustum block base for a casting light (point = 6 cube faces, dir/spot = 1).
// ANO_SHADOW_NONE when the type's pool is exhausted.
static uint32_t shadow_frustum_alloc(RendererState* st, uint32_t lightType) {
    if (lightType == LIGHT_TYPE_POINT)
        return st->rtPointFreeCount  ? st->rtPointFree[--st->rtPointFreeCount]   : ANO_SHADOW_NONE;
    return     st->rtSingleFreeCount ? st->rtSingleFree[--st->rtSingleFreeCount] : ANO_SHADOW_NONE;
}
static void shadow_frustum_free(RendererState* st, uint32_t base) {
    if (base == ANO_SHADOW_NONE) return;
    // Bounds-guard the push.
    if (base >= ANO_SHADOW_RT_POINT_BASE) {
        if (st->rtPointFreeCount  < ANO_SHADOW_RT_POINT_COUNT)  st->rtPointFree[st->rtPointFreeCount++]   = base;
    } else {
        if (st->rtSingleFreeCount < ANO_SHADOW_RT_SINGLE_COUNT) st->rtSingleFree[st->rtSingleFreeCount++] = base;
    }
}

// Stage light shadow info+config. Budget-full -> castsShadow=0.
void shadow_caster_attach(RendererState* st, uint32_t lightPalIdx, uint32_t regRow,
                                 uint32_t lightType, uint32_t frameIndex) {
    uint32_t base = shadow_frustum_alloc(st, lightType);
    st->lightRegistry.rowShadowBase[regRow] = base; // NONE if budget full
    if (base == ANO_SHADOW_NONE) {
        ShadowLightInfo si = {0}; // clear prior caster's info on this row
        slot_upload_stage(&st->shadowInfo, frameIndex, lightPalIdx, &si);
        return;
    }
    uint32_t blockSize = (lightType == LIGHT_TYPE_POINT) ? ANO_SHADOW_CUBE_FACES : 1u;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = { .lightIndex = lightPalIdx, .lightType = lightType,
            .faceIndex = (lightType == LIGHT_TYPE_POINT ? f : 0u), .active = 1u };
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // recycled block, layers stale
    // Row mirror is complete at attach time, seeded by the caller.
    LightData* mir = &st->lightRegistry.rowMirror[regRow];
    shadow_volume_set(st, base, blockSize, mir->transformIndex, mir->localOffset, mir->range);
    ShadowLightInfo si = { .castsShadow = 1u, .baseFrustum = base, .frustumCount = blockSize, .pad = 0u };
    slot_upload_stage(&st->shadowInfo, frameIndex, lightPalIdx, &si);
}

// Free a registry row's runtime shadow caster (if any).
// Marks its frustum block inactive (mirror + SlotUpload), returns it to the pool, rowShadowBase -> NONE.
void shadow_caster_detach(RendererState* st, uint32_t regRow, uint32_t frameIndex) {
    uint32_t base = st->lightRegistry.rowShadowBase[regRow];
    if (base == ANO_SHADOW_NONE) return;
    uint32_t blockSize = (base >= ANO_SHADOW_RT_POINT_BASE) ? ANO_SHADOW_CUBE_FACES : 1u;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = st->shadowCfgMirror[base + f];
        c.active = 0u;
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // freed block, stale content
    shadow_volume_clear(st, base, blockSize);
    shadow_frustum_free(st, base);
    st->lightRegistry.rowShadowBase[regRow] = ANO_SHADOW_NONE;
}

// Disable + quarantine every runtime light attached to a renderable being destroyed.
// Stages enabled=0 into this frame per child, frees its runtime shadow frustum, rows return to the free-list.
void cascade_detach_lights(RendererState* state, uint32_t parentRid, uint32_t frameIndex) {
    uint32_t rows[64]; uint32_t n;
    LightData off = {0}; // enabled == 0
    do {
        n = light_registry_detach_children(&state->lightRegistry, parentRid, state->globalFrame, rows, 64u);
        for (uint32_t k = 0; k < n; k++) {
            slot_upload_stage(&state->lightBuffer, frameIndex, rows[k], &off);
            shadow_caster_detach(state, rows[k] - state->lightRegistry.base, frameIndex);
        }
    } while (n == 64u);
}

// Register STATIC-region caster for staged light row. Past budget -> shadowless.
void register_static_shadow(RendererState* st, uint32_t lightIdx, uint32_t lightType,
                                   uint32_t frameIndex, uint32_t parentSlot, float range) {
    uint32_t budget = lightType == LIGHT_TYPE_DIRECTIONAL ? ANO_SHADOW_DIR_COUNT
                    : lightType == LIGHT_TYPE_POINT       ? ANO_SHADOW_POINT_COUNT
                                                          : ANO_SHADOW_SPOT_COUNT;
    uint32_t blockSize = lightType == LIGHT_TYPE_POINT ? ANO_SHADOW_CUBE_FACES : 1u;
    if (st->shadowTypeUsed[lightType] >= budget ||
        st->shadowFrustumNext + blockSize > ANO_SHADOW_STATIC_FRUSTUM_COUNT)
        return; // budget/region full, stays shadowless
    uint32_t base = st->shadowFrustumNext;
    for (uint32_t f = 0; f < blockSize; f++) {
        ShadowFrustumConfig c = { .lightIndex = lightIdx, .lightType = lightType,
            .faceIndex = (lightType == LIGHT_TYPE_POINT ? f : 0u), .active = 1u };
        st->shadowCfgMirror[base + f] = c;
        slot_upload_stage(&st->shadowConfig, frameIndex, base + f, &c);
    }
    shadow_layers_invalidate(st, base, blockSize); // fresh static block, render before first sample
    // create-with-light rides the slot origin (localOffset zero).
    float zeroOff[3] = { 0.0f, 0.0f, 0.0f };
    shadow_volume_set(st, base, blockSize, parentSlot, zeroOff, range);
    ShadowLightInfo si = { .castsShadow = 1u, .baseFrustum = base, .frustumCount = blockSize, .pad = 0u };
    slot_upload_stage(&st->shadowInfo, frameIndex, lightIdx, &si);
    st->shadowFrustumNext += blockSize;
    st->shadowTypeUsed[lightType] += 1u;
}

