#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include<unistd.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "./src/vulkanMaster.h"

// Structs


// Variables


// Function Prototypes


// Main function

int main()
{
	GLFWwindow *window = initWindow();
	if (window == NULL)
	{
	    // Handle error
	    printf("Window initialization failed.\n");
	    unInitVulkan();
	    return 0;
	}

	vulkanGarbage.window = window;
	
	VulkanComponents *components = initVulkan(window);
	if (components == NULL)
	{
	    // Handle error
	    printf("Vulkan initialization failed.\n");
	    unInitVulkan();
	    return 0;
	}

	vulkanGarbage.components = components;

    // Main loop
	while (!glfwWindowShouldClose(window))
	{
        glfwPollEvents();
        sleep(0.01);

        // Record and submit commands for graphics and compute operations
        // ...
        // Present the image to the window
        // ...
    }

    // Clean up
    unInitVulkan();

    return 0;
}