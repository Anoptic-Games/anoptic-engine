/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#ifndef VULKANCONFIG_H
#define VULKANCONFIG_H

#include <vulkan/vulkan.h>

#include "vulkan_backend/structs.h"


// Function interfaces


//====	Write functions

// Sets the passed name as the preferred GPU. If a matching device is found during instance initialization, it will be used
bool requestDevice(char* deviceName);

// Sets the passed value as the preffered frame presentation mode. If a matching index is found during swapchain creation, it will be used
bool requestPresentMode(VkPresentModeKHR presentMode);

// Sets the desired resolution for window creation/update
bool setResolution(Dimensions2D dimensions);

// Sets the monitor the window will be created at, or moved to
bool setMonitor(uint32_t index);

// Sets whether the window will be created or updated to use borderless mode
bool setBorderless(bool borderless);

//====	Read functions

// Retrieves the current preferred GPU
char* getChosenDevice();

// Ditto but for the present mode
VkPresentModeKHR getChosenPresentMode();

// You get the idea
Dimensions2D getChosenResolution();

uint32_t getChosenMonitor();

bool getChosenBorderless();

//====	Active functions

// Updates the window to the currently set configuration
bool updateWindow(GLFWwindow *window);


/* More configuration options to come as development continues */


#endif
