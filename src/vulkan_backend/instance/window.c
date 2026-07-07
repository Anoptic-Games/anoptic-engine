/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <anoptic_memory.h>
#include <anoptic_logging.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"

void enumerateMonitors(Monitors* monitors) // Instance creation helper
{
	GLFWmonitor** glfwMonitors = glfwGetMonitors(&(monitors->monitorCount));
	monitors->monitorInfos = mi_malloc(monitors->monitorCount * sizeof(MonitorInfo));
	for (int i = 0; i < monitors->monitorCount; i++) 
	{
		monitors->monitorInfos[i].modes = glfwGetVideoModes(glfwMonitors[i], &(monitors->monitorInfos[i].modeCount));
	}
}

static void forward_input(const AnoInputEvent* ie); // defined below; the resize callback forwards too

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) // Called by GLFW on window resize, not part of instance creation but related
{
	static uint32_t count = 0;
	// VulkanContext* ctx = glfwGetWindowUserPointer(window);
	ano_debug_log(ANO_INFO, "Resize: %d", count);
	count++;
	rendererState.framebufferResized = true; // swapchain recreate stays render-owned
	// Also forward to logic so it learns the new aspect (for UI / cursor interpretation).
	AnoInputEvent ie = { .kind = ANO_INPUT_FRAMEBUFFER_RESIZE,
	                     .u.resize = { (uint32_t)width, (uint32_t)height } };
	forward_input(&ie);
}

// Slots of the events ring reserved for LOSSLESS render->logic facts (slot retirement, batch acks).
// Input is best-effort and shares the one ring with those facts, so it must never fill the ring's
// last slots and starve them: input is dropped once free space falls to this margin (audit 4.11).
#define ANO_INPUT_RING_RESERVE 256u

// Forward one input sample to the logic master over the events ring (audit 4.11). All GLFW input
// flows through here. Render must NOT block (it shares the thread with glfwPollEvents), so input is
// best-effort: it is dropped (never blocks, never overruns the lossless reserve) when the ring is
// near full. The reserved headroom guarantees a same-frame REVENT_SLOT_RETIRED still has room.
static void forward_input(const AnoInputEvent* ie)
{
	AnoSpscRing* r = &rendererState.bridge.events;
	if (!r->buffer) return; // ring not built yet (a window-creation-time callback); nothing to forward
	uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed); // render is the sole producer
	uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
	if ((tail - head) + ANO_INPUT_RING_RESERVE > r->mask) return; // keep headroom for lossless facts
	RenderEvent ev = { .kind = REVENT_INPUT, .u.input = *ie };
	(void)ano_render_emit_event(&rendererState.bridge, &ev); // room checked above
}

// GLFW input callbacks: build one AnoInputEvent and forward it. GLFW codes are passed verbatim (the
// keymap layer is the game's). The cursor callback also caches the position for the picking readback.
static void cursorPosCallback(GLFWwindow* window, double x, double y)
{
	(void)window;
	rendererState.cursorX = (float)x;
	rendererState.cursorY = (float)y;
	AnoInputEvent ie = { .kind = ANO_INPUT_CURSOR_POS, .u.cursor = { (float)x, (float)y } };
	forward_input(&ie);
}
static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	(void)window;
	AnoInputEvent ie = { .kind = ANO_INPUT_MOUSE_BUTTON, .u.button = { button, action, mods } };
	forward_input(&ie);
}
static void scrollCallback(GLFWwindow* window, double dx, double dy)
{
	(void)window;
	AnoInputEvent ie = { .kind = ANO_INPUT_SCROLL, .u.scroll = { (float)dx, (float)dy } };
	forward_input(&ie);
}
static void windowFocusCallback(GLFWwindow* window, int focused)
{
	(void)window;
	AnoInputEvent ie = { .kind = ANO_INPUT_FOCUS, .u.focus = { focused } };
	forward_input(&ie);
}
static void charCallback(GLFWwindow* window, unsigned int codepoint)
{
	(void)window;
	AnoInputEvent ie = { .kind = ANO_INPUT_CHAR, .u.ch = { codepoint } };
	forward_input(&ie);
}

// L cycles the lighting mode: shadow maps -> hybrid (RC point + shadow-mapped dir/spot) ->
// radiance cascades. Edge-triggered on press so one keystroke advances one mode. The setter just
// updates the render state; updateCullingBuffers publishes it into the GlobalUBO. See AnoLightingMode.
// Runs on the main (render) thread alongside the frame loop.
// These L/H/[]/;' keys stay render-side dev tooling (they tune render-thread-only state); the same
// keystrokes are ALSO forwarded to logic, which today consumes only the camera-control keys.
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	(void)window;
	AnoInputEvent ie = { .kind = ANO_INPUT_KEY, .u.key = { key, scancode, action, mods } };
	forward_input(&ie);
	if (key == GLFW_KEY_L && action == GLFW_PRESS) {
		AnoLightingMode next = (AnoLightingMode)(((uint32_t)ano_render_get_lighting_mode() + 1u) % (uint32_t)ANO_LIGHTING_MODE_COUNT);
		ano_render_set_lighting_mode(next);
		static const char* const names[ANO_LIGHTING_MODE_COUNT] = { "SHADOWMAP", "HYBRID", "RADIANCE_CASCADES" };
		ano_log(ANO_INFO, "Lighting mode: %s", names[next]);
	}
	// LOD bias inspection (review 4.9 step 2): [ biases finer, ] coarser. Repeats while held so the
	// scene can be swept from finest to coarsest; a large bias pins every LOD-chain mesh to one end.
	if ((key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_RIGHT_BRACKET) &&
	    (action == GLFW_PRESS || action == GLFW_REPEAT)) {
		int32_t bias = ano_render_get_lod_bias() + (key == GLFW_KEY_RIGHT_BRACKET ? 1 : -1);
		ano_render_set_lod_bias(bias);
		ano_log(ANO_INFO, "LOD bias: %+d", ano_render_get_lod_bias());
	}
	// Shadow-caster LOD offset inspection (review 4.9 step 2, revised): ; finer shadows, ' coarser.
	// Shadows track the view-0 LOD by default (offset 0); this trades shadow cost against silhouette
	// quality live by biasing RELATIVE to that matched level (0 = exact match with the visible mesh).
	if ((key == GLFW_KEY_SEMICOLON || key == GLFW_KEY_APOSTROPHE) &&
	    (action == GLFW_PRESS || action == GLFW_REPEAT)) {
		int32_t bias = ano_render_get_shadow_lod_bias() + (key == GLFW_KEY_APOSTROPHE ? 1 : -1);
		ano_render_set_shadow_lod_bias(bias);
		ano_log(ANO_INFO, "Shadow LOD bias: %+d", ano_render_get_shadow_lod_bias());
	}
	// Hi-Z occlusion toggle (review 4.9 step 3): H flips the main view's GPU occlusion cull on/off for
	// A/B inspection (default off). With it on, entities fully behind nearer geometry stop drawing.
	if (key == GLFW_KEY_H && action == GLFW_PRESS) {
		bool on = !ano_render_get_view_hiz_enable(0u);
		ano_render_set_view_hiz_enable(0u, on);
		ano_log(ANO_INFO, "Hi-Z occlusion (view 0): %s", on ? "ON" : "OFF");
	}
}

GLFWwindow* initWindow(VulkanContext* ctx, Monitors* monitors) // Initializes a GLFW window, necessary for instance creation but general in scope
{
	if (!glfwInit())
	{
		ano_log(ANO_FATAL, "Failed to initialize GLFW!");
		return NULL;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	
	// Choose the monitor
	GLFWmonitor* chosenMonitor = NULL;
	uint32_t monitorIndex = getChosenMonitor();
	if (monitorIndex >= 0 && monitorIndex < monitors->monitorCount)
	{
		GLFWmonitor** glfwMonitors = glfwGetMonitors(NULL);
		chosenMonitor = glfwMonitors[monitorIndex];
	}
	else if (monitorIndex >= 0)
	{ // Default to primary if index is out of range
		chosenMonitor = glfwGetPrimaryMonitor();
	}

	// If borderless fullscreen is requested
	Dimensions2D resolution = getChosenResolution();
	if (getChosenBorderless() && chosenMonitor)
	{
		const GLFWvidmode* mode = glfwGetVideoMode(chosenMonitor);
		resolution.width = mode->width;
		resolution.height = mode->height;
	}

	if (monitorIndex == -1)
	{
		chosenMonitor = NULL;
	}
	
	GLFWwindow *window = glfwCreateWindow((int)resolution.width, (int)resolution.height, "Vulkan", chosenMonitor, NULL);
	
	glfwSetWindowUserPointer(window, &rendererState);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
	// All input flows to the logic master via the events ring (audit 4.11). GLFW pins these to the
	// render thread; each callback forwards one AnoInputEvent.
	glfwSetKeyCallback(window, keyCallback);            // also tunes render-side debug state (L/H/[]/;')
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetScrollCallback(window, scrollCallback);
	glfwSetWindowFocusCallback(window, windowFocusCallback);
	glfwSetCharCallback(window, charCallback);

	return window;
}


void cleanupMonitors(Monitors* monitors)
{
	if (monitors->monitorInfos)
	{
		free(monitors->monitorInfos);
		monitors->monitorInfos = NULL;
		monitors->monitorCount = 0;
	}
}


