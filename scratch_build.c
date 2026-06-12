void buildIndirectCommands(RendererState* rs, VulkanComponents* vc, uint32_t frameIndex)
{
    VkDrawIndexedIndirectCommand* cmds = rs->indirectBuffer.mapped[frameIndex];
    uint32_t drawCount = 0;
    uint32_t entityCount = vc->renderComp.buffers.entityCount;
    RenderEntity* entities = vc->renderComp.buffers.entities;

    // For Stage 4 we just do one draw per texture index (simplified grouping)
    // Actually, we could just do one indirect draw per entity if we don't sort, but the plan says ONE indirect call for ALL entities.
    // Since we still need to change Set 1 (texture) per entity, we MUST do one indirect draw per texture batch.
    // Let's sort entities by textureIndex or just do O(entities) indirect draws for now if we can't change Set 1.
    // Wait, if we do O(entities) indirect draws, it's:
    // vkCmdBindDescriptorSets(Set 1)
    // vkCmdDrawIndexedIndirect(..., drawCount=1, ...)
    // That's still valid!
}
