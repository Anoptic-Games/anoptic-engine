/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Core includes (compiled in both graphical and headless builds)
#include <anoptic_memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <math.h>
#include "anoptic_time.h"
#include "anoptic_threads.h"
#include "anoptic_filesystem.h"
#include "anoptic_log_crash.h"   // anoptic_log.h + crash blackbox

#ifndef HEADLESS_BUILD
// Renderer contract + GLFW, graphical engine only.
#include <anoptic_render.h>
#include <anoptic_text.h> // logic-side shaping over anoRenderTextBake()
#include <vulkan/vulkan.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif
#endif // !HEADLESS_BUILD

// Variables

#ifndef HEADLESS_BUILD
// Logic/ECS master: the sole render-command producer.
// Runs on its own thread while the render world owns the main thread.
// main() sets g_logicShouldStop on window close, then joins the producer
// before unInitVulkan() destroys the bridge.
static atomic_bool g_logicShouldStop = false;

// ---------------------------------------------------------------------------
// Scene composition (logic owns the scene)
// ---------------------------------------------------------------------------
// The render world loaded the glTF assets + fallback cube and assigned GPU mesh/material indices.
// The logic master composes the scene and emits creates through the bridge.

// Backpressure-safe submit: retry until it fits the command ring.
static void submit_blocking(AnoRenderBridge* bridge, const RenderCommand* c) {
	while (!ano_render_submit(bridge, c)) ano_sleep(1000);
}

// Spawn one renderable per primitive of asset `asset_id` at `root`, sharing `motion` (+ speed for spin/orbit).
// Returns the first primitive's render_id. Advances *nextId.
// Cap on primitives spawned per asset in one call.
#define SPAWN_ASSET_MAX_PRIMS 256u
static uint32_t spawn_asset(AnoRenderBridge* bridge, uint32_t* nextId, uint32_t asset_id,
                            const mat4 root, AnoMotionType motion, float speed) {
	AnoRenderableDesc descs[SPAWN_ASSET_MAX_PRIMS];
	uint32_t n = anoRenderAssetPrimitives(asset_id, root, descs, SPAWN_ASSET_MAX_PRIMS);
	if (n == 0u) { ano_log(ANO_WARN, "Producer: asset %u has no primitives; nothing spawned.", asset_id); return UINT32_MAX; }
	if (n > SPAWN_ASSET_MAX_PRIMS) { ano_log(ANO_WARN, "Producer: asset %u has %u primitives; spawning only the first %u.", asset_id, n, SPAWN_ASSET_MAX_PRIMS); n = SPAWN_ASSET_MAX_PRIMS; }
	uint32_t first = *nextId;
	for (uint32_t i = 0; i < n; i++) {
		RenderCommand c = { .kind = RCMD_CREATE, .render_id = (*nextId)++,
			.mesh_index = descs[i].mesh_index, .material_index = descs[i].material_index,
			.light_index = ANO_RENDER_NO_LIGHT };
		memcpy(c.transform, descs[i].transform, sizeof(mat4));
		c.motion.type = (uint32_t)motion;
		if (motion == ANO_MOTION_SPIN || motion == ANO_MOTION_ORBIT) c.motion.p0.v[1] = speed; // about +Y
		submit_blocking(bridge, &c);
	}
	return first;
}

// Spawn a static procedural box renderable (fallback cube + default material) with a full world transform.
// Advances *nextId. Returns its render_id.
static uint32_t spawn_box(AnoRenderBridge* bridge, uint32_t* nextId, const mat4 transform) {
	uint32_t id = (*nextId)++;
	RenderCommand c = { .kind = RCMD_CREATE, .render_id = id,
		.mesh_index = anoRenderFallbackMesh(), .material_index = anoRenderDefaultMaterial(),
		.light_index = ANO_RENDER_NO_LIGHT };
	memcpy(c.transform, transform, sizeof(mat4));
	c.motion.type = (uint32_t)ANO_MOTION_STATIC;
	submit_blocking(bridge, &c);
	return id;
}

// Spawn a mesh-less scene light-entity: its transform drives the light (position = column 3, forward = -column 2 for dir/spot).
// light_index is a static-region palette row. Casting lights take a static shadow frustum.
// `motion` animates the slot. Advances *nextId. Returns its render_id.
static uint32_t spawn_light_entity(AnoRenderBridge* bridge, uint32_t* nextId, const mat4 transform,
                                   uint32_t light_index, const RenderLightParams* params,
                                   AnoMotionType motion, float speed) {
	uint32_t id = (*nextId)++;
	RenderCommand c = { .kind = RCMD_CREATE, .render_id = id,
		.mesh_index = ANO_RENDER_NO_MESH, .material_index = 0u,
		.light_index = light_index, .light = *params };
	memcpy(c.transform, transform, sizeof(mat4));
	c.motion.type = (uint32_t)motion;
	if (motion == ANO_MOTION_SPIN || motion == ANO_MOTION_ORBIT) c.motion.p0.v[1] = speed; // about +Y
	submit_blocking(bridge, &c);
	return id;
}

// Compose the whole scene once: geometry, scene lights, candle lights. render_id and static light_index
// are the logic master's namespaces to assign.
static void spawn_scene(AnoRenderBridge* bridge) {
	uint32_t nextId = 0u;

	// Viking room: glTF is Z-up, rotate -90 deg about X to the engine's Y-up. Spins about +Y at 1 rad/s.
	mat4 vikingRoot = {{1,0,0,0},{0,0,-1,0},{0,1,0,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, 0u, vikingRoot, ANO_MOTION_SPIN, 1.0f);

	// Two transmissive candle holders orbiting +Y at 0.5 rad/s at radii 2.0 / 2.2.
	// The first candle anchors the decorative candle lights below.
	mat4 candle1 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.0f,0,0,1}};
	mat4 candle2 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.2f,0,0,1}};
	uint32_t candleSlot = spawn_asset(bridge, &nextId, 1u, candle1, ANO_MOTION_ORBIT, 0.5f);
	spawn_asset(bridge, &nextId, 1u, candle2, ANO_MOTION_ORBIT, 0.5f);

	// Sponza (asset_id 2): the scene environment. Y-up with its 0.008 scale baked into the node transform, dropped in at identity, static.
	// Its 103 primitives spawn as individual renderables and supply the floor/walls the directional + point/spot shadows fall on.
	// A no-op if Sponza failed to load (asset_id 2 unregistered). The viking room + candles sit as props on its floor.
	mat4 sponzaRoot = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, 2u, sponzaRoot, ANO_MOTION_STATIC, 0.0f);

	// Small sun-marker cube at the directional light's source (static).
	mat4 sunMarker = {{0.2f,0,0,0},{0,0.2f,0,0},{0,0,0.2f,0},{2.59f,5.18f,1.55f,1}};
	spawn_box(bridge, &nextId, sunMarker);

	// Scene lights as mesh-less light-entities (static palette rows 0..5, casting: 1 dir + 4 point +
	// 1 spot = the 26-frustum static shadow atlas). Dir/spot direction is the transform's -column2.
	uint32_t li = 0u;
    { mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
      x[2][0]=0.2f; x[2][1]=1.0f; x[2][2]=0.0f; // Shines directly overhead (straight down)
      RenderLightParams p = { .color={1.0f,0.96f,0.9f}, .intensity=2.5f, .range=0.0f, .type=RENDER_LIGHT_DIRECTIONAL, .castsShadow=1u };
      spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_STATIC, 0.0f); }
	{ mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,1.5f,1.2f,1}}; // warm point ORBITS +Y (exercises the anim path)
	  RenderLightParams p = { .color={1.0f,0.95f,0.8f}, .intensity=5.0f, .range=10.0f, .type=RENDER_LIGHT_POINT, .castsShadow=1u };
	  spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_ORBIT, 0.5f); }
	{ mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{-2.0f,2.0f,-1.0f,1}};
	  RenderLightParams p = { .color={0.4f,0.6f,1.0f}, .intensity=4.0f, .range=10.0f, .type=RENDER_LIGHT_POINT, .castsShadow=1u };
	  spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_STATIC, 0.0f); }
	{ mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.0f,0.5f,0.0f,1}};
	  RenderLightParams p = { .color={1.0f,0.3f,0.3f}, .intensity=3.5f, .range=10.0f, .type=RENDER_LIGHT_POINT, .castsShadow=1u };
	  spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_STATIC, 0.0f); }
	{ mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0.0f,-1.0f,1.0f,1}};
	  RenderLightParams p = { .color={0.3f,1.0f,0.8f}, .intensity=2.0f, .range=10.0f, .type=RENDER_LIGHT_POINT, .castsShadow=1u };
	  spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_STATIC, 0.0f); }
	{ mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0.0f,4.0f,0.0f,1}};
	  x[2][0]=0.0f; x[2][1]=1.0f; x[2][2]=0.0f; // forward = -column2 = (0,-1,0): aim straight down
	  RenderLightParams p = { .color={1.0f,1.0f,1.0f}, .intensity=20.0f, .range=12.0f,
	      .innerConeCos=0.966f, .outerConeCos=0.906f, .type=RENDER_LIGHT_SPOT, .castsShadow=1u };
	  spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_STATIC, 0.0f); }

	// Decorative candle lights: ride the first candle's slot at model-space offsets via the runtime
	// attach API, non-casting (the static shadow atlas is full). light_id is the producer's namespace.
	uint32_t lid = 100u;
	struct { float col[3], in, rng, inner, outer; uint32_t type; float dir[3], ox, oy, oz; } cl[5] = {
		{{1.0f,0.5f,0.15f}, 6.0f, 4.0f, 0,0, RENDER_LIGHT_POINT, {0,0,0},  0.6f,0.3f,0.0f},
		{{0.2f,0.8f,1.0f},  6.0f, 4.0f, 0,0, RENDER_LIGHT_POINT, {0,0,0}, -0.6f,0.3f,0.0f},
		{{1.0f,0.2f,0.8f},  5.0f, 4.0f, 0,0, RENDER_LIGHT_POINT, {0,0,0},  0.0f,0.8f,0.0f},
		{{0.5f,1.0f,0.6f}, 12.0f, 6.0f, 0.95f,0.85f, RENDER_LIGHT_SPOT, { 0.7f,-0.7f,0.0f}, 0.0f,1.2f,0.0f},
		{{1.0f,0.7f,0.3f}, 12.0f, 6.0f, 0.95f,0.85f, RENDER_LIGHT_SPOT, {-0.7f,-0.7f,0.0f}, 0.0f,1.2f,0.0f},
	};
	if (candleSlot != UINT32_MAX) // skip if the candle asset spawned no primitive to anchor them
	for (int i = 0; i < 5; i++) {
		RenderLightParams p = { .color={cl[i].col[0],cl[i].col[1],cl[i].col[2]}, .intensity=cl[i].in,
			.range=cl[i].rng, .innerConeCos=cl[i].inner, .outerConeCos=cl[i].outer, .type=cl[i].type,
			.localDir={cl[i].dir[0],cl[i].dir[1],cl[i].dir[2]} };
		while (!ano_render_light_attach(bridge, lid++, candleSlot, &p, cl[i].ox, cl[i].oy, cl[i].oz))
			ano_sleep(1000); // backpressure: retry until it fits
	}
}

// Logic-side text (v0 bridge): shape UTF-8 against the renderer's bake on THIS thread
// and ship the instances as named blocks. text_id is the producer's namespace. The demo drives all
// verbs: a persistent title (SET), a transient notice (CLEAR), a per-second camera readout (REPLACE),
// and a persistent unicode sampler.
#define HUD_TEXT_TITLE   1u
#define HUD_TEXT_NOTICE  2u
#define HUD_TEXT_CAM     3u
#define HUD_TEXT_UNICODE 4u
#define HUD_TEXT_HOMER   5u
#define HUD_TEXT_CAP     128u

// Shape + submit one block, clamping the reported need to what the buffer holds.
static bool hud_text_submit(AnoRenderBridge* bridge, uint32_t text_id,
                            AnoGlyphInstance* inst, uint32_t shaped) {
	if (shaped > HUD_TEXT_CAP) shaped = HUD_TEXT_CAP;
	return ano_render_text_set(bridge, text_id, inst, shaped);
}

// Logic-side UI (v0 bridge): layout, styling, and hit-testing. The renderer only receives prim blocks.
// Two blocks: a persistent status bar and an M-toggled menu, resubmitted on change only.
#define HUD_UI_BAR   1u
#define HUD_UI_MENU  2u
#define HUD_UI_GCAP  96u

// Menu geometry in overlay logical units, shared by rendering and hit-testing.
typedef struct MenuLayout {
	float panel[4];      // minX minY maxX maxY
	float button[3][4];
} MenuLayout;

static void menu_layout(float vpW, float vpH, MenuLayout* out)
{
	float x0 = vpW * 0.5f - 160.0f, y0 = vpH * 0.5f - 150.0f;
	out->panel[0] = x0; out->panel[1] = y0;
	out->panel[2] = x0 + 320.0f; out->panel[3] = y0 + 300.0f;
	for (int i = 0; i < 3; i++) {
		float by = y0 + 84.0f + (float)i * 64.0f;
		out->button[i][0] = x0 + 20.0f;  out->button[i][1] = by;
		out->button[i][2] = x0 + 300.0f; out->button[i][3] = by + 48.0f;
	}
}

// Cursor (overlay logical units) -> hovered button, -1 when none.
static int menu_hit(const MenuLayout* m, float cx, float cy)
{
	for (int i = 0; i < 3; i++)
		if (cx >= m->button[i][0] && cx <= m->button[i][2]
		    && cy >= m->button[i][1] && cy <= m->button[i][3])
			return i;
	return -1;
}

// Shapes one centered label into the glyph array and emits its UI_GLYPHS prim (aux block-local).
// Baseline: optical centering (~0.7 em caps).
static void ui_label(AnoUiBuilder* b, const AnoFontBake* bake, anostr_t text, float sizePx,
                     const float rect[4], const float color[4],
                     AnoGlyphInstance* glyphs, uint32_t* gcount)
{
	if (bake == NULL || *gcount >= HUD_UI_GCAP) return;
	float w, h;
	ano_text_measure(bake, text, sizePx, &w, &h);
	float ox = rect[0] + ((rect[2] - rect[0]) - w) * 0.5f;
	float baseline = rect[1] + ((rect[3] - rect[1]) + 0.70f * sizePx) * 0.5f;
	uint32_t first = *gcount;
	uint32_t n = ano_text_shape(bake, text, sizePx, (float[2]){ ox, baseline }, color,
	                            glyphs + first, HUD_UI_GCAP - first, NULL);
	if (n > HUD_UI_GCAP - first) n = HUD_UI_GCAP - first;
	*gcount = first + n;
	float lo[2] = { ox - 2.0f, baseline - bake->ascender * sizePx - 2.0f };
	float hi[2] = { ox + w + 2.0f, baseline - bake->descender * sizePx + 2.0f };
	float white[4] = { 1, 1, 1, 1 };
	ano_ui_glyphs(b, lo, hi, first, n, white, ANO_UI_REF_NONE, 0);
}

// Builds + submits the menu block (or clears it). false == ring full, retry next tick.
static bool submit_menu(AnoRenderBridge* bridge, const AnoFontBake* bake, const MenuLayout* m,
                        bool visible, int hovered, uint32_t optionsCount)
{
	if (!visible)
		return ano_render_ui_clear(bridge, HUD_UI_MENU);
	AnoUiPrim prims[24];
	AnoUiPaint paints[2];
	AnoUiStop stops[4];
	uint32_t curves[128];
	AnoGlyphInstance glyphs[HUD_UI_GCAP];
	uint32_t gcount = 0;
	AnoUiBuilder b;
	ano_ui_builder_init(&b, prims, 24, NULL, 0, paints, 2, stops, 4);
	ano_ui_builder_curves(&b, curves, 128);
	float shadow[4], white[4], rim[4], btn[4], btnHot[4], btnRim[4], label[4], title[4], glow[4];
	ano_ui_color_srgb((float[4]){ 0.00f, 0.00f, 0.00f, 0.60f }, shadow);
	ano_ui_color_srgb((float[4]){ 1.00f, 1.00f, 1.00f, 1.0f }, white); // gradient carrier
	ano_ui_color_srgb((float[4]){ 0.62f, 0.65f, 0.70f, 1.0f }, rim);
	ano_ui_color_srgb((float[4]){ 0.22f, 0.24f, 0.30f, 1.0f }, btn);
	ano_ui_color_srgb((float[4]){ 0.28f, 0.45f, 0.80f, 1.0f }, btnHot);
	ano_ui_color_srgb((float[4]){ 0.75f, 0.80f, 0.88f, 0.90f }, btnRim);
	ano_ui_color_srgb((float[4]){ 0.92f, 0.94f, 0.97f, 1.0f }, label);
	ano_ui_color_srgb((float[4]){ 1.00f, 0.80f, 0.35f, 1.0f }, title);
	ano_ui_color_srgb((float[4]){ 0.25f, 0.45f, 0.85f, 0.0f }, glow); // ADD: rgb only
	// Vertical plate gradient (lighter top -> darker bottom).
	AnoUiStop plateStops[2];
	ano_ui_color_srgb((float[4]){ 0.17f, 0.18f, 0.22f, 0.97f }, plateStops[0].color);
	plateStops[0].t = 0.0f;
	ano_ui_color_srgb((float[4]){ 0.09f, 0.10f, 0.13f, 0.97f }, plateStops[1].color);
	plateStops[1].t = 1.0f;
	uint32_t plateGrad = ano_ui_paint_linear(&b, (float[2]){ m->panel[0], m->panel[1] },
	                                         (float[2]){ m->panel[0], m->panel[3] }, plateStops, 2);
	float r12[4] = { 12, 12, 12, 12 }, r8[4] = { 8, 8, 8, 8 };
	ano_ui_shadow(&b, (float[2]){ m->panel[0] + 6, m->panel[1] + 10 },
	              (float[2]){ m->panel[2] + 6, m->panel[3] + 10 }, 12.0f, 9.0f, shadow,
	              ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, white, 0.0f,
	             plateGrad, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, rim, 2.0f,
	             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	float titleRect[4] = { m->panel[0], m->panel[1] + 14, m->panel[2], m->panel[1] + 58 };
	ui_label(&b, bake, anostr_lit("MENU"), 26.0f, titleRect, title, glyphs, &gcount);
	for (int i = 0; i < 3; i++) {
		bool hot = hovered == i;
		if (hot)
			ano_ui_shadow(&b, &m->button[i][0], &m->button[i][2], 8.0f, 8.0f, glow,
			              ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
		ano_ui_rrect(&b, &m->button[i][0], &m->button[i][2], r8, hot ? btnHot : btn, 0.0f,
		             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
		ano_ui_rrect(&b, &m->button[i][0], &m->button[i][2], r8, btnRim, hot ? 2.0f : 1.0f,
		             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
		char text[32];
		int len;
		if (i == 1 && optionsCount > 0)
			len = snprintf(text, sizeof text, "OPTIONS (%u)", optionsCount);
		else
			len = snprintf(text, sizeof text, "%s", (const char*[]){ "RESUME", "OPTIONS", "QUIT" }[i]);
		if (len > 0)
			ui_label(&b, bake, anostr_view(text, (size_t)len), 20.0f, m->button[i],
			         label, glyphs, &gcount);
	}
	// Filled play-triangle on RESUME, baked to monotone quads, sent over the bridge curve transport.
	float rb0 = m->button[0][0], rcy = 0.5f * (m->button[0][1] + m->button[0][3]);
	AnoUiPathSeg play[3] = {
		{ ANO_UI_SEG_MOVE, { rb0 + 22.0f, rcy - 9.0f, 0.0f, 0.0f } },
		{ ANO_UI_SEG_LINE, { rb0 + 38.0f, rcy, 0.0f, 0.0f } },
		{ ANO_UI_SEG_LINE, { rb0 + 22.0f, rcy + 9.0f, 0.0f, 0.0f } },
	};
	ano_ui_path_fill(&b, play, 3, label, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	return ano_render_ui_set(bridge, HUD_UI_MENU, 128, &b, glyphs, gcount);
}

// Persistent status bar, bottom-left. Resubmitted when the logical viewport changes.
static bool submit_bar(AnoRenderBridge* bridge, const AnoFontBake* bake, float vpH)
{
	AnoUiPrim prims[8];
	AnoGlyphInstance glyphs[HUD_UI_GCAP];
	uint32_t gcount = 0;
	AnoUiBuilder b;
	ano_ui_builder_init(&b, prims, 8, NULL, 0, NULL, 0, NULL, 0);
	float shadow[4], plate[4], rim[4], label[4];
	ano_ui_color_srgb((float[4]){ 0.00f, 0.00f, 0.00f, 0.50f }, shadow);
	ano_ui_color_srgb((float[4]){ 0.10f, 0.11f, 0.13f, 0.92f }, plate);
	ano_ui_color_srgb((float[4]){ 0.50f, 0.54f, 0.60f, 1.0f }, rim);
	ano_ui_color_srgb((float[4]){ 0.88f, 0.90f, 0.94f, 1.0f }, label);
	float rect[4] = { 24.0f, vpH - 68.0f, 24.0f + 420.0f, vpH - 24.0f };
	float r10[4] = { 10, 10, 10, 10 };
	ano_ui_shadow(&b, (float[2]){ rect[0] + 4, rect[1] + 6 }, (float[2]){ rect[2] + 4, rect[3] + 6 },
	              10.0f, 6.0f, shadow, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &rect[0], &rect[2], r10, plate, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &rect[0], &rect[2], r10, rim, 1.5f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	ui_label(&b, bake, anostr_lit("UI bridge v0 · M toggles menu"), 20.0f, rect, label,
	         glyphs, &gcount);
	return ano_render_ui_set(bridge, HUD_UI_BAR, 16, &b, glyphs, gcount);
}

void* anoLogicThreadMain(void* arg)
{
	(void)arg;
	AnoRenderBridge* bridge = anoRenderBridge();

	// Compose the scene (logic owns it now): geometry + scene lights + candle lights, emitted through the bridge.
	spawn_scene(bridge);

	// One-time HUD blocks (below the renderer's own profiling OSD), backpressure-retried.
	const AnoFontBake* bake = anoRenderTextBake();
	AnoGlyphInstance hud[HUD_TEXT_CAP];
	if (bake != NULL) {
		// Each run's byteCount is sizeof its own segment.
		#define TITLE_HEAD "logic HUD"
		#define TITLE_TAIL " :: text bridge v0"
		const AnoTextRun titleRuns[2] = {
			{ sizeof TITLE_HEAD - 1, 24.0f, { 1.0f, 0.78f, 0.32f, 1.0f } }, // amber
			{ sizeof TITLE_TAIL - 1, 24.0f, { 0.9f, 0.9f, 0.9f, 1.0f } },
		};
		const float titleOrg[2] = { 24.0f, 150.0f };
		uint32_t n = ano_text_shape_runs_lit(bake, TITLE_HEAD TITLE_TAIL, titleRuns, 2,
		                                     titleOrg, hud, HUD_TEXT_CAP, NULL);
		while (!hud_text_submit(bridge, HUD_TEXT_TITLE, hud, n)) ano_sleep(1000);
		#undef TITLE_HEAD
		#undef TITLE_TAIL

		const float noticeOrg[2] = { 24.0f, 180.0f };
		const float grey[4] = { 0.6f, 0.6f, 0.6f, 1.0f };
		n = ano_text_shape_lit(bake, "this line clears itself in 15 s",
		                       20.0f, noticeOrg, grey, hud, HUD_TEXT_CAP, NULL);
		while (!hud_text_submit(bridge, HUD_TEXT_NOTICE, hud, n)) ano_sleep(1000);

		// Unicode sampler: the Gallehus horn inscription in Elder Futhark ("ek hlewagastiz holtijaz
		// horna tawido", I Hlewagastiz of Holt made the horn), plus Latin-1 and Cyrillic, one UTF-8 value.
		const float samplerOrg[2] = { 24.0f, 240.0f };
		const float gold[4] = { 1.0f, 0.85f, 0.45f, 1.0f };
		n = ano_text_shape_lit(bake,
		                       "ᛖᚲ ᚺᛚᛖᚹᚨᚷᚨᛊᛏᛁᛉ ᚺᛟᛚᛏᛁᛃᚨᛉ ᚺᛟᚱᚾᚨ ᛏᚨᚹᛁᛞᛟ · Руны · æ ß",
		                       22.0f, samplerOrg, gold, hud, HUD_TEXT_CAP, NULL);
		while (!hud_text_submit(bridge, HUD_TEXT_UNICODE, hud, n)) ano_sleep(1000);

		// Homer, Odyssey 1.1 in polytonic Greek: "Andra moi ennepe, Mousa, polytropon" (Tell me, Muse,
		// of the man of many turns). Greek + Greek Extended bake ranges.
		const float homerOrg[2] = { 24.0f, 270.0f };
		const float aegean[4] = { 0.55f, 0.80f, 1.0f, 1.0f };
		n = ano_text_shape_lit(bake,
		                       "Ἄνδρα μοι ἔννεπε, Μοῦσα, πολύτροπον",
		                       22.0f, homerOrg, aegean, hud, HUD_TEXT_CAP, NULL);
		while (!hud_text_submit(bridge, HUD_TEXT_HOMER, hud, n)) ano_sleep(1000);
	}
	uint64_t noticeDeadline = 0; // armed 15 s after frames start flowing
	bool     noticeCleared = false;

	// Free-fly camera owned by logic (audit 4.11): drain forwarded input, integrate a WASD + right-drag
	// look camera, publish its pose. Starts at the renderer's old fallback pose, with the forward derived
	// (pitch ~ -0.21 rad) so there is no jump on the first publish.
	float    camEye[3] = { 0.0f, 0.9f, 3.5f };
	float    camYaw = 0.0f, camPitch = -0.211f;
	bool     inW = false, inA = false, inS = false, inD = false, inUp = false, inDown = false;
	bool     looking = false, haveCursor = false;
	float    prevCx = 0.0f, prevCy = 0.0f;
	uint64_t lastCam = ano_timestamp_us();
	uint64_t camSeq = 0;
	uint64_t lastSnapLog = ano_timestamp_us();

	// UI demo state: blocks resubmit on change only. ANO_MENU opens the menu at boot (bench drivers that cannot inject keys).
	bool     menuVisible = getenv("ANO_MENU") != NULL;
	bool     menuDirty = menuVisible, barSubmitted = false;
	int      menuHovered = -1;
	uint32_t optionsCount = 0;
	float    vpW = 0.0f, vpH = 0.0f; // last-known logical viewport (RenderSnapshot)
	float    barVpH = 0.0f;          // logical height the bar was last laid out for

	while (!atomic_load(&g_logicShouldStop))
	{
		uint64_t now = ano_timestamp_us();

		// Drain the render -> logic back-channel: input, picking, slot retirement (audit 4.11).
		RenderEvent ev;
		while (ano_render_poll_event(bridge, &ev)) {
			switch (ev.kind) {
			case REVENT_INPUT: {
				const AnoInputEvent* ie = &ev.u.input;
				if (ie->kind == ANO_INPUT_KEY) {
					bool down = (ie->u.key.action != GLFW_RELEASE); // PRESS or REPEAT
					switch (ie->u.key.key) {
					case GLFW_KEY_W:            inW = down;    break;
					case GLFW_KEY_S:            inS = down;    break;
					case GLFW_KEY_A:            inA = down;    break;
					case GLFW_KEY_D:            inD = down;    break;
					case GLFW_KEY_SPACE:        inUp = down;   break;
					case GLFW_KEY_LEFT_CONTROL: inDown = down; break;
					case GLFW_KEY_M:
						if (ie->u.key.action == GLFW_PRESS) {
							menuVisible = !menuVisible;
							menuHovered = -1;
							menuDirty = true;
						}
						break;
					default: break;
					}
				} else if (ie->kind == ANO_INPUT_MOUSE_BUTTON) {
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_RIGHT)
						looking = (ie->u.button.action == GLFW_PRESS);
					else if (ie->u.button.button == GLFW_MOUSE_BUTTON_LEFT
					         && ie->u.button.action == GLFW_PRESS
					         && menuVisible && vpW > 0.0f) {
						// Click resolves against the rendered layout.
						MenuLayout ml;
						menu_layout(vpW, vpH, &ml);
						switch (menu_hit(&ml, prevCx, prevCy)) {
						case 0: menuVisible = false; menuDirty = true; break;   // RESUME
						case 1: optionsCount++;      menuDirty = true; break;   // OPTIONS
						case 2:                                                  // QUIT
							menuVisible = false;
							menuDirty = true;
							ano_log(ANO_INFO, "Menu: quit selected (demo no-op).");
							break;
						default: break;
						}
					}
				} else if (ie->kind == ANO_INPUT_CURSOR_POS) {
					float cx = ie->u.cursor.x, cy = ie->u.cursor.y;
					if (looking && haveCursor) {
						camYaw   += (cx - prevCx) * 0.003f;
						camPitch -= (cy - prevCy) * 0.003f;
						if (camPitch >  1.5f) camPitch =  1.5f;   // avoid gimbal at the poles
						if (camPitch < -1.5f) camPitch = -1.5f;
					}
					prevCx = cx; prevCy = cy; haveCursor = true;
				}
				break;
			}
			case REVENT_PICK_RESULT:
				if (ev.u.pick_render_id != ANO_RENDER_NO_PICK)
					ano_debug_log(ANO_INFO, "Pick: cursor over render_id %u", ev.u.pick_render_id);
				break;
			case REVENT_SLOT_RETIRED:   break; // ECS id recycling lands with the real producer
			case REVENT_BATCH_CONSUMED: break; // borrowed-batch ack, unused by this stand-in
			case REVENT_CAPACITY:
				ano_log(ANO_WARN, "Producer: back-channel saturated; some input samples were dropped.");
				break;
			}
		}

		// Integrate + publish the camera once per tick.
		{
			float dt = (now - lastCam) / 1000000.0f; lastCam = now;
			if (dt > 0.1f) dt = 0.1f; // clamp a long stall so a hitch is not a teleport
			float cp = cosf(camPitch), sp = sinf(camPitch), sy = sinf(camYaw), cy = cosf(camYaw);
			float fwd[3]   = { cp * sy, sp, -cp * cy }; // RH, looks down -Z at yaw 0
			float right[3] = { cy, 0.0f, sy };          // normalize(cross(fwd, worldUp))
			float step = 2.5f * dt;                     // units/sec
			float mF = (float)((int)inW - (int)inS);
			float mR = (float)((int)inD - (int)inA);
			float mU = (float)((int)inUp - (int)inDown);
			for (int k = 0; k < 3; k++)
				camEye[k] += (fwd[k] * mF + right[k] * mR) * step;
			camEye[1] += mU * step;

			AnoViewState view = { .fovYDeg = 45.0f, .seq = ++camSeq };
			for (int k = 0; k < 3; k++) {
				view.eye[k]    = camEye[k];
				view.center[k] = camEye[k] + fwd[k];
				view.up[k]     = 0.0f;
			}
			view.up[1] = 1.0f;
			ano_render_publish_view(bridge, &view);
		}

		// One-shot: retire the transient notice (RCMD_TEXT_CLEAR). A full ring returns false and this
		// retries next tick.
		if (bake != NULL && !noticeCleared && noticeDeadline != 0 && now > noticeDeadline)
			noticeCleared = ano_render_text_clear(bridge, HUD_TEXT_NOTICE);

		// Menu hover tracks the cursor. Any change resubmits the block (full replace).
		// A full ring keeps it dirty for the next tick.
		if (menuVisible && vpW > 0.0f) {
			MenuLayout ml;
			menu_layout(vpW, vpH, &ml);
			int h = menu_hit(&ml, prevCx, prevCy);
			if (h != menuHovered) {
				menuHovered = h;
				menuDirty = true;
			}
		}
		if (menuDirty && vpW > 0.0f) {
			MenuLayout ml;
			menu_layout(vpW, vpH, &ml);
			if (submit_menu(bridge, bake, &ml, menuVisible, menuHovered, optionsCount))
				menuDirty = false;
		}

		// Snapshot path: log the renderer's published frame id ~once/sec, and refresh the camera
		// readout block on the same cadence (REPLACE semantics).
		{
			RenderSnapshot snap;
			if (noticeDeadline == 0 && ano_render_acquire_snapshot(bridge, &snap))
				noticeDeadline = now + 15000000ull; // first published frame: arm the notice
			if (ano_render_acquire_snapshot(bridge, &snap)) {
				// Re-center the menu on viewport change.
				if (menuVisible && (vpW != snap.uiWidth || vpH != snap.uiHeight))
					menuDirty = true;
				vpW = snap.uiWidth;
				vpH = snap.uiHeight;
			}
			// Status bar: resubmitted when the logical viewport height moves, retried per tick.
			if ((!barSubmitted || barVpH != vpH) && vpH > 0.0f) {
				barSubmitted = submit_bar(bridge, bake, vpH);
				if (barSubmitted)
					barVpH = vpH;
			}
			if (ano_render_acquire_snapshot(bridge, &snap) && now - lastSnapLog > 1000000) {
				ano_debug_log(ANO_INFO, "Snapshot: frameId %llu, viewport %ux%u",
				       (unsigned long long)snap.frameId, snap.vpWidth, snap.vpHeight);
				lastSnapLog = now;
				if (bake != NULL) {
					char cam[96];
					int len = snprintf(cam, sizeof cam, "cam  x %+.2f  y %+.2f  z %+.2f",
					                   (double)camEye[0], (double)camEye[1], (double)camEye[2]);
					if (len > 0) {
						const float camOrg[2] = { 24.0f, 210.0f };
						const float mint[4] = { 0.45f, 0.95f, 0.6f, 1.0f };
						uint32_t n = ano_text_shape(bake, anostr_view(cam, (size_t)len),
						                            20.0f, camOrg, mint, hud, HUD_TEXT_CAP, NULL);
						(void)hud_text_submit(bridge, HUD_TEXT_CAM, hud, n);
					}
				}
			}
		}
		ano_sleep(2000); // ~2 ms logic tick
	}
	return NULL;
}
#endif // !HEADLESS_BUILD

// Main function
int main()
{
    mi_version();

    // Resolve assets relative to the executable, not the launch directory.
    // Shaders resolve against ano_fs_gamepath() directly (loadFile in pipeline.c);
    // only the CWD-relative asset loads (glTF, textures) need this.
    // Interim shim until the Resource Manager owns asset paths.
    if (!ano_fs_chdir_gamepath())
        ano_rlog(ANO_WARN, ANO_TERM | ANO_NOW, "Warning: could not set the working directory to the executable's; "
               "assets will load relative to the current working directory.");

    #ifdef DEBUG_BUILD

    mi_option_enable(mi_option_show_errors);
    mi_option_enable(mi_option_show_stats);
    mi_option_enable(mi_option_verbose);
    ano_debug_rlog(ANO_INFO, ANO_TERM | ANO_NOW, "Running in debug mode!");

    #endif

    // Singleton logger for the whole of main (device selection, renderer init, the frame loop).
    // Cleans itself on scope exit.
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    if (logAlive != 0) {
        ano_log(ANO_FATAL, "Logger initialization failed; something is very wrong.");
        return EXIT_FAILURE;
    }

    // Blackbox arms right after the logger: a fatal signal writes the CRASH log, then hail-mary flushes.
    if (ano_log_crash_init() != 0)
        ano_log(ANO_WARN, "Blackbox failed to arm; a crash will leave no CRASH log.");

    // Warn when the initial thread's stack budget (the environment's) is under ANO_THREAD_STACK_SIZE.
    size_t mainStack = ano_thread_main_stack();
    if (mainStack != 0 && mainStack < ANO_THREAD_STACK_SIZE)
        ano_log(ANO_WARN, "Main-thread stack budget is %zu KiB, under the engine's %zu KiB: "
                "deep main-thread call chains may overflow (raise `ulimit -s`).",
                mainStack >> 10, (size_t)ANO_THREAD_STACK_SIZE >> 10);

#ifndef HEADLESS_BUILD
    // GLFW pins window + event handling to the main thread (mandatory on macOS).
    // The render world (all Vulkan + GLFW) runs HERE on the main thread.
    // initVulkan creates the bridge synchronously before the producer starts
    // with no readiness handshake.
    if (!initVulkan())
    {
        ano_log(ANO_FATAL, "Vulkan initialization failed.");
        return -1;
    }

    // Logic/ECS master spun onto its own thread as the sole render-command producer.
    anothread_t logicThread;
    if (ano_thread_create(&logicThread, NULL, anoLogicThreadMain, NULL) != 0)
    {
        ano_log(ANO_FATAL, "Failed to spawn logic thread.");
        unInitVulkan();
        return -1;
    }

    // Render loop (main thread): pump window events, then draw.
    // The logic thread feeds discrete ECS->render transitions concurrently.
    while (!anoShouldClose())
    {
        glfwPollEvents();
        drawFrame();
    }

    // Window closed: stop the producer FIRST and join it.
    // No submit can then race the bridge destruction in unInitVulkan().
    atomic_store(&g_logicShouldStop, true);
    ano_thread_join(logicThread, NULL);

    unInitVulkan();
#else
    // Headless engine: no renderer. Console / server entry point.
    ano_rlog(ANO_INFO, ANO_TERM, "Anoptic Engine — headless console mode.");
    while (true) {
        ano_rlog(ANO_INFO, ANO_TERM, "Waiting...");
        ano_sleep(3 * 1000000);
    };
    // TODO: simulation / server loop goes here.
#endif

    return 0;
}
