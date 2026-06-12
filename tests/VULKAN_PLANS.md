# Vulkan Backend Unit Test Suite Plan

This document outlines the strategy for creating a targeted unit test suite for the Anoptic Engine's Vulkan backend. The goal is to ensure stability and correctness by validating both program-specific behavior and strict compliance with the Vulkan API specification.

## 1. Overview and Test Framework Integration

The Vulkan tests will be integrated into the existing `tests/` directory following the established pattern (e.g., standalone C executables like `anotest_logging.c`). Due to the headless nature of unit tests, window surface dependencies will need to be mocked or run via an off-screen/headless Vulkan context where appropriate to allow CI/CD automation without a physical display.

## 2. Validating Program-Specific Behavior

These tests will focus on the Anoptic Engine's internal Vulkan abstractions, specifically the lifecycle and state management handled in `vulkanMaster.c` and `components.h`.

*   **Initialization and Teardown Lifecycle (`anotest_vk_lifecycle.c`)**
    *   **Test:** Call `initVulkan()` followed by `unInitVulkan()` repeatedly.
    *   **Expectation:** `VulkanComponents` are populated correctly, no memory leaks occur, and `VulkanGarbage` successfully cleans up all remaining handles.
*   **Component & Render Primitive Tracking (`anotest_vk_components.c`)**
    *   **Test:** Simulate the allocation and registration of `RenderPrimitives` (vertices, indices, textures) and verify that their `usageCount` increments/decrements as expected.
    *   **Expectation:** `PipelinePrototype` and `MeshBuffer` structs update correctly, allowing for safe binding and garbage collection when usage hits zero.
*   **Vulkan Garbage Collector (`anotest_vk_garbage.c`)**
    *   **Test:** Artificially push valid and null handles to `vulkanGarbage`.
    *   **Expectation:** The cleanup routines securely check for valid objects and tear them down without segfaulting or double-freeing.
*   **Pipeline Setup Validation (`anotest_vk_pipeline.c`)**
    *   **Test:** Validate that `DataPattern` and `DataChain` structures build consistent descriptor set layouts without crashing.

## 3. Validating Vulkan Spec Compliance

These tests will enforce that the engine interacts with the driver according to the Vulkan specification, ensuring portability across different vendors (NVIDIA, AMD, Intel, etc.).

*   **Validation Layer Enforcement (`anotest_vk_compliance_layers.c`)**
    *   **Strategy:** Run all Vulkan tests with `VK_LAYER_KHRONOS_validation` explicitly enabled via `VK_EXT_debug_utils`.
    *   **Expectation:** The tests must install a debug messenger callback that explicitly **fails the test** if any `ERROR` or `WARNING` severity messages are intercepted from the validation layers.
*   **Memory Allocation and Alignment (`anotest_vk_memory.c`)**
    *   **Test:** Allocate buffers/images through `src/vulkan_backend/memory/memory.c`.
    *   **Expectation:** Verify that `vkGetBufferMemoryRequirements` rules (specifically `alignment` constraints and `memoryTypeBits`) are strictly adhered to before binding memory.
*   **Synchronization Primitives (`anotest_vk_sync.c`)**
    *   **Test:** Submit dummy command buffers utilizing `inFlightFence` and image available semaphores.
    *   **Expectation:** Test correct wait stages (`VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`) and verify that we do not destroy fences or semaphores while they are still in use by the queue (which would violate spec).
*   **Physical Device Selection and Fallbacks (`anotest_vk_device.c`)**
    *   **Test:** Query `vkEnumeratePhysicalDevices`.
    *   **Expectation:** Ensure the device selector explicitly checks for required device extensions (e.g., `VK_KHR_swapchain`) and correctly falls back or cleanly aborts if the current system does not meet minimum hardware spec requirements.

## 4. Next Steps

1.  Create a headless Vulkan initialization helper for the test suite (to bypass GLFW window creation where possible).
2.  Implement the Debug Utils Messenger callback inside the test harness to catch Validation Layer errors automatically.
3.  Write the first `anotest_vk_lifecycle.c` test to harden `initVulkan()` and `unInitVulkan()`.
4.  Expand the suite iteratively based on the above plan.
