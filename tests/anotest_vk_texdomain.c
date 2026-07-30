#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/gpu_alloc.h"

extern struct VulkanGarbage vulkanGarbage;
extern bool g_AnoVkNoSuitableGpu;
extern uint32_t g_ValidationErrors;

// Probe byte 128: UNORM = 128/255, SRGB = EOTF(128/255). Alpha linear both formats.
#define PROBE_BYTE   128
#define WANT_UNORM   0.50196f
#define WANT_SRGB    0.21586f
#define TOL_UNORM    0.004f
#define TOL_SRGB     0.005f

// in: got, want, tol. out: true iff |got - want| <= tol.
static bool within(float got, float want, float tol)
{
    float d = got - want;
    return (d < 0.0f ? -d : d) <= tol;
}

int main(void)
{
    printf("Starting Vulkan texture-domain readback test...\n");
    g_ValidationErrors = 0;

    if (!initVulkan()) {
        if (g_AnoVkNoSuitableGpu) {
            printf("SKIP: no Vulkan device here can run the renderer.\n");
            return 77; // ctest SKIP_RETURN_CODE
        }
        printf("Failed to init Vulkan!\n");
        return 1;
    }
    VulkanContext* ctx = vulkanGarbage.ctx;

    // Probe resources; unconditional teardown at done.
    int status = 1;
    TexturePackage pkg = {};
    VkBuffer ssbo = VK_NULL_HANDLE;
    GpuAllocation ssboAlloc = {};
    VkDescriptorSetLayout probeLayout = VK_NULL_HANDLE;
    VkDescriptorPool probePool = VK_NULL_HANDLE;
    VkDescriptorSet probeSet = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;
    struct Buffer code = {};

    bool passed = [&]() -> bool {
    if (g_ValidationErrors > 0) {
        printf("Error: validation errors occurred during initVulkan!\n");
        return false;
    }

    // 4x4 solid RGBA=PROBE_BYTE, COLOR|DATA roles. Transient CB. No staging kept.
    unsigned char pixels[4 * 4 * 4];
    memset(pixels, PROBE_BYTE, sizeof pixels);

    AnoTextureResult built = createTextureImageFromPixels(ctx, VK_NULL_HANDLE, &pkg, pixels, 4, 4,
                                                          TEXTURE_USE_COLOR | TEXTURE_USE_DATA, false);
    if (built.code != ANO_TEXTURE_BUILT) {
        printf("Error: createTextureImageFromPixels refused both roles (code %d)!\n", (int)built.code);
        return false;
    }

    // One image, one alloc, two distinct views (srgb + unorm).
    if (pkg.image == VK_NULL_HANDLE || pkg.alloc.memory == VK_NULL_HANDLE) {
        printf("Error: BUILT package carries no image or no allocation!\n");
        return false;
    }
    if (pkg.srgbView == VK_NULL_HANDLE || pkg.unormView == VK_NULL_HANDLE) {
        printf("Error: both roles were asked for but only one view was built!\n");
        return false;
    }
    if (pkg.srgbView == pkg.unormView) {
        printf("Error: the two roles collapsed onto one view handle!\n");
        return false;
    }
    if (pkg.staging != VK_NULL_HANDLE) {
        printf("Error: keepStaging was false but the staging buffer was published!\n");
        return false;
    }
    if (g_ValidationErrors > 0) {
        printf("Error: the mutable-format image and its two views drew %u validation errors!\n", g_ValidationErrors);
        return false;
    }
    printf("One image, one allocation, two distinct views; MUTABLE_FORMAT + format list accepted.\n");

    // Both views into the engine bindless array.
    uint32_t srgbSlot = bindless_register_texture(ctx, &rendererState.bindlessTextures, pkg.srgbView, rendererState.textureSampler);
    uint32_t unormSlot = bindless_register_texture(ctx, &rendererState.bindlessTextures, pkg.unormView, rendererState.textureSampler);
    if (srgbSlot == ANO_BINDLESS_NONE || unormSlot == ANO_BINDLESS_NONE) {
        printf("Error: the bindless array refused a view (srgb %u, unorm %u)!\n", srgbSlot, unormSlot);
        return false;
    }
    if (srgbSlot == unormSlot) {
        printf("Error: both views landed in bindless slot %u!\n", srgbSlot);
        return false;
    }
    printf("Bindless slots: srgb %u, unorm %u.\n", srgbSlot, unormSlot);

    // Host-visible coherent SSBO, zeroed. Eight floats: srgb RGBA then unorm RGBA.
    const VkDeviceSize probeBytes = sizeof(float) * 8;
    if (!createDataBuffer(ctx, &stagingAllocator, probeBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &ssbo, &ssboAlloc) || ssboAlloc.mapped == NULL) {
        printf("Error: failed to create a host-visible probe SSBO!\n");
        return false;
    }
    memset(ssboAlloc.mapped, 0, (size_t)probeBytes);

    // Set 0 = probe SSBO. Set 1 = engine bindless array.
    VkDescriptorSetLayoutBinding ssboBinding = {};
    ssboBinding.binding = 0;
    ssboBinding.descriptorCount = 1;
    ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo probeLayoutInfo = {};
    probeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    probeLayoutInfo.bindingCount = 1;
    probeLayoutInfo.pBindings = &ssboBinding;
    if (vkCreateDescriptorSetLayout(ctx->device, &probeLayoutInfo, NULL, &probeLayout) != VK_SUCCESS) {
        printf("Error: failed to create the probe descriptor set layout!\n");
        return false;
    }

    VkDescriptorPoolSize probePoolSize = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
    VkDescriptorPoolCreateInfo probePoolInfo = {};
    probePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    probePoolInfo.poolSizeCount = 1;
    probePoolInfo.pPoolSizes = &probePoolSize;
    probePoolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(ctx->device, &probePoolInfo, NULL, &probePool) != VK_SUCCESS) {
        printf("Error: failed to create the probe descriptor pool!\n");
        return false;
    }

    VkDescriptorSetAllocateInfo probeAlloc = {};
    probeAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    probeAlloc.descriptorPool = probePool;
    probeAlloc.descriptorSetCount = 1;
    probeAlloc.pSetLayouts = &probeLayout;
    if (vkAllocateDescriptorSets(ctx->device, &probeAlloc, &probeSet) != VK_SUCCESS) {
        printf("Error: failed to allocate the probe descriptor set!\n");
        return false;
    }

    VkDescriptorBufferInfo ssboInfo = { .buffer = ssbo, .offset = 0, .range = VK_WHOLE_SIZE };
    VkWriteDescriptorSet ssboWrite = {};
    ssboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssboWrite.dstSet = probeSet;
    ssboWrite.dstBinding = 0;
    ssboWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboWrite.descriptorCount = 1;
    ssboWrite.pBufferInfo = &ssboInfo;
    vkUpdateDescriptorSets(ctx->device, 1, &ssboWrite, 0, NULL);

    // Compute pipeline: two set layouts, two-uint push constants.
    VkPushConstantRange push = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(uint32_t) * 2 };
    VkDescriptorSetLayout setLayouts[2] = { probeLayout, rendererState.bindlessTextures.layout };
    VkPipelineLayoutCreateInfo pipeLayoutInfo = {};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 2;
    pipeLayoutInfo.pSetLayouts = setLayouts;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(ctx->device, &pipeLayoutInfo, NULL, &pipeLayout) != VK_SUCCESS) {
        printf("Error: failed to create the probe pipeline layout!\n");
        return false;
    }

    if (!loadFile("resources/shaders/texdomain_probe.comp.spv", &code)) {
        printf("Error: failed to load resources/shaders/texdomain_probe.comp.spv!\n");
        return false;
    }
    module = createShaderModule(ctx->device, &code);

    VkComputePipelineCreateInfo pipeInfo = {};
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.layout = pipeLayout;
    if (!ano_pipeline_stage(VK_SHADER_STAGE_COMPUTE_BIT, module, NULL, &pipeInfo.stage)) {
        printf("Error: the probe shader module was refused!\n");
        return false;
    }
    if (vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &pipe) != VK_SUCCESS) {
        printf("Error: failed to create the probe compute pipeline!\n");
        return false;
    }

    // One dispatch, fence-synced.
    VkCommandBuffer cmd = beginSingleTimeCommands(ctx);
    if (cmd == VK_NULL_HANDLE) {
        printf("Error: failed to mint the probe command buffer!\n");
        return false;
    }
    VkDescriptorSet sets[2] = { probeSet, rendererState.bindlessTextures.set };
    uint32_t indices[2] = { srgbSlot, unormSlot };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeLayout, 0, 2, sets, 0, NULL);
    vkCmdPushConstants(cmd, pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof indices, indices);
    vkCmdDispatch(cmd, 1, 1, 1);

    // Host-read barrier before fence.
    VkMemoryBarrier toHost = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         1, &toHost, 0, NULL, 0, NULL);

    if (!endSingleTimeCommandsChecked(ctx, cmd)) {
        printf("Error: the probe dispatch failed to submit or complete!\n");
        return false;
    }

    // probe[0..3] = srgb view, probe[4..7] = unorm view.
    const float* probe = static_cast<const float*>(ssboAlloc.mapped);
    printf("SRGB  view RGBA: %.5f %.5f %.5f %.5f\n", probe[0], probe[1], probe[2], probe[3]);
    printf("UNORM view RGBA: %.5f %.5f %.5f %.5f\n", probe[4], probe[5], probe[6], probe[7]);

    if (!within(probe[4], WANT_UNORM, TOL_UNORM) || !within(probe[5], WANT_UNORM, TOL_UNORM)
        || !within(probe[6], WANT_UNORM, TOL_UNORM)) {
        printf("Error: the UNORM view read %.5f, not %.5f +/- %.3f!\n", probe[4], WANT_UNORM, TOL_UNORM);
        return false;
    }
    if (!within(probe[0], WANT_SRGB, TOL_SRGB) || !within(probe[1], WANT_SRGB, TOL_SRGB)
        || !within(probe[2], WANT_SRGB, TOL_SRGB)) {
        printf("Error: the SRGB view read %.5f, not %.5f +/- %.3f!\n", probe[0], WANT_SRGB, TOL_SRGB);
        return false;
    }
    // Alpha linear in both formats.
    if (!within(probe[3], WANT_UNORM, TOL_UNORM) || !within(probe[7], WANT_UNORM, TOL_UNORM)) {
        printf("Error: alpha decoded (srgb %.5f, unorm %.5f), it must stay linear!\n", probe[3], probe[7]);
        return false;
    }
    if (g_ValidationErrors > 0) {
        printf("Error: %u validation errors during registration and dispatch!\n", g_ValidationErrors);
        return false;
    }

    printf("One image, two views, two domains: %.5f through SRGB and %.5f through UNORM.\n", probe[0], probe[4]);
    return true;
    }();
    status = passed ? 0 : 1;

    // Destroy probe objects. Bindless slots and arena spans stay until unInitVulkan.
    vkDestroyPipeline(ctx->device, pipe, NULL);
    vkDestroyShaderModule(ctx->device, module, NULL);
    ano_aligned_free(code.data);
    vkDestroyPipelineLayout(ctx->device, pipeLayout, NULL);
    vkDestroyDescriptorPool(ctx->device, probePool, NULL); // + probeSet
    vkDestroyDescriptorSetLayout(ctx->device, probeLayout, NULL);
    vkDestroyBuffer(ctx->device, ssbo, NULL);
    vkDestroyImageView(ctx->device, pkg.unormView, NULL);
    vkDestroyImageView(ctx->device, pkg.srgbView, NULL);
    vkDestroyImage(ctx->device, pkg.image, NULL);
    unInitVulkan();
    printf(status == 0 ? "Texture-domain readback test passed successfully!\n"
                       : "Texture-domain readback test FAILED.\n");
    return status;
}
