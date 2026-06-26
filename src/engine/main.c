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

void* anoLogicThreadMain(void* arg)
{
	(void)arg;
	// Stand-in producer: until the real DisplayState graphics-extract exists, drive
	// one discrete transition across the bridge on a timer — toggle render_id 0's
	// mesh between its original geometry and the fallback cube. Proves the producer
	// -> SPSC ring -> render-consumer path across the thread boundary.
	AnoRenderBridge* bridge = anoRenderBridge();
	uint32_t originalMesh   = anoRenderEntity0Mesh();
	bool showingFallback    = false;
	uint64_t lastSwap       = ano_timestamp_us();

	// STAND-IN (Path B v2): stream a vertical bob onto render_ids 0,1 (typed
	// ANO_MOTION_STREAMED at spawn) so the GPU scatter lane runs on real hardware. Each
	// entity bobs around its real seeded base pose — a streamed transform is a full world
	// matrix in the same space as initialTransform, so fabricating a bare identity would
	// teleport and mirror them. Zero-copy path: reserve the next ring slice, write the
	// transforms straight into mapped GPU memory, and publish. begin returns false only
	// when every slice is still in flight (the render side then holds the last slice).
	// Remove when the real graphics-extract drives the lane.
	float    streamPhase = 0.0f;
	uint64_t lastStream  = ano_timestamp_us();

	// Seeded base poses for the two streamed entities, fetched once (stable after init);
	// identity fallback keeps the fabricated matrix valid if an id fails to resolve.
	mat4 streamBase[2];
	for (uint32_t e = 0; e < 2; e++) {
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				streamBase[e][r][c] = (r == c) ? 1.0f : 0.0f;
		anoRenderEntityBaseTransform(e, streamBase[e]); // overwrites on success
	}

	// STAND-IN (3.3): exercise the bulk commands. Toggle a tint on render_ids {2,3,4} via
	// one RCMD_BULK_UPDATE every ~2 s (the mass-state-change case), then a one-shot
	// mass-despawn of {3,4} at ~8 s. Both retry on a full ring (backpressure, never drop);
	// after the despawn the tint update's {3,4} resolve to nothing and are skipped.
	uint64_t startTime     = ano_timestamp_us();
	uint64_t lastTint      = startTime;
	bool     tintOn        = false;
	bool     bulkDestroyed = false;

	// STAND-IN (4.7 Phase 3): runtime light attach / update / detach. ~3 s: attach a warm casting spot
	// (light_id 5000) to render_id 2 and a cyan point (5001) to render_id 3, each riding its parent at
	// a model-space offset. Pulse 5000's intensity every ~0.5 s. ~7 s: toggle 5000's shadow casting OFF
	// (frustum returns to the pool, light stays lit); ~9.5 s: toggle it back ON (re-allocates, proving
	// pool reuse). ~12 s: detach 5000. 5001 is left attached so the 16 s bulk-despawn of {3,4} exercises
	// the parent-DESTROY cascade (the render side auto-disables and reclaims it). Remove for a real producer.
	bool     rt5000     = false;
	bool     rt5001     = false;
	bool     rtCastOff  = false;
	bool     rtCastOn   = false;
	bool     rtDetached = false;
	uint64_t lastPulse  = startTime;
	float    pulsePhase = 0.0f;

	// Free-fly camera owned by logic (audit 4.11). The renderer's view-0 camera is now driven from
	// here: drain forwarded input, integrate a WASD + right-drag look camera, publish its pose. This
	// is the demonstrable input -> logic -> render round trip, and the first piece of render-loop
	// scene control moved into the logic thread. Starts at the renderer's old fallback pose, with the
	// forward derived (pitch ~ -0.21 rad) so there is no jump on the first publish.
	float    camEye[3] = { 0.0f, 0.9f, 3.5f };
	float    camYaw = 0.0f, camPitch = -0.211f;
	bool     inW = false, inA = false, inS = false, inD = false, inUp = false, inDown = false;
	bool     looking = false, haveCursor = false;
	float    prevCx = 0.0f, prevCy = 0.0f;
	uint64_t lastCam = ano_timestamp_us();
	uint64_t camSeq = 0;
	uint64_t lastSnapLog = startTime;

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
		if (now - lastStream > 16000) // ~16 ms (roughly frame cadence; keeps ring pressure low)
		{
			lastStream = now;
			streamPhase += 0.05f;
			if (streamPhase >= 2.0f) streamPhase -= 2.0f;
			float tri = streamPhase < 1.0f ? streamPhase : 2.0f - streamPhase; // 0..1..0, no libm
			float bob = tri - 0.5f;

			AnoStreamRegion reg;
			if (ano_render_stream_begin(&reg) && reg.capacity >= 2) {
				for (uint32_t e = 0; e < 2; e++) {
					reg.ids[e] = e;
					memcpy(&reg.xforms[e], &streamBase[e], sizeof(mat4)); // real base pose...
					reg.xforms[e][3][1] += bob;                           // ...bobbed in Y
				}
				if (!ano_render_stream_commit(&reg, 2))
					printf("Producer: stream control ring full; tick dropped.\n");
			}
			// else: no free slice this tick; the render side holds the last published one.
		}

		if (now - lastSwap > 1000000) // 1 s
		{
			bool next = !showingFallback;
			RenderCommand cmd = {
				.kind           = RCMD_UPDATE,
				.render_id      = 0,
				.fields         = RFIELD_MESH_MAT,
				.mesh_index     = next ? FALLBACK_MESH_INDEX : originalMesh,
				.material_index = 0,
				.light_index    = ANO_RENDER_NO_LIGHT,
			};
			// Backpressure: advance only on a successful enqueue; a full ring means retry
			// next tick, never drop (policy — see ano_render_submit).
			if (ano_render_submit(bridge, &cmd)) { showingFallback = next; lastSwap = now; }
		}

		if (now - lastTint > 2000000) // ~2 s: bulk-toggle a tint on render_ids {2,3,4}
		{
			uint32_t ids[3] = { 2u, 3u, 4u };
			AnoInstanceData inst[3] = {0};
			if (!tintOn)
				for (int k = 0; k < 3; k++) { inst[k].packed[0] = 0xFFFF8040u; inst[k].packed[1] = 1u; } // bluish tint + enable bit
			RenderUpdateBatch ub = {
				.count = 3, .fields = RFIELD_USERDATA, .render_ids = ids, .instance_data = inst,
			};
			if (ano_render_submit_bulk_update(bridge, &ub)) { tintOn = !tintOn; lastTint = now; }
			// else: ring full; retry next tick (backpressure)
		}

		if (!bulkDestroyed && now - startTime > 16000000) // one-shot mass-despawn of {3,4}
		{
			uint32_t ids[2] = { 3u, 4u };
			if (ano_render_submit_bulk_destroy(bridge, ids, 2)) bulkDestroyed = true;
			// else: ring full; retry next tick
		}

		// Runtime light lifecycle stand-in (4.7 Phase 3). Each attach/update/detach is one ring
		// message; advance its flag only on a successful enqueue (backpressure, never drop).
		if (!rt5000 && now - startTime > 3000000) {
			// A CASTING runtime spot (audit 4.7 budget expansion): attaches to rid2 at a +Y offset,
			// aims down, and allocates a runtime shadow frustum so it casts its own shadow. Detached
			// at ~12 s, which frees the frustum back to the pool.
			RenderLightParams warm = { .color = {1.0f, 0.6f, 0.2f}, .intensity = 12.0f, .range = 8.0f,
			                           .innerConeCos = 0.95f, .outerConeCos = 0.80f, .type = RENDER_LIGHT_SPOT,
			                           .localDir = {0.0f, -1.0f, 0.0f}, .castsShadow = 1u };
			if (ano_render_light_attach(bridge, 5000u, 2u, &warm, 0.0f, 2.0f, 0.0f)) rt5000 = true;
		}
		if (!rt5001 && now - startTime > 3000000) {
			RenderLightParams cyan = { .color = {0.2f, 0.8f, 1.0f}, .intensity = 8.0f,
			                           .range = 5.0f, .type = RENDER_LIGHT_POINT };
			if (ano_render_light_attach(bridge, 5001u, 3u, &cyan, 0.0f, 1.0f, 0.0f)) rt5001 = true;
		}
		if (rt5000 && !rtDetached && now - lastPulse > 500000) {
			pulsePhase += 0.25f; if (pulsePhase >= 2.0f) pulsePhase -= 2.0f;
			float tri = pulsePhase < 1.0f ? pulsePhase : 2.0f - pulsePhase; // 0..1..0, no libm
			// Partial update: pulse ONLY intensity. The other params are left zero on purpose — if the
			// render-side mirror works, the light keeps its warm color/range/offset from attach.
			RenderLightParams pulse = { .intensity = 3.0f + 9.0f * tri };
			if (ano_render_light_update_fields(bridge, 5000u, &pulse, 0.0f, 0.0f, 0.0f, ANO_LIGHT_FIELD_INTENSITY))
				lastPulse = now;
		}
		// Runtime cast toggle (4.7 budget expansion): flip 5000's shadow casting without re-attaching.
		// Only castsShadow is read for ANO_LIGHT_FIELD_CAST; the warm color/intensity/offset persist.
		if (rt5000 && !rtCastOff && now - startTime > 7000000) {
			RenderLightParams off = { .castsShadow = 0u };
			if (ano_render_light_update_fields(bridge, 5000u, &off, 0.0f, 0.0f, 0.0f, ANO_LIGHT_FIELD_CAST))
				rtCastOff = true;
		}
		if (rtCastOff && !rtCastOn && now - startTime > 9500000) {
			RenderLightParams on = { .castsShadow = 1u };
			if (ano_render_light_update_fields(bridge, 5000u, &on, 0.0f, 0.0f, 0.0f, ANO_LIGHT_FIELD_CAST))
				rtCastOn = true;
		}
		if (rt5000 && !rtDetached && now - startTime > 12000000) {
			if (ano_render_light_detach(bridge, 5000u)) rtDetached = true;
		}
		ano_sleep(2000); // ~2 ms logic tick (stand-in pacing)
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
