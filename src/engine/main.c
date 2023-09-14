//========================================================================
// Anoptic Engine 0.01
//------------------------------------------------------------------------
// Copyright (c) 2023 Matei Anghel
// Copyright (c) 2023 Cristian Necsoiu
//
// This file is part of 'The Anoptic Engine'.
// 
// 'The Anoptic Engine' is free software: you can redistribute it
// and/or modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation, version 3 of the License.
//
// 'The Anoptic Engine' is distributed WITHOUT ANY WARRANTY, without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
// See the GNU General Public License for more details.
//
// This notice may not be removed or altered from any source distribution.
//
// You should have received a copy of the GNU General Public License along with this software. 
// If not, see <https://www.gnu.org/licenses/>.
//
//========================================================================

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
