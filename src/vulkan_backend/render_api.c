/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_logging.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/render_api.h"

// Loaded-asset registry (anoptic_render.h), recorded in load order.
// g_defaultMaterial is the first asset's first material.
#define ANO_MAX_LOADED_ASSETS 16u
static ModelAsset* g_assets[ANO_MAX_LOADED_ASSETS];
static uint32_t    g_assetCount;
static uint32_t    g_defaultMaterial;

uint32_t anoRenderAssetCount(void) { return g_assetCount; }

uint32_t anoRenderAssetPrimitives(uint32_t asset_id, const mat4 root, AnoRenderableDesc* out, uint32_t cap) {
    if (asset_id >= g_assetCount) return 0u;
    return model_flatten(g_assets[asset_id], root, out, cap);
}

uint32_t anoRenderFallbackMesh(void)    { return FALLBACK_MESH_INDEX; }
uint32_t anoRenderDefaultMaterial(void) { return g_defaultMaterial; }
uint32_t anoRenderStaticLightBase(void) { return ANO_STATIC_LIGHT_COUNT; }

// Baked font for logic-side shaping (anoptic_render.h). NULL when the text stack is down.
const AnoFontBake* anoRenderTextBake(void)
{
    return rendererState.textOverlay ? &rendererState.textBake : NULL;
}
// Lighting-mode control. Published into the GlobalUBO tail by updateCullingBuffers.
void ano_render_set_lighting_mode(AnoLightingMode mode) {
    if ((uint32_t)mode >= (uint32_t)ANO_LIGHTING_MODE_COUNT) return;
    if (rendererState.lightingMode != (uint32_t)mode) {
        rendererState.lightingMode = (uint32_t)mode;
        // Discard the in-progress timing window.
        ano_profile_reset_window();
    }
}

AnoLightingMode ano_render_get_lighting_mode(void) {
    return (AnoLightingMode)rendererState.lightingMode;
}

// Per-view screen-area cull threshold. Squared into CullUBO.viewCullParams[view][1] by updateCullingBuffers.
// Pixels of projected bounding-sphere radius; 0 disables the test, negative clamps to 0.
void ano_render_set_view_cull_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.cullPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
}

float ano_render_get_view_cull_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.cullPixelThreshold[view];
}

// Per-view LOD threshold. Copied into CullUBO.viewCullParams[view][2] by updateCullingBuffers.
// Pixels of projected radius at which level 1 begins; 0 disables LOD, negative clamps to 0.
void ano_render_set_view_lod_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.lodPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
    if (view == 0u) rendererState.shadowGlobalDirty = true; // shadow LOD tracks view 0
}

float ano_render_get_view_lod_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.lodPixelThreshold[view];
}

// Global LOD bias added to every entity's level in cull.comp. + = coarser, - = finer.
// Published into viewCullParams[v][3] by updateCullingBuffers. Clamped to +/- ANO_MAX_LOD.
void ano_render_set_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.lodBias = bias < -lim ? -lim : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD
}

int32_t ano_render_get_lod_bias(void) {
    return rendererState.lodBias;
}

// Shadow LOD offset relative to view 0's LOD (0 = exact match, + = coarser shadow).
// Published into CullUBO.shadowLodBias by updateCullingBuffers. Clamped to [0, ANO_MAX_LOD].
void ano_render_set_shadow_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.shadowLodBias = bias < 0 ? 0 : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD
}

int32_t ano_render_get_shadow_lod_bias(void) {
    return rendererState.shadowLodBias;
}

// Per-view Hi-Z occlusion toggle. Rejects entities behind last frame's depth pyramid.
// Published into CullUBO.hizParams[view].z (mipCount when on, 0 when off) by updateCullingBuffers. Default off.
void ano_render_set_view_hiz_enable(uint32_t view, bool enable) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.hizEnable[view] = enable ? 1u : 0u;
}

bool ano_render_get_view_hiz_enable(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return false;
    return rendererState.hizEnable[view] != 0u;
}

// Parse the scene's glTF assets into the loaded-asset registry.
// On a fatal parse failure runs unInitVulkan and returns false.
bool ano_render_load_scene_assets(void)
{
	// Load the scene's glTF assets into GPU memory. Load order is the asset_id namespace.
	ModelAsset* vikingRoomAsset = parseGltf(&ctx, "viking_room.gltf");
	if (!vikingRoomAsset)
	{
		ano_log(ANO_FATAL, "Failed to parse glTF file!");
		unInitVulkan();
		return false;
	}
	ModelAsset* candleHolderAsset = parseGltf(&ctx, "GlassHurricaneCandleHolder.gltf");
	if (!candleHolderAsset)
	{
		ano_log(ANO_FATAL, "Failed to parse GlassHurricaneCandleHolder glTF file!");
		unInitVulkan();
		return false;
	}
	g_assets[g_assetCount++] = vikingRoomAsset;   // asset_id 0
	g_assets[g_assetCount++] = candleHolderAsset; // asset_id 1

	// Sponza: a large multi-material interior parsed under one node as the scene environment.
	// Non-fatal: a missing/failed Sponza leaves asset_id 2 unregistered.
	ModelAsset* sponzaAsset = parseGltf(&ctx, "sponza/2.0/Sponza/glTF/Sponza.gltf");
	if (sponzaAsset)
		g_assets[g_assetCount++] = sponzaAsset;   // asset_id 2
	else
		ano_log(ANO_WARN, "Warning: failed to parse Sponza glTF; continuing without it.");

	// Default material for procedural renderables: the first asset's first primitive material.
	{
		mat4 ident = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		AnoRenderableDesc d0;
		if (model_flatten(vikingRoomAsset, ident, &d0, 1u) > 0u) g_defaultMaterial = d0.material_index;
	}

	return true;
}
