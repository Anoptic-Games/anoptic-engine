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
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

// Rendering module still WIP
#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/instanceInit.h"
#include "vulkan_backend/structs.h"

// Testing
#ifdef DEBUG_BUILD
#include "anoptic_time.h"
#include <time.h>
#endif

// Structs


// Variables

extern struct VulkanGarbage vulkanGarbage;


// Function Prototypes


// Main function

int main()
{
	#ifdef DEBUG_BUILD

	printf("Running in debug mode!\n");
    uint64_t nsTS = ano_timestamp_raw();
    uint64_t usTS = ano_timestamp_us();
    uint32_t msTS = ano_timestamp_ms();

    printf("nanoseconds stamped:\t%llu\n"
           "microseconds stamped:\t%llu\n"
           "milliseconds stamped:\t%u\n",
           nsTS, usTS, msTS);


    int64_t unixtime = ano_timestamp_unix(); // Assume this function returns the current Unix timestamp
    time_t time = (time_t) unixtime;

    struct tm *utc_time = gmtime(&time);
    char buffer[80];
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S UTC", utc_time);

    printf("Current UTC time: %s\n", buffer);

    struct tm *local_time = localtime(&time);
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S %Z", local_time);

    printf("Current Local time: %s\n", buffer);


    uint64_t startTime, endTime;
    printf("\nStarting nano sleep 1a: 100 nanoseconds\n");
    startTime = ano_timestamp_raw();
    ano_busywait(100);
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 1a\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);

    printf("Starting nano sleep 1b: 10000 nanoseconds\n");
    startTime = ano_timestamp_raw();
    ano_busywait(10000);
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 1b\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);

    printf("Starting nano sleep 1c: 16 ms\n");
    startTime = ano_timestamp_raw();
    ano_busywait(16000000);
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 1c\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);

    printf("Starting nano sleep 1d: 1s\n");
    startTime = ano_timestamp_raw();
    ano_busywait((uint64_t)(1 * 1e9));
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 1d\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);

    printf("Starting OS sleep 2a: 10 microseconds\n");
    startTime = ano_timestamp_raw();
    ano_sleep(10);
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 2a\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);

    printf("Starting OS sleep 2b: 1000 microseconds\n");
    startTime = ano_timestamp_raw();
    ano_sleep(1000);
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 2b\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);

    printf("Starting OS sleep 2c: 16ms\n");
    startTime = ano_timestamp_raw();
    ano_sleep(16000);
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 2c\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);

    printf("Starting OS sleep 2d: 2 seconds\n");
    startTime = ano_timestamp_raw();
    ano_sleep((uint64_t)2e6);
    endTime = ano_timestamp_raw();
    printf("Finished Sleep 2d\n"
           "Time Elapsed: \t%llu\n\n",
           endTime - startTime);
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
