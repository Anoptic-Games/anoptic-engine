/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: initWindow's window-creation failure arm. The function guards glfwInit's failure with
// FATAL + return NULL (window.c:173-:179) but never checks glfwCreateWindow (window.c:214, docs/BUGS.md,
// Render / Vulkan backend / Implementation) 〜 GLFW returns NULL there for a headless/dead display
// (GLFW_PLATFORM_ERROR) or a 0x0 configured resolution (GLFW_INVALID_VALUE) 〜 so the NULL handle rides
// glfwSetWindowUserPointer (:226), glfwSetFramebufferSizeCallback (:227), glfwGetWindowContentScale
// (:231) and six more callback registrations (:234-:241), every one requiring a valid window handle
// (assert in a debug GLFW, a straight NULL deref in release), and boot crashes inside GLFW before the
// function can honor its own header contract "returns a window pointer or NULL on failure"
// (instanceInit.h:26) whose caller-side FATAL-and-teardown arm (vulkanMaster.c:324-:327) exists to hear
// exactly this. Harness: compiles the REAL window.c TU and satisfies its link seams (glfw entry points,
// vulkanConfig getters, render-api knobs, the overlay rescalers, rendererState) with stubs; every
// handle-taking glfw stub records a NULL window as the contract breach GLFW would assert on, instead of
// crashing, so the failure is a countable ledger. CONTROL: a successful create returns the minted
// handle, registers all ten post-create calls on it, and touches no NULL 〜 a fix that refuses every
// window cannot pass. TRIGGER: the create fails; the contract demands NULL back with the dead handle
// left untouched, and today ten GLFW entry points consume it. A crash is a valid failure signal.
// Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/instance/instanceInit.h"
#include <anoptic_render.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledger 〜 what the glfw seam saw */

static int         g_windowStorage;                          // backing byte for the minted handle
#define LIVE_WINDOW ((GLFWwindow*)(void*)&g_windowStorage)

static bool        g_failCreate;      // trigger switch: glfwCreateWindow returns NULL
static uint32_t    g_createCalls;     // glfwCreateWindow calls since reset
static int         g_lastW, g_lastH;  // last requested extent
static GLFWmonitor* g_lastMonitor;    // last requested monitor
static uint32_t    g_nullCalls;       // handle-taking glfw calls that received NULL 〜 the breach
static uint32_t    g_goodCalls;       // handle-taking glfw calls on the live handle
static char        g_nullNames[512];  // breach roll for the report
static GLFWwindow* g_userPtrWindow;   // glfwSetWindowUserPointer's window argument
static void*       g_userPtr;         // ...and its pointer argument

// Clears the ledger and the per-phase renderer state.
static void reset_ledger(void)
{
    g_createCalls = 0; g_lastW = g_lastH = 0; g_lastMonitor = NULL;
    g_nullCalls = 0; g_goodCalls = 0; g_nullNames[0] = '\0';
    g_userPtrWindow = NULL; g_userPtr = NULL;
}

// in: seam name + the window it was handed. NULL is GLFW's assert(window != NULL) 〜 recorded, not crashed.
static void touch(const char* fn, GLFWwindow* w)
{
    if (w == NULL) {
        g_nullCalls++;
        size_t len = strlen(g_nullNames);
        snprintf(g_nullNames + len, sizeof g_nullNames - len, "%s%s", len ? " " : "", fn);
        printf(" step: %s(NULL) 〜 GLFW contract breach\n", fn);
    } else {
        g_goodCalls++;
    }
}

// ANO_FLOAT / ANO_POS must not steer the TU's getenv arms.
static void scrub_env(void)
{
#ifdef _WIN32
    _putenv("ANO_FLOAT=");
    _putenv("ANO_POS=");
#else
    unsetenv("ANO_FLOAT");
    unsetenv("ANO_POS");
#endif
}


/* Link seams 〜 globals + engine helpers window.c externs (vulkanMaster.c / vulkanConfig.c /
   render_api.c / ui_raster.c / text_raster.c deliberately not linked) */

RendererState rendererState;

uint32_t     getChosenMonitor(void)    { return (uint32_t)-1; }                   // windowed
Dimensions2D getChosenResolution(void) { return (Dimensions2D){ 1280, 720 }; }
bool         getChosenBorderless(void) { return false; }

void            ano_render_set_lighting_mode(AnoLightingMode mode) { (void)mode; }
AnoLightingMode ano_render_get_lighting_mode(void) { return (AnoLightingMode)0; }
void    ano_render_set_lod_bias(int32_t bias) { (void)bias; }
int32_t ano_render_get_lod_bias(void) { return 0; }
void    ano_render_set_shadow_lod_bias(int32_t bias) { (void)bias; }
int32_t ano_render_get_shadow_lod_bias(void) { return 0; }
void ano_render_set_view_hiz_enable(uint32_t view, bool enable) { (void)view; (void)enable; }
bool ano_render_get_view_hiz_enable(uint32_t view) { (void)view; return false; }

void ano_vk_ui_rescale(RendererState* state)   { (void)state; }
void ano_vk_text_rescale(RendererState* state) { (void)state; }


/* Link seams 〜 the glfw entry points window.c references (glfw not linked) */

int  glfwInit(void) { return GLFW_TRUE; }
int  glfwGetError(const char** description) { if (description) *description = "stub"; return 0; }
void glfwWindowHint(int hint, int value) { (void)hint; (void)value; }

GLFWmonitor** glfwGetMonitors(int* count) { if (count) *count = 0; return NULL; }
const GLFWvidmode* glfwGetVideoModes(GLFWmonitor* monitor, int* count) { (void)monitor; if (count) *count = 0; return NULL; }
GLFWmonitor* glfwGetPrimaryMonitor(void) { static int s; return (GLFWmonitor*)(void*)&s; }
const GLFWvidmode* glfwGetVideoMode(GLFWmonitor* monitor) { (void)monitor; static const GLFWvidmode m = { .width = 1920, .height = 1080 }; return &m; }

// The seam under audit: NULL on the trigger phase, contract-faithful to a failed create.
GLFWwindow* glfwCreateWindow(int width, int height, const char* title, GLFWmonitor* monitor, GLFWwindow* share)
{
    (void)title; (void)share;
    g_createCalls++; g_lastW = width; g_lastH = height; g_lastMonitor = monitor;
    if (g_failCreate) { printf(" step: glfwCreateWindow -> NULL (platform failure)\n"); return NULL; }
    return LIVE_WINDOW;
}

void glfwSetWindowPos(GLFWwindow* window, int xpos, int ypos) { (void)xpos; (void)ypos; touch("glfwSetWindowPos", window); }
void glfwSetWindowUserPointer(GLFWwindow* window, void* pointer) { touch("glfwSetWindowUserPointer", window); g_userPtrWindow = window; g_userPtr = pointer; }
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow* window, GLFWframebuffersizefun callback) { (void)callback; touch("glfwSetFramebufferSizeCallback", window); return NULL; }
void glfwGetWindowContentScale(GLFWwindow* window, float* xscale, float* yscale) { touch("glfwGetWindowContentScale", window); if (xscale) *xscale = 1.0f; if (yscale) *yscale = 1.0f; }
GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow* window, GLFWwindowcontentscalefun callback) { (void)callback; touch("glfwSetWindowContentScaleCallback", window); return NULL; }
GLFWkeyfun glfwSetKeyCallback(GLFWwindow* window, GLFWkeyfun callback) { (void)callback; touch("glfwSetKeyCallback", window); return NULL; }
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* window, GLFWmousebuttonfun callback) { (void)callback; touch("glfwSetMouseButtonCallback", window); return NULL; }
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* window, GLFWcursorposfun callback) { (void)callback; touch("glfwSetCursorPosCallback", window); return NULL; }
GLFWscrollfun glfwSetScrollCallback(GLFWwindow* window, GLFWscrollfun callback) { (void)callback; touch("glfwSetScrollCallback", window); return NULL; }
GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow* window, GLFWwindowfocusfun callback) { (void)callback; touch("glfwSetWindowFocusCallback", window); return NULL; }
GLFWcharfun glfwSetCharCallback(GLFWwindow* window, GLFWcharfun callback) { (void)callback; touch("glfwSetCharCallback", window); return NULL; }
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height) { touch("glfwGetWindowSize", window); if (width) *width = 1280; if (height) *height = 720; }
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height) { touch("glfwGetFramebufferSize", window); if (width) *width = 1280; if (height) *height = 720; }


int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    scrub_env();
    VulkanContext ctx = {0};
    Monitors monitors = {0};

    // control: the create succeeds 〜 initWindow must return the minted handle and register all
    // ten post-create calls on it, so a fix that refuses every window cannot pass
    reset_ledger();
    memset(&rendererState, 0, sizeof rendererState);
    g_failCreate = false;
    GLFWwindow* win = initWindow(&ctx, &monitors);
    CHECK(win == LIVE_WINDOW, "control: initWindow returns the created window");
    CHECK(g_createCalls == 1, "control: exactly one glfwCreateWindow");
    CHECK(g_lastMonitor == NULL && g_lastW == 1280 && g_lastH == 720, "control: windowed create at the configured resolution");
    CHECK(g_nullCalls == 0, "control: no glfw entry point sees a NULL window");
    CHECK(g_userPtrWindow == LIVE_WINDOW && g_userPtr == (void*)&rendererState, "control: user pointer registered on the live window");
    CHECK(g_goodCalls == 10, "control: all ten post-create registrations land on the live window");

    // trigger: the create fails (headless / dead display / 0x0 resolution) 〜 the header contract
    // (instanceInit.h:26) demands NULL back with the dead handle untouched; today window.c:214's
    // missing guard hands the NULL to ten GLFW entry points that assert or deref it
    printf("trigger: glfwCreateWindow fails 〜 expect NULL returned and the handle never consumed\n");
    reset_ledger();
    memset(&rendererState, 0, sizeof rendererState);
    g_failCreate = true;
    GLFWwindow* win2 = initWindow(&ctx, &monitors);
    CHECK(win2 == NULL, "trigger: initWindow returns NULL on failure (instanceInit.h:26)");
    CHECK(g_nullCalls == 0, "trigger: no glfw entry point consumed the NULL window (window.c:214 unguarded)");
    if (g_nullCalls)
        printf(" breached: %" PRIu32 " call(s) 〜 %s\n", g_nullCalls, g_nullNames);

    if (failures) {
        printf("anotest_windowcreateguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_windowcreateguard: all passed\n");
    return 0;
}
