/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Core includes (compiled in both graphical and headless builds)
#include <anoptic_memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include "anoptic_time.h"
#include "anoptic_threads.h"
#include "anoptic_filesystem.h"

#ifndef HEADLESS_BUILD
// Renderer contract + GLFW — only compiled into the graphical engine.
#include <anoptic_render.h>
#include <vulkan/vulkan.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif
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
// Logic/ECS master: the sole render-command producer.
// Runs on its own thread while the render world owns the main thread.
// main() sets g_logicShouldStop on window close, then joins the producer
// before unInitVulkan() destroys the bridge.
static atomic_bool g_logicShouldStop = false;

void* anoLogicThreadMain(void* arg)
{
    (void)arg;
    // Stand-in producer until the real DisplayState graphics-extract exists.
    // Drives one discrete transition on a timer that toggles render_id 0's mesh
    // between its original geometry and the fallback cube.
    // Proves the producer -> SPSC ring -> render-consumer path across threads.
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
#include "anoptic_logging.h"
int main()
{
    mi_version();

    // Resolve assets relative to the executable, not the launch directory.
    // Shaders resolve against ano_fs_gamepath() directly (loadFile in pipeline.c);
    // only the CWD-relative asset loads (glTF, textures) need this.
    // Interim shim until the Resource Manager owns asset paths (docs/resourcesmg.md).
    if (!ano_fs_chdir_gamepath())
        printf("Warning: could not set the working directory to the executable's; "
               "assets will load relative to the current working directory.\n");

    #ifdef DEBUG_BUILD

    mi_option_enable(mi_option_show_errors);
    mi_option_enable(mi_option_show_stats);
    mi_option_enable(mi_option_verbose);
    printf("Running in debug mode!\n");

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
    // GLFW pins window + event handling to the main thread (mandatory on macOS).
    // The render world (all Vulkan + GLFW) runs HERE on the main thread.
    // initVulkan creates the bridge synchronously before the producer starts
    // with no readiness handshake.
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

    // Render loop (main thread): pump window events, then draw.
    // The logic thread feeds discrete ECS->render transitions concurrently.
    while (!anoShouldClose())
    {
        glfwPollEvents();
        drawFrame();
    }

    // Window closed: stop the producer FIRST and join it.
    // No submit can then race the bridge destruction in unInitVulkan().
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
