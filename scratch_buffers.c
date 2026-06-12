void createTransformBuffer(VulkanComponents* components, RendererState* state, uint32_t maxEntities) {
    state->transformBuffer.capacity = maxEntities;
    state->transformBuffer.count = 0;
    
    VkDeviceSize bufferSize = sizeof(mat4) * maxEntities;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(components->deviceQueueComp.device, &bufferInfo, NULL, &state->transformBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create transform buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->transformBuffer.buffer[i], &memRequirements);
        
        state->transformBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->transformBuffer.buffer[i], state->transformBuffer.allocs[i].memory, state->transformBuffer.allocs[i].offset);
        
        state->transformBuffer.mapped[i] = (mat4*)state->transformBuffer.allocs[i].mapped;
    }
}

void createIndirectDrawBuffer(VulkanComponents* components, RendererState* state, uint32_t maxDraws) {
    state->indirectBuffer.capacity = maxDraws;
    
    VkDeviceSize bufferSize = sizeof(VkDrawIndexedIndirectCommand) * maxDraws;
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        state->indirectBuffer.drawCount[i] = 0;
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(components->deviceQueueComp.device, &bufferInfo, NULL, &state->indirectBuffer.buffer[i]) != VK_SUCCESS) {
            printf("Failed to create indirect draw buffer!\n");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(components->deviceQueueComp.device, state->indirectBuffer.buffer[i], &memRequirements);
        
        state->indirectBuffer.allocs[i] = gpu_alloc(&gpuAllocator, memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkBindBufferMemory(components->deviceQueueComp.device, state->indirectBuffer.buffer[i], state->indirectBuffer.allocs[i].memory, state->indirectBuffer.allocs[i].offset);
        
        state->indirectBuffer.mapped[i] = (VkDrawIndexedIndirectCommand*)state->indirectBuffer.allocs[i].mapped;
    }
}
