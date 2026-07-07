/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

#include <math.h>
#include <string.h>
#include <anoptic_logging.h>
#include <anoptic_time.h>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/backend.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/frame/frame.h"

bool updateUniformBuffer(VulkanContext* ctx, RendererState* state)
{ // Updates the per-frame camera projection params.
	static uint64_t time = 0;
	static uint64_t oldTime = 0;
	static uint64_t startTime = 0;
	static uint32_t frameCount = 0;

	time = ano_timestamp_us();
	if (startTime == 0) {
		startTime = time;
		oldTime = time;
	}

	float deltaTime = (time - oldTime) / 1000000.0f;
	float elapsedTime = (time - startTime) / 1000000.0f;

	float centerDefault[] = {0.0f, 0.15f, 0.0f}; // look target
	float upDefault[]     = {0.0f, 1.0f, 0.0f};  // up vector
	float fovDefault      = 45.0f;               // degrees

	// Shared projection params, per-view extent from viewExtent[v].
	float near = 0.1f;
	float far = 100.0f;

	// View 0 uses the published logic pose if available, else built-in fallback.
	AnoViewState vs;
	bool haveVs = ano_render_acquire_view(&state->bridge, &vs);

	for (uint32_t v = 0; v < ANO_VIEW_COUNT; v++)
	{
		GlobalUBO* u = &state->uboData[v];
		u->time = elapsedTime;
		u->deltaTime = deltaTime;
		u->frameCount = frameCount;

		float eye[3], center[3], up[3], fov;
		if (v == 0 && haveVs) {
			for (int k = 0; k < 3; k++) { eye[k] = vs.eye[k]; center[k] = vs.center[k]; up[k] = vs.up[k]; }
			fov = vs.fovYDeg;
		} else if (v == 0) {
			eye[0] = 0.0f; eye[1] = 0.9f; eye[2] = 3.5f;   // up and back
			for (int k = 0; k < 3; k++) { center[k] = centerDefault[k]; up[k] = upDefault[k]; }
			fov = fovDefault;
		} else {
			// Inset orbit.
			float a = elapsedTime * 0.5f;
			float r = 4.0f;
			eye[0] = r * sinf(a);
			eye[1] = 2.5f;
			eye[2] = r * cosf(a);
			for (int k = 0; k < 3; k++) { center[k] = centerDefault[k]; up[k] = upDefault[k]; }
			fov = fovDefault;
		}

		lookAt(u->view, eye, center, up);

		// Publish camera world position.
		u->cameraPos[0] = eye[0];
		u->cameraPos[1] = eye[1];
		u->cameraPos[2] = eye[2];
		u->cameraPos[3] = 1.0f;

		float aspect = (float)state->viewExtent[v].width / (float)state->viewExtent[v].height;
		perspective(u->proj, fov, aspect, near, far);

		// Clustered-forward froxel params.
		u->cameraNear = near;
		u->cameraFar = far;
		u->screenWidth = (float)state->viewExtent[v].width;
		u->screenHeight = (float)state->viewExtent[v].height;
		u->clusterDimX = ANO_CLUSTER_X;
		u->clusterDimY = ANO_CLUSTER_Y;
		u->clusterDimZ = ANO_CLUSTER_Z;
		u->maxLightsPerCluster = ANO_CLUSTER_MAX_LIGHTS;

		// Premultiplied clip transform + fragment unprojector (see vertex.h).
		multiplyMat4(u->viewProj, u->proj, u->view);
		mat4 invVP, pixelToNdc;
		if (!invertMat4(invVP, u->viewProj))
			memcpy(invVP, u->viewProj, sizeof(mat4)); // degenerate camera placeholder
		memset(pixelToNdc, 0, sizeof(mat4));
		pixelToNdc[0][0] = 2.0f / u->screenWidth;
		pixelToNdc[1][1] = 2.0f / u->screenHeight;
		pixelToNdc[2][2] = 1.0f;
		pixelToNdc[3][0] = -1.0f;
		pixelToNdc[3][1] = -1.0f;
		pixelToNdc[3][3] = 1.0f;
		multiplyMat4(u->invVPPixel, invVP, pixelToNdc);

		memcpy(state->frames[state->frameIndex].views[v].uniformMapped, u, sizeof(GlobalUBO));
	}
	frameCount++;

	oldTime = time;

	return true;
}

void printUniformTransferState()
{
	ano_debug_log(ANO_INFO, "=== Swap Chain Components ===");
	ano_debug_log(ANO_INFO, "Image count: %d", rendererState.imageCount);
	ano_debug_log(ANO_INFO, "Image extent: width = %d, height = %d", rendererState.imageExtent.width, rendererState.imageExtent.height);
	
	ano_debug_log(ANO_INFO, "=== Buffer Components ===");
	ano_debug_log(ANO_INFO, "Live render slots: %u", rendererState.slots.slotHighWater); // entities[] gone
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		ano_debug_log(ANO_INFO, "Uniform buffer %d (view 0): %p", i, (void*)rendererState.frames[i].views[0].uniformBuffer);
		ano_debug_log(ANO_INFO, "Uniform alloc %d (view 0): %p", i, (void*)rendererState.frames[i].views[0].uniformAlloc.memory);
		ano_debug_log(ANO_INFO, "Uniform buffer mapping %d (view 0): %p", i, rendererState.frames[i].views[0].uniformMapped);
	}

	ano_debug_log(ANO_INFO, "=== Synchronization Components ===");
	ano_debug_log(ANO_INFO, "Current frame index: %d", rendererState.frameIndex);
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		ano_debug_log(ANO_INFO, "Frame %d submitted: %d", i, rendererState.frames[i].frameSubmitted);
	}
	ano_debug_log(ANO_INFO, "======================================");
}

void updateTransformBuffer(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
	// Deprecated: transforms are device-local.
	state->transformBuffer.count = state->entityCount;
}

void updateCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
    // slotHighWater is the cull/dispatch bound, dead slots self-skip in shaders.
    state->entityCount = state->slots.slotHighWater;
    uint32_t entityCount = state->entityCount;

    // Draw count is device-local, zeroed by vkCmdFillBuffer in recordCommandBuffer.

    // Update CullUBO. One frustum per view, derived from each view's camera.
    CullUBO* ubo = state->culling.ubo.mapped[frameIndex];
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; ++v) {
        GlobalUBO* viewUbo = state->frames[frameIndex].views[v].uniformMapped;
        multiplyMat4(ubo->views[v].viewProj, viewUbo->proj, viewUbo->view);
        extractFrustumPlanes(ubo->views[v].frustumPlanes, ubo->views[v].viewProj);
        // Screen-area cull knobs. scale = |proj[1][1]| * 0.5 * screenHeight, threshold squared.
        float screenAreaScale = fabsf(viewUbo->proj[1][1]) * 0.5f * viewUbo->screenHeight;
        float threshold = state->cullPixelThreshold[v];
        ubo->viewCullParams[v][0] = screenAreaScale;
        ubo->viewCullParams[v][1] = threshold * threshold;
        ubo->viewCullParams[v][2] = state->lodPixelThreshold[v]; // LOD level-1 onset
        ubo->viewCullParams[v][3] = (float)state->lodBias;       // global LOD bias
        // Hi-Z occlusion reprojection via the sampled pyramid's viewProj (lags hizLag frames).
        // hizParams.z = mipCount when enabled and warm, else 0.
        uint32_t hizLag = state->asyncHiz ? 2u : 1u;
        uint32_t histSlot = (frameIndex + MAX_FRAMES_IN_FLIGHT - hizLag) % MAX_FRAMES_IN_FLIGHT;
        bool hizWarm = state->timelineOrdinal + 1u >= state->hizValidOrdinal;
        memcpy(ubo->prevViewProj[v], state->viewProjHist[histSlot][v], sizeof(mat4));
        memcpy(state->viewProjHist[frameIndex][v], ubo->views[v].viewProj, sizeof(mat4));
        ubo->hizParams[v][0] = (float)state->frames[frameIndex].views[v].hizWidth;
        ubo->hizParams[v][1] = (float)state->frames[frameIndex].views[v].hizHeight;
        ubo->hizParams[v][2] = (state->hizEnable[v] && hizWarm) ? (float)state->frames[frameIndex].views[v].hizMipCount : 0.0f;
        ubo->hizParams[v][3] = 0.0f;
        ubo->hizProj[v][0] = viewUbo->proj[0][0];
        ubo->hizProj[v][1] = viewUbo->proj[1][1];
        ubo->hizProj[v][2] = viewUbo->proj[2][2];
        ubo->hizProj[v][3] = viewUbo->proj[3][2];
        // Task-shader meshlet cull tail: republish planes/reprojection/gating into this view's GlobalUBO.
        memcpy(viewUbo->frustumPlanes, ubo->views[v].frustumPlanes, sizeof(viewUbo->frustumPlanes));
        memcpy(viewUbo->prevViewProj, ubo->prevViewProj[v], sizeof(mat4));
        memcpy(viewUbo->hizParams, ubo->hizParams[v], sizeof(viewUbo->hizParams));
        memcpy(viewUbo->hizProj, ubo->hizProj[v], sizeof(viewUbo->hizProj));
        // Publish light count to each view's fragment stage.
        viewUbo->lightCount = state->lightBuffer.count;
        // Publish runtime lighting mode + debug selector.
        viewUbo->lightingMode = state->lightingMode;
        viewUbo->debugView = state->debugView;
    }

    // Publish the live view-0 camera to the logic master for picking rays + LOD.
    {
        RenderSnapshot snap;
        memcpy(snap.viewProj, ubo->views[0].viewProj, sizeof(mat4));
        if (!invertMat4(snap.invViewProj, ubo->views[0].viewProj))
            memcpy(snap.invViewProj, ubo->views[0].viewProj, sizeof(mat4)); // degenerate camera placeholder
        memcpy(snap.frustum, ubo->views[0].frustumPlanes, sizeof snap.frustum);
        snap.vpWidth  = state->imageExtent.width;
        snap.vpHeight = state->imageExtent.height;
        snap.frameId  = state->globalFrame;
        ano_render_publish_snapshot(&state->bridge, &snap);
    }

    ubo->viewCount = ANO_VIEW_COUNT;
    ubo->entityCount = entityCount;
    ubo->maxEntities = state->culling.maxEntities;
    ubo->drawSlotCount = ano_draw_pipeline_count();
    ubo->shadowLodBias = state->shadowLodBias; // shadow LOD offset relative to view-0 LOD

    // Publish the PipelineType -> draw-slot map cull.comp compacts by.
    for (uint32_t t = 0; t < 16u; ++t)
        ubo->drawSlotOf[t] = (t < PIPELINE_TYPE_COUNT) ? ano_draw_slot_of((PipelineType)t) : ANO_NO_DRAW_SLOT;

    // Special lanes the cull pass branches on. ANO_NO_DRAW_SLOT when absent.
    ubo->specialSlots[0] = ano_draw_slot_of(PIPELINE_ADDITIVE);
    ubo->specialSlots[1] = ano_draw_slot_of(PIPELINE_TRANSMISSION);
    ubo->specialSlots[2] = ano_draw_slot_of(PIPELINE_FLAT_MASKED); // casters -> per-frustum MASKED shadow partition
    ubo->specialSlots[3] = ANO_NO_DRAW_SLOT;

    // Task-shader meshlet cull: sizes mesh-path commands as ceil(meshletCount/32) task workgroups.
    ubo->taskParams[0] = state->taskCull ? 1u : 0u;
    ubo->taskParams[1] = 0u;
    ubo->taskParams[2] = 0u;
    ubo->taskParams[3] = 0u;

    // EntitySSBO is seeded at init and mutated sparsely via the command bridge, not per-frame.

    // Update MeshSSBO and MeshBoundsSSBO, clamped to ANO_MAX_MESHES.
    uint32_t meshCount = state->globalGeometryPool.meshCount;
    if (meshCount > ANO_MAX_MESHES) meshCount = ANO_MAX_MESHES;
    uint32_t* meshData = (uint32_t*)state->culling.meshDataMapped[frameIndex];
    float* meshBounds = (float*)state->culling.meshBoundsMapped[frameIndex];
    
    for(uint32_t i=0; i < meshCount; i++) {
        MeshRegion* mesh = &state->globalGeometryPool.meshes[i];
        
        meshData[i*9 + 0] = mesh->meshletCount;
        meshData[i*9 + 1] = mesh->meshletOffset;
        meshData[i*9 + 2] = mesh->uniqueVerticesOffset;
        meshData[i*9 + 3] = mesh->trianglesOffset;
        meshData[i*9 + 4] = mesh->vertexOffset;
        meshData[i*9 + 5] = mesh->classicIndexCount;       // fallback indexCount
        meshData[i*9 + 6] = mesh->classicIndexOffset / 4;  // fallback firstIndex (u32 units)
        meshData[i*9 + 7] = mesh->boundsOffset;            // per-meshlet bounds byte offset
        meshData[i*9 + 8] = mesh->lodCount;                // contiguous LOD count

        meshBounds[i*4 + 0] = mesh->boundingSphereCenter[0];
        meshBounds[i*4 + 1] = mesh->boundingSphereCenter[1];
        meshBounds[i*4 + 2] = mesh->boundingSphereCenter[2];
        meshBounds[i*4 + 3] = mesh->boundingSphereRadius;
    }
}
