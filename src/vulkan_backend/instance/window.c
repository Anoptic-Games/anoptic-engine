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

static void forward_input(const AnoInputEvent* ie); // defined below

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) // GLFW window-resize callback
{
	static uint32_t count = 0;
	// VulkanContext* ctx = glfwGetWindowUserPointer(window);
	ano_debug_log(ANO_INFO, "Resize: %d", count);
	count++;
	rendererState.framebufferResized = true; // swapchain recreate stays render-owned
	// Forward to logic
	AnoInputEvent ie = { .kind = ANO_INPUT_FRAMEBUFFER_RESIZE,
	                     .u.resize = { (uint32_t)width, (uint32_t)height } };
	forward_input(&ie);
}

// Events-ring slots reserved for lossless render->logic facts.
#define ANO_INPUT_RING_RESERVE 256u

// Forward one input sample to logic over the events ring, best-effort.
static void forward_input(const AnoInputEvent* ie)
{
	AnoSpscRing* r = &rendererState.bridge.events;
	if (!r->buffer) return; // ring not built yet
	uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed); // render is the sole producer
	uint32_t head = atomic_load_explicit(&r->head, memory_order_acquire);
	if ((tail - head) + ANO_INPUT_RING_RESERVE > r->mask) return; // keep headroom for lossless facts
	RenderEvent ev = { .kind = REVENT_INPUT, .u.input = *ie };
	(void)ano_render_emit_event(&rendererState.bridge, &ev); // room checked above
}

// GLFW input callbacks: build one AnoInputEvent and forward it.
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

// L cycles the lighting mode: shadowmap -> hybrid -> radiance cascades.
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
	// LOD bias: [ finer, ] coarser.
	if ((key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_RIGHT_BRACKET) &&
	    (action == GLFW_PRESS || action == GLFW_REPEAT)) {
		int32_t bias = ano_render_get_lod_bias() + (key == GLFW_KEY_RIGHT_BRACKET ? 1 : -1);
		ano_render_set_lod_bias(bias);
		ano_log(ANO_INFO, "LOD bias: %+d", ano_render_get_lod_bias());
	}
	// Shadow LOD bias: ; finer, ' coarser.
	if ((key == GLFW_KEY_SEMICOLON || key == GLFW_KEY_APOSTROPHE) &&
	    (action == GLFW_PRESS || action == GLFW_REPEAT)) {
		int32_t bias = ano_render_get_shadow_lod_bias() + (key == GLFW_KEY_APOSTROPHE ? 1 : -1);
		ano_render_set_shadow_lod_bias(bias);
		ano_log(ANO_INFO, "Shadow LOD bias: %+d", ano_render_get_shadow_lod_bias());
	}
	// Hi-Z occlusion toggle: H flips view 0 GPU occlusion cull.
	if (key == GLFW_KEY_H && action == GLFW_PRESS) {
		bool on = !ano_render_get_view_hiz_enable(0u);
		ano_render_set_view_hiz_enable(0u, on);
		ano_log(ANO_INFO, "Hi-Z occlusion (view 0): %s", on ? "ON" : "OFF");
	}
}

GLFWwindow* initWindow(VulkanContext* ctx, Monitors* monitors) // Initializes a GLFW window
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
	// All input flows to logic via the events ring.
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


