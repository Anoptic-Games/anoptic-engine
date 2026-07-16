/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

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
// Graphical: renderer + GLFW
#include <anoptic_render.h>
#include <anoptic_text.h> // logic-side shaping over anoRenderTextBake()
#include <anoptic_ui.h>
// Composer runs INSIDE the audio callback (src/music/ANOPTIC_MUSICGEN.md).
// Logic never touches it — steers over the audio bridge and hears bars as they sound.
#include <anoptic_audio.h>
#include <anoptic_music.h>
#include <anoptic_synth.h>
#include <vulkan/vulkan.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif
#endif // !HEADLESS_BUILD

/* Variables */

#ifndef HEADLESS_BUILD
// Logic/ECS master: sole render-command producer (own thread; render world owns main).
// main() sets g_logicShouldStop on close, joins before unInitVulkan() destroys the bridge.
static atomic_bool g_logicShouldStop = false;

/* Scene Composition. Logic owns the scene. */

// Logic composes scene + emits creates. Render world owns GPU assets.

// Retry until the command ring accepts.
static void submit_blocking(AnoRenderBridge* bridge, const RenderCommand* c) {
	while (!ano_render_submit(bridge, c)) ano_sleep(1000);
}

// One renderable per primitive of asset_id at root. Shares motion (+ speed for spin/orbit).
// Returns first render_id. Advances *nextId.
#define SPAWN_ASSET_MAX_PRIMS 256u // max primitives per call
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

// Static fallback-cube box at transform. Advances *nextId. Returns render_id.
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

// Mesh-less light-entity: pos = col3, forward = -col2 (dir/spot; localDir default -Z). light_index = static palette row.
// Casting takes static-region shadow frustums (dir/spot 1, point 6). Advances *nextId. Returns render_id.
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

// Compose scene once. render_id + static light_index are the logic master's namespaces.
static void spawn_scene(AnoRenderBridge* bridge) {
	uint32_t nextId = 0u;

	// Viking room: Z-up glTF -> Y-up (-90 X). Spins +Y at 1 rad/s.
	mat4 vikingRoot = {{1,0,0,0},{0,0,-1,0},{0,1,0,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, 0u, vikingRoot, ANO_MOTION_SPIN, 1.0f);

	// Candle holders orbit +Y at 0.5 rad/s (r=2.0 / 2.2). First anchors decorative lights.
	mat4 candle1 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.0f,0,0,1}};
	mat4 candle2 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.2f,0,0,1}};
	uint32_t candleSlot = spawn_asset(bridge, &nextId, 1u, candle1, ANO_MOTION_ORBIT, 0.5f);
	spawn_asset(bridge, &nextId, 1u, candle2, ANO_MOTION_ORBIT, 0.5f);

	// Sponza (asset_id 2): environment, Y-up, static at identity. No-op if unregistered.
	mat4 sponzaRoot = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, 2u, sponzaRoot, ANO_MOTION_STATIC, 0.0f);

	// Sun-marker cube (static), decorative pose near overhead light aim.
	mat4 sunMarker = {{0.2f,0,0,0},{0,0.2f,0,0},{0,0,0.2f,0},{2.59f,5.18f,1.55f,1}};
	spawn_box(bridge, &nextId, sunMarker);

	// Scene lights: palette rows 0..5 (1 dir + 4 point + 1 spot = 26 static shadow frustums). Dir/spot aim via -col2.
	uint32_t li = 0u;
    { mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
      x[2][0]=0.2f; x[2][1]=1.0f; x[2][2]=0.0f; // mostly down, slight +X in col2
      RenderLightParams p = { .color={1.0f,0.96f,0.9f}, .intensity=2.5f, .range=0.0f, .type=RENDER_LIGHT_DIRECTIONAL, .castsShadow=1u };
      spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_STATIC, 0.0f); }
	{ mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,1.5f,1.2f,1}}; // warm point, orbits +Y
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
	  x[2][0]=0.0f; x[2][1]=1.0f; x[2][2]=0.0f; // forward = -col2 = (0,-1,0)
	  RenderLightParams p = { .color={1.0f,1.0f,1.0f}, .intensity=20.0f, .range=12.0f,
	      .innerConeCos=0.966f, .outerConeCos=0.906f, .type=RENDER_LIGHT_SPOT, .castsShadow=1u };
	  spawn_light_entity(bridge, &nextId, x, li++, &p, ANO_MOTION_STATIC, 0.0f); }

	// Decorative candle lights: attach to first candle slot (non-casting). light_id = producer namespace.
	uint32_t lid = 100u;
	struct { float col[3], in, rng, inner, outer; uint32_t type; float dir[3], ox, oy, oz; } cl[5] = {
		{{1.0f,0.5f,0.15f}, 6.0f, 4.0f, 0,0, RENDER_LIGHT_POINT, {0,0,0},  0.6f,0.3f,0.0f},
		{{0.2f,0.8f,1.0f},  6.0f, 4.0f, 0,0, RENDER_LIGHT_POINT, {0,0,0}, -0.6f,0.3f,0.0f},
		{{1.0f,0.2f,0.8f},  5.0f, 4.0f, 0,0, RENDER_LIGHT_POINT, {0,0,0},  0.0f,0.8f,0.0f},
		{{0.5f,1.0f,0.6f}, 12.0f, 6.0f, 0.95f,0.85f, RENDER_LIGHT_SPOT, { 0.7f,-0.7f,0.0f}, 0.0f,1.2f,0.0f},
		{{1.0f,0.7f,0.3f}, 12.0f, 6.0f, 0.95f,0.85f, RENDER_LIGHT_SPOT, {-0.7f,-0.7f,0.0f}, 0.0f,1.2f,0.0f},
	};
	if (candleSlot != UINT32_MAX) // first candle primitive anchors attaches
	for (int i = 0; i < 5; i++) {
		RenderLightParams p = { .color={cl[i].col[0],cl[i].col[1],cl[i].col[2]}, .intensity=cl[i].in,
			.range=cl[i].rng, .innerConeCos=cl[i].inner, .outerConeCos=cl[i].outer, .type=cl[i].type,
			.localDir={cl[i].dir[0],cl[i].dir[1],cl[i].dir[2]} };
		while (!ano_render_light_attach(bridge, lid++, candleSlot, &p, cl[i].ox, cl[i].oy, cl[i].oz))
			ano_sleep(1000); // ring full: retry
	}
}

/* HUD Text */

// Logic-side text (v0): shape on this thread, ship named blocks. text_id = producer namespace.
#define HUD_TEXT_TITLE   1u
#define HUD_TEXT_NOTICE  2u
#define HUD_TEXT_CAM     3u
#define HUD_TEXT_UNICODE 4u
#define HUD_TEXT_HOMER   5u
#define HUD_TEXT_CAP     128u

// Shape + submit one block. Clamp to HUD_TEXT_CAP.
static bool hud_text_submit(AnoRenderBridge* bridge, uint32_t text_id,
                            AnoGlyphInstance* inst, uint32_t shaped) {
	if (shaped > HUD_TEXT_CAP) shaped = HUD_TEXT_CAP;
	return ano_render_text_set(bridge, text_id, inst, shaped);
}

/* HUD UI */

// Logic-side UI (v0): layout/style/hit-test. Renderer gets prim blocks only.
// Blocks: status bar + M-toggled menu (resubmit on change).
#define HUD_UI_BAR   1u
#define HUD_UI_MENU  2u
#define HUD_UI_GCAP  192u

// Menu geometry in overlay logical units (render + hit-test).
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

// Centered label -> glyph array + UI_GLYPHS prim. Baseline ~0.7 em optical center.
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
	// Plate gradient: light top -> dark bottom.
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
	// RESUME play-triangle via curve transport.
	float rb0 = m->button[0][0], rcy = 0.5f * (m->button[0][1] + m->button[0][3]);
	AnoUiPathSeg play[3] = {
		{ ANO_UI_SEG_MOVE, { rb0 + 22.0f, rcy - 9.0f, 0.0f, 0.0f } },
		{ ANO_UI_SEG_LINE, { rb0 + 38.0f, rcy, 0.0f, 0.0f } },
		{ ANO_UI_SEG_LINE, { rb0 + 22.0f, rcy + 9.0f, 0.0f, 0.0f } },
	};
	ano_ui_path_fill(&b, play, 3, label, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	return ano_render_ui_set(bridge, HUD_UI_MENU, 128, &b, glyphs, gcount);
}

/* Music World */

// Main-thread bring-up before the logic producer; teardown after join — same discipline as the render bridge: no submit may race destruction.
// Composer lives on the audio thread (mixer callback), two bars ahead of the playhead. Logic reaches music ONLY through the audio bridge.

#define MUSIC_RATE 48000u
#define MUSIC_SEED 2718u

static AnoSynth       *g_synth;
static AnoMusicEngine *g_music;

static void music_config(AnoMusicConfig *c)
{
	*c = ano_music_config_default();
	c->hasMapper = true;
	c->mapper = ano_mapping_table_electronic();
	c->hasDramaturg = true;
	c->dramaturg = ano_dramaturg_config_default();
	c->phraseGroove = true;
	c->cadenceRit = 0.03;
	c->wanderPhrases = 6;
	c->form.cadential64 = c->form.periods = c->form.hypermeter = true;
	c->form.bassInversions = c->form.split64 = true;
	c->texture.doubling = c->texture.animate = c->texture.imitation = true;
	c->texture.rotate = c->texture.counter = true;
	c->ties.anacrusis = c->ties.suspension = c->ties.syncopation = true;
	c->clock.codetta = c->clock.extension = c->clock.elision = true;
	c->melody.planApex = c->melody.counterpoint = true;
	c->useChains = c->performChains = true;
	// Panel start affect: calm, slightly bright
	c->valence = 0.30f;
	c->energy = 0.35f;
	c->tension = 0.20f;
}

// false -> silent run (non-fatal).
static bool music_world_start(void)
{
	AnoMusicConfig cfg;
	music_config(&cfg);

	g_synth = ano_synth_create(&(AnoSynthDesc){ .sampleRate = MUSIC_RATE });
	g_music = ano_music_create(&cfg, MUSIC_SEED);
	if (g_synth == NULL || g_music == NULL)
		return false;
	if (!ano_synth_attach_music(g_synth, g_music))
		return false;

	AnoAudioBusDesc layout[ANO_SYNTH_CONSOLE_BUSES];
	uint32_t buses = ano_synth_console_layout(layout, ANO_SYNTH_CONSOLE_BUSES);

	// Synth seams: generate, control, poll, stats, commands.
	AnoAudioConfig acfg = {
		.sampleRate = MUSIC_RATE,
		.busCount = buses,
		.busLayout = layout,
		.generator         = ano_synth_generator,
		.generatorUser     = g_synth,
		.generatorControl  = ano_synth_control,
		.generatorPoll     = ano_synth_poll,
		.generatorStats    = ano_synth_stats,
		.generatorCommands = ano_synth_commands,
	};
	if (!ano_audio_init(&acfg))
		return false;

	AnoAudioBridge *ab = anoAudioBridge();
	AnoAudioOfflineEvent setup[64];
	uint32_t n = ano_synth_console_setup(setup, 64);
	for (uint32_t i = 0; i < n; i++)
		while (!ano_audio_submit(ab, &setup[i].cmd))
			ano_sleep(1000);

	// Transport start a few blocks ahead of playhead.
	AnoAudioTelemetry t;
	for (uint32_t spin = 0; spin < 200u && !ano_audio_acquire_telemetry(ab, &t); spin++)
		ano_sleep(5000);
	ano_synth_transport_start(g_synth, (t.blockIndex + 8u) * (uint64_t)t.blockFrames);
	ano_log(ANO_INFO, "Music: composing live at %u Hz (seed %u).", MUSIC_RATE,
	        (unsigned)MUSIC_SEED);
	return true;
}

static void music_world_stop(void)
{
	if (g_synth != NULL) {
		ano_synth_transport_stop(g_synth);
		ano_sleep(50000); // drain mixer stop + tails
	}
	ano_audio_shutdown();
	ano_synth_destroy(g_synth);
	ano_music_destroy(g_music);
	g_synth = NULL;
	g_music = NULL;
}

/* Music Panel */

// Composer's three axes verbatim (no middle layer): XY = valence (brightness/modes) x energy (tempo/layers); slider = tension (cadence/dissonance).

#define HUD_UI_MUSIC 3u

enum { MUS_DRAG_NONE = 0, MUS_DRAG_XY, MUS_DRAG_TENSION };

typedef struct MusicLayout {
	float panel[4];  // x0, y0, x1, y1
	float pad[4];    // valence x energy square
	float slider[4]; // tension track
} MusicLayout;

static void music_layout(float vpW, float vpH, MusicLayout* o)
{
	(void)vpH;
	const float w = 300.0f, h = 436.0f, pad = 220.0f;
	// Below profiling OSD
	float x0 = vpW - w - 24.0f, y0 = 104.0f;
	o->panel[0] = x0;      o->panel[1] = y0;
	o->panel[2] = x0 + w;  o->panel[3] = y0 + h;
	float px = x0 + (w - pad) * 0.5f, py = y0 + 62.0f;
	o->pad[0] = px;        o->pad[1] = py;
	o->pad[2] = px + pad;  o->pad[3] = py + pad;
	o->slider[0] = px;              o->slider[1] = py + pad + 52.0f;
	o->slider[2] = px + pad;        o->slider[3] = py + pad + 52.0f + 14.0f;
}

static bool in_rect(const float r[4], float x, float y)
{
	return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

// Cursor -> control. Slider grab taller than track.
static int music_hit(const MusicLayout* m, float x, float y)
{
	if (in_rect(m->pad, x, y))
		return MUS_DRAG_XY;
	float grab[4] = { m->slider[0] - 10.0f, m->slider[1] - 12.0f,
	                  m->slider[2] + 10.0f, m->slider[3] + 12.0f };
	if (in_rect(grab, x, y))
		return MUS_DRAG_TENSION;
	return MUS_DRAG_NONE;
}

static float clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Filled from AEVT_MUSIC_BAR — arrives on the bar's DOWNBEAT, not when composed, so a cadence lights when audible.
typedef struct MusicState {
	float valence, energy, tension; // the three axes, as the panel holds them
	int   bar, keyTonic, mode, chordDegree;
	bool  isCadence;
	uint32_t genUs;   // compose cost on the audio thread (us)
	uint64_t flashUntil; // cadence rim until
} MusicState;

static const char *const MODE_NAMES[7] = { "ionian", "dorian", "phrygian", "lydian",
                                           "mixolydian", "aeolian", "locrian" };
static const char *const PC_NAMES[12] = { "C", "C#", "D", "D#", "E", "F",
                                          "F#", "G", "G#", "A", "A#", "B" };
static const char *const ROMAN[8] = { "-", "I", "II", "III", "IV", "V", "VI", "VII" };

// Cursor -> axes (valence x, energy y, tension slider). Drag clamps outside the control.
static void music_drag_apply(const MusicLayout* m, int drag, float x, float y,
                             MusicState* st)
{
	if (drag == MUS_DRAG_XY) {
		float u = (x - m->pad[0]) / (m->pad[2] - m->pad[0]);
		float v = (m->pad[3] - y) / (m->pad[3] - m->pad[1]);
		st->valence = clamp01f(u) * 2.0f - 1.0f; // -1 .. +1
		st->energy = clamp01f(v);                //  0 .. 1
	} else if (drag == MUS_DRAG_TENSION) {
		st->tension = clamp01f((x - m->slider[0]) / (m->slider[2] - m->slider[0]));
	}
}

// Builds + submits the music panel (or clears it). false == ring full, retry next tick.
static bool submit_music(AnoRenderBridge* bridge, const AnoFontBake* bake,
                         const MusicLayout* m, bool visible, const MusicState* st,
                         int hovered, uint64_t now)
{
	if (!visible)
		return ano_render_ui_clear(bridge, HUD_UI_MUSIC);

	AnoUiPrim prims[40];
	AnoUiPaint paints[4];
	AnoUiStop stops[8];
	AnoGlyphInstance glyphs[HUD_UI_GCAP];
	uint32_t gcount = 0;
	AnoUiBuilder b;
	ano_ui_builder_init(&b, prims, 40, NULL, 0, paints, 4, stops, 8);

	float shadow[4], white[4], rim[4], label[4], title[4], track[4], knob[4], glow[4], dim[4];
	ano_ui_color_srgb((float[4]){ 0.00f, 0.00f, 0.00f, 0.60f }, shadow);
	ano_ui_color_srgb((float[4]){ 1.00f, 1.00f, 1.00f, 1.00f }, white); // gradient carrier
	ano_ui_color_srgb((float[4]){ 0.62f, 0.65f, 0.70f, 1.00f }, rim);
	ano_ui_color_srgb((float[4]){ 0.92f, 0.94f, 0.97f, 1.00f }, label);
	ano_ui_color_srgb((float[4]){ 0.55f, 0.85f, 1.00f, 1.00f }, title);
	ano_ui_color_srgb((float[4]){ 0.10f, 0.11f, 0.14f, 1.00f }, track);
	ano_ui_color_srgb((float[4]){ 0.96f, 0.97f, 1.00f, 1.00f }, knob);
	ano_ui_color_srgb((float[4]){ 0.30f, 0.70f, 1.00f, 0.00f }, glow); // ADD: rgb only
	ano_ui_color_srgb((float[4]){ 0.55f, 0.60f, 0.68f, 1.00f }, dim);

	// The plate.
	AnoUiStop plate[2];
	ano_ui_color_srgb((float[4]){ 0.17f, 0.18f, 0.22f, 0.97f }, plate[0].color);
	plate[0].t = 0.0f;
	ano_ui_color_srgb((float[4]){ 0.09f, 0.10f, 0.13f, 0.97f }, plate[1].color);
	plate[1].t = 1.0f;
	uint32_t plateGrad = ano_ui_paint_linear(&b, (float[2]){ m->panel[0], m->panel[1] },
	                                         (float[2]){ m->panel[0], m->panel[3] }, plate, 2);
	float r12[4] = { 12, 12, 12, 12 }, r6[4] = { 6, 6, 6, 6 };
	ano_ui_shadow(&b, (float[2]){ m->panel[0] + 6, m->panel[1] + 10 },
	              (float[2]){ m->panel[2] + 6, m->panel[3] + 10 }, 12.0f, 9.0f, shadow,
	              ANO_UI_REF_NONE, 0);

	// Cadence flash: ADD rim, 0.5s decay.
	if (now < st->flashUntil) {
		float k = (float)(st->flashUntil - now) / 500000.0f;
		float pulse[4];
		ano_ui_color_srgb((float[4]){ 0.35f * k, 0.75f * k, 1.00f * k, 0.0f }, pulse);
		ano_ui_shadow(&b, (float[2]){ m->panel[0] - 2, m->panel[1] - 2 },
		              (float[2]){ m->panel[2] + 2, m->panel[3] + 2 }, 14.0f, 12.0f, pulse,
		              ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
	}
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, white, 0.0f, plateGrad,
	             ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, rim, 2.0f, ANO_UI_REF_NONE,
	             ANO_UI_REF_NONE, 0);
	float titleRect[4] = { m->panel[0], m->panel[1] + 12, m->panel[2], m->panel[1] + 52 };
	ui_label(&b, bake, anostr_lit("MUSIC"), 24.0f, titleRect, title, glyphs, &gcount);

	// Valence x energy square. Gradient: cold-dark -> warm-bright.
	AnoUiStop axis[3];
	ano_ui_color_srgb((float[4]){ 0.10f, 0.13f, 0.26f, 1.0f }, axis[0].color);
	axis[0].t = 0.0f;
	ano_ui_color_srgb((float[4]){ 0.16f, 0.17f, 0.20f, 1.0f }, axis[1].color);
	axis[1].t = 0.5f;
	ano_ui_color_srgb((float[4]){ 0.34f, 0.26f, 0.13f, 1.0f }, axis[2].color);
	axis[2].t = 1.0f;
	uint32_t axisGrad = ano_ui_paint_linear(&b, (float[2]){ m->pad[0], m->pad[1] },
	                                        (float[2]){ m->pad[2], m->pad[1] }, axis, 3);
	ano_ui_rrect(&b, &m->pad[0], &m->pad[2], r6, white, 0.0f, axisGrad,
	             ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->pad[0], &m->pad[2], r6, hovered == MUS_DRAG_XY ? rim : dim,
	             hovered == MUS_DRAG_XY ? 2.0f : 1.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

	// Knob at (valence, energy)
	float kx = m->pad[0] + (st->valence * 0.5f + 0.5f) * (m->pad[2] - m->pad[0]);
	float ky = m->pad[3] - st->energy * (m->pad[3] - m->pad[1]);
	float hair[4];
	ano_ui_color_srgb((float[4]){ 0.80f, 0.85f, 0.92f, 0.22f }, hair);
	ano_ui_rrect(&b, (float[2]){ m->pad[0] + 1, ky - 0.5f },
	             (float[2]){ m->pad[2] - 1, ky + 0.5f }, (float[4]){ 0, 0, 0, 0 }, hair,
	             0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, (float[2]){ kx - 0.5f, m->pad[1] + 1 },
	             (float[2]){ kx + 0.5f, m->pad[3] - 1 }, (float[4]){ 0, 0, 0, 0 }, hair,
	             0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	// Glow radius scales with energy
	float gr = 10.0f + 16.0f * st->energy;
	float lit[4];
	ano_ui_color_srgb((float[4]){ 0.30f * (0.3f + st->energy), 0.70f * (0.3f + st->energy),
	                              1.00f * (0.3f + st->energy), 0.0f }, lit);
	ano_ui_shadow(&b, (float[2]){ kx - gr, ky - gr }, (float[2]){ kx + gr, ky + gr },
	              gr, 9.0f, lit, ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
	float kr[4] = { 7, 7, 7, 7 };
	ano_ui_rrect(&b, (float[2]){ kx - 7, ky - 7 }, (float[2]){ kx + 7, ky + 7 }, kr, knob,
	             0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

	float axisRow[4] = { m->pad[0], m->pad[3] + 2, m->pad[2], m->pad[3] + 26 };
	ui_label(&b, bake, anostr_lit("dark  <  brightness  >  bright"), 15.0f, axisRow, dim,
	         glyphs, &gcount);

	// Tension slider
	ano_ui_rrect(&b, &m->slider[0], &m->slider[2], r6, track, 0.0f, ANO_UI_REF_NONE,
	             ANO_UI_REF_NONE, 0);
	float fillX = m->slider[0] + st->tension * (m->slider[2] - m->slider[0]);
	if (fillX > m->slider[0] + 1.0f) {
		float hot[4];
		ano_ui_color_srgb((float[4]){ 0.85f, 0.30f + 0.30f * (1.0f - st->tension), 0.30f,
		                              1.0f }, hot);
		ano_ui_rrect(&b, &m->slider[0], (float[2]){ fillX, m->slider[3] }, r6, hot, 0.0f,
		             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	}
	ano_ui_rrect(&b, &m->slider[0], &m->slider[2], r6,
	             hovered == MUS_DRAG_TENSION ? rim : dim,
	             hovered == MUS_DRAG_TENSION ? 2.0f : 1.0f, ANO_UI_REF_NONE,
	             ANO_UI_REF_NONE, 0);
	float sy = 0.5f * (m->slider[1] + m->slider[3]);
	ano_ui_rrect(&b, (float[2]){ fillX - 6, sy - 10 }, (float[2]){ fillX + 6, sy + 10 },
	             (float[4]){ 5, 5, 5, 5 }, knob, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	char tenText[40];
	int tl = snprintf(tenText, sizeof tenText, "tension  %.2f", (double)st->tension);
	float tenRow[4] = { m->slider[0], m->slider[1] - 26, m->slider[2], m->slider[1] - 4 };
	if (tl > 0)
		ui_label(&b, bake, anostr_view(tenText, (size_t)tl), 15.0f, tenRow, dim, glyphs,
		         &gcount);

	// AEVT_MUSIC_BAR readout: composer telling the game what it just played (panel shows; a game would react).
	char line1[64], line2[64];
	int n1, n2;
	if (st->bar >= 0) {
		n1 = snprintf(line1, sizeof line1, "bar %d  %s %s  %s", st->bar,
		              PC_NAMES[st->keyTonic % 12], MODE_NAMES[st->mode % 7],
		              ROMAN[(st->chordDegree >= 0 && st->chordDegree <= 7)
		                        ? st->chordDegree : 0]);
		n2 = snprintf(line2, sizeof line2, "composed in %u us on the audio thread",
		              st->genUs);
	} else {
		n1 = snprintf(line1, sizeof line1, "waiting for the first bar");
		n2 = snprintf(line2, sizeof line2, " ");
	}
	float row1[4] = { m->panel[0], m->slider[3] + 16, m->panel[2], m->slider[3] + 42 };
	float row2[4] = { m->panel[0], m->slider[3] + 40, m->panel[2], m->slider[3] + 62 };
	if (n1 > 0)
		ui_label(&b, bake, anostr_view(line1, (size_t)n1), 17.0f,
		         row1, st->isCadence ? title : label, glyphs, &gcount);
	if (n2 > 0)
		ui_label(&b, bake, anostr_view(line2, (size_t)n2), 13.0f, row2, dim, glyphs,
		         &gcount);

	return ano_render_ui_set(bridge, HUD_UI_MUSIC, 96, &b, glyphs, gcount);
}

// Status bar, bottom-left. Resubmit on logical viewport change.
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
	ui_label(&b, bake, anostr_lit("M menu · N music · drag the square"), 20.0f, rect, label,
	         glyphs, &gcount);
	return ano_render_ui_set(bridge, HUD_UI_BAR, 16, &b, glyphs, gcount);
}

/* Logic Thread */

void* anoLogicThreadMain(void* arg)
{
	(void)arg;
	AnoRenderBridge* bridge = anoRenderBridge();

	// Compose scene through the bridge.
	spawn_scene(bridge);

	// One-time HUD text blocks (below OSD), ring-retried.
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

		// Unicode sampler: Elder Futhark + Latin-1 + Cyrillic.
		const float samplerOrg[2] = { 24.0f, 240.0f };
		const float gold[4] = { 1.0f, 0.85f, 0.45f, 1.0f };
		n = ano_text_shape_lit(bake,
		                       "ᛖᚲ ᚺᛚᛖᚹᚨᚷᚨᛊᛏᛁᛉ ᚺᛟᛚᛏᛁᛃᚨᛉ ᚺᛟᚱᚾᚨ ᛏᚨᚹᛁᛞᛟ · Руны · æ ß",
		                       22.0f, samplerOrg, gold, hud, HUD_TEXT_CAP, NULL);
		while (!hud_text_submit(bridge, HUD_TEXT_UNICODE, hud, n)) ano_sleep(1000);

		// Homer Odyssey 1.1 (polytonic Greek).
		const float homerOrg[2] = { 24.0f, 270.0f };
		const float aegean[4] = { 0.55f, 0.80f, 1.0f, 1.0f };
		n = ano_text_shape_lit(bake,
		                       "Ἄνδρα μοι ἔννεπε, Μοῦσα, πολύτροπον",
		                       22.0f, homerOrg, aegean, hud, HUD_TEXT_CAP, NULL);
		while (!hud_text_submit(bridge, HUD_TEXT_HOMER, hud, n)) ano_sleep(1000);
	}
	uint64_t noticeDeadline = 0; // armed 15s after first frame
	bool     noticeCleared = false;

	// Free-fly camera (logic): WASD + right-drag look. Fallback eye + pitch so first publish has no jump.
	float    camEye[3] = { 0.0f, 0.9f, 3.5f };
	float    camYaw = 0.0f, camPitch = -0.211f;
	bool     inW = false, inA = false, inS = false, inD = false, inUp = false, inDown = false;
	bool     looking = false, haveCursor = false;
	float    prevCx = 0.0f, prevCy = 0.0f;
	uint64_t lastCam = ano_timestamp_us();
	uint64_t camSeq = 0;
	uint64_t lastSnapLog = ano_timestamp_us();

	// UI demo: resubmit on change. ANO_MENU opens menu at boot.
	bool     menuVisible = getenv("ANO_MENU") != NULL;
	bool     menuDirty = menuVisible, barSubmitted = false;
	int      menuHovered = -1;
	uint32_t optionsCount = 0;
	float    vpW = 0.0f, vpH = 0.0f; // last-known logical viewport (RenderSnapshot)
	float    barVpH = 0.0f;          // bar layout height

	// Music panel: logic owns layout + input; never touches the composer — steers with commands and listens back via the audio bridge.
	AnoAudioBridge* ab = anoAudioBridge(); // NULL if music_world_start failed
	MusicState mus = { .valence = 0.30f, .energy = 0.35f, .tension = 0.20f, .bar = -1 };
	bool musicVisible = false, musicDirty = false, affectDirty = false, flashOn = false;
	int  musicDrag = MUS_DRAG_NONE, musicHovered = MUS_DRAG_NONE;
	uint64_t lastTelem = 0;

	while (!atomic_load(&g_logicShouldStop))
	{
		uint64_t now = ano_timestamp_us();

		// Drain render->logic: input, picking, slot retirement.
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
					case GLFW_KEY_N:
						if (ie->u.key.action == GLFW_PRESS && ab != NULL) {
							musicVisible = !musicVisible;
							musicHovered = MUS_DRAG_NONE;
							musicDrag = MUS_DRAG_NONE;
							musicDirty = true;
						}
						break;
					default: break;
					}
				} else if (ie->kind == ANO_INPUT_MOUSE_BUTTON) {
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_RIGHT)
						looking = (ie->u.button.action == GLFW_PRESS);
					else if (ie->u.button.button == GLFW_MOUSE_BUTTON_LEFT
					         && ie->u.button.action == GLFW_RELEASE) {
						musicDrag = MUS_DRAG_NONE;
					}
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_LEFT
					    && ie->u.button.action == GLFW_PRESS
					    && musicVisible && vpW > 0.0f) {
						// Hit-test against music layout.
						MusicLayout ml;
						music_layout(vpW, vpH, &ml);
						musicDrag = music_hit(&ml, prevCx, prevCy);
						if (musicDrag != MUS_DRAG_NONE) {
							music_drag_apply(&ml, musicDrag, prevCx, prevCy, &mus);
							affectDirty = musicDirty = true;
						}
					}
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_LEFT
					         && ie->u.button.action == GLFW_PRESS
					         && menuVisible && vpW > 0.0f) {
						// Hit-test against rendered layout.
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
					if (musicDrag != MUS_DRAG_NONE && vpW > 0.0f) {
						MusicLayout ml;
						music_layout(vpW, vpH, &ml);
						music_drag_apply(&ml, musicDrag, cx, cy, &mus);
						affectDirty = musicDirty = true; // coalesced: one command per tick
					}
					if (looking && haveCursor) {
						camYaw   += (cx - prevCx) * 0.003f;
						camPitch -= (cy - prevCy) * 0.003f;
						if (camPitch >  1.5f) camPitch =  1.5f;   // pitch clamp
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
			case REVENT_SLOT_RETIRED:   break; // render_id free to recycle (ignored)
			case REVENT_BATCH_CONSUMED: break; // batch ack (ignored)
			case REVENT_CAPACITY:
				ano_log(ANO_WARN, "Producer: back-channel saturated; some input samples were dropped.");
				break;
			}
		}

		// Integrate + publish the camera once per tick.
		{
			float dt = (now - lastCam) / 1000000.0f; lastCam = now;
			if (dt > 0.1f) dt = 0.1f; // clamp hitch
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

		// Clear transient notice once. Ring full -> retry next tick.
		if (bake != NULL && !noticeCleared && noticeDeadline != 0 && now > noticeDeadline)
			noticeCleared = ano_render_text_clear(bridge, HUD_TEXT_NOTICE);

		// Menu hover -> dirty resubmit. Ring full keeps dirty.
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

		// Audiovisual loop
		if (ab != NULL) {
			// Down: coalesced one ACMD_MUSIC_AFFECT per tick; urgent = next BARLINE.
			if (affectDirty) {
				AnoAudioCommand c = { .kind = ACMD_MUSIC_AFFECT, .urgent = true,
				                      .affect = { mus.valence, mus.energy, mus.tension } };
				if (ano_audio_submit(ab, &c))
					affectDirty = false; // else: ring full, try again next tick
			}

			// Up: AEVT_MUSIC_BAR on downbeat (composed two bars ahead; held until audible).
			AnoAudioEvent aev;
			while (ano_audio_poll_event(ab, &aev)) {
				if (aev.kind != AEVT_MUSIC_BAR)
					continue;
				mus.bar = aev.u.music.bar;
				mus.keyTonic = aev.u.music.keyTonic;
				mus.mode = aev.u.music.mode;
				mus.chordDegree = aev.u.music.chordDegree;
				mus.isCadence = aev.u.music.isCadence;
				if (aev.u.music.isCadence) {
					mus.flashUntil = now + 500000ull; // cadence flash 0.5s
					flashOn = true;
				}
				musicDirty = musicVisible;
			}
			if (flashOn && now >= mus.flashUntil) { // flash done: one last frame
				flashOn = false;
				musicDirty = musicVisible;
			}
			if (musicVisible && now - lastTelem > 500000ull) {
				AnoAudioTelemetry t;
				if (ano_audio_acquire_telemetry(ab, &t) && t.genUs != mus.genUs) {
					mus.genUs = t.genUs;
					musicDirty = true;
				}
				lastTelem = now;
			}
		}

		// Music hover -> dirty resubmit.
		if (musicVisible && vpW > 0.0f) {
			MusicLayout ml;
			music_layout(vpW, vpH, &ml);
			int h = musicDrag != MUS_DRAG_NONE ? musicDrag : music_hit(&ml, prevCx, prevCy);
			if (h != musicHovered) {
				musicHovered = h;
				musicDirty = true;
			}
		}
		if (musicDirty && vpW > 0.0f) {
			MusicLayout ml;
			music_layout(vpW, vpH, &ml);
			if (submit_music(bridge, bake, &ml, musicVisible, &mus, musicHovered, now))
				musicDirty = false;
		}

		// Snapshot: log frameId ~1/s, refresh cam readout.
		{
			RenderSnapshot snap;
			if (noticeDeadline == 0 && ano_render_acquire_snapshot(bridge, &snap))
				noticeDeadline = now + 15000000ull; // first published frame: arm the notice
			if (ano_render_acquire_snapshot(bridge, &snap)) {
				// Viewport change -> recenter menu.
				if (menuVisible && (vpW != snap.uiWidth || vpH != snap.uiHeight))
					menuDirty = true;
				if (musicVisible && (vpW != snap.uiWidth || vpH != snap.uiHeight))
					musicDirty = true; // right-anchored: resize moves panel
				vpW = snap.uiWidth;
				vpH = snap.uiHeight;
			}
			// Status bar: resubmit on vpH change.
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

/* Main */

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

    // Process-wide logger. Cleans on scope exit.
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    if (logAlive != 0) {
        ano_log(ANO_FATAL, "Logger initialization failed; something is very wrong.");
        return EXIT_FAILURE;
    }

    // Blackbox: fatal signal -> CRASH log + flush.
    if (ano_log_crash_init() != 0)
        ano_log(ANO_WARN, "Blackbox failed to arm; a crash will leave no CRASH log.");

    // Warn if main stack < ANO_THREAD_STACK_SIZE.
    size_t mainStack = ano_thread_main_stack();
    if (mainStack != 0 && mainStack < ANO_THREAD_STACK_SIZE)
        ano_log(ANO_WARN, "Main-thread stack budget is %zu KiB, under the engine's %zu KiB: "
                "deep main-thread call chains may overflow (raise `ulimit -s`).",
                mainStack >> 10, (size_t)ANO_THREAD_STACK_SIZE >> 10);

#ifndef HEADLESS_BUILD
    // GLFW pins window/events to the main thread (mandatory on macOS). Vulkan + GLFW run here. initVulkan creates the bridge before the producer starts, with no readiness handshake.
    if (!initVulkan())
    {
        ano_log(ANO_FATAL, "Vulkan initialization failed.");
        return -1;
    }

    // Audio world before the producer exists — same ordering as the render bridge: nothing may submit into a bridge being destroyed. false -> silent run.
    if (!music_world_start())
        ano_log(ANO_WARN, "Music: the audio world did not come up; running silent.");

    // Logic/ECS master: sole render-command producer.
    anothread_t logicThread;
    if (ano_thread_create(&logicThread, NULL, anoLogicThreadMain, NULL) != 0)
    {
        ano_log(ANO_FATAL, "Failed to spawn logic thread.");
        music_world_stop();
        unInitVulkan();
        return -1;
    }

    // Render loop (main): poll + draw. Logic feeds ECS->render concurrently.
    while (!anoShouldClose())
    {
        glfwPollEvents();
        drawFrame();
    }

    // Stop producer FIRST and join. No submit races bridge destruction in unInitVulkan().
    atomic_store(&g_logicShouldStop, true);
    ano_thread_join(logicThread, NULL);

    // Producer quiesced; only then destroy the audio bridge.
    music_world_stop();

    unInitVulkan();
#else
    // Headless engine: no renderer. Idle console loop.
    ano_rlog(ANO_INFO, ANO_TERM, "Anoptic Engine — headless console mode.");
    while (true) {
        ano_rlog(ANO_INFO, ANO_TERM, "Waiting...");
        ano_sleep(3 * 1000000);
    };
#endif

    return 0;
}
