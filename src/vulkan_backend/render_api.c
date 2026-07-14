/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_log.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/render_api.h"

// Loaded-asset registry (anoptic_render.h). asset_id is a fixed slot: a failed parse
// leaves it NULL and the scene composes without it.
// g_defaultMaterial: the first asset's first material, or the built-in row.
#define ANO_MAX_LOADED_ASSETS 16u
static ModelAsset* g_assets[ANO_MAX_LOADED_ASSETS];
static uint32_t    g_assetCount;
static uint32_t    g_defaultMaterial;

// Material SSBO row 0, claimed before any glTF parse.
#define ANO_DEFAULT_MATERIAL_INDEX 0u

uint32_t anoRenderAssetCount(void) { return g_assetCount; }

uint32_t anoRenderAssetPrimitives(uint32_t asset_id, const mat4 root, AnoRenderableDesc* out, uint32_t cap) {
    if (asset_id >= g_assetCount || g_assets[asset_id] == NULL) return 0u;
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

// Claim material SSBO row 0 with the stock white PBR row in every frame-in-flight copy.
// No-op if the buffer is absent or row 0 is already taken.
// Invariant: runs before the first parseGltf, which allocates rows from count upward.
//
/// TODO: this is fucked up. Hand-writing a material row into a mapped SSBO from the
/// asset-load path, with positional asset slots, is a shim. Redo with the asset manager.
static void register_default_material(void)
{
	if (rendererState.materialBuffer.capacity == 0u || rendererState.materialBuffer.count != 0u)
		return;

	MaterialData mat;
	ano_vk_init_default_material_data(&mat);
	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
		rendererState.materialBuffer.mapped[frame][ANO_DEFAULT_MATERIAL_INDEX] = mat;
	rendererState.materialBuffer.count = 1u;
}

// Parse the scene's glTF assets into the loaded-asset registry.
// An unparsable asset leaves its slot NULL. Always returns true.
bool ano_render_load_scene_assets(void)
{
	// Row 0 before any parse. glTF materials allocate from row 1 up.
	register_default_material();
	g_defaultMaterial = ANO_DEFAULT_MATERIAL_INDEX;

	// Load the scene's glTF assets into GPU memory. Load order is the asset_id namespace.
	g_assets[0] = parseGltf(&ctx, "viking_room.gltf");
	if (!g_assets[0])
		ano_log(ANO_ERROR, "viking_room unavailable; continuing without it.");

	g_assets[1] = parseGltf(&ctx, "GlassHurricaneCandleHolder.gltf");
	if (!g_assets[1])
		ano_log(ANO_ERROR, "GlassHurricaneCandleHolder unavailable; continuing without it.");

	// Sponza: the scene environment, parsed under one node.
	g_assets[2] = parseGltf(&ctx, "sponza/2.0/Sponza/glTF/Sponza.gltf");
	if (!g_assets[2])
		ano_log(ANO_WARN, "Warning: failed to parse Sponza glTF; continuing without it.");

	g_assetCount = 3u;

	// Default material for procedural renderables: the first asset's first primitive material.
	if (g_assets[0])
	{
		mat4 ident = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		AnoRenderableDesc d0;
		if (model_flatten(g_assets[0], ident, &d0, 1u) > 0u) g_defaultMaterial = d0.material_index;
	}

	return true;
}
