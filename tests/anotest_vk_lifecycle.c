#include <stdio.h>
#include <stdbool.h>

#include "vulkan_backend/vulkanMaster.h"

int main() {
    printf("Starting Vulkan lifecycle test...\n");
    bool result = initVulkan();
    if (!result) {
        fprintf(stderr, "initVulkan() failed.\n");
        return 1;
    }
    printf("initVulkan() succeeded.\n");
    unInitVulkan();
    printf("unInitVulkan() completed.\n");
    return 0;
}
