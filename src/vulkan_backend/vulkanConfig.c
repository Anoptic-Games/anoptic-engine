/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include "vulkan_backend/vulkanConfig.h"


// Static parameters. preferredMode MAX_ENUM = "nothing selected". preferredMsaa default 4x.
static VulkanSettings vulkanSettings = {.preferredDevice = "", .preferredMode = VK_PRESENT_MODE_MAX_ENUM_KHR, .preferredMsaa = 4};

static WindowParameters windowParameters = {.width = 800, .height = 600, .monitorIndex = ANO_WINDOWED_MONITOR, .borderless = false};

/* If an API function isn't here, it's probably implemented in vulkanMaster.c */

/* Write Functions */

bool requestDevice(const char* deviceName)
{
	vulkanSettings.preferredDevice = deviceName;
	return true;
}

bool requestPresentMode(VkPresentModeKHR presentMode)
{
	vulkanSettings.preferredMode = presentMode;
	return true;
}

bool requestMsaaSamples(uint32_t samples)
{
	vulkanSettings.preferredMsaa = samples;
	return true;
}

bool setResolution(Dimensions2D dimensions)
{
	windowParameters.width = dimensions.width;
	windowParameters.height = dimensions.height;
	return true;
}

bool setMonitor(uint32_t index)
{
	windowParameters.monitorIndex = index;
	return true;
}

bool setBorderless(bool borderless)
{
	windowParameters.borderless = borderless;
	return true;
}

/* Read Functions */

const char* getChosenDevice()
{
	return vulkanSettings.preferredDevice;
}

VkPresentModeKHR getChosenPresentMode()
{
	return vulkanSettings.preferredMode;
}

uint32_t getChosenMsaaSamples()
{
	return vulkanSettings.preferredMsaa;
}

Dimensions2D getChosenResolution()
{
	Dimensions2D dimensions = {.width = windowParameters.width, .height = windowParameters.height};
	return dimensions;
}

uint32_t getChosenMonitor()
{
	return windowParameters.monitorIndex;
}

bool getChosenBorderless()
{
	return windowParameters.borderless;
}

/* Active Functions */

bool updateWindow(GLFWwindow *window)
{
    if (!window) return false;

    // Current context
    GLFWwindow* currentContext = glfwGetCurrentContext();

    // 1. Window size
    int currentWidth, currentHeight;
    glfwGetWindowSize(window, &currentWidth, &currentHeight);
    if (currentWidth != windowParameters.width || currentHeight != windowParameters.height)
    {
        glfwSetWindowSize(window, (int)windowParameters.width, (int)windowParameters.height);
    }

    // 2. Monitor. -1 wraps to UINT32_MAX, ignored (structs.h)
    if (windowParameters.monitorIndex != ANO_WINDOWED_MONITOR)
    {
        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        if (windowParameters.monitorIndex < count)
        {
            GLFWmonitor* targetMonitor = monitors[windowParameters.monitorIndex];
            const GLFWvidmode* mode = glfwGetVideoMode(targetMonitor);
            
            // Fullscreen on target monitor
            glfwSetWindowMonitor(window, targetMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    }

    // 3. Borderless
    if (windowParameters.borderless)
    {
        // Native res borderless
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primary);

        glfwSetWindowMonitor(window, primary, 0, 0, mode->width, mode->height, GLFW_DONT_CARE);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
    } else
    {
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
    }

    // Restore context
    glfwMakeContextCurrent(currentContext);

    return true;
}
