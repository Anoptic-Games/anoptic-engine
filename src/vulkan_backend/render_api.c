/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_logging.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/frame/frame.h"
#include "vulkan_backend/render_api.h"

// Loaded-asset registry (anoptic_render.h). initVulkan parses the glTF assets and records them here
// in load order; the logic master flattens them into renderable primitives to compose the scene.
// g_defaultMaterial is the first asset's first material (the procedural ground/markers borrow it).
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

// The baked font for logic-side shaping (anoptic_render.h). Immutable from init to
// unInitVulkan, NULL when the text stack is down.
const AnoFontBake* anoRenderTextBake(void)
{
    return rendererState.textOverlay ? &rendererState.textBake : NULL;
}
// Lighting-mode control. Stored on the render state and published into the
// GlobalUBO tail by updateCullingBuffers; takes effect from the next recorded frame. Mutated only
// from the render thread (frame record + L-key callback are both main-thread), so no atomics.
void ano_render_set_lighting_mode(AnoLightingMode mode) {
    if ((uint32_t)mode >= (uint32_t)ANO_LIGHTING_MODE_COUNT) return;
    if (rendererState.lightingMode != (uint32_t)mode) {
        rendererState.lightingMode = (uint32_t)mode;
        // Discard the in-progress timing window so the next printed average is pure for the new mode.
        ano_profile_reset_window();
    }
}

AnoLightingMode ano_render_get_lighting_mode(void) {
    return (AnoLightingMode)rendererState.lightingMode;
}

// Per-view screen-area cull threshold (review 4.9 step 1). Stored on the render state and squared
// into CullUBO.viewCullParams[view][1] by updateCullingBuffers; takes effect from the next recorded
// frame. Pixels of projected bounding-sphere radius; 0 disables the test for that view, negative is
// clamped to 0. Out-of-range view index is ignored. Render-thread only (no atomics), like the
// lighting-mode setter.
void ano_render_set_view_cull_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.cullPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
}

float ano_render_get_view_cull_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.cullPixelThreshold[view];
}

// Per-view LOD threshold (review 4.9 step 2). Copied into CullUBO.viewCullParams[view][2] by
// updateCullingBuffers; takes effect from the next recorded frame. Pixels of projected radius at
// which level 1 begins; 0 disables LOD selection (always level 0), negative clamps to 0. Inert
// until meshes carry LOD chains. Out-of-range view ignored. Render-thread only, like the others.
void ano_render_set_view_lod_threshold(uint32_t view, float pixels) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.lodPixelThreshold[view] = (pixels > 0.0f) ? pixels : 0.0f;
    if (view == 0u) rendererState.shadowGlobalDirty = true; // shadow LOD tracks view 0 (finding 8)
}

float ano_render_get_view_lod_threshold(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return 0.0f;
    return rendererState.lodPixelThreshold[view];
}

// Global LOD bias (review 4.9 step 2): added to every entity's auto-selected level in cull.comp,
// then clamped to the mesh's chain range. + = coarser, - = finer; published into viewCullParams[v][3]
// by updateCullingBuffers; takes effect next recorded frame. Clamped to +/- ANO_MAX_LOD (the shader
// clamps to the real chain length per entity). Render-thread only, like the other knobs.
void ano_render_set_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.lodBias = bias < -lim ? -lim : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD (finding 8)
}

int32_t ano_render_get_lod_bias(void) {
    return rendererState.lodBias;
}

// Shadow LOD offset (review 4.9 step 2, revised): shadow casters now select the primary camera view's
// (view 0) LOD so a caster's shadow silhouette tracks its visible geometry; this bias is an extra
// RELATIVE offset added on top (0 = exact match, + = coarser shadow). Published into CullUBO.shadowLodBias
// by updateCullingBuffers; takes effect next recorded frame. cull.comp clamps per-entity to the real
// chain length, so non-LOD meshes always cast level 0. Clamped to [0, ANO_MAX_LOD]; negative (a shadow
// finer than the visible mesh) is pointless and clamps to 0. Render-thread only, like the other knobs.
void ano_render_set_shadow_lod_bias(int32_t bias) {
    int32_t lim = (int32_t)ANO_MAX_LOD;
    rendererState.shadowLodBias = bias < 0 ? 0 : (bias > lim ? lim : bias);
    rendererState.shadowGlobalDirty = true; // cached shadow layers hold the old LOD (finding 8)
}

int32_t ano_render_get_shadow_lod_bias(void) {
    return rendererState.shadowLodBias;
}

// Per-view Hi-Z occlusion toggle (review 4.9 step 3). When enabled, the cull rejects entities fully
// behind LAST frame's depth pyramid for that view (single-phase; ~1-frame disocclusion latency).
// Published into CullUBO.hizParams[view].z (mipCount when on, 0 when off) by updateCullingBuffers;
// takes effect next recorded frame. Default off. Render-thread only; out-of-range view ignored.
void ano_render_set_view_hiz_enable(uint32_t view, bool enable) {
    if (view >= ANO_VIEW_COUNT) return;
    rendererState.hizEnable[view] = enable ? 1u : 0u;
}

bool ano_render_get_view_hiz_enable(uint32_t view) {
    if (view >= ANO_VIEW_COUNT) return false;
    return rendererState.hizEnable[view] != 0u;
}

// Parse the scene's glTF assets into the loaded-asset registry (hoisted from initVulkan; uses the
// file-global ctx). The render world owns asset LOADING; the logic master composes scene instances
// via the anoRenderAsset* query API above. On a fatal parse failure it runs unInitVulkan itself and
// returns false (initVulkan then just returns false).
bool ano_render_load_scene_assets(void)
{
	// Load the scene's glTF assets into GPU memory (geometry pool + materials + textures). The render
	// world owns asset LOADING; the logic master COMPOSES scene instances from them via the public
	// anoRenderAsset* query API and emits the creates itself (audit: logic owns the scene). Asset load
	// order is the asset_id namespace (0 = viking room, 1 = candle holder).
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

	// Sponza: a large multi-material interior (103 primitives / 25 materials / 69 textures) parsed under
	// one node, dropped in as the scene environment to stress-test node-mesh placement and the renderer's
	// graceful fallback for unimplemented PBR attributes. Texture URIs resolve relative to the glTF's own
	// directory (see parseGltf). Non-fatal: a missing/failed Sponza just leaves asset_id 2 unregistered,
	// and the logic master's spawn degrades to a no-op for it (the base scene still comes up).
	ModelAsset* sponzaAsset = parseGltf(&ctx, "sponza/2.0/Sponza/glTF/Sponza.gltf");
	if (sponzaAsset)
		g_assets[g_assetCount++] = sponzaAsset;   // asset_id 2
	else
		ano_log(ANO_WARN, "Warning: failed to parse Sponza glTF; continuing without it.");

	// Default material for procedural renderables (ground slab / debug markers the logic master builds
	// without an asset): the first asset's first primitive material, matching the old rig's choice.
	{
		mat4 ident = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
		AnoRenderableDesc d0;
		if (model_flatten(vikingRoomAsset, ident, &d0, 1u) > 0u) g_defaultMaterial = d0.material_index;
	}

	return true;
}
