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
{ // Changes the perspective (camera) parameters applied to the world-space for a given frame. Should be generalized with its parameters exposed via an interface
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

	float centerDefault[] = {0.0f, 0.15f, 0.0f}; // default look target (scene origin)
	float upDefault[]     = {0.0f, 1.0f, 0.0f};  // world is unflipped
	float fovDefault      = 45.0f;               // degrees

	// Shared projection params. Each view renders at its OWN extent (view 0 = swapchain, insets =
	// W/3 x H/3, review finding 6): aspect + froxel screen size come from viewExtent[v] below. The
	// renderer always owns the projection (aspect/near/far); logic only supplies the view-0 pose.
	float near = 0.1f;
	float far = 100.0f;

	// View 0 is the gameplay camera the logic master owns (audit 4.11): use its published pose if it
	// has published one, else fall back to the built-in camera (no first-frame regression, no init
	// handshake). View 1 stays the orbiting inset demo (audit 4.8).
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
			eye[0] = 0.0f; eye[1] = 0.9f; eye[2] = 3.5f;   // main fallback: up and back
			for (int k = 0; k < 3; k++) { center[k] = centerDefault[k]; up[k] = upDefault[k]; }
			fov = fovDefault;
		} else {
			// Inset: orbit the scene at a higher angle so the feed is obviously live + distinct.
			float a = elapsedTime * 0.5f;
			float r = 4.0f;
			eye[0] = r * sinf(a);
			eye[1] = 2.5f;
			eye[2] = r * cosf(a);
			for (int k = 0; k < 3; k++) { center[k] = centerDefault[k]; up[k] = upDefault[k]; }
			fov = fovDefault;
		}

		lookAt(u->view, eye, center, up);

		// Publish the camera world position so the fragment stage doesn't have to
		// recover it via a per-fragment inverse(view).
		u->cameraPos[0] = eye[0];
		u->cameraPos[1] = eye[1];
		u->cameraPos[2] = eye[2];
		u->cameraPos[3] = 1.0f;

		float aspect = (float)state->viewExtent[v].width / (float)state->viewExtent[v].height;
		perspective(u->proj, fov, aspect, near, far);

		// Clustered-forward froxel params: near/far + THIS VIEW's render extent (the light-cull
		// pass and the fragment shader reconstruct froxels from these), and the fixed grid dims.
		u->cameraNear = near;
		u->cameraFar = far;
		u->screenWidth = (float)state->viewExtent[v].width;
		u->screenHeight = (float)state->viewExtent[v].height;
		u->clusterDimX = ANO_CLUSTER_X;
		u->clusterDimY = ANO_CLUSTER_Y;
		u->clusterDimZ = ANO_CLUSTER_Z;
		u->maxLightsPerCluster = ANO_CLUSTER_MAX_LIGHTS;

		// Premultiplied clip transform + fragment unprojector (see vertex.h). pixelToNdc maps
		// (px, py, z, 1) to (2px/w - 1, 2py/h - 1, z, 1) for this view's extent; the Y flip
		// already lives in proj, and the viewport is the standard positive-height 0..1 one.
		multiplyMat4(u->viewProj, u->proj, u->view);
		mat4 invVP, pixelToNdc;
		if (!invertMat4(invVP, u->viewProj))
			memcpy(invVP, u->viewProj, sizeof(mat4)); // singular (degenerate camera): harmless placeholder
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
	// Swap Chain Components
	ano_debug_log(ANO_INFO, "=== Swap Chain Components ===");
	ano_debug_log(ANO_INFO, "Image count: %d", rendererState.imageCount);
	ano_debug_log(ANO_INFO, "Image extent: width = %d, height = %d", rendererState.imageExtent.width, rendererState.imageExtent.height);
	
	// Buffer Components
	ano_debug_log(ANO_INFO, "=== Buffer Components ===");
	ano_debug_log(ANO_INFO, "Live render slots: %u", rendererState.slots.slotHighWater); // scene is logic-composed, entities[] gone
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		ano_debug_log(ANO_INFO, "Uniform buffer %d (view 0): %p", i, (void*)rendererState.frames[i].views[0].uniformBuffer);
		ano_debug_log(ANO_INFO, "Uniform alloc %d (view 0): %p", i, (void*)rendererState.frames[i].views[0].uniformAlloc.memory);
		ano_debug_log(ANO_INFO, "Uniform buffer mapping %d (view 0): %p", i, rendererState.frames[i].views[0].uniformMapped);
	}

	// Synchronization Components
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
	// Deprecated: transforms are now device-local. A renderable's base pose arrives per-slot via
	// RCMD_CREATE -> stage_command_fields -> initialTransformBuffer; update.comp derives the live one.
	state->transformBuffer.count = state->entityCount;
}

void updateCullingBuffers(VulkanContext* ctx, RendererState* state, uint32_t frameIndex)
{
    // The render slot authority is the cull/dispatch bound; dead slots within
    // [0, slotHighWater) self-skip in the shaders (meshIndex == NO_MESH_INDEX).
    state->entityCount = state->slots.slotHighWater;
    uint32_t entityCount = state->entityCount;

    // Draw count is DEVICE_LOCAL now: zeroed on the GPU timeline by the vkCmdFillBuffer
    // in recordCommandBuffer (before the cull pass), not by a CPU memset.

    // Update CullUBO. One frustum per view: derive viewProj + planes from each view's camera
    // (updateUniformBuffer already wrote each view's matrices into its mapped uniform). The cull
    // dispatch tests every entity against all of them in one pass.
    CullUBO* ubo = state->culling.ubo.mapped[frameIndex];
    for (uint32_t v = 0; v < ANO_VIEW_COUNT; ++v) {
        GlobalUBO* viewUbo = state->frames[frameIndex].views[v].uniformMapped;
        multiplyMat4(ubo->views[v].viewProj, viewUbo->proj, viewUbo->view);
        extractFrustumPlanes(ubo->views[v].frustumPlanes, ubo->views[v].viewProj);
        // Screen-area cull knobs (review 4.9 step 1). scale = cot(fovY/2) * half viewport height in
        // px: |proj[1][1]| is cot(fovY/2) (negative under the Vulkan Y-flip), so rpx = worldRadius *
        // scale / dist. Threshold is squared here so the shader compares without a sqrt; 0 disables.
        float screenAreaScale = fabsf(viewUbo->proj[1][1]) * 0.5f * viewUbo->screenHeight;
        float threshold = state->cullPixelThreshold[v];
        ubo->viewCullParams[v][0] = screenAreaScale;
        ubo->viewCullParams[v][1] = threshold * threshold;
        ubo->viewCullParams[v][2] = state->lodPixelThreshold[v]; // LOD level-1 onset (review 4.9 step 2)
        ubo->viewCullParams[v][3] = (float)state->lodBias;       // global LOD bias (debug/tuning), replicated per view
        // Hi-Z occlusion (review 4.9 step 3): publish the viewProj the sampled pyramid was rendered
        // with, for reprojection — the slot `lag` frames back (1 = in-frame build, 2 = async build:
        // review finding 2) — then save this frame's into its own slot. hizParams.z = mipCount only
        // when this view's test is enabled AND past the post-(re)create warmup (hizValidOrdinal =
        // every sampled slot rebuilt at the current resolution); else 0 = test off (the default).
        // hizProj = the projection terms the cull needs (screen radius from proj00/proj11, ZO
        // nearest-depth from proj22/proj32).
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
        // Task-shader meshlet cull tail (review priority 10): flat.task culls per view with the
        // SAME planes/reprojection/gating the entity cull uses, republished into this view's
        // GlobalUBO (only flat.task declares the tail; every other shader reads a prefix).
        memcpy(viewUbo->frustumPlanes, ubo->views[v].frustumPlanes, sizeof(viewUbo->frustumPlanes));
        memcpy(viewUbo->prevViewProj, ubo->prevViewProj[v], sizeof(mat4));
        memcpy(viewUbo->hizParams, ubo->hizParams[v], sizeof(viewUbo->hizParams));
        memcpy(viewUbo->hizProj, ubo->hizProj[v], sizeof(viewUbo->hizProj));
        // Publish the active light count to each view's fragment stage.
        viewUbo->lightCount = state->lightBuffer.count;
        // Publish the runtime lighting mode + debug selector. The fragment
        // stage gates per-light shadow sampling on this; the shadow depth render is gated to match.
        viewUbo->lightingMode = state->lightingMode;
        viewUbo->debugView = state->debugView;
    }

    // Publish the live view-0 camera to the logic master (audit 4.11 / 7.1): the gameplay camera +
    // viewport it needs for picking rays and attention-driven LOD. Done here because this is where
    // this frame's view-0 viewProj + frustum planes are resolved; invViewProj unprojects a cursor
    // texel to a world ray. Latest-wins, lock-free (logic never laps the once-per-frame producer).
    {
        RenderSnapshot snap;
        memcpy(snap.viewProj, ubo->views[0].viewProj, sizeof(mat4));
        if (!invertMat4(snap.invViewProj, ubo->views[0].viewProj))
            memcpy(snap.invViewProj, ubo->views[0].viewProj, sizeof(mat4)); // singular (degenerate camera): publish a harmless placeholder
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
    ubo->shadowLodBias = state->shadowLodBias; // shadow LOD offset (review 4.9 step 2, revised): relative to the view-0 LOD

    // Publish the PipelineType -> draw-slot map cull.comp compacts by. Frame-invariant, but
    // rewritten here so each frame's UBO (incl. a freshly grown one) is always populated.
    for (uint32_t t = 0; t < 16u; ++t)
        ubo->drawSlotOf[t] = (t < PIPELINE_TYPE_COUNT) ? ano_draw_slot_of((PipelineType)t) : ANO_NO_DRAW_SLOT;

    // Special lanes the cull pass branches on: additive (slot x) emits no shadow caster, transmission
    // (slot y) is the depth-sorted "over" lane (tpsort.comp). ANO_NO_DRAW_SLOT when a lane is absent.
    ubo->specialSlots[0] = ano_draw_slot_of(PIPELINE_ADDITIVE);
    ubo->specialSlots[1] = ano_draw_slot_of(PIPELINE_TRANSMISSION);
    ubo->specialSlots[2] = ano_draw_slot_of(PIPELINE_FLAT_MASKED); // casters -> per-frustum MASKED shadow partition
    ubo->specialSlots[3] = ANO_NO_DRAW_SLOT;

    // Task-shader meshlet cull (review priority 10): tells emitDraw to size mesh-path commands as
    // ceil(meshletCount/32) task workgroups. Must track the pipelines' stage set (taskCull).
    ubo->taskParams[0] = state->taskCull ? 1u : 0u;
    ubo->taskParams[1] = 0u;
    ubo->taskParams[2] = 0u;
    ubo->taskParams[3] = 0u;

    // The EntitySSBO (mesh/material per slot) is seeded once at init and mutated
    // sparsely through the command bridge (render_apply_commands) — no per-frame
    // O(N) rewrite. MeshSSBO/MeshBoundsSSBO below are per-mesh (bounded by
    // meshCount, not entity count) and refreshed so geometry-pool changes apply.

    // Update MeshSSBO and MeshBoundsSSBO. meshCount can't exceed ANO_MAX_MESHES (the geometry pool
    // refuses to register past it), but clamp defensively: these buffers are fixed-size mapped device
    // memory, so a future cap divergence must never let this loop write past their last slot.
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
        meshData[i*9 + 5] = mesh->classicIndexCount;       // fallback: VkDrawIndexedIndirectCommand.indexCount
        meshData[i*9 + 6] = mesh->classicIndexOffset / 4;  // fallback: firstIndex (u32 index units)
        meshData[i*9 + 7] = mesh->boundsOffset;            // byte offset of per-meshlet bounds (sphere+cone) in metadata buffer; consumed by flat.mesh cone cull
        meshData[i*9 + 8] = mesh->lodCount;                // review 4.9 step 2: contiguous LOD count (cull reads off the base mesh)

        meshBounds[i*4 + 0] = mesh->boundingSphereCenter[0];
        meshBounds[i*4 + 1] = mesh->boundingSphereCenter[1];
        meshBounds[i*4 + 2] = mesh->boundingSphereCenter[2];
        meshBounds[i*4 + 3] = mesh->boundingSphereRadius;
    }
}
