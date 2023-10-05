/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "engine/main.h" // This works

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>

// TODO: This shouldnt be in main lol

// Rendering module still WIP
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/instanceInit.h"
#include "vulkan_backend/structs.h"


// Structs

VulkanSettings vulkanSettings =
{ //!TODO change this dynamically via vulkanSettings.h interface
	.preferredDevice = "",
	.preferredMode = 1
};

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

	WindowParameters parameters =
	{
		.width = 800,
    	.height = 600,
    	.monitorIndex = -1,        // Desired monitor index for fullscreen, -1 for windowed
    	.borderless = 0
	};
	
	Monitors monitors =
	{
		.monitorInfos = NULL,	// Array of MonitorInfo for each monitor
		.monitorCount = 0		// Total number of monitors
	};

	vulkanGarbage.monitors = &monitors;
	cleanupMonitors(&monitors);
	printf("Here");
	enumerateMonitors(&monitors);
	
	GLFWwindow *window = initWindow(components, parameters, &monitors);
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
