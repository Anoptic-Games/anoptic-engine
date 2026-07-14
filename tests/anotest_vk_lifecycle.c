#include <stdio.h>
#include <stdbool.h>

#include "vulkan_backend/vulkanMaster.h"

extern bool g_AnoVkNoSuitableGpu;

int main() {
    printf("Starting Vulkan lifecycle test...\n");
    bool result = initVulkan();
    if (!result) {
        if (g_AnoVkNoSuitableGpu) {
            printf("SKIP: no Vulkan device here can run the renderer.\n");
            return 77; // ctest SKIP_RETURN_CODE
        }
        fprintf(stderr, "initVulkan() failed.\n");
        return 1;
    }
    printf("initVulkan() succeeded.\n");
    unInitVulkan();
    printf("unInitVulkan() completed.\n");
    return 0;
}
