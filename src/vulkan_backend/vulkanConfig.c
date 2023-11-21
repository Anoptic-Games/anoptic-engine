/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */


#include "vulkan_backend/vulkanConfig.h"


// Static parameters
																// "Nothing selected" value
static VulkanSettings vulkanSettings = {.preferredDevice = "", .preferredMode = 0b111111111};

static WindowParameters windowParameters = {.width = 800, .height = 600, .monitorIndex = -1, .borderless = false};

/* If an API function isn't here, it's probably implemented in vulkanMaster.c */

// Write functions

bool requestDevice(char* deviceName)
{
	vulkanSettings.preferredDevice = deviceName;
	return true;
}

bool requestPresentMode(VkPresentModeKHR presentMode)
{
	vulkanSettings.preferredMode = presentMode;
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

// Read functions

char* getChosenDevice()
{
	return vulkanSettings.preferredDevice;
}

VkPresentModeKHR getChosenPresentMode()
{
	return vulkanSettings.preferredMode;
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

// Active functions

bool updateWindow(GLFWwindow *window)
{
    if (!window) return false;

    // Get the current context
    GLFWwindow* currentContext = glfwGetCurrentContext();

    // 1. Handle window size change
    int currentWidth, currentHeight;
    glfwGetWindowSize(window, &currentWidth, &currentHeight);
    if (currentWidth != windowParameters.width || currentHeight != windowParameters.height)
    {
        glfwSetWindowSize(window, (int)windowParameters.width, (int)windowParameters.height);
    }

    // 2. Handle monitor change (if monitorIndex != -1)
    if (windowParameters.monitorIndex != -1)
    {
        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        if (windowParameters.monitorIndex >= 0 && windowParameters.monitorIndex < count)
        {
            GLFWmonitor* targetMonitor = monitors[windowParameters.monitorIndex];
            const GLFWvidmode* mode = glfwGetVideoMode(targetMonitor);
            
            // Update the window to fullscreen on the desired monitor
            glfwSetWindowMonitor(window, targetMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    }

    // 3. Handle borderless mode
    if (windowParameters.borderless)
    {
        // Assuming you want to use the native resolution for borderless fullscreen
        // TODO: Figure out where these supposed to be used
        // int monitorCount;
        // GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(primary);

        glfwSetWindowMonitor(window, primary, 0, 0, mode->width, mode->height, GLFW_DONT_CARE);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
    } else
    {
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
    }

    // Restore the original context
    glfwMakeContextCurrent(currentContext);

    return true;
}
