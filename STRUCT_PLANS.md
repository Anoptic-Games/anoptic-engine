# Vulkan Backend Struct Refactor — Planning Document

## Scope

This document plans the incremental refactor of `src/vulkan_backend/structs.h` and the surrounding data ownership scheme. The codebase is in a half-migrated state between two parallel root structs — the legacy `VulkanComponents` and the newer `RendererState` — with muddled ownership boundaries, dead fields, scattered per-frame arrays, and a `VkDeviceMemory` / `GpuAllocation` inconsistency that is a correctness hazard.

The refactor is broken into **5 phases**, ordered from zero-risk to high-touch. Each phase is independently landable and leaves the code in a compilable, working state.

### Files in scope

Every file that directly includes `structs.h` or references `VulkanComponents` / `RendererState`:

| File | References |
|------|------------|
| `src/vulkan_backend/structs.h` | Defines both root structs |
| `src/vulkan_backend/vulkanMaster.c` | Declares both as globals (L19–20), main init/draw/cleanup orchestration |
| `src/vulkan_backend/vulkanMaster.h` | `extern RendererState rendererState;`, public API |
| `src/vulkan_backend/instance/instanceInit.c` | All init/cleanup functions, 22 functions taking `VulkanComponents*` |
| `src/vulkan_backend/instance/instanceInit.h` | Prototypes for above |
| `src/vulkan_backend/instance/pipeline.c` | 5 functions taking both `VulkanComponents*` + `RendererState*` |
| `src/vulkan_backend/instance/pipeline.h` | Prototypes for above |
| `src/vulkan_backend/instance/pipeline_old.c` | Legacy, references `VulkanComponents` |
| `src/vulkan_backend/texture/texture.c` | `createImage()`, `createTextureImage()`, `bindless_register_texture()` |
| `src/vulkan_backend/texture/texture.h` | Prototypes for above |
| `src/vulkan_backend/components.h` | Defines `PipelinePrototype`, `RenderPrimitives`, `TextureData`, `MeshData` |
| `src/vulkan_backend/components.c` | Implements mesh/texture registration (operates on `RenderPrimitives*` only) |
| `src/vulkan_backend/geometry.h` | `GeometryPool`, mesh upload functions |
| `src/vulkan_backend/geometry.c` | `geometry_pool_upload()`, acknowledged staging leak |
| `src/vulkan_backend/gpu_alloc.h` | `GpuAllocator`, `GpuAllocation`, extern globals |
| `src/vulkan_backend/gpu_alloc.c` | Allocator implementation |
| `src/vulkan_backend/vulkanConfig.h` | Includes `structs.h` |
| `src/vulkan_backend/vulkanConfig.c` | Config helpers |
| `src/render/gltf/ano_GltfParser.h` | `parseGltf(VulkanComponents*, ...)` |
| `src/render/gltf/ano_GltfParser.c` | Uses `VulkanComponents*`, writes entities and materials |
| `src/render/gltf/scratch_process.c` | References `VulkanComponents` |
| `src/engine/main.h` | Includes `structs.h` |

---

## Phase 1 — Remove Dead Code

> **Risk**: Zero. Removes unreferenced fields and types. No behavioral change.

### 1.1 Delete `FrameImageGroup`

**Why**: Typedef defined at `structs.h:34–39`, never instantiated or referenced in any `.c` file.

**Change**: Delete lines 33–39 and the blank line following.

```diff
- // New struct for per-frame images
- typedef struct FrameImageGroup
- {
-     VkImage image;
-     VkImageView view;
-     VkDeviceMemory imageMemory; // This won't be used for the final present images, memory is managed by the swapchain
- } FrameImageGroup;
```

**Affected files**: `structs.h` only.

---

### 1.2 Delete `SwapChainGroup.imageMemory`

**Why**: Self-documented as "Not actually used, swapchain image memory managed by Vulkan." Never written or read.

**Change**: Remove the field from `SwapChainGroup` at `structs.h:57`.

```diff
  typedef struct SwapChainGroup
  {
  	VkSwapchainKHR swapChain;
  	VkFormat imageFormat;
  	VkExtent2D imageExtent;
  	uint32_t imageCount;
  	VkImage* images;
- 	VkDeviceMemory imageMemory[MAX_FRAMES_IN_FLIGHT]; // Not actually used, swapchain image memory managed by Vulkan.
  	VkImage colorImage;
  	VkDeviceMemory colorImageMemory;
  } SwapChainGroup;
```

**Affected files**: `structs.h` only (confirm with `grep -rn "imageMemory\[" src/` — only hits are the dead `SwapChainGroup.imageMemory` field and the `FrameImageGroup.imageMemory` field also being deleted).

---

### 1.3 Delete `SynchronizationComponents.imageIndex`

**Why**: Comment says "Used to track submitted frames for presentation, move to swapchain" but the field is never read — `imageIndex` is always a local variable in `drawFrame()` (vulkanMaster.c:450).

**Change**: Remove from `SynchronizationComponents` at `structs.h:161`.

```diff
  typedef struct SynchronizationComponents
  {
      VkSemaphore imageAvailableSemaphore[MAX_FRAMES_IN_FLIGHT];
      VkSemaphore renderFinishedSemaphore[MAX_FRAMES_IN_FLIGHT];
      VkFence inFlightFence[MAX_FRAMES_IN_FLIGHT];
  	bool frameSubmitted[MAX_FRAMES_IN_FLIGHT];
      uint32_t frameIndex;
-     uint32_t imageIndex; // Used to track submitted frames for presentation, move to swapchain
      bool framebufferResized;
  } SynchronizationComponents;
```

**Verification**: `grep -rn 'syncComp\.imageIndex\|syncComp->imageIndex' src/` — should return zero results.

**Affected files**: `structs.h` only.

---

### 1.4 Delete vestigial `RendererState` sync/entity fields

**Why**: These were placeholders for a migration that never happened. All seven fields have zero references in the entire codebase (never written, read, or cleaned up).

**Change**: Remove from `RendererState` at `structs.h:311–315` and `structs.h:348–349`.

```diff
  typedef struct RendererState
  {
      // Pipeline system (Stage 0+)
      PipelinePrototype       prototypes[PIPELINE_TYPE_COUNT];

      // Descriptor infrastructure (to be populated per-stage)
      VkDescriptorPool        globalDescriptorPool;
      VkDescriptorSetLayout   globalSetLayout;        // Set 0
      VkDescriptorSet         globalSets[MAX_FRAMES_IN_FLIGHT];

-     // Synchronization — lifted from SynchronizationComponents
-     VkSemaphore             imageAvailable[MAX_FRAMES_IN_FLIGHT];
-     VkSemaphore             renderFinished[MAX_FRAMES_IN_FLIGHT];
-     VkFence                 frameFence[MAX_FRAMES_IN_FLIGHT];
-     uint32_t                frameIndex;
      // Geometry
      GeometryPool            globalGeometryPool;
      ...
-
-     RenderEntity*           entities;
-     uint32_t                entityCount;
  } RendererState;
```

**Verification**: `grep -rn 'rendererState\.imageAvailable\|rendererState\.renderFinished\|rendererState\.frameFence\|rendererState\.frameIndex\b' src/` — zero results. `grep -rn 'rendererState\.entities\b\|rendererState\.entityCount' src/` — confirm `entityCount` is only referenced in `createCullingBuffers` (vulkanMaster.c:625) where `state->entityCount = maxEntities` is a capacity assignment, not actual entity tracking. That one write needs to be redirected to the `CullingBuffers.maxEntities` field created in Phase 2.

**Affected files**: `structs.h`. One call site in `vulkanMaster.c:625` needs adjustment (deferred to Phase 2 where `CullingBuffers` gains a `maxEntities` field).

> [!NOTE]
> For Phase 1, temporarily keep `entityCount` to avoid touching `createCullingBuffers`. It will be absorbed into `CullingBuffers` in Phase 2.

---

### 1.5 Phase 1 validation

After all Phase 1 changes:

```bash
cmake --build build 2>&1 | grep -c "error:"
# Expected: 0
```

No runtime behavior changes. All tests and the main render loop should be unaffected.

---

## Phase 2 — Wrap Culling Arrays into `CullingBuffers`

> **Risk**: Low. Purely structural — wraps 12 loose parallel arrays and 2 descriptor fields into a named struct. All access sites are mechanically updated from `state->entityBuffer[i]` to `state->culling.entityBuffer[i]`.

### 2.1 Define `CullingBuffers` struct

**Where**: `structs.h`, insert before `RendererState`.

```c
typedef struct CullingBuffers
{
    CullUboBuffer           ubo;

    // Per-entity culling input
    VkBuffer                entityBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           entityAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   entityMapped[MAX_FRAMES_IN_FLIGHT];

    // Mesh draw parameters (firstIndex, indexCount, vertexOffset per mesh)
    VkBuffer                meshDataBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshDataAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshDataMapped[MAX_FRAMES_IN_FLIGHT];

    // Bounding volumes for frustum testing
    VkBuffer                meshBoundsBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           meshBoundsAllocs[MAX_FRAMES_IN_FLIGHT];
    void*                   meshBoundsMapped[MAX_FRAMES_IN_FLIGHT];

    // GPU-written draw count (atomic counter output from cull shader)
    VkBuffer                drawCountBuffer[MAX_FRAMES_IN_FLIGHT];
    GpuAllocation           drawCountAllocs[MAX_FRAMES_IN_FLIGHT];
    uint32_t*               drawCountMapped[MAX_FRAMES_IN_FLIGHT];

    // Descriptor infrastructure
    VkDescriptorSetLayout   setLayout;
    VkDescriptorSet         sets[MAX_FRAMES_IN_FLIGHT];

    // Capacity tracking (was RendererState.entityCount, repurposed)
    uint32_t                maxEntities;
} CullingBuffers;
```

### 2.2 Replace loose fields in `RendererState`

Remove the 14 loose culling fields + `cullSetLayout` + `cullSets` + `cullUboBuffer` + `entityCount`, replace with single member:

```diff
  typedef struct RendererState
  {
      ...
      IndirectDrawBuffer      indirectBuffer;
-     CullUboBuffer           cullUboBuffer;
      BindlessTextureArray    bindlessTextures;

      // Fallback resources
      VkImage                 fallbackImage;
      VkImageView             fallbackImageView;

      DeletionQueue           deletionQueues[MAX_FRAMES_IN_FLIGHT];

-     // Culling system (Stage 6)
-     VkDescriptorSetLayout   cullSetLayout;
-     VkDescriptorSet         cullSets[MAX_FRAMES_IN_FLIGHT];
-     VkBuffer                entityBuffer[MAX_FRAMES_IN_FLIGHT];
-     GpuAllocation           entityAllocs[MAX_FRAMES_IN_FLIGHT];
-     void*                   entityMapped[MAX_FRAMES_IN_FLIGHT];
-     VkBuffer                meshDataBuffer[MAX_FRAMES_IN_FLIGHT];
-     GpuAllocation           meshDataAllocs[MAX_FRAMES_IN_FLIGHT];
-     void*                   meshDataMapped[MAX_FRAMES_IN_FLIGHT];
-     VkBuffer                meshBoundsBuffer[MAX_FRAMES_IN_FLIGHT];
-     GpuAllocation           meshBoundsAllocs[MAX_FRAMES_IN_FLIGHT];
-     void*                   meshBoundsMapped[MAX_FRAMES_IN_FLIGHT];
-     VkBuffer                drawCountBuffer[MAX_FRAMES_IN_FLIGHT];
-     GpuAllocation           drawCountAllocs[MAX_FRAMES_IN_FLIGHT];
-     uint32_t*               drawCountMapped[MAX_FRAMES_IN_FLIGHT];
-
-     RenderEntity*           entities;
-     uint32_t                entityCount;
+     // Culling system
+     CullingBuffers          culling;
  } RendererState;
```

### 2.3 Mechanical access-path updates

Every `state->cullSetLayout` becomes `state->culling.setLayout`, etc. The full mapping:

| Old path | New path |
|----------|----------|
| `state->cullUboBuffer` | `state->culling.ubo` |
| `state->cullSetLayout` | `state->culling.setLayout` |
| `state->cullSets[i]` | `state->culling.sets[i]` |
| `state->entityBuffer[i]` | `state->culling.entityBuffer[i]` |
| `state->entityAllocs[i]` | `state->culling.entityAllocs[i]` |
| `state->entityMapped[i]` | `state->culling.entityMapped[i]` |
| `state->meshDataBuffer[i]` | `state->culling.meshDataBuffer[i]` |
| `state->meshDataAllocs[i]` | `state->culling.meshDataAllocs[i]` |
| `state->meshDataMapped[i]` | `state->culling.meshDataMapped[i]` |
| `state->meshBoundsBuffer[i]` | `state->culling.meshBoundsBuffer[i]` |
| `state->meshBoundsAllocs[i]` | `state->culling.meshBoundsAllocs[i]` |
| `state->meshBoundsMapped[i]` | `state->culling.meshBoundsMapped[i]` |
| `state->drawCountBuffer[i]` | `state->culling.drawCountBuffer[i]` |
| `state->drawCountAllocs[i]` | `state->culling.drawCountAllocs[i]` |
| `state->drawCountMapped[i]` | `state->culling.drawCountMapped[i]` |
| `state->entityCount` | `state->culling.maxEntities` |

Files that need updating (search for `rendererState.cull\|state->cull\|rendererState\.entity\|state->entity\|rendererState\.meshData\|state->meshData\|rendererState\.meshBounds\|state->meshBounds\|rendererState\.drawCount\|state->drawCount`):

| File | Approximate hit count |
|------|----------------------|
| `vulkanMaster.c` | ~40 (createCullingBuffers, updateCullingBuffers, recordCommandBuffer, drawFrame) |
| `instanceInit.c` | ~30 (createDescriptorSets, updateUboDescriptorSets, cleanupVulkan) |

### 2.4 Phase 2 validation

```bash
cmake --build build 2>&1 | grep -c "error:"
# Expected: 0
```

Run the engine. Culling, indirect draw, and the full render loop must behave identically.

---

## Phase 3 — Create `PerFrameResources`

> **Risk**: Medium. Consolidates ~30 per-frame arrays from both `VulkanComponents` and `RendererState` into a single `PerFrameResources frames[MAX_FRAMES_IN_FLIGHT]` array. Touches the init, draw, and cleanup paths.

### 3.1 Design the struct

```c
typedef struct PerFrameResources
{
    // Synchronization
    VkSemaphore         imageAvailable;
    VkSemaphore         renderFinished;
    VkFence             frameFence;
    bool                frameSubmitted;

    // Command recording
    VkCommandBuffer     commandBuffer;

    // Global UBO (view/proj)
    VkBuffer            uniformBuffer;
    GpuAllocation       uniformAlloc;
    void*               uniformMapped;

    // Depth attachment
    VkImage             depthImage;
    GpuAllocation       depthAlloc;     // Phase 5: replaces VkDeviceMemory
    VkImageView         depthView;

    // Descriptor sets
    VkDescriptorSet     globalSet;
    VkDescriptorSet     cullSet;

    // Deferred resource deletion
    DeletionQueue       deletionQueue;
} PerFrameResources;
```

### 3.2 Where to place it

The `PerFrameResources` struct definition goes in `structs.h`. The `frames[MAX_FRAMES_IN_FLIGHT]` array will initially live in `RendererState` (the target for migration):

```c
typedef struct RendererState
{
    PerFrameResources       frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    bool                    framebufferResized;
    ...
};
```

### 3.3 Migration mapping

Each field moves from its old location:

| New location | Old location | Old owner |
|---|---|---|
| `frames[i].imageAvailable` | `syncComp.imageAvailableSemaphore[i]` | `VulkanComponents` |
| `frames[i].renderFinished` | `syncComp.renderFinishedSemaphore[i]` | `VulkanComponents` |
| `frames[i].frameFence` | `syncComp.inFlightFence[i]` | `VulkanComponents` |
| `frames[i].frameSubmitted` | `syncComp.frameSubmitted[i]` | `VulkanComponents` |
| `frames[i].commandBuffer` | `cmdComp.commandBuffer[i]` | `VulkanComponents` |
| `frames[i].uniformBuffer` | `renderComp.buffers.uniform[i]` | `VulkanComponents` |
| `frames[i].uniformAlloc` | `renderComp.buffers.uniformAlloc[i]` | `VulkanComponents` |
| `frames[i].uniformMapped` | `renderComp.buffers.uniformMapped[i]` | `VulkanComponents` |
| `frames[i].depthImage` | `renderComp.buffers.depth[i]` | `VulkanComponents` |
| `frames[i].depthAlloc` | `renderComp.buffers.depthMemory[i]` | `VulkanComponents` (broken — see Phase 5) |
| `frames[i].depthView` | `renderComp.buffers.depthView[i]` | `VulkanComponents` |
| `frames[i].globalSet` | `globalSets[i]` | `RendererState` |
| `frames[i].cullSet` | `culling.sets[i]` (after Phase 2) | `RendererState` |
| `frames[i].deletionQueue` | `deletionQueues[i]` | `RendererState` |
| `state->frameIndex` | `syncComp.frameIndex` | `VulkanComponents` |
| `state->framebufferResized` | `syncComp.framebufferResized` | `VulkanComponents` |

### 3.4 Update plan by file

Because this migration is cross-cutting, it must be done file-by-file. Each file's changes are listed below.

#### `structs.h`

1. Add the `PerFrameResources` struct definition (before `RendererState`).
2. Add `PerFrameResources frames[MAX_FRAMES_IN_FLIGHT]`, `uint32_t frameIndex`, and `bool framebufferResized` to `RendererState`.
3. Remove `globalSets[MAX_FRAMES_IN_FLIGHT]` from `RendererState` (absorbed into `frames[i].globalSet`).
4. Remove `deletionQueues[MAX_FRAMES_IN_FLIGHT]` from `RendererState` (absorbed into `frames[i].deletionQueue`).
5. Remove `cullSets[MAX_FRAMES_IN_FLIGHT]` from `CullingBuffers` (after Phase 2) — absorbed into `frames[i].cullSet`.
6. Remove `SynchronizationComponents` entirely (all fields absorbed).
7. Remove `CommandComponents.commandBuffer[]` — only `commandPool` remains, move to `RendererState` directly.
8. Remove per-frame fields from `BufferComponents` (`uniform[]`, `uniformAlloc[]`, `uniformMapped[]`, `depth[]`, `depthMemory[]`, `depthView[]`).
9. Evaluate whether `BufferComponents`, `RenderComponents`, `SynchronizationComponents`, and `CommandComponents` are still needed or can be collapsed.

#### `vulkanMaster.c`

This file has the heaviest usage. The `static VulkanComponents components` global and the `RendererState rendererState` global are both declared here.

Key function-by-function changes:

| Function | Lines | What changes |
|----------|-------|-------------|
| `recordCommandBuffer` | 123 | `components.cmdComp.commandBuffer[...]` → `rendererState.frames[...].commandBuffer` |
| `drawFrame` | 437 | ~25 references to `components.syncComp.*` → `rendererState.frames[fi].*` and `rendererState.frameIndex` / `rendererState.framebufferResized` |
| `updateTransformBuffer` | 326 | `components->renderComp.buffers.uniformMapped` → `state->frames[frameIndex].uniformMapped` |
| `updateCullingBuffers` | 338 | Same pattern for `uniformMapped` access |
| `createCullingBuffers` | 624 | No per-frame sync changes, but descriptor set writes need `state->frames[i].cullSet` |
| `initVulkan` | 754 | Rewrite init calls to populate `rendererState.frames[]` instead of `components.syncComp` / `components.cmdComp` / `components.renderComp.buffers` |
| `flush_deletion_queue` | 83 | `state->deletionQueues[frameIndex]` → `state->frames[frameIndex].deletionQueue` |

#### `instanceInit.c`

| Function | Lines | What changes |
|----------|-------|-------------|
| `createSyncObjects` | 1562 | Write to `rendererState.frames[i].imageAvailable/renderFinished/frameFence` instead of `components->syncComp.*` |
| `createCommandBuffer` | 1542 | Write to `rendererState.frames[i].commandBuffer` instead of `components->cmdComp.commandBuffer[i]` |
| `createUniformBuffers` | 1029 | Write to `rendererState.frames[i].uniformBuffer/uniformAlloc/uniformMapped` |
| `createDepthResources` | 1154 | Write to `rendererState.frames[i].depthImage/depthAlloc/depthView` |
| `updateUniformBuffer` | 1062 | Read `components->renderComp.uniform`, memcpy to `rendererState.frames[fi].uniformMapped` |
| `createDescriptorSets` | 1262 | Write to `rendererState.frames[i].globalSet` and `rendererState.frames[i].cullSet` |
| `updateUboDescriptorSets` | 1304 | Read from `rendererState.frames[i].uniformBuffer` instead of `components->renderComp.buffers.uniform[i]` |
| `cleanupSwapChain` | 833 | Iterate `rendererState.frames[]` for depth image/view destruction |
| `cleanupVulkan` | 1634 | Iterate `rendererState.frames[]` for sync, command, uniform, depth, descriptor cleanup |
| `recreateSwapChain` | 884 | May reference per-frame depth/sync objects |

#### `instanceInit.h`

Update function signatures. Several functions that currently take only `VulkanComponents*` will need `RendererState*` too (or the per-frame struct itself):
- `createSyncObjects`, `createCommandBuffer`, `createUniformBuffers`, `createDepthResources` — all currently write into `VulkanComponents` fields that are moving.

**Strategy**: These functions should take `RendererState*` as an additional parameter, since that's where the per-frame data is migrating to. Alternatively, they can take `PerFrameResources*` directly if they only need to populate a single frame's resources.

#### Other files

- `pipeline.c` / `pipeline.h`: May reference `state->globalSets` or `state->cullSets` — update to `state->frames[i].globalSet` / `.cullSet`.
- `ano_GltfParser.c`: References `components->renderComp.buffers.entities` — unchanged in this phase (entity storage stays in `VulkanComponents` until Phase 4).
- `texture.c`: No per-frame array access — unaffected.

### 3.5 Intermediate step: dual-write

To de-risk Phase 3, it can be split into sub-phases:

1. **3a**: Add `PerFrameResources` to `RendererState` and have init functions write to **both** old and new locations. Verify build.
2. **3b**: Switch all read sites to the new locations, one function at a time (start with `drawFrame` since it's the hot path). Verify each.
3. **3c**: Remove the old fields from `VulkanComponents` once no readers remain. Remove the dual-writes from init. Verify build.

### 3.6 Phase 3 validation

```bash
cmake --build build 2>&1 | grep -c "error:"
# Expected: 0
```

Full manual test: launch engine, load scene, verify rendering, resize window (exercises `recreateSwapChain` → depth recreation + sync reset), close cleanly.

---

## Phase 4 — Complete the Migration

> **Risk**: Medium-high. Collapses `VulkanComponents` down to a minimal "Vulkan context" struct holding only instance/device/surface/queues, and moves all remaining live data into `RendererState`.

### 4.1 Goal state

After Phase 4, the two-struct system becomes:

```c
// Immutable after init — Vulkan plumbing
typedef struct VulkanContext
{
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    bool                     enableValidationLayers;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         physicalDevice;
    DeviceCapabilities       deviceCapabilities;
    QueueFamilyIndices       queueFamilyIndices;
    VkSampleCountFlagBits    msaaSamples;
    VkDevice                 device;
    VkQueue                  graphicsQueue;
    VkQueue                  computeQueue;
    VkQueue                  transferQueue;
    VkQueue                  presentQueue;
} VulkanContext;

// Mutable, frame-varying — everything that changes at runtime
typedef struct RendererState
{
    PerFrameResources       frames[MAX_FRAMES_IN_FLIGHT];
    uint32_t                frameIndex;
    bool                    framebufferResized;

    // Swapchain
    VkSwapchainKHR          swapChain;
    VkFormat                imageFormat;
    VkExtent2D              imageExtent;
    uint32_t                imageCount;
    VkImage*                images;
    VkImage                 colorImage;
    GpuAllocation           colorImageAlloc;  // fixes the VkDeviceMemory issue
    VkImageView             colorView;
    uint32_t                viewCount;
    VkImageView*            views;

    // Command pool
    VkCommandPool           commandPool;

    // Render data
    GlobalUBO               uboData;
    VkSampler               textureSampler;
    RenderEntity*           entities;
    uint32_t                entityCount;

    // Pipeline system
    PipelinePrototype       prototypes[PIPELINE_TYPE_COUNT];

    // Descriptor infrastructure
    VkDescriptorPool        globalDescriptorPool;
    VkDescriptorSetLayout   globalSetLayout;

    // Geometry
    GeometryPool            globalGeometryPool;
    RenderPrimitives        primitives;

    // GPU buffers
    TransformBuffer         transformBuffer;
    MaterialBuffer          materialBuffer;
    IndirectDrawBuffer      indirectBuffer;
    CullingBuffers          culling;
    BindlessTextureArray    bindlessTextures;

    // Fallback
    VkImage                 fallbackImage;
    VkImageView             fallbackImageView;
} RendererState;
```

### 4.2 What moves

| Data | From | To |
|------|------|----|
| Entity list (`entities`, `entityCount`) | `VulkanComponents.renderComp.buffers` | `RendererState` |
| `GlobalUBO uniform` (CPU-side UBO data) | `VulkanComponents.renderComp.uniform` | `RendererState.uboData` |
| `VkSampler textureSampler` | `VulkanComponents.renderComp.textureSampler` | `RendererState` |
| `VkCommandPool commandPool` | `VulkanComponents.cmdComp.commandPool` | `RendererState` |
| Swapchain group | `VulkanComponents.swapChainComp.swapChainGroup` | `RendererState` (flattened) |
| Image view group | `VulkanComponents.swapChainComp.viewGroup` | `RendererState` (flattened) |
| Depth format | `VulkanComponents.renderComp.buffers.depthFormat` | `RendererState` |
| Physical device names | `VulkanComponents.physicalDeviceComp.availableDevices` | `VulkanContext` or dropped (only used during init) |

### 4.3 Function signature changes

Every function currently taking `VulkanComponents*` changes to take `VulkanContext*` (for device/queue access) and/or `RendererState*` (for mutable state). The mapping:

| Current signature | New signature | Rationale |
|---|---|---|
| `createInstance(VulkanComponents*)` | `createInstance(VulkanContext*)` | Only writes instance/debug |
| `pickPhysicalDevice(VulkanComponents*, ...)` | `pickPhysicalDevice(VulkanContext*, ...)` | Only writes physical device info |
| `createLogicalDevice(VulkanComponents*, ...)` | `createLogicalDevice(VulkanContext*, ...)` | Only writes device/queues |
| `initSwapChain(VulkanComponents*, ...)` | `initSwapChain(VulkanContext*, RendererState*, ...)` | Reads device, writes swapchain state |
| `createImageViews(VulkanComponents*)` | `createImageViews(VulkanContext*, RendererState*)` | Reads device, writes views |
| `createCommandPool(VulkanComponents*)` | `createCommandPool(VulkanContext*, RendererState*)` | Reads device/queue, writes pool |
| `createDepthResources(VulkanComponents*)` | `createDepthResources(VulkanContext*, RendererState*)` | Reads device, writes per-frame depth |
| `createColorResources(VulkanComponents*)` | `createColorResources(VulkanContext*, RendererState*)` | Reads device, writes color image |
| `createUniformBuffers(VulkanComponents*)` | `createUniformBuffers(VulkanContext*, RendererState*)` | Reads device, writes per-frame UBOs |
| `createCommandBuffer(VulkanComponents*)` | `createCommandBuffer(VulkanContext*, RendererState*)` | Reads pool, writes per-frame CBs |
| `createSyncObjects(VulkanComponents*)` | `createSyncObjects(VulkanContext*, RendererState*)` | Reads device, writes per-frame sync |
| `cleanupSwapChain(VulkanComponents*, ...)` | `cleanupSwapChain(VulkanContext*, RendererState*, ...)` | Reads device, destroys swapchain resources |
| `recreateSwapChain(VulkanComponents*, ...)` | `recreateSwapChain(VulkanContext*, RendererState*, ...)` | Full swapchain rebuild |
| `cleanupVulkan(VulkanComponents*)` | `cleanupVulkan(VulkanContext*, RendererState*)` | Reads device, destroys everything |
| `updateUniformBuffer(VulkanComponents*)` | `updateUniformBuffer(RendererState*)` | Only needs UBO data + mapped buffer |
| `updateMeshTransforms(VulkanComponents*, ...)` | `updateMeshTransforms(RendererState*, ...)` | Only needs entity list |
| `createImage(VulkanComponents*, ...)` | `createImage(VkDevice, ...)` or `createImage(VulkanContext*, ...)` | Only needs device for Vulkan calls |
| `createTextureSampler(VulkanComponents*)` | `createTextureSampler(VulkanContext*, RendererState*)` | Reads device, writes sampler |
| `beginSingleTimeCommands(VulkanComponents*)` | `beginSingleTimeCommands(VkDevice, VkCommandPool)` | Only needs these two handles |
| `endSingleTimeCommands(VulkanComponents*, ...)` | `endSingleTimeCommands(VkDevice, VkQueue, VkCommandPool, ...)` | Only needs these handles |
| `parseGltf(VulkanComponents*, ...)` | `parseGltf(VulkanContext*, RendererState*, ...)` | Needs device for uploads, RendererState for entity list and textures |
| `flush_deletion_queue(VulkanComponents*, RendererState*, ...)` | `flush_deletion_queue(VulkanContext*, RendererState*, ...)` | Replace VulkanComponents with context |

### 4.4 Files affected

**Every file in the "Files in scope" table** will be touched. This is the largest phase.

Priority order for conversion:
1. `structs.h` — define `VulkanContext`, update `RendererState`
2. `vulkanMaster.h` — update externs and public API
3. `vulkanMaster.c` — replace `static VulkanComponents components` with `static VulkanContext ctx`
4. `instanceInit.h` — update all prototypes
5. `instanceInit.c` — update all implementations
6. `pipeline.h` / `pipeline.c` — update signatures
7. `texture.h` / `texture.c` — update signatures
8. `ano_GltfParser.h` / `ano_GltfParser.c` — update `parseGltf` signature
9. `components.h` / `components.c` — no changes expected (operates on `RenderPrimitives*`)
10. `geometry.h` / `geometry.c` — may need `VulkanContext*` for `geometry_pool_upload`

### 4.5 Sub-structs to remove

After Phase 4, these become empty shells and should be deleted:

- `InstanceDebugComponents` — fields absorbed into `VulkanContext`
- `PhysicalDeviceComponents` — fields absorbed into `VulkanContext` (except `availableDevices` which is init-only)
- `DeviceQueueComponents` — fields absorbed into `VulkanContext`
- `SwapChainComponents` — fields absorbed into `RendererState`
- `SwapChainGroup` — fields absorbed into `RendererState`
- `ImageViewGroup` — fields absorbed into `RendererState`
- `RenderComponents` — fields absorbed into `RendererState`
- `BufferComponents` — per-frame fields absorbed into `PerFrameResources`, non-per-frame absorbed into `RendererState`
- `SynchronizationComponents` — fully absorbed in Phase 3
- `CommandComponents` — `commandPool` moved to `RendererState`, `commandBuffer[]` absorbed in Phase 3
- `VulkanComponents` — entirely replaced by `VulkanContext`

### 4.6 The `VulkanGarbage` struct

`VulkanGarbage` (structs.h:217–222) holds `VulkanComponents*`, `GLFWwindow*`, `Monitors*`. After this phase it should hold `VulkanContext*` + `RendererState*` + window/monitors, or be refactored into the cleanup API directly.

### 4.7 Phase 4 validation

Full rebuild. Manual test with scene load, window resize, clean shutdown. Verify no leaks with Vulkan validation layers enabled:

```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/anoptic_engine 2>&1 | grep -i "error\|leak\|warning"
```

---

## Phase 5 — Fix `createImage()` Signature

> **Risk**: Medium. Changes a function signature used in image creation paths (depth, color, textures). The current interface silently loses sub-allocation offset information.

### 5.1 The problem

`createImage()` in `texture.c:160–195` calls `gpu_alloc()` internally but only exposes the result through a `VkDeviceMemory* imageMemory` out-parameter:

```c
GpuAllocation alloc = gpu_alloc(allocator, memRequirements, properties);
*imageMemory = alloc.memory;  // offset is LOST
```

The `alloc.offset` is correctly used in `vkBindImageMemory` inside `createImage`, so rendering works. But the **caller** now holds a `VkDeviceMemory` handle that is actually a shared arena block. Calling `vkFreeMemory()` on it would destroy all sub-allocations. The fields storing these handles (`depthMemory[]`, `colorImageMemory`, `textureImageMemory`) are misleading.

### 5.2 The fix

Change the out-parameter from `VkDeviceMemory*` to `GpuAllocation*`:

```diff
  // texture.h
- bool createImage(VulkanComponents* components, GpuAllocator* allocator, uint32_t width, uint32_t height,
-     uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
-     VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
-     VkImage* image, VkDeviceMemory* imageMemory, bool flag16);
+ bool createImage(VulkanComponents* components, GpuAllocator* allocator, uint32_t width, uint32_t height,
+     uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
+     VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
+     VkImage* image, GpuAllocation* imageAlloc, bool flag16);
```

> [!NOTE]
> If Phase 4 has already landed, the first parameter changes from `VulkanComponents*` to `VulkanContext*` (or `VkDevice`). The `GpuAllocation*` fix is independent of that change.

### 5.3 Internal implementation change

```diff
  // texture.c createImage() body
  GpuAllocation alloc = gpu_alloc(allocator, memRequirements, properties);
- *imageMemory = alloc.memory;
+ *imageAlloc = alloc;

  vkBindImageMemory(..., alloc.memory, alloc.offset);
```

### 5.4 Callers to update

| Caller | File | What changes |
|--------|------|-------------|
| `createDepthResources` | instanceInit.c:1154 | Pass `&frame->depthAlloc` instead of `&frame->depthMemory` (after Phase 3) |
| `createColorResources` | instanceInit.c:1189 | Pass `&state->colorImageAlloc` instead of `&components->swapChainComp.swapChainGroup.colorImageMemory` (after Phase 4) |
| `createTextureImage` | texture.c:383 | Pass `GpuAllocation*` — `TextureData` struct in `components.h:55` changes `VkDeviceMemory textureImageMemory` to `GpuAllocation textureImageAlloc` |
| `createTextureImageFromPixels` | texture.c | Same pattern |

### 5.5 Struct field changes

| Struct | Field change |
|--------|-------------|
| `PerFrameResources` (Phase 3) | `depthMemory` → `depthAlloc` (type `GpuAllocation`) |
| `RendererState` (Phase 4) | `colorImageMemory` → `colorImageAlloc` (type `GpuAllocation`) |
| `TextureData` (components.h:55) | `VkDeviceMemory textureImageMemory` → `GpuAllocation textureImageAlloc` |

### 5.6 Cleanup changes

Currently, cleanup code avoids calling `vkFreeMemory()` on these fields (correctly, since they're arena sub-allocations). After this change, cleanup code should continue to **not** free them individually — the arena handles bulk deallocation via `gpu_alloc_destroy()` / `gpu_alloc_reset()`. The `GpuAllocation` fields simply make this explicit and safe.

Verify that `cleanupSwapChain` (instanceInit.c:870–873) no longer sets `colorImageMemory = VK_NULL_HANDLE` and instead relies on the arena reset at L881.

### 5.7 Phase 5 validation

```bash
cmake --build build 2>&1 | grep -c "error:"
# Expected: 0
```

Validate with Vulkan validation layers. Pay special attention to:
- Depth image creation/destruction across swapchain recreates
- Texture loading (glTF parse path)
- Clean shutdown (no validation errors about leaked memory)

---

## Phase Summary

| Phase | Risk | Scope | Outcome |
|-------|------|-------|---------|
| 1 | Zero | `structs.h` only | 7 dead fields removed, cleaner struct definitions |
| 2 | Low | `structs.h` + 2 `.c` files | 12 loose culling arrays wrapped in `CullingBuffers` |
| 3 | Medium | 4 headers + 2 `.c` files | 30+ per-frame arrays consolidated into `PerFrameResources` |
| 4 | Medium-high | All 20+ files in scope | `VulkanComponents` replaced by minimal `VulkanContext`, clean ownership |
| 5 | Medium | `texture.c/h`, `instanceInit.c`, `components.h` | `createImage()` returns `GpuAllocation`, no more lost offsets |

### Dependencies

```
Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4
                                         │
                              Phase 5 ◄──┘
```

Phase 5 depends on Phase 4 only for the `colorImageMemory` field migration. It can technically be done after Phase 3 if `createImage`'s first parameter is left as `VulkanComponents*` temporarily.

### Staging buffer leak (bonus)

The acknowledged leak in `geometry.c:194–196` (arena doesn't support individual free) is a separate concern from this refactor. It should be addressed by either:
- Adding a `gpu_free()` function to the allocator (turning it into a proper sub-allocator), or
- Using a dedicated short-lived allocator for staging that gets reset after each upload batch

This is out of scope for this document but worth tracking.
