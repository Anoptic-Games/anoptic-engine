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
#include <anoptic_log.h>
#include <anoptic_keybindings.h>

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#include "instanceInit.h"
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"
#include "vulkan_backend/ui_raster.h"

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

// Map cursor from GLFW window coords -> framebuffer px in place. Zero window size = no-op.
static void cursorToFramebuffer(GLFWwindow* window, double* x, double* y)
{
	int winW = 0, winH = 0, fbW = 0, fbH = 0;
	glfwGetWindowSize(window, &winW, &winH);
	glfwGetFramebufferSize(window, &fbW, &fbH);
	if (winW <= 0 || winH <= 0)
		return;
	*x *= (double)fbW / (double)winW;
	*y *= (double)fbH / (double)winH;
}

// Overlay surface scale from the platform content scale, isotropic by contract (a
// divergent y scale is taken as x, warned once). Recomposes the retained UI/text blocks.
static void applyContentScale(float xs, float ys)
{
	static bool warned = false;
	if (!warned && fabsf(xs - ys) > 0.01f) {
		ano_log(ANO_WARN, "Content scale is anisotropic (%.2f x %.2f); using x.", (double)xs, (double)ys);
		warned = true;
	}
	if (xs <= 0.0f || xs == rendererState.uiScale)
		return;
	rendererState.uiScale = xs;
	ano_vk_ui_rescale(&rendererState);
	ano_vk_text_rescale(&rendererState);
}
static void contentScaleCallback(GLFWwindow* window, float xs, float ys)
{
	(void)window;
	applyContentScale(xs, ys);
}

// GLFW input callbacks: build one AnoInputEvent and forward it.
static void cursorPosCallback(GLFWwindow* window, double x, double y)
{
	cursorToFramebuffer(window, &x, &y);
	rendererState.cursorX = (float)x; // picking samples the id image in framebuffer px
	rendererState.cursorY = (float)y;
	// Logic hit-tests in overlay logical units: framebuffer px / content scale.
	float s = rendererState.uiScale > 0.0f ? rendererState.uiScale : 1.0f;
	AnoInputEvent ie = { .kind = ANO_INPUT_CURSOR_POS,
	                     .u.cursor = { (float)x / s, (float)y / s } };
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

// Raw key forwarding plus configurable render-action dispatch.
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	(void)window;
	AnoInputEvent ie = { .kind = ANO_INPUT_KEY, .u.key = { key, scancode, action, mods } };
	forward_input(&ie);
	anostr_sid bound = ano_keybindings_current_action(key, mods);
	if (bound == ANO_ACTION_LIGHTING_CYCLE && action == GLFW_PRESS) {
		AnoLightingMode next = (AnoLightingMode)(((uint32_t)ano_render_get_lighting_mode() + 1u) % (uint32_t)ANO_LIGHTING_MODE_COUNT);
		ano_render_set_lighting_mode(next);
		static const char* const names[ANO_LIGHTING_MODE_COUNT] = { "SHADOWMAP", "HYBRID", "RADIANCE_CASCADES" };
		ano_log(ANO_INFO, "Lighting mode: %s", names[next]);
	}
	if ((bound == ANO_ACTION_LOD_FINER || bound == ANO_ACTION_LOD_COARSER)
	    && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
		int32_t bias = ano_render_get_lod_bias() + (bound == ANO_ACTION_LOD_COARSER ? 1 : -1);
		ano_render_set_lod_bias(bias);
		ano_log(ANO_INFO, "LOD bias: %+d", ano_render_get_lod_bias());
	}
	if ((bound == ANO_ACTION_SHADOW_LOD_FINER || bound == ANO_ACTION_SHADOW_LOD_COARSER)
	    && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
		int32_t bias = ano_render_get_shadow_lod_bias()
		             + (bound == ANO_ACTION_SHADOW_LOD_COARSER ? 1 : -1);
		ano_render_set_shadow_lod_bias(bias);
		ano_log(ANO_INFO, "Shadow LOD bias: %+d", ano_render_get_shadow_lod_bias());
	}
	if (bound == ANO_ACTION_HIZ_TOGGLE && action == GLFW_PRESS) {
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

	// ANO_FLOAT keeps the window above normal windows (bench: unoccluded without focus).
	if (getenv("ANO_FLOAT") != NULL)
		glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);

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

	// ANO_POS=XxY places the window, in screen coordinates. Windowed mode only.
	const char* posEnv = getenv("ANO_POS");
	if (posEnv != NULL && chosenMonitor == NULL) {
		int px = 0, py = 0;
		if (sscanf(posEnv, "%dx%d", &px, &py) == 2)
			glfwSetWindowPos(window, px, py);
		else
			ano_log(ANO_WARN, "ANO_POS \"%s\" invalid (want XxY); ignoring", posEnv);
	}

	glfwSetWindowUserPointer(window, &rendererState);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
	// Overlay surface scale: seed from the live value, track monitor/DPI changes.
	{
		float xs = 1.0f, ys = 1.0f;
		glfwGetWindowContentScale(window, &xs, &ys);
		applyContentScale(xs, ys);
	}
	glfwSetWindowContentScaleCallback(window, contentScaleCallback);
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


