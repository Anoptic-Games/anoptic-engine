# Pipeline and Renderer Refactor Plan

## Overview

This document describes a stage-by-stage refactor of the Vulkan rendering backend from its current monolithic, tutorial-derived state into a **GPU-driven, data-oriented renderer** capable of drawing millions of flat-shaded entities at interactive frame rates.

Each stage is self-contained and produces a working renderer. Stages build on each other in dependency order — no stage requires a future stage to function.

### Design Pillars

1. **Compile-Time-Known Structures.** Pipeline types, vertex formats, and descriptor layouts are defined by C enums and struct literals. No JSON parsing, no runtime schema discovery. The compiler validates what the driver will consume.

2. **PipelinePrototype/PipelineImplementation Model.** Adopted from `components.h`. A `PipelinePrototype` defines a *class* of rendering work (flat geometry, particles, SDF compositing). Each prototype can have multiple `PipelineImplementation` variants (opaque vs. transparent blend, different vertex formats). The enum is known at compile time; the implementations are created at init from compile-time definitions.

3. **GPU-Driven Submission.** The CPU's per-frame work converges toward a fixed ~15-command sequence regardless of entity count. Per-entity decisions (culling, draw command generation) move to compute shaders. The rendering loop becomes a viewport into the simulation, not a bottleneck on it.

4. **Arena-Aligned Memory.** GPU memory management mirrors the engine's CPU arena philosophy: bulk allocation, sub-allocation within, deterministic teardown. No per-resource `vkAllocateMemory`.

5. **Dynamic Rendering (Vulkan 1.3).** `VkRenderPass` and `VkFramebuffer` are replaced by `vkCmdBeginRendering`. Passes become data — a list of attachment descriptions and a pipeline prototype to bind — not baked Vulkan objects.

### Current Architecture (What We're Replacing)

```
VulkanComponents (god-object)
├── 1 VkRenderPass, 1 VkPipeline, 1 VkPipelineLayout
├── 2 VkDescriptorSetLayouts (Set 0: camera UBO, Set 1: per-entity UBO + texture)
├── EntityBuffer[] — each entity owns: VkBuffer×3, VkDeviceMemory×3, VkImage, VkImageView, VkDescriptorSet
├── Per-resource vkAllocateMemory (no sub-allocation)
├── recordCommandBuffer: bind pipeline → bind Set 0 → for each entity { bind VB, bind IB, bind Set 1, drawIndexed }
└── drawFrame: hardcoded 3 entities, hardcoded transform offsets, full re-record every frame
```

### Target Architecture (What We're Building)

```
RendererState
├── PipelinePrototype[] — compile-time-known pipeline classes, each with N implementations
├── GeometryPool — shared mega vertex/index buffers, mesh regions tracked by offset
├── GpuAllocator — sub-allocator over a small number of VkDeviceMemory blocks
├── BindlessTextureArray — one descriptor set, all textures, indexed by material ID
├── TransformSSBO — all entity mat4s in one buffer, updated via persistent map
├── MaterialSSBO — all material properties in one buffer, texture indices included
├── IndirectDrawBuffer — VkDrawIndexedIndirectCommand[], built by compute shader
├── PassList — ordered sequence of {attachments, prototype, compute dispatches}
└── drawFrame: upload transforms → dispatch cull → for each pass { beginRendering, bind prototype, drawIndirectCount }
```

---

## Stage 0 — Cleanup and Foundation

**Goal:** Remove dead code, resolve the `components.h` split, and establish the memory allocation foundation. No rendering changes — the demo still draws 3 viking rooms identically to before.

### 0.1 Dead Code Removal

| Target | Action |
|--------|--------|
| `memory/memory.c` | Delete file. All its functions are duplicated in `instanceInit.c` and `memory.c` is not compiled by CMakeLists. |
| `memory/memory.h` | Delete file. Only includes other headers, provides nothing. |
| `structs.h` lines 31–55 | Delete commented-out `VulkanResourceVTable` / `VulkanResource`. Preserve the design intent as a comment block or move to this document. |
| `structs.h` `uiImage`, `uiImageMemory`, `uiView` | Delete fields from `SwapChainGroup` / `ImageViewGroup`. Remove corresponding allocation in `createColorResources`. These are allocated and never used. |
| `structs.h` `GlyphTexture` | Delete. Stale artifact from text rendering branch. |
| `structs.h` `skipCheck` | Delete from `SynchronizationComponents`. Confirmed unused in current frame loop. |
| `components.h` `DataPattern`, `DataChain` | Delete. Over-engineered "descriptors for descriptors" that the new system replaces with compile-time struct definitions. |
| `components.h` `AssetDataType` enum | Delete. Superseded by the descriptor layout being implicit in each `PipelineDef`. |
| `components.c` realloc-based register functions | Retain for now but mark for replacement by arena-backed registration in Stage 3. |

### 0.2 Reconcile `components.h` Into Active Architecture

The `PipelinePrototype` and `PipelineImplementation` types in `components.h` become the **canonical** pipeline representation. The single `VkPipeline graphicsPipeline` in `RenderComponents` and all of its surrounding layout/pool fields are replaced by an array of prototypes.

Evolve the existing types:

```c
// ===== components.h (revised) =====

typedef enum PipelineType
{
    PIPELINE_FLAT = 0,          // Flat-shaded geometry (replaces PIPELINE_BASIC)
    PIPELINE_PARTICLE,          // Point-sprite / billboard particles
    PIPELINE_SDF_COMPOSITE,     // SDF raymarching compositing pass (future)
    PIPELINE_UI,                // UI overlay (future)
    PIPELINE_TYPE_COUNT         // Sentinel — array sizing, not a real type
} PipelineType;

// A concrete Vulkan pipeline object — one variant of a prototype.
// Variants differ by blend mode, vertex format, or shader permutation.
typedef struct PipelineImplementation
{
    VkPipeline           pipeline;
    VkPipelineBindPoint  bindPoint;
    VkBool32             depthWrite;    // whether this variant writes depth
    VkBool32             blendEnable;   // opaque vs. transparent
} PipelineImplementation;

// A logical pipeline class. Known at compile time. Created at init.
// Owns the layout (shared by all its implementations) and the cache.
typedef struct PipelinePrototype
{
    PipelineType                type;
    VkPipelineLayout            layout;           // shared across all implementations
    uint32_t                    implementationCount;
    PipelineImplementation*     implementations;  // allocated as a flat array, not FAM
    VkPipelineCache             cache;
} PipelinePrototype;
```

**Why flat array instead of flexible array member:** FAM structs cannot be stack-allocated or placed in static arrays. A flat pointer-to-array gives the same data locality when allocated contiguously, and allows `PipelinePrototype prototypes[PIPELINE_TYPE_COUNT]` as a fixed-size array in the renderer state.

### 0.3 Introduce `RendererState`

Begin decomposing the `VulkanComponents` god-object. The new `RendererState` struct owns rendering-specific resources and is separate from device/instance/swapchain management:

```c
typedef struct RendererState
{
    // Pipeline system (Stage 0+)
    PipelinePrototype       prototypes[PIPELINE_TYPE_COUNT];

    // Descriptor infrastructure (to be populated per-stage)
    VkDescriptorPool        globalDescriptorPool;
    VkDescriptorSetLayout   globalSetLayout;        // Set 0
    VkDescriptorSet         globalSets[MAX_FRAMES_IN_FLIGHT];

    // Geometry (Stage 3+)
    // ... (added incrementally)

    // Synchronization — lifted from SynchronizationComponents
    VkSemaphore             imageAvailable[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore             renderFinished[MAX_FRAMES_IN_FLIGHT];
    VkFence                 frameFence[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    bool                    frameFenceSubmitted[MAX_FRAMES_IN_FLIGHT];
} RendererState;
```

`VulkanComponents` is retained for device, instance, surface, swapchain, and physical device state. `RendererState` is a *separate* struct passed alongside it — not embedded. This keeps the ownership boundary clean: swapchain recreation touches `VulkanComponents`; frame rendering touches `RendererState`.

### 0.4 GPU Memory Sub-Allocator (Foundation Only)

Introduce a thin sub-allocator wrapping `vkAllocateMemory`. The current code makes one `vkAllocateMemory` per buffer and per image — this will hit the driver's `maxMemoryAllocationCount` (~4096) long before we reach target entity counts.

```c
// gpu_alloc.h — minimal sub-allocator interface

typedef struct GpuBlock
{
    VkDeviceMemory  memory;
    VkDeviceSize    size;
    VkDeviceSize    offset;     // next free offset (bump allocator)
    uint32_t        memoryType;
    void*           mapped;     // persistently mapped if HOST_VISIBLE, else NULL
} GpuBlock;

typedef struct GpuAllocator
{
    VkDevice        device;
    GpuBlock*       blocks;
    uint32_t        blockCount;
    VkPhysicalDeviceMemoryProperties memProps;
} GpuAllocator;

typedef struct GpuAllocation
{
    VkDeviceMemory  memory;     // which block
    VkDeviceSize    offset;     // offset within block
    VkDeviceSize    size;       // allocation size
    void*           mapped;     // mapped pointer + offset, or NULL
} GpuAllocation;

// Allocate a region from the appropriate memory type.
// Alignment is handled internally. Creates a new block if needed.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs,
                        VkMemoryPropertyFlags props);

// Free is deferred — blocks are freed on allocator teardown or explicit reset.
// Individual allocations are not freed (arena semantics).
void gpu_alloc_reset(GpuAllocator* alloc);   // reset all blocks to offset=0
void gpu_alloc_destroy(GpuAllocator* alloc);  // free all VkDeviceMemory
```

This is a **bump allocator** over GPU memory blocks — the same arena pattern as the engine's CPU memory model. Individual `gpu_alloc` calls return sub-regions within large (e.g. 256 MiB) `VkDeviceMemory` blocks. No per-resource `vkAllocateMemory`.

At this stage, wire the allocator into new code paths only. Existing `EntityBuffer` allocations continue using direct allocation until they are replaced in Stage 1–3.

### Acceptance Criteria (Stage 0)

- [ ] `memory/memory.c` and `memory/memory.h` deleted
- [ ] Dead fields removed from `structs.h`
- [ ] `components.h` contains only `PipelineType`, `PipelineImplementation`, `PipelinePrototype`, `RenderPrimitives`, and primitive data types
- [ ] `RendererState` struct defined and instantiated alongside `VulkanComponents`
- [ ] `GpuAllocator` compiles and passes a basic test (allocate, sub-allocate, destroy)
- [ ] Demo still renders 3 viking rooms identically

---

## Stage 1 — Push Constants and Descriptor Frequency Split

**Goal:** Eliminate per-entity UBO descriptor sets. The model transform moves to push constants. Descriptors are reorganized by update frequency. This is the single largest CPU-side performance improvement.

### 1.1 New Descriptor Layout

```
Set 0 — Global (bound once per frame):
  Binding 0: UBO { mat4 view; mat4 proj; }          // camera only, no model matrix
  Binding 1: UBO { vec4 time; vec4 params; }         // global shader params (future)

Push Constants (per draw call, 80 bytes):
  mat4  model;            // 64 bytes — entity world transform
  uint32_t materialIndex; // 4 bytes  — material ID (used from Stage 5)
  uint32_t _pad[3];       // 12 bytes — alignment to 16 bytes
```

Set 1 (material textures) is *not introduced yet*. At this stage, the single texture is passed via a simplified per-entity descriptor set that holds only the combined image sampler — the UBO is gone from it.

### 1.2 Shader Changes

```glsl
// ===== flat.vert =====
#version 450

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
} global;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint materialIndex;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = global.proj * global.view * pc.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
```

```glsl
// ===== flat.frag =====
#version 450

layout(set = 1, binding = 0) uniform sampler2D texSampler;  // per-entity for now

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, fragTexCoord);
}
```

### 1.3 Pipeline Layout Changes

In `createGraphicsPipeline` (or its replacement):

```c
VkPushConstantRange pushRange = {
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    .offset     = 0,
    .size       = 80,  // mat4 (64) + uint (4) + pad (12)
};

VkDescriptorSetLayout setLayouts[2] = {
    rendererState->globalSetLayout,   // Set 0: camera UBO
    perEntityTextureLayout,           // Set 1: sampler (temporary, replaced in Stage 5)
};

VkPipelineLayoutCreateInfo layoutInfo = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount         = 2,
    .pSetLayouts            = setLayouts,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges    = &pushRange,
};
```

### 1.4 EntityBuffer Simplification

Remove transform UBO fields from `EntityBuffer`:

```c
typedef struct EntityBuffer
{
    VkBuffer        vertex;
    VkDeviceMemory  vertexMemory;
    uint32_t        indexCount;
    VkBuffer        index;
    VkDeviceMemory  indexMemory;
    VkImage         textureImage;
    VkDeviceMemory  textureImageMemory;
    VkImageView     textureImageView;
    VkDescriptorSet textureDescriptorSet;   // now only holds sampler, no UBO
    mat4            transform;               // CPU-side, pushed each draw
} EntityBuffer;
```

Deleted: `VkBuffer transform`, `VkDeviceMemory transformMemory`, `void* transformMapped`, `VkDescriptorSet meshDescriptorSet` (replaced by `textureDescriptorSet`).

### 1.5 Command Recording Changes

```c
void recordCommandBuffer(RendererState* rs, VulkanComponents* vc, uint32_t imageIndex)
{
    VkCommandBuffer cmd = vc->cmdComp.commandBuffer[rs->frameIndex];
    // ... begin command buffer, begin render pass ...

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      rs->prototypes[PIPELINE_FLAT].implementations[0].pipeline);

    // Set 0: global (once per frame)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            rs->prototypes[PIPELINE_FLAT].layout,
                            0, 1, &rs->globalSets[rs->frameIndex], 0, NULL);

    // Dynamic state
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (uint32_t i = 0; i < entityCount; i++)
    {
        EntityBuffer* e = &entities[i];

        // Set 1: per-entity texture
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                rs->prototypes[PIPELINE_FLAT].layout,
                                1, 1, &e->textureDescriptorSet, 0, NULL);

        // Push constant: model transform
        vkCmdPushConstants(cmd, rs->prototypes[PIPELINE_FLAT].layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &e->transform);

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &e->vertex, offsets);
        vkCmdBindIndexBuffer(cmd, e->index, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, e->indexCount, 1, 0, 0, 0);
    }

    // ... end render pass, end command buffer ...
}
```

### 1.6 Uniform Buffer Changes

`updateUniformBuffer` writes only `view` and `proj` — no `model` matrix. The `UniformComponents` struct shrinks:

```c
typedef struct GlobalUBO
{
    mat4 view;
    mat4 proj;
} GlobalUBO;
```

The old `UniformComponents` (model/view/proj) and `ModelTransforms` (translation/rotation/scale) are both deleted. Per-entity transforms are now plain `mat4` values computed on the CPU and pushed directly.

### Acceptance Criteria (Stage 1)

- [ ] Per-entity UBO allocation removed — zero `VkBuffer` / `VkDeviceMemory` per entity for transforms
- [ ] Push constants carry model matrix, confirmed via validation layers
- [ ] `ExtraModelTransforms` UBO removed from shader and pipeline
- [ ] Set 0 bound once per frame, Set 1 bound per entity (texture only)
- [ ] `updateMeshTransforms` deleted — transforms stored as plain `mat4` on `EntityBuffer`
- [ ] Demo renders identically

---

## Stage 2 — PipelinePrototype System and Dynamic Rendering

**Goal:** Replace the hardcoded single-pipeline creation with the `PipelinePrototype` system. Simultaneously transition from `VkRenderPass` to Vulkan 1.3 dynamic rendering. These two changes are coupled because dynamic rendering eliminates the render pass compatibility constraints that would otherwise complicate multi-pipeline support.

### 2.1 Compile-Time Pipeline Definitions

Pipeline state is defined by a static C struct, not by runtime configuration:

```c
// pipeline_defs.h — compile-time pipeline specifications

typedef struct PipelineDef
{
    PipelineType            type;
    const char*             vertShaderPath;      // relative to PROJECT_ROOT/resources/shaders/
    const char*             fragShaderPath;
    const char*             compShaderPath;      // non-NULL for compute pipelines
    VkPrimitiveTopology     topology;
    VkCullModeFlags         cullMode;
    VkFrontFace             frontFace;
    VkPolygonMode           polygonMode;
    VkBool32                depthTest;
    VkBool32                depthWrite;
    VkCompareOp             depthCompareOp;
    VkBool32                blendEnable;
    VkBlendFactor           srcColorBlend;
    VkBlendFactor           dstColorBlend;
    VkBlendOp               colorBlendOp;
    VkBlendFactor           srcAlphaBlend;
    VkBlendFactor           dstAlphaBlend;
    VkBlendOp               alphaBlendOp;
    VkSampleCountFlagBits   msaaSamples;         // 0 = use device max
} PipelineDef;
```

```c
// pipeline_defs.c — the actual definitions

static const PipelineDef flatOpaque = {
    .type            = PIPELINE_FLAT,
    .vertShaderPath  = "flat.vert.spv",
    .fragShaderPath  = "flat.frag.spv",
    .topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .cullMode        = VK_CULL_MODE_BACK_BIT,
    .frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .polygonMode     = VK_POLYGON_MODE_FILL,
    .depthTest       = VK_TRUE,
    .depthWrite      = VK_TRUE,
    .depthCompareOp  = VK_COMPARE_OP_LESS,
    .blendEnable     = VK_FALSE,
};

static const PipelineDef flatTransparent = {
    .type            = PIPELINE_FLAT,
    .vertShaderPath  = "flat.vert.spv",
    .fragShaderPath  = "flat.frag.spv",
    .topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .cullMode        = VK_CULL_MODE_NONE,         // no culling for transparent
    .frontFace       = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .polygonMode     = VK_POLYGON_MODE_FILL,
    .depthTest       = VK_TRUE,
    .depthWrite      = VK_FALSE,                  // don't write depth
    .depthCompareOp  = VK_COMPARE_OP_LESS,
    .blendEnable     = VK_TRUE,
    .srcColorBlend   = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlend   = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .colorBlendOp    = VK_BLEND_OP_ADD,
    .srcAlphaBlend   = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlend   = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp    = VK_BLEND_OP_ADD,
};

// Master table — one entry per PipelineType
static const PipelineDef* g_prototypeDefs[PIPELINE_TYPE_COUNT] = {
    [PIPELINE_FLAT] = (const PipelineDef[]){ flatOpaque, flatTransparent },
};

static const uint32_t g_prototypeVariantCounts[PIPELINE_TYPE_COUNT] = {
    [PIPELINE_FLAT] = 2,
};
```

### 2.2 Pipeline Creation from Definitions

```c
// pipeline_manager.c

bool createPrototype(VulkanComponents* vc, RendererState* rs,
                     PipelineType type,
                     const PipelineDef* defs, uint32_t defCount)
{
    PipelinePrototype* proto = &rs->prototypes[type];
    proto->type = type;
    proto->implementationCount = defCount;
    proto->implementations = calloc(defCount, sizeof(PipelineImplementation));

    // Create pipeline cache (persisted to disk for fast startup)
    VkPipelineCacheCreateInfo cacheInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    vkCreatePipelineCache(vc->deviceQueueComp.device, &cacheInfo, NULL, &proto->cache);

    // Shared layout (push constants + descriptor set layouts are the same for all variants)
    // Layout creation as in Stage 1 ...
    proto->layout = /* created from shared push constant range + set layouts */;

    for (uint32_t i = 0; i < defCount; i++)
    {
        // Build VkGraphicsPipelineCreateInfo from defs[i]
        // This is a mechanical translation — each PipelineDef field maps to
        // exactly one VkPipeline create info sub-struct field.
        // See existing createGraphicsPipeline() for the template.

        // For compute pipelines (compShaderPath != NULL), use vkCreateComputePipelines instead.

        VkPipeline pipeline;
        vkCreateGraphicsPipelines(vc->deviceQueueComp.device, proto->cache,
                                  1, &pipelineInfo, NULL, &pipeline);

        proto->implementations[i] = (PipelineImplementation){
            .pipeline    = pipeline,
            .bindPoint   = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .depthWrite  = defs[i].depthWrite,
            .blendEnable = defs[i].blendEnable,
        };
    }

    return true;
}
```

### 2.3 Dynamic Rendering Transition

Replace `VkRenderPass` + `VkFramebuffer` with `vkCmdBeginRendering` (Vulkan 1.3 core).

**What gets deleted:**
- `createRenderPass()` in `pipeline.c`
- `VkRenderPass renderPass` from `RenderComponents`
- `FrameBufferGroup` struct and all framebuffer creation code
- `VkFramebuffer` array from `SwapChainComponents`

**What replaces it:**

```c
// In recordCommandBuffer:

VkRenderingAttachmentInfo colorAttachment = {
    .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView   = vc->swapChainComp.viewGroup.colorView,    // MSAA target
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
    .clearValue  = { .color = {{ 0.0f, 0.0f, 0.0f, 1.0f }} },
    .resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT,
    .resolveImageView   = vc->swapChainComp.viewGroup.views[imageIndex],
    .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
};

VkRenderingAttachmentInfo depthAttachment = {
    .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView   = vc->renderComp.buffers.depthView[imageIndex],
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .clearValue  = { .depthStencil = { 1.0f, 0 } },
};

VkRenderingInfo renderInfo = {
    .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .renderArea           = { .extent = swapExtent },
    .layerCount           = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments    = &colorAttachment,
    .pDepthAttachment     = &depthAttachment,
};

vkCmdBeginRendering(cmd, &renderInfo);
// ... draw commands ...
vkCmdEndRendering(cmd);
```

**Image layout transitions** previously handled by render pass initial/final layouts must now be done explicitly via `vkCmdPipelineBarrier2`:

```c
// Before rendering: transition swapchain image UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
// After rendering:  transition swapchain image COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR
```

This is more explicit but also more flexible — it enables arbitrary pass ordering without render pass compatibility constraints.

### 2.4 Pipeline Creation Without Render Pass

With dynamic rendering, pipelines use `VkPipelineRenderingCreateInfo` instead of a `VkRenderPass` reference:

```c
VkPipelineRenderingCreateInfo renderingInfo = {
    .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount    = 1,
    .pColorAttachmentFormats = &swapchainFormat,
    .depthAttachmentFormat   = depthFormat,
};

// Chain into VkGraphicsPipelineCreateInfo.pNext
pipelineInfo.pNext      = &renderingInfo;
pipelineInfo.renderPass = VK_NULL_HANDLE;  // no render pass
```

### 2.5 Swapchain Recreation Simplification

Without framebuffers and render passes, `recreateSwapChain` becomes significantly simpler:

```
Old: destroy framebuffers → destroy render pass → recreate swapchain → recreate views →
     recreate color/depth images → recreate render pass → recreate framebuffers → recreate pipelines

New: destroy views → destroy color/depth images → recreate swapchain → recreate views →
     recreate color/depth images
     (pipelines survive — they reference formats, not render pass objects)
```

### Acceptance Criteria (Stage 2)

- [ ] `PipelineDef` structs defined, flat pipeline definitions compile
- [ ] `createPrototype()` creates pipelines from definitions, stores in `RendererState.prototypes[]`
- [ ] `VkPipelineCache` integrated, cache data serialized to disk on shutdown
- [ ] `VkRenderPass`, `VkFramebuffer`, and all associated code deleted
- [ ] `vkCmdBeginRendering` / `vkCmdEndRendering` used for all rendering
- [ ] Explicit image layout transitions via pipeline barriers
- [ ] Swapchain recreation no longer recreates pipelines or render passes
- [ ] Demo renders identically

---

## Stage 3 — Shared Geometry and Memory Sub-Allocation

**Goal:** All mesh geometry lives in shared mega-buffers. Per-entity vertex/index `VkBuffer` and `VkDeviceMemory` allocations are eliminated. The GPU sub-allocator from Stage 0 becomes the sole allocation path for GPU resources.

### 3.1 Geometry Pool

```c
// geometry.h

typedef struct MeshRegion
{
    uint32_t    vertexOffset;   // byte offset into the vertex mega-buffer
    uint32_t    indexOffset;    // byte offset into the index mega-buffer
    uint32_t    indexCount;     // number of indices
    uint32_t    vertexCount;    // number of vertices (for non-indexed draws)
    int32_t     baseVertex;     // added to each index value before fetching
} MeshRegion;

typedef struct GeometryPool
{
    GpuAllocation   vertexAlloc;    // sub-allocation from GpuAllocator
    GpuAllocation   indexAlloc;

    VkBuffer        vertexBuffer;   // one buffer, bound to vertexAlloc.memory at vertexAlloc.offset
    VkBuffer        indexBuffer;    // one buffer, bound to indexAlloc.memory at indexAlloc.offset

    uint32_t        vertexWriteOffset;  // current write head (bytes)
    uint32_t        indexWriteOffset;

    uint32_t        meshCount;
    MeshRegion*     meshes;             // registered mesh regions
} GeometryPool;

// Upload mesh data, return a MeshRegion handle (index into meshes[]).
// Data is staged through a staging buffer → device-local transfer.
uint32_t geometry_pool_upload(GeometryPool* pool, GpuAllocator* alloc,
                              VkCommandPool cmdPool, VkQueue transferQueue,
                              const Vertex* vertices, uint32_t vertexCount,
                              const uint16_t* indices, uint32_t indexCount);
```

### 3.2 Staging Transfers

Replace `beginSingleTimeCommands` / `endSingleTimeCommands` (which call `vkQueueWaitIdle`) with a **transfer command buffer** that uses a fence for completion:

```c
typedef struct TransferContext
{
    VkCommandPool   pool;        // dedicated transfer command pool
    VkCommandBuffer cmd;
    VkFence         fence;
    VkBuffer        stagingBuffer;
    GpuAllocation   stagingAlloc;  // HOST_VISIBLE staging memory
} TransferContext;
```

For bulk mesh upload at load time, record all transfers into one command buffer and submit once. For runtime uploads (streaming), use a ring buffer of staging memory and per-frame fenced transfers. In either case, `vkQueueWaitIdle` is never called — the fence signals completion.

### 3.3 Revised EntityBuffer → RenderEntity

With shared geometry, the entity no longer owns any GPU resources:

```c
typedef struct RenderEntity
{
    uint32_t    meshIndex;       // index into GeometryPool.meshes[]
    uint32_t    textureIndex;    // index into texture registry (bindless in Stage 5)
    mat4        transform;       // world transform (pushed in Stage 1, SSBO in Stage 4)
} RenderEntity;
```

This is 76 bytes. A million of them is 76 MB — fits in L3 cache on target hardware (Ryzen 9).

### 3.4 Draw Loop Update

With shared geometry, vertex and index buffers are bound **once per frame**, not per entity:

```c
// Bind geometry pool buffers once
VkDeviceSize offsets[] = {0};
vkCmdBindVertexBuffers(cmd, 0, 1, &geoPool->vertexBuffer, offsets);
vkCmdBindIndexBuffer(cmd, geoPool->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

for (uint32_t i = 0; i < entityCount; i++)
{
    RenderEntity* e = &entities[i];
    MeshRegion* mesh = &geoPool->meshes[e->meshIndex];

    // Texture descriptor (per-entity until Stage 5)
    vkCmdBindDescriptorSets(cmd, ..., 1, 1, &textureDescriptors[e->textureIndex], 0, NULL);

    // Push transform
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mat4), &e->transform);

    // Indexed draw with base vertex
    vkCmdDrawIndexed(cmd, mesh->indexCount, 1, mesh->indexOffset / sizeof(uint16_t),
                     mesh->baseVertex, 0);
}
```

**Key improvement:** Zero vertex/index buffer rebinding regardless of entity count.

### 3.5 Migrate All Allocations to GpuAllocator

Every remaining `vkAllocateMemory` call moves through `gpu_alloc()`:
- Uniform buffers (camera UBO)
- Depth images
- MSAA color images
- Texture images (existing entities)
- Staging buffers

After this stage, `vkAllocateMemory` is called only inside `gpu_alloc()`.

### 3.6 RenderPrimitives Evolution

The existing `RenderPrimitives` tracking in `components.c` (usage-counted vertex/index/texture registries) is restructured to track `MeshRegion` handles and texture indices rather than raw `VkBuffer`/`VkDeviceMemory`. The usage-count pattern is preserved — it's the correct approach for resource lifetime management.

### Acceptance Criteria (Stage 3)

- [ ] `GeometryPool` allocated from `GpuAllocator`, one vertex buffer, one index buffer
- [ ] All meshes uploaded via `geometry_pool_upload`, returned as `MeshRegion` handles
- [ ] `EntityBuffer` struct deleted, replaced by `RenderEntity`
- [ ] `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer` called once per frame
- [ ] `vkCmdDrawIndexed` uses `baseVertex` / index offsets from `MeshRegion`
- [ ] No direct `vkAllocateMemory` calls outside `gpu_alloc()`
- [ ] Transfer commands use fenced submission, not `vkQueueWaitIdle`
- [ ] Demo renders identically with reduced memory allocation count

---

## Stage 4 — Transform SSBO and Indirect Drawing

**Goal:** Move per-entity transforms from push constants to a GPU-side SSBO. Replace per-entity `vkCmdDrawIndexed` with `vkCmdDrawIndexedIndirect`. The CPU issues one indirect draw call per pipeline variant, not one per entity.

### 4.1 Transform SSBO

```c
typedef struct TransformBuffer
{
    VkBuffer        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation   allocs[MAX_FRAMES_IN_FLIGHT];
    mat4*           mapped[MAX_FRAMES_IN_FLIGHT];  // persistently mapped
    uint32_t        capacity;   // max entities
    uint32_t        count;      // current entity count
} TransformBuffer;
```

Transforms are written by `memcpy` into the persistently mapped pointer — same pattern as the current UBO update, but for all entities at once into a contiguous buffer.

### 4.2 Shader Changes — SSBO Access

```glsl
// ===== flat.vert (Stage 4) =====
#version 450
#extension GL_ARB_shader_draw_parameters : require

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
} global;

layout(set = 0, binding = 1) readonly buffer TransformSSBO {
    mat4 transforms[];
} transformBuf;

// We store the entity index in the indirect command's firstInstance field,
// which the shader reads as gl_InstanceIndex (with firstInstance = entityIndex, instanceCount = 1).

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) flat out uint outMaterialIndex;

layout(push_constant) uniform PushConstants {
    uint transformBaseOffset;  // base offset into transform SSBO for this batch
} pc;

void main() {
    uint entityIndex = pc.transformBaseOffset + gl_InstanceIndex;
    mat4 model = transformBuf.transforms[entityIndex];

    gl_Position = global.proj * global.view * model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    outMaterialIndex = entityIndex;  // will index into material SSBO in Stage 5
}
```

**Encoding entity index into `firstInstance`:** Each `VkDrawIndexedIndirectCommand` sets `firstInstance` to the entity's index in the transform SSBO. `instanceCount` is 1. The shader reads `gl_InstanceIndex` which equals `firstInstance` when `instanceCount == 1`. This avoids any push constant changes between draws.

### 4.3 Indirect Draw Buffer

```c
typedef struct IndirectDrawBuffer
{
    VkBuffer                        buffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation                   allocs[MAX_FRAMES_IN_FLIGHT];
    VkDrawIndexedIndirectCommand*   mapped[MAX_FRAMES_IN_FLIGHT];
    uint32_t                        capacity;
    uint32_t                        drawCount[MAX_FRAMES_IN_FLIGHT];
} IndirectDrawBuffer;
```

### 4.4 CPU-Side Draw Command Generation

At this stage, the CPU builds the indirect draw buffer. (GPU-side generation moves to Stage 6.)

```c
void buildIndirectCommands(RendererState* rs, RenderEntity* entities, uint32_t entityCount,
                           GeometryPool* geoPool, uint32_t frameIndex)
{
    VkDrawIndexedIndirectCommand* cmds = rs->indirectBuffer.mapped[frameIndex];
    uint32_t drawCount = 0;

    // Sort entities by pipeline -> material -> mesh for optimal batching
    // (sorting happens on the entity array or an index array, not shown here)

    for (uint32_t i = 0; i < entityCount; i++)
    {
        RenderEntity* e = &entities[i];
        MeshRegion* mesh = &geoPool->meshes[e->meshIndex];

        cmds[drawCount++] = (VkDrawIndexedIndirectCommand){
            .indexCount    = mesh->indexCount,
            .instanceCount = 1,
            .firstIndex    = mesh->indexOffset / sizeof(uint16_t),
            .vertexOffset  = mesh->baseVertex,
            .firstInstance = i,  // entity index -> gl_InstanceIndex in shader
        };
    }

    rs->indirectBuffer.drawCount[frameIndex] = drawCount;
}
```

### 4.5 Command Recording — Indirect Draws

```c
void recordCommandBuffer(RendererState* rs, VulkanComponents* vc, uint32_t imageIndex)
{
    VkCommandBuffer cmd = vc->cmdComp.commandBuffer[rs->frameIndex];
    // ... begin, image transitions, begin rendering ...

    // Bind shared geometry (once)
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &geoPool->vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, geoPool->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    // For each pipeline prototype with active draws:
    for (uint32_t p = 0; p < PIPELINE_TYPE_COUNT; p++)
    {
        PipelinePrototype* proto = &rs->prototypes[p];
        if (!proto->implementations) continue;

        // Bind opaque variant first, then transparent
        for (uint32_t v = 0; v < proto->implementationCount; v++)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              proto->implementations[v].pipeline);

            // Set 0: global descriptors (camera + transform SSBO)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    proto->layout, 0, 1,
                                    &rs->globalSets[rs->frameIndex], 0, NULL);

            // Push constants: transform base offset for this batch
            uint32_t baseOffset = 0;
            vkCmdPushConstants(cmd, proto->layout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(uint32_t), &baseOffset);

            // ONE indirect draw call for ALL entities using this pipeline variant
            vkCmdDrawIndexedIndirect(
                cmd,
                rs->indirectBuffer.buffer[rs->frameIndex],
                batchOffset * sizeof(VkDrawIndexedIndirectCommand),
                batchDrawCount,
                sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    // ... end rendering, image transitions, end command buffer ...
}
```

**Result:** The inner loop over entities is gone from command recording. The CPU records O(pipeline variants) draw commands, not O(entities).

### 4.6 Descriptor Set 0 Update

Set 0 now includes the transform SSBO:

```
Set 0 — Global (bound once per frame):
  Binding 0: UBO    { mat4 view; mat4 proj; }
  Binding 1: SSBO   { mat4 transforms[]; }
```

### Acceptance Criteria (Stage 4)

- [ ] Transform SSBO created, persistently mapped, updated via `memcpy` per frame
- [ ] Indirect draw buffer created, CPU-built per frame
- [ ] `vkCmdDrawIndexedIndirect` replaces per-entity `vkCmdDrawIndexed`
- [ ] Entity index encoded in `firstInstance`, read as `gl_InstanceIndex` in shader
- [ ] Command recording is O(pipeline variants), not O(entities)
- [ ] Push constants reduced to batch metadata (base offset)
- [ ] Demo renders identically, even with 1000+ entities (test by duplicating)

---

## Stage 5 — Bindless Descriptors

**Goal:** Eliminate per-material descriptor set binding. All textures live in a single unsized descriptor array. Materials are an SSBO indexed by entity. Zero descriptor set switches for material changes.

### 5.1 Vulkan Feature Requirements

Enable at device creation (all Vulkan 1.2 core):
```c
VkPhysicalDeviceVulkan12Features features12 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    .descriptorIndexing                          = VK_TRUE,
    .shaderSampledImageArrayNonUniformIndexing    = VK_TRUE,
    .runtimeDescriptorArray                       = VK_TRUE,
    .descriptorBindingPartiallyBound              = VK_TRUE,
    .descriptorBindingVariableDescriptorCount     = VK_TRUE,
    .descriptorBindingSampledImageUpdateAfterBind  = VK_TRUE,
};
```

### 5.2 Descriptor Layout — Final Form

```
Set 0 — Global (bound once per frame):
  Binding 0: UBO    { mat4 view; mat4 proj; float time; }
  Binding 1: SSBO   { mat4 transforms[]; }             — all entity transforms
  Binding 2: SSBO   { MaterialData materials[]; }       — all material properties

Set 1 — Bindless Textures (bound once, updated incrementally):
  Binding 0: sampler2D textures[]                       — variable count, partially bound
```

Push constants reduce to per-batch metadata only:
```c
layout(push_constant) uniform PushConstants {
    uint transformBase;   // 4 bytes — offset into transform SSBO
    uint materialBase;    // 4 bytes — offset into material SSBO
} pc;
```

### 5.3 Material SSBO

```c
typedef struct MaterialData
{
    uint32_t    albedoIndex;        // index into bindless texture array
    uint32_t    normalIndex;        // 0 = no normal map
    float       roughness;          // non-PBR: controls stylized specular falloff
    float       emissive;           // emissive intensity multiplier
    float       color[4];           // tint color (RGBA)
} MaterialData;
```

```glsl
// flat.frag (Stage 5)
#version 450
#extension GL_EXT_nonuniform_qualifier : require

struct MaterialData {
    uint  albedoIndex;
    uint  normalIndex;
    float roughness;
    float emissive;
    vec4  color;
};

layout(set = 0, binding = 2) readonly buffer MaterialSSBO {
    MaterialData materials[];
} materialBuf;

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) flat in uint materialIndex;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialData mat = materialBuf.materials[materialIndex];
    vec4 texColor = texture(textures[nonuniformEXT(mat.albedoIndex)], fragTexCoord);
    outColor = texColor * mat.color;
}
```

### 5.4 Texture Registration

```c
typedef struct BindlessTextureArray
{
    VkDescriptorPool        pool;
    VkDescriptorSetLayout   layout;
    VkDescriptorSet         set;            // ONE set, holds ALL textures
    uint32_t                maxTextures;    // upper bound (e.g. 4096)
    uint32_t                textureCount;   // current count
    VkSampler               defaultSampler; // shared linear/repeat sampler
} BindlessTextureArray;

// Register a texture, returns its index in the bindless array.
// Uses vkUpdateDescriptorSets to write the new entry.
uint32_t bindless_register_texture(BindlessTextureArray* bta, VkImageView view);
```

The descriptor set is created with `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT` and `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT`, allowing the array to grow dynamically without recreating the set.

### 5.5 Entity → Material Mapping

`RenderEntity` gains a material index:

```c
typedef struct RenderEntity
{
    uint32_t    meshIndex;
    uint32_t    materialIndex;   // index into MaterialSSBO
    mat4        transform;
} RenderEntity;
```

The material index is encoded into the indirect draw command alongside the entity index. The vertex shader passes it to the fragment shader via a flat output.

### Acceptance Criteria (Stage 5)

- [ ] `VK_EXT_descriptor_indexing` features enabled at device creation
- [ ] Bindless texture array created with partial binding and variable count
- [ ] All textures registered in a single descriptor set
- [ ] Material SSBO populated and bound as Set 0, Binding 2
- [ ] Fragment shader indexes into `textures[]` using material's `albedoIndex`
- [ ] Per-entity texture descriptor sets deleted entirely
- [ ] Only 2 descriptor sets bound per frame (Set 0 global, Set 1 bindless textures)
- [ ] Demo renders with multiple distinct materials

---

## Stage 6 — GPU-Driven Culling and Multi-Pass

**Goal:** Move entity culling to a compute shader. The GPU builds the indirect draw buffer. The CPU's per-frame rendering work becomes a fixed-size command sequence. Introduce a pass list for multi-pass rendering.

### 6.1 Compute Culling Shader

```glsl
// ===== cull.comp =====
#version 450

layout(local_size_x = 256) in;

struct EntityInfo {
    uint meshIndex;
    uint materialIndex;
};

layout(set = 0, binding = 0) uniform CullUBO {
    mat4  viewProj;
    vec4  frustumPlanes[6];
    uint  entityCount;
};

layout(set = 0, binding = 1) readonly buffer TransformSSBO {
    mat4 transforms[];
};

layout(set = 0, binding = 2) readonly buffer EntitySSBO {
    EntityInfo entities[];
};

// Packed mesh region data
layout(set = 0, binding = 3) readonly buffer MeshSSBO {
    // x = indexCount, y = firstIndex, z = baseVertex, w = boundingSphereRadius
    uvec4 meshDrawData[];
};

layout(set = 0, binding = 4) readonly buffer MeshBoundsSSBO {
    // xyz = bounding sphere center (object space), w = radius
    vec4 meshBounds[];
};

layout(set = 0, binding = 5) buffer IndirectBuffer {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
} draws[];

layout(set = 0, binding = 6) buffer DrawCount {
    uint drawCount;
};

bool isVisible(vec3 center, float radius, mat4 model) {
    vec4 worldCenter = model * vec4(center, 1.0);
    float worldRadius = radius * max(max(
        length(model[0].xyz),
        length(model[1].xyz)),
        length(model[2].xyz));

    for (int i = 0; i < 6; i++) {
        if (dot(frustumPlanes[i], worldCenter) < -worldRadius) {
            return false;
        }
    }
    return true;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= entityCount) return;

    EntityInfo entity = entities[idx];
    mat4 model = transforms[idx];
    vec4 bounds = meshBounds[entity.meshIndex];
    uvec4 meshData = meshDrawData[entity.meshIndex];

    if (isVisible(bounds.xyz, bounds.w, model)) {
        uint drawIdx = atomicAdd(drawCount, 1);

        draws[drawIdx].indexCount    = meshData.x;
        draws[drawIdx].instanceCount = 1;
        draws[drawIdx].firstIndex    = meshData.y;
        draws[drawIdx].vertexOffset  = int(meshData.z);
        draws[drawIdx].firstInstance = idx;  // entity index
    }
}
```

### 6.2 Compute Pipeline Prototype

The `PipelinePrototype` / `PipelineImplementation` model naturally extends to compute:

```c
static const PipelineDef cullCompute = {
    .type           = PIPELINE_COMPUTE_CULL,
    .compShaderPath = "cull.comp.spv",
};
```

`PipelineImplementation.bindPoint` is `VK_PIPELINE_BIND_POINT_COMPUTE`, and the pipeline is created via `vkCreateComputePipelines` instead of `vkCreateGraphicsPipelines`. The `PipelinePrototype` structure is unchanged — it holds the layout, cache, and implementations regardless of bind point.

Add to the enum:

```c
typedef enum PipelineType
{
    PIPELINE_FLAT = 0,
    PIPELINE_PARTICLE,
    PIPELINE_SDF_COMPOSITE,
    PIPELINE_UI,
    PIPELINE_COMPUTE_CULL,      // compute: frustum culling
    PIPELINE_TYPE_COUNT
} PipelineType;
```

### 6.3 Per-Frame Command Sequence — Final Form

```c
void recordFrame(RendererState* rs, VulkanComponents* vc, uint32_t imageIndex)
{
    VkCommandBuffer cmd = vc->cmdComp.commandBuffer[rs->frameIndex];
    vkBeginCommandBuffer(cmd, &beginInfo);

    // --- Phase 1: Reset draw count ---
    vkCmdFillBuffer(cmd, rs->drawCountBuffer.buffer[rs->frameIndex],
                    0, sizeof(uint32_t), 0);

    VkMemoryBarrier2 resetBarrier = {
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
    };
    VkDependencyInfo resetDep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &resetBarrier,
    };
    vkCmdPipelineBarrier2(cmd, &resetDep);

    // --- Phase 2: GPU culling ---
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      rs->prototypes[PIPELINE_COMPUTE_CULL].implementations[0].pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            rs->prototypes[PIPELINE_COMPUTE_CULL].layout,
                            0, 1, &rs->cullDescriptorSet[rs->frameIndex], 0, NULL);
    vkCmdDispatch(cmd, (entityCount + 255) / 256, 1, 1);

    // Barrier: compute writes -> indirect draw reads
    VkMemoryBarrier2 cullBarrier = {
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
        .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    };
    VkDependencyInfo cullDep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &cullBarrier,
    };
    vkCmdPipelineBarrier2(cmd, &cullDep);

    // --- Phase 3: Image transition -> COLOR_ATTACHMENT_OPTIMAL ---
    // ... vkCmdPipelineBarrier2 for swapchain image ...

    // --- Phase 4: Geometry pass ---
    vkCmdBeginRendering(cmd, &geometryRenderInfo);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &geoPool->vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, geoPool->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    // Opaque pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      rs->prototypes[PIPELINE_FLAT].implementations[0].pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            rs->prototypes[PIPELINE_FLAT].layout,
                            0, 2, descriptorSets, 0, NULL);

    vkCmdDrawIndexedIndirectCount(
        cmd,
        rs->indirectBuffer.buffer[rs->frameIndex], 0,
        rs->drawCountBuffer.buffer[rs->frameIndex], 0,
        rs->indirectBuffer.capacity,
        sizeof(VkDrawIndexedIndirectCommand));

    vkCmdEndRendering(cmd);

    // --- Phase 5: Image transition -> PRESENT_SRC_KHR ---
    // ... vkCmdPipelineBarrier2 for swapchain image ...

    vkEndCommandBuffer(cmd);
}
```

**CPU per-frame work:** update transform SSBO (`memcpy`), update cull UBO (camera + frustum), record ~15 commands. **O(1) regardless of entity count.**

### 6.4 Pass List Architecture

Generalize the rendering loop to support multiple passes:

```c
typedef enum PassType
{
    PASS_COMPUTE,       // compute dispatch (culling, SDF evaluation, etc.)
    PASS_GRAPHICS,      // rasterization pass
} PassType;

typedef struct RenderPassDef
{
    PassType            type;
    PipelineType        prototype;              // which pipeline prototype to bind
    uint32_t            implementationIndex;    // which variant (opaque, transparent, etc.)

    // Graphics-only:
    uint32_t                colorAttachmentCount;
    VkFormat                colorFormats[4];
    VkFormat                depthFormat;
    VkAttachmentLoadOp      colorLoadOp;
    VkAttachmentLoadOp      depthLoadOp;
    VkClearValue            colorClear;
    VkClearValue            depthClear;
    VkResolveModeFlagBits   resolveMode;

    // Compute-only:
    uint32_t                dispatchX, dispatchY, dispatchZ;
} RenderPassDef;
```

The frame is defined as an ordered array of `RenderPassDef`:

```c
static const RenderPassDef g_framePasses[] = {
    // 1. GPU culling
    {
        .type       = PASS_COMPUTE,
        .prototype  = PIPELINE_COMPUTE_CULL,
        .dispatchX  = 0,  // computed from entityCount at runtime
    },
    // 2. Opaque geometry
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 0,  // opaque variant
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .resolveMode            = VK_RESOLVE_MODE_AVERAGE_BIT,
    },
    // 3. Transparent geometry (same attachments, no clear, no depth write)
    {
        .type                   = PASS_GRAPHICS,
        .prototype              = PIPELINE_FLAT,
        .implementationIndex    = 1,  // transparent variant
        .colorAttachmentCount   = 1,
        .colorLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
        .depthLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD,
    },
    // 4. UI overlay (future)
    // 5. SDF composite (future)
};
```

The command recording loop iterates this array, inserting barriers between passes and dispatching/drawing as specified. New passes are added by appending to the array — no structural changes to the recording logic.

### 6.5 Future Pass Slots

| Pass | Prototype | Purpose |
|------|-----------|---------|
| Shadow depth | `PIPELINE_SHADOW` | Depth-only render to shadow map (no color attachment) |
| SDF evaluation | `PIPELINE_COMPUTE_SDF` | Compute: evaluate SDF field for space environments |
| SDF composite | `PIPELINE_SDF_COMPOSITE` | Graphics: fullscreen quad, raymarch against SDF texture |
| UI | `PIPELINE_UI` | Graphics: SDF text + UI elements over final image |
| Post-process | `PIPELINE_COMPUTE_POST` | Compute: tone mapping, bloom (if ever needed) |

Each of these is a `PipelineType` enum value, a compile-time `PipelineDef`, and a `RenderPassDef` entry. The system is extensible without structural changes.

### Acceptance Criteria (Stage 6)

- [ ] Compute culling shader compiles and executes
- [ ] `vkCmdDrawIndexedIndirectCount` consumes GPU-built indirect buffer
- [ ] Draw count verified by validation layers (non-zero, <= entityCount)
- [ ] Pass list architecture implemented — passes defined as data, iterated generically
- [ ] Barriers correctly placed between compute and draw stages
- [ ] CPU per-frame command count is fixed regardless of entity count
- [ ] 100K+ entities render at interactive frame rates (>30fps on target hardware)
- [ ] Frustum culling visually confirmed (off-screen entities not drawn)

---

## Appendix A — Migration Checklist (Files Affected)

| File | Stage | Changes |
|------|-------|---------|
| `memory/memory.c` | 0 | Delete |
| `memory/memory.h` | 0 | Delete |
| `components.h` | 0 | Strip to pipeline types + render primitives |
| `components.c` | 0 | Retain usage-count logic, evolve in Stage 3 |
| `structs.h` | 0-3 | Incremental: delete dead fields (0), add `RendererState` (0), delete `EntityBuffer` (3) |
| `pipeline.h` | 2 | Replace declarations with `createPrototype()` |
| `pipeline.c` | 1-2 | Stage 1: add push constants. Stage 2: rewrite as `pipeline_manager.c` |
| `vulkanMaster.h` | 0 | Update function signatures to take `RendererState*` |
| `vulkanMaster.c` | 1-6 | Incremental rewrite of `drawFrame` and `recordCommandBuffer` |
| `instanceInit.c` | 0-3 | Stage 0: delete duplicated functions. Stage 3: migrate allocations to `GpuAllocator` |
| `texture/texture.c` | 5 | Migrate to `BindlessTextureArray` registration |
| `vertex/vertex.h` | 1 | Delete `UniformComponents`, `ModelTransforms`. Vertex struct unchanged. |
| `vertex/vertex.c` | 1 | Delete `updateMeshTransforms`. Math functions retained. |
| `base.vert` -> `flat.vert` | 1 | Rewrite for push constants |
| `base.frag` -> `flat.frag` | 1 | Simplify to single sampler (bindless in Stage 5) |
| *New:* `gpu_alloc.h/c` | 0 | GPU memory sub-allocator |
| *New:* `pipeline_defs.h/c` | 2 | Compile-time pipeline definitions |
| *New:* `pipeline_manager.c` | 2 | Pipeline creation from definitions |
| *New:* `geometry.h/c` | 3 | Geometry pool (shared mega-buffers) |
| *New:* `cull.comp` | 6 | GPU frustum culling shader |

## Appendix B — Struct Evolution Summary

```
Stage 0:  VulkanComponents (trimmed) + RendererState (new) + GpuAllocator (new)
Stage 1:  EntityBuffer loses transform UBO -> gains mat4 transform + textureDescriptorSet
Stage 2:  PipelinePrototype[] replaces single graphicsPipeline. Dynamic rendering replaces VkRenderPass.
Stage 3:  EntityBuffer -> RenderEntity (76 bytes). GeometryPool replaces per-entity VkBuffers.
Stage 4:  RenderEntity.transform -> TransformSSBO. IndirectDrawBuffer replaces per-entity drawIndexed.
Stage 5:  BindlessTextureArray. MaterialSSBO. Per-entity descriptor sets deleted entirely.
Stage 6:  GPU culling. Pass list. CPU records ~15 commands per frame regardless of entity count.
```

## Appendix C — Descriptor Set Layout Evolution

| Stage | Set 0 | Set 1 | Push Constants |
|-------|-------|-------|----------------|
| Current | UBO: model+view+proj | UBO: translation+rotation+scale, sampler | none |
| 1 | UBO: view+proj | sampler only (per-entity) | mat4 model (64B) + materialIndex (4B) + pad (12B) |
| 3 | (same) | (same) | (same) |
| 4 | UBO: view+proj, SSBO: transforms[] | sampler only (per-entity) | uint transformBase (4B) + pad |
| 5 | UBO: view+proj, SSBO: transforms[], SSBO: materials[] | sampler2D textures[] (bindless, one set) | uint transformBase, uint materialBase |
| 6 | (same + cull UBO for compute) | (same) | (same) |

## Appendix D — Performance Projections

Assumptions: Ryzen 9 CPU, RTX 4090 GPU, flat-shaded geometry, average 500 triangles/entity.

| Metric | Current (3 entities) | Stage 1 | Stage 4 | Stage 6 (1M entities) |
|--------|---------------------|---------|---------|----------------------|
| CPU draw commands / frame | 3 | N | ~5 | ~15 |
| Descriptor set binds / frame | N+1 | N+1 | 2 | 2 |
| VB/IB binds / frame | N | 1 | 1 | 1 |
| `vkAllocateMemory` calls total | ~5N | ~5N | ~20 | ~20 |
| GPU draw calls / frame | N | N | 1 | 1 |
| CPU time for cmd recording | ~us | ~Nus | ~50us | ~50us |

At 1M entities, Stage 6 records the same ~15 commands as with 3 entities. The GPU executes 1M draws from a single `vkCmdDrawIndexedIndirectCount`. Culling reduces actual fragment work to visible entities only.
