/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include "engine/main.h" // This works

// Includes
#include <mimalloc.h>
#include <mimalloc-override.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <string.h>
#include "anoptic_time.h"

// TODO: Figure out if this actually needs to be in main.c
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

// Rendering module still WIP

// Structs

VulkanSettings vulkanSettings =
{ //!TODO change this dynamically via vulkanSettings.h interface
	.preferredDevice = "",
	.preferredMode = 1
};

// Variables

// Helper Funcs (?)

double findAverage(const uint64_t arr[], uint32_t n) {
    if (n == 0) {
        return 0; // Avoid division by zero
    }

    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += arr[i];
    }

    return (double)sum / n;
}

// TODO: Move this somewhere more sane
void measureFrameTime()
{
	static uint64_t frameTimes[200] = {};
	static uint32_t timeIndex = 0;

	uint64_t currentTime = ano_timestamp_us();
	if (timeIndex > 0) {
		frameTimes[timeIndex - 1] = currentTime - frameTimes[timeIndex - 1];
	}

	if (timeIndex == 199) {
		frameTimes[timeIndex] = currentTime - frameTimes[timeIndex];

		// Print the frame times
		for (int i = 0; i < 200; i++) {
            // TODO: uhh errrm
			printf("Frame %d: %llu\n", i, frameTimes[i]);
		}
		
		printf("Average frametime: %f\n", findAverage(frameTimes, 199)/1000);
		
		timeIndex = 0;
	} else {
		frameTimes[timeIndex] = currentTime;
		timeIndex++;
	}
}



// Main function
#include "anoptic_logging.h"
int main()
{
    mi_version();
	#ifdef DEBUG_BUILD

    mi_option_enable(mi_option_show_errors);
    mi_option_enable(mi_option_show_stats);
    mi_option_enable(mi_option_verbose);
    mi_option_set(mi_option_reserve_huge_os_pages, 4);

    // Try to allocate 4 GB of HUGE pages.
    int gigaMallocStatus = mi_reserve_huge_os_pages_at(4, 0, 10000);
    printf("Huge Page Status: %d\n", gigaMallocStatus);
	printf("Running in debug mode!\n");

    int ladcount = 128;
    int *theboys = mi_malloc(ladcount * sizeof(int));
    for (int i = 0; i < ladcount; i++) {
        theboys[i] = i + 1;
    }

    printf("Printing mi_malloc'd heap contents");
    for (int i = 0; i < ladcount; i++) {
        printf("Lad %d contents: %d\n", i, theboys[i]);
    }

    mi_free(theboys);

    // huge malloc
    //uint64_t *hugeBox = mi_malloc(1000000000 * sizeof(uint64_t));   // This fucks up the heap
    //mi_free(hugeBox);

    ano_log_init();
    for(int i = 0; i < 172; i++) {
        ano_log_error("Enqueued Log Message # %d\n", (i + 1));
    }

    ano_log_error("01234567890123456789012");

    ano_log_debug_now("Instantaneous Debug Message!\n");

    for(int i = 0; i < 216; i++) {
        ano_log_error("Enqueued Log Message # %d\n", (i + 1));
    }

    ano_log_debug_now("Instantaneous Debug Message!\n");

    ano_log_cleanup();

	#endif

	// Initialize Vulkan
	if (!initVulkan())
	{
	    // Handle error
	    printf("Vulkan initialization failed.\n");
	    unInitVulkan();
	    return 0;
	}

	// Create a graphics pipeline

	//parseGltf("viking_room.gltf");

    // Main loop
	while (!anoShouldClose())
	{	
        glfwPollEvents();

        // Record and submit commands for graphics and compute operations
        // ...
        // Present the image to the window
        // ...
        //printf("Test: %d\n", components->viewGroup.viewCount);
        drawFrame();
		//measureFrameTime();
    }

    // Clean up
    // unInitVulkan();
    return 0;
}
