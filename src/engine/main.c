/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include "engine/main.h" // This works

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>

// TODO: This shouldnt be in main lol
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

// Rendering module still WIP
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/instanceInit.h"
#include "vulkan_backend/structs.h"


// Structs


// Variables

extern struct VulkanGarbage vulkanGarbage;


// Function Prototypes


// Main function

int main()
{
	#ifdef DEBUG_BUILD
	printf("Running in debug mode!\n");
	#endif	
	VulkanComponents* components = (VulkanComponents*) malloc(sizeof(VulkanComponents));
	GLFWwindow *window = initWindow(components);
	if (window == NULL)
	{
	    // Handle error
	    printf("Window initialization failed.\n");
	    unInitVulkan();
	    return 0;
	}

	vulkanGarbage.window = window;

	// Initialize Vulkan
	
	components = initVulkan(window, components);
	if (components == NULL)
	{
	    // Handle error
	    printf("Vulkan initialization failed.\n");
	    unInitVulkan();
	    return 0;
	}

	vulkanGarbage.components = components;

	// Create a graphics pipeline

    // Main loop
	while (!glfwWindowShouldClose(window))
	{
        glfwPollEvents();

        // Record and submit commands for graphics and compute operations
        // ...
        // Present the image to the window
        // ...
        //printf("Test: %d\n", components->viewGroup.viewCount);
        drawFrame(components, window);
    }

    // Clean up
    unInitVulkan();

    return 0;
}
