/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Core includes (compiled in both graphical and headless builds)
#include <mimalloc.h>
#include <mimalloc-override.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include "anoptic_time.h"
#include "anoptic_threads.h"
#include "anoptic_filesystem.h"

#ifndef HEADLESS_BUILD
// Renderer interface + Vulkan/GLFW — only compiled into the graphical engine.
#include "engine/main.h" // This works
#include <vulkan/vulkan.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

// Structs
VulkanSettings vulkanSettings =
{ //!TODO change this dynamically via vulkanSettings.h interface
	.preferredDevice = "",
	.preferredMode = 1
};
#endif // !HEADLESS_BUILD

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



#ifndef HEADLESS_BUILD
// Logic/ECS master: the sole render-command producer. Runs on its own thread
// while the render world owns the main thread. main() sets g_logicShouldStop on
// window close, then joins, guaranteeing the producer quiesces before the bridge
// is destroyed in unInitVulkan().
static atomic_bool g_logicShouldStop = false;

void* anoLogicThreadMain(void* arg)
{
	(void)arg;
	// Stand-in producer: until the real DisplayState graphics-extract exists, drive
	// one discrete transition across the bridge on a timer — toggle render_id 0's
	// mesh between its original geometry and the fallback cube. Proves the producer
	// -> SPSC ring -> render-consumer path across the thread boundary.
	AnoRenderBridge* bridge = anoRenderBridge();
	uint32_t originalMesh   = anoRenderEntity0Mesh();
	bool showingFallback    = false;
	uint64_t lastSwap       = ano_timestamp_us();

	while (!atomic_load(&g_logicShouldStop))
	{
		uint64_t now = ano_timestamp_us();
		if (now - lastSwap > 1000000) // 1 s
		{
			lastSwap = now;
			showingFallback = !showingFallback;
			RenderCommand cmd = {
				.kind           = RCMD_UPDATE,
				.render_id      = 0,
				.fields         = RFIELD_MESH_MAT,
				.mesh_index     = showingFallback ? FALLBACK_MESH_INDEX : originalMesh,
				.material_index = 0,
				.light_index    = ANO_RENDER_NO_LIGHT,
			};
			if (!ano_render_submit(bridge, &cmd))
				printf("Producer: command ring full; swap dropped.\n");
		}
		ano_sleep(2000); // ~2 ms logic tick (stand-in pacing)
	}
	return NULL;
}
#endif // !HEADLESS_BUILD

// Main function
#include "anoptic_strings.h"
#include "anoptic_logging.h"
int main()
{
    mi_version();

    // Resolve assets relative to the executable, not the launch directory, so the
    // binary runs from any working directory. Shaders already use PROJECT_ROOT;
    // only the CWD-relative asset loads (glTF, textures) needed this. Interim shim
    // until the Resource Manager owns asset paths.
    if (!ano_fs_chdir_gamepath())
        printf("Warning: could not set the working directory to the executable's; "
               "assets will load relative to the current working directory.\n");

	#ifdef DEBUG_BUILD

    mi_option_enable(mi_option_show_errors);
    mi_option_enable(mi_option_show_stats);
    mi_option_enable(mi_option_verbose);
    mi_option_set(mi_option_reserve_huge_os_pages, 4);

    // Try to allocate 4 GB of HUGE pages.
    int gigaMallocStatus = mi_reserve_huge_os_pages_at(4, 0, 10000);
    printf("Huge Page Status: %d\n", gigaMallocStatus);
	printf("Running in debug mode!\n");



    autoStringTest();



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

#ifndef HEADLESS_BUILD
	// GLFW pins window + event handling to the main thread (mandatory on macOS), so
	// the render world (all Vulkan + GLFW) runs HERE on the main thread. initVulkan
	// creates the bridge synchronously before the producer starts, so there is no
	// readiness handshake.
	if (!initVulkan())
	{
	    printf("Vulkan initialization failed.\n");
	    return -1;
	}

	// Logic/ECS master spun onto its own thread as the sole render-command producer.
	anothread_t logicThread;
	if (ano_thread_create(&logicThread, NULL, anoLogicThreadMain, NULL) != 0)
	{
	    printf("Failed to spawn logic thread.\n");
	    unInitVulkan();
	    return -1;
	}

	// Render loop (main thread): pump window events, then draw. The logic thread
	// feeds discrete ECS->render transitions across the bridge concurrently.
	while (!anoShouldClose())
	{
	    glfwPollEvents();
	    drawFrame();
	}

	// Window closed: stop the producer FIRST and join it, so no submit can race the
	// bridge destruction that unInitVulkan() performs.
	atomic_store(&g_logicShouldStop, true);
	ano_thread_join(logicThread, NULL);

	unInitVulkan();
#else
	// Headless engine: no renderer. Console / server entry point.
	printf("Anoptic Engine — headless console mode.\n");
    while (true) {
        printf("Waiting...\n");
        ano_sleep(3 * 1000000);
    };
	// TODO: simulation / server loop goes here.
#endif

    return 0;
}
