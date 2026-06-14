void processGltfMeshes(VulkanContext* ctx, GltfElements* elements)
{
    Vector3 defaultColor = {0.5f, 0.5f, 0.5f};

    for (uint32_t i = 0; i < elements->meshCount; i++)
    {
        GltfMesh* mesh = &elements->meshes[i];
        for (uint32_t primitiveIndex = 0; primitiveIndex < mesh->primitiveCount; ++primitiveIndex)
        {
            GltfPrimitive* primitive = &mesh->primitives[primitiveIndex];

            // Vertices
            GltfAccessor* positionAccessor = &elements->accessors[primitive->position];
            GltfAccessor* texcoordAccessor = &elements->accessors[primitive->texcoord];
            uint32_t vertexCount = positionAccessor->count;
            Vertex* vertices = malloc(sizeof(Vertex) * vertexCount);
            for (uint32_t v = 0; v < vertexCount; v++) {
                Vector3* positionData = getPositionData(elements, positionAccessor, v);
                Vector2* texcoordData = getTexcoordData(elements, texcoordAccessor, v);
                vertices[v].position = *positionData;
                vertices[v].texCoord = *texcoordData;
                vertices[v].color = defaultColor;
            }

            // Indices
            GltfAccessor* indexAccessor = &elements->accessors[primitive->indices];
            uint32_t indexCount = indexAccessor->count;
            uint32_t* indices = malloc(sizeof(uint32_t) * indexCount);
            for (uint32_t ind = 0; ind < indexCount; ind++) {
                uint16_t* indexData = getIndexData(elements, indexAccessor, ind);
                indices[ind] = *indexData;
            }

            // Upload
            primitive->meshIndex = geometry_pool_upload(&rendererState.globalGeometryPool, &gpuAllocator, ctx->device, state->commandPool, ctx->transferQueue, vertices, vertexCount, indices, indexCount);

            free(vertices);
            free(indices);
        }

        // Create texture buffer
        printf("Creating mesh#%d texture!\n", i);
        uploadTextureDataToGPU(components, elements, mesh);
    } 
}
