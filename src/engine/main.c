/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Core includes (compiled in both graphical and headless builds)
#include <mimalloc.h>
#include <mimalloc-override.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <math.h>
#include "anoptic_time.h"
#include "anoptic_threads.h"
#include "anoptic_filesystem.h"

#ifndef HEADLESS_BUILD
// Renderer contract + GLFW — only compiled into the graphical engine.
#include <anoptic_render.h>
#include <vulkan/vulkan.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif
#endif // !HEADLESS_BUILD

// Variables

// Helper Funcs (?)

double findAverage(const uint64_t arr[], uint32_t n) {
    if (n == 0) {
        return 0; // Avoid division by zero
    }

    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += arr[i];
    }

    return (double)sum / n;
}

// TODO: Move this somewhere more sane
void measureFrameTime()
{
	static uint64_t frameTimes[200] = {};
	static uint32_t timeIndex = 0;

	uint64_t currentTime = ano_timestamp_us();
	if (timeIndex > 0) {
		frameTimes[timeIndex - 1] = currentTime - frameTimes[timeIndex - 1];
	}

	if (timeIndex == 199) {
		frameTimes[timeIndex] = currentTime - frameTimes[timeIndex];

		// Print the frame times
		for (int i = 0; i < 200; i++) {
            // TODO: uhh errrm
			printf("Frame %d: %llu\n", i, frameTimes[i]);
		}
		
		printf("Average frametime: %f\n", findAverage(frameTimes, 199)/1000);
		
		timeIndex = 0;
	} else {
		frameTimes[timeIndex] = currentTime;
		timeIndex++;
	}
}



#ifndef HEADLESS_BUILD
// Logic/ECS master: the sole render-command producer. Runs on its own thread
// while the render world owns the main thread. main() sets g_logicShouldStop on
// window close, then joins, guaranteeing the producer quiesces before the bridge
// is destroyed in unInitVulkan().
static atomic_bool g_logicShouldStop = false;

// ---------------------------------------------------------------------------
// Scene composition (logic owns the scene)
// ---------------------------------------------------------------------------
// The render world loaded the glTF assets + fallback cube and assigned their GPU mesh/material
// indices; the logic master composes the scene from them and emits the creates through the bridge —
// the same command path a runtime spawn takes. This replaces the renderer's old hardcoded init rig.

// Backpressure-safe submit for the one-time, small scene burst: retry until it fits the command ring.
static void submit_blocking(AnoRenderBridge* bridge, const RenderCommand* c) {
	while (!ano_render_submit(bridge, c)) ano_sleep(1000);
}

// Spawn one renderable per primitive of asset `asset_id` at `root`, all sharing `motion` (+ speed for
// spin/orbit). Returns the first primitive's render_id (so a caller can attach lights to it); advances *nextId.
// Cap on primitives spawned per asset in one call; sized for Sponza's 103-primitive single node.
#define SPAWN_ASSET_MAX_PRIMS 256u
static uint32_t spawn_asset(AnoRenderBridge* bridge, uint32_t* nextId, uint32_t asset_id,
                            const mat4 root, AnoMotionType motion, float speed) {
	AnoRenderableDesc descs[SPAWN_ASSET_MAX_PRIMS];
	uint32_t n = anoRenderAssetPrimitives(asset_id, root, descs, SPAWN_ASSET_MAX_PRIMS);
	if (n == 0u) { printf("Producer: asset %u has no primitives; nothing spawned.\n", asset_id); return UINT32_MAX; }
	if (n > SPAWN_ASSET_MAX_PRIMS) { printf("Producer: asset %u has %u primitives; spawning only the first %u.\n", asset_id, n, SPAWN_ASSET_MAX_PRIMS); n = SPAWN_ASSET_MAX_PRIMS; }
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

// Spawn a procedural box renderable (fallback cube + default material) with a full world transform,
// static. Advances *nextId; returns its render_id.
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

// Spawn a mesh-less scene light-entity: its transform drives the light (position = column 3, forward =
// -column 2 for dir/spot); light_index is a static-region palette row; casting lights take a static
// shadow frustum. `motion` animates the slot (an orbiting light rides it for free). Advances *nextId;
// returns its render_id.
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

	// Viking room: the glTF is Z-up; rotate -90 deg about X to the engine's Y-up (this is the exact
	// matrix the old rig's rotateMatrix(identity,'X',-pi/2) produced). Spins about +Y at 1 rad/s.
	mat4 vikingRoot = {{1,0,0,0},{0,0,-1,0},{0,1,0,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, 0u, vikingRoot, ANO_MOTION_SPIN, 1.0f);

	// Two transmissive candle holders orbiting +Y at 0.5 rad/s at radii 2.0 / 2.2 (their camera-space
	// order swaps each revolution — exercises the transparency sort). The first candle anchors the
	// decorative candle lights below.
	mat4 candle1 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.0f,0,0,1}};
	mat4 candle2 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.2f,0,0,1}};
	uint32_t candleSlot = spawn_asset(bridge, &nextId, 1u, candle1, ANO_MOTION_ORBIT, 0.5f);
	spawn_asset(bridge, &nextId, 1u, candle2, ANO_MOTION_ORBIT, 0.5f);

	// Sponza (asset_id 2): the scene environment. Y-up already, with its 0.008 scale baked into the node
	// transform (~30 m atrium, floor at y ~ -1), so it drops in at identity; static. Its 103 primitives
	// spawn as individual renderables (node-mesh placement stress) and supply the floor/walls the
	// directional + point/spot shadows now fall on — so the old wide ground slab is gone. A no-op if
	// Sponza failed to load (asset_id 2 unregistered). The viking room + candles sit as props on its floor.
	mat4 sponzaRoot = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, 2u, sponzaRoot, ANO_MOTION_STATIC, 0.0f);

	// Small sun-marker cube at the directional light's source (static), so the light's origin is visible.
	mat4 sunMarker = {{0.2f,0,0,0},{0,0.2f,0,0},{0,0,0.2f,0},{2.59f,5.18f,1.55f,1}};
	spawn_box(bridge, &nextId, sunMarker);

	// Scene lights as mesh-less light-entities (static palette rows 0..5, casting: 1 dir + 4 point +
	// 1 spot = the 26-frustum static shadow atlas). Dir/spot direction is the transform's -column2.
	uint32_t li = 0u;
    { mat4 x = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
      x[2][0]=0.5f; x[2][1]=1.0f; x[2][2]=0.0f; // Shines directly overhead (straight down)
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

void* anoLogicThreadMain(void* arg)
{
	(void)arg;
	AnoRenderBridge* bridge = anoRenderBridge();

	// Compose the scene (logic owns it now): geometry + scene lights + candle lights, emitted through
	// the bridge — the same command path a runtime spawn takes. Replaces the renderer's hardcoded rig.
	spawn_scene(bridge);

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
					default: break;
					}
				} else if (ie->kind == ANO_INPUT_MOUSE_BUTTON) {
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_RIGHT)
						looking = (ie->u.button.action == GLFW_PRESS);
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
					printf("Pick: cursor over render_id %u\n", ev.u.pick_render_id);
				break;
			case REVENT_SLOT_RETIRED:   break; // ECS id recycling lands with the real producer
			case REVENT_BATCH_CONSUMED: break; // borrowed-batch ack (audit 4.10); unused by this stand-in
			case REVENT_CAPACITY:
				printf("Producer: back-channel saturated; some input samples were dropped.\n");
				break;
			}
		}

		// Integrate + publish the camera once per tick.
		{
			float dt = (now - lastCam) / 1000000.0f; lastCam = now;
			if (dt > 0.1f) dt = 0.1f; // clamp a long stall so a hitch is not a teleport
			float cp = cosf(camPitch), sp = sinf(camPitch), sy = sinf(camYaw), cy = cosf(camYaw);
			float fwd[3]   = { cp * sy, sp, -cp * cy }; // RH, looks down -Z at yaw 0 (math_conventions.md)
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

		// Prove the snapshot path: log the renderer's published frame id ~once/sec.
		{
			RenderSnapshot snap;
			if (ano_render_acquire_snapshot(bridge, &snap) && now - lastSnapLog > 1000000) {
				printf("Snapshot: frameId %llu, viewport %ux%u\n",
				       (unsigned long long)snap.frameId, snap.vpWidth, snap.vpHeight);
				lastSnapLog = now;
			}
		}
		ano_sleep(2000); // ~2 ms logic tick
	}
	return NULL;
}
#endif // !HEADLESS_BUILD

// Main function
#include "anoptic_logging.h"
int main()
{
    mi_version();

    // Resolve assets relative to the executable, not the launch directory, so the
    // binary runs from any working directory. Shaders already use PROJECT_ROOT;
    // only the CWD-relative asset loads (glTF, textures) needed this. Interim shim
    // until the Resource Manager owns asset paths.
    if (!ano_fs_chdir_gamepath())
        printf("Warning: could not set the working directory to the executable's; "
               "assets will load relative to the current working directory.\n");

	#ifdef DEBUG_BUILD

    mi_option_enable(mi_option_show_errors);
    mi_option_enable(mi_option_show_stats);
    mi_option_enable(mi_option_verbose);
	printf("Running in debug mode!\n");

    ano_log_init();
    for(int i = 0; i < 172; i++) {
        ano_log_error("Enqueued Log Message # %d\n", (i + 1));
    }

    ano_log_error("01234567890123456789012");

    ano_log_debug_now("Instantaneous Debug Message!\n");

    for(int i = 0; i < 216; i++) {
        ano_log_error("Enqueued Log Message # %d\n", (i + 1));
    }

    ano_log_debug_now("Instantaneous Debug Message!\n");

    ano_log_cleanup();

	#endif

#ifndef HEADLESS_BUILD
	// GLFW pins window + event handling to the main thread (mandatory on macOS), so
	// the render world (all Vulkan + GLFW) runs HERE on the main thread. initVulkan
	// creates the bridge synchronously before the producer starts, so there is no
	// readiness handshake.
	if (!initVulkan())
	{
	    printf("Vulkan initialization failed.\n");
	    return -1;
	}

	// Logic/ECS master spun onto its own thread as the sole render-command producer.
	anothread_t logicThread;
	if (ano_thread_create(&logicThread, NULL, anoLogicThreadMain, NULL) != 0)
	{
	    printf("Failed to spawn logic thread.\n");
	    unInitVulkan();
	    return -1;
	}

	// Render loop (main thread): pump window events, then draw. The logic thread
	// feeds discrete ECS->render transitions across the bridge concurrently.
	while (!anoShouldClose())
	{
	    glfwPollEvents();
	    drawFrame();
	}

	// Window closed: stop the producer FIRST and join it, so no submit can race the
	// bridge destruction that unInitVulkan() performs.
	atomic_store(&g_logicShouldStop, true);
	ano_thread_join(logicThread, NULL);

	unInitVulkan();
#else
	// Headless engine: no renderer. Console / server entry point.
	printf("Anoptic Engine — headless console mode.\n");
    while (true) {
        printf("Waiting...\n");
        ano_sleep(3 * 1000000);
    };
	// TODO: simulation / server loop goes here.
#endif

    return 0;
}
