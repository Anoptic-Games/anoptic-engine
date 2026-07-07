# Renderer refactor plan

Structural breakdown of the renderer's monolithic files into per-domain modules under
src/vulkan_backend/, following the existing instance/ / texture/ / vertex/ convention:
cohesive multi-file domains get a subfolder with a private header, single-concern files sit at
the module root (like render_slots.c, gpu_alloc.c). Individual files target under 1000 lines.
vulkanMaster.c remains the top of the cascade: lifecycle (initVulkan / unInitVulkan) and the
frame loop (drawFrame), calling into the split modules.

Every phase is pure code motion: no behavior change, no signature redesign, no renaming of
functions, comments move with their code. Review moves with `git diff --color-moved=dimmed-zebra`.

## Current inventory

Files over (or near) the 1000-line target, with the heavy functions measured:

| file | lines | dominant contents |
|---|---|---|
| vulkanMaster.c | 4847 | recordCommandBuffer 1127, initVulkan 452, drawFrame 283, render_apply_commands 276, createShadowResources 171, updateCullingBuffers 135, createCullingBuffers 135, + ~15 more subsystems |
| instance/instanceInit.c | 3383 | updateUboDescriptorSets 321, cleanupVulkan 245, createDescriptorSets 200, createLogicalDevice 188, cleanupSwapChain 138, pickPhysicalDevice 133 |
| instance/pipeline.c | 1643 | ano_vk_init_pipelines 523, ano_vk_init_shadow 260, cleanup_pipelines 190, cull layout 169, global layout 153, tonemap 131 |
| structs.h | 1359 | all backend types: context/device, buffers, lights, shadows, cull, ViewResources/PerFrameResources/RendererState |
| text_raster.c | 1088 | text GPU lane; cohesive, only marginally over |

Everything else is under 1000 and untouched (components.c/h, geometry.c, render_slots.c,
gpu_alloc.c, vertex/, texture/, instance/pipelines/{flat,transmission,additive}.c,
src/render_bridge/, src/render/gltf/).

vulkanMaster.c region map (coordinates for the mover):

- 1-100: globals (static ctx, rendererState, 4 allocators, PFNs, vulkanGarbage, window/monitors
  statics), capacity #defines, profiling statics + ano_ts + allocator_used_bytes
- 102-200: unInitVulkan, anoShouldClose, deferred_delete_resource, flush_deletion_queue
- 204-362: g_framePasses table, lightTypeShadowMapped
- 364-484: record_hiz_pyramid_chain, recordHiZCompute, recordLightcullCompute
- 486-1612: recordCommandBuffer — uploads flush 524-556, shared compute loop 560-701, prelude
  split 710-716, swapchain barrier 720-738, shadow region 740-1069, tpsort 1073-1099, per-view
  loop 1101-1352 (geometry passes + picking 1304-1333 + HDR-to-read), text record 1358,
  composite 1360-1423, Hi-Z tail + depth restore 1425-1577, present barrier 1581-1606
- 1616-1648: printUniformTransferState, updateTransformBuffer (deprecated stub)
- 1650-1784: updateCullingBuffers (CullUBO fill, snapshot publish, mesh SSBO refresh)
- 1786-2212: stage_command_fields, growBufferSet, SlotUpload suite, ensureEntityCapacity,
  stage_stream_frame, free_owned_bulk
- 2214-2429: LightRegistry + LightData param helpers
- 2431-2777: shadow cache machinery — runtime frustum pools, layer invalidation, swept-bound
  movers, caster volumes, shadow_track_motion, shadow_caster_attach/detach, cascade_detach_lights
- 2779-3054: render_apply_commands + retirement/collect/compact + stream staging
- 3056-3294: public contract impls — anoRenderBridge, asset registry (g_assets, anoRenderAsset*),
  anoRenderTextBake, stream begin/commit, bulk submit packers, runtime knob setters/getters
- 3296-3395: ano_print_profiling, ano_collect_frame_stats, ano_collect_pick
- 3397-3679: drawFrame (fence/collect/reclaim, acquire, updates, apply, record, three submit
  paths, present, frame advance)
- 3681-4393: create* buffer family (material/light/motion/instance/stream/transform/lightRuntime/
  indirect/cluster/shadow/culling/fallback), register_static_shadow, createMappedSsboSet
- 4395-4847: initVulkan

## Shared state

Almost everything hangs off two globals, which makes the split mechanical:

- rendererState: already extern in vulkanMaster.h. New TUs just include it.
- ctx (VulkanContext): file-static in vulkanMaster.c. Promote to a plain global with
  `extern VulkanContext ctx;` in a new private header `vulkan_backend/backend.h`. instanceInit.c
  functions take a VulkanContext* parameter named ctx — the parameter legally shadows the global
  and those TUs need not include backend.h, so no churn there. Do not rename; note the shadowing
  in the header comment.
- gpuAllocator / stagingAllocator / swapchainAllocator / textureAllocator: already extern in
  gpu_alloc.h. Nothing to do.
- pfnVkCmdDrawMeshTasks*: already extern in vulkanMaster.h.
- window, monitors: stay static in vulkanMaster.c — only initVulkan/drawFrame/anoShouldClose use
  them, and all three stay there.
- Profiling statics (g_tsAccumMs, g_tsFrames, g_shadowRenderAccum/Frames) move with the profiling
  code into one TU and stay static. ano_ts becomes a small extern function there (a handful of
  calls per frame; inlining is irrelevant).
- g_framePasses becomes `const RenderPassDef ano_frame_passes[]` + count in frame/passes.c,
  declared in frame/frame.h (two consumers after the split). The table stays ONE array — pass
  order encodes the prepass/EQUAL depth contract.
- g_assets/g_assetCount/g_defaultMaterial move (static) into the file that owns the asset API.

backend.h contents: extern ctx, nothing else. Module-private headers otherwise: frame/frame.h,
shadow/shadow.h, bridge/bridge.h. instance/instanceInit.h stays the umbrella for the instance/
split so no caller includes change.

## Target layout

```
src/vulkan_backend/
  vulkanMaster.c        ~720  lifecycle + frame loop (see below)
  backend.h              new  extern VulkanContext ctx
  slot_upload.c         ~300  SlotUpload suite, growBufferSet, ensureEntityCapacity
  scene_buffers.c       ~560  create{Material,Motion,InstanceData,Stream,Transform,LightRuntime,
                              Indirect,Cluster,Culling,Light}Buffer, createMappedSsboSet,
                              createFallbackResources, ano_vk_create_scene_resources() aggregate
  light_registry.c      ~240  LightRegistry + light_data_from_params/light_apply_fields/light_set_dir
  render_api.c          ~200  asset registry + anoRenderAsset*, anoRenderTextBake, runtime knob
                              setters/getters (lighting mode, cull/LOD thresholds, lod bias,
                              shadow lod bias, hiz enable), glTF load block hoisted from initVulkan
  frame/
    frame.h              new  module entry points + ano_frame_passes decl
    passes.c            ~160  the pass table
    record.c            ~380  recordCommandBuffer orchestrator: begin/timestamps, upload flush,
                              shared compute loop, prelude split, swapchain + present barriers,
                              tpsort dispatch; calls shadow/views/hiz record functions
    record_views.c      ~360  per-view loop: geometry passes, picking readback, HDR transitions,
                              composite/tonemap + PiP + text hooks
    hiz.c               ~300  record_hiz_pyramid_chain, recordHiZCompute, recordLightcullCompute,
                              in-frame Hi-Z tail + depth restore block
    submit.c            ~220  ano_frame_submit(imageIndex, ordinal): the three submit paths
                              (plain / split prelude+main / async lc+hiz compute submits)
    update.c            ~260  updateUniformBuffer (from instanceInit.c), updateCullingBuffers,
                              updateTransformBuffer stub, snapshot publish
    profiling.c         ~170  ts statics, ano_ts, allocator_used_bytes, ano_collect_frame_stats,
                              ano_collect_pick, ano_print_profiling + OSD
  shadow/
    shadow.h             new
    shadow_record.c     ~360  the shadow region of recordCommandBuffer: classify + budget,
                              per-frustum depth render, blur phases, phase barriers,
                              lightTypeShadowMapped
    shadow_cache.c      ~430  swept-bound movers, caster volumes, exposure rebuilds,
                              shadow_track_motion, layer invalidation, mover_refresh_slot,
                              shadow_volumes_reparent
    shadow_casters.c    ~260  runtime frustum pools, shadow_caster_attach/detach,
                              cascade_detach_lights, register_static_shadow
    shadow_resources.c  ~190  createShadowResources (atlas/temp/transient depth/config uploads,
                              cache-mode env gates, pool seeding)
  bridge/
    bridge.h             new  (render-side endpoints; transport rings stay in src/render_bridge/)
    apply.c             ~420  render_apply_commands, stage_command_fields, free_owned_bulk,
                              stage_stream_frame, retirement collect + compact + light count publish
    producer.c          ~140  anoRenderBridge, ano_render_stream_begin/commit,
                              ano_render_submit_bulk_update/destroy (logic-thread entry points —
                              the file boundary documents the threading boundary)
  instance/
    instanceInit.h       keep as umbrella (declarations unchanged)
    window.c            ~200  initWindow, monitor enumeration/cleanup, GLFW input callbacks,
                              forward_input, framebufferResizeCallback
    instance.c          ~250  createInstance, validation layer check, debug messenger set,
                              getRequiredExtensions, createSurface, g_ValidationErrors def
    device.c            ~700  findQueueFamilies, populateCapabilities, checkDeviceExtensionSupport,
                              isDeviceSuitable, getMaxUsableSampleCount, deviceHasMeshShader,
                              name-match helpers, maxDeviceLocalHeapSize, pickPhysicalDevice,
                              createLogicalDevice
    swapchain.c         ~430  querySwapChainSupport, choose* helpers, initSwapChain,
                              cleanupSwapChain, recreateSwapChain, createImageView(s)
    attachments.c       ~260  findSupportedFormat/findDepthFormat/hasStencilComponent,
                              createColorResources, createDepthResources, createHiZResources
    descriptors.c       ~850  createDescriptorPool, createBindlessTextureArray,
                              createDescriptorSets, update{Ubo,Tonemap,HiZ,Cluster,Shadow}DescriptorSets
    commands.c          ~330  createCommandPool, createDataBuffer, createUniformBuffers,
                              findMemoryType, stagingTransfer, begin/endSingleTimeCommands,
                              copyBuffer, createCommandBuffer, createSyncObjects
    cleanup.c           ~270  cleanupVulkan
    pipeline.c          ~450  loadFile/openEngineFile/createShaderModule, ano_pipeline_task_stage,
                              ano_vk_init_pipelines slimmed to orchestration + the graphics
                              prototype walk, ano_vk_cleanup_pipelines
    layouts.c           ~410  ano_vk_init_global_layout, ano_vk_init_cull_layout,
                              ano_vk_init_material_layouts
    pipelines/
      compute.c         ~360  the compute pipeline creation block out of ano_vk_init_pipelines
                              (update/scatter/lightsetup/shadowsetup/cull/lightcull/tpsort/hiz)
      tonemap.c         ~140  ano_vk_init_tonemap
      shadow_pipe.c     ~280  ano_vk_init_shadow (depth-only + masked shadow pipelines, blur
                              pipeline, compare sampler)
```

vulkanMaster.c after: globals + capacity defines (~100), unInitVulkan + deletion queue +
anoShouldClose (~110), drawFrame slimmed to fence/collect/reclaim, acquire, update calls, apply,
text refresh, record call, ano_frame_submit call, present + error handling, frame advance
(~150), initVulkan slimmed by hoisting the scene-buffer block into
ano_vk_create_scene_resources() and the glTF asset block into render_api.c (~360). Total ~720.

recordCommandBuffer decomposes into functions, not files-per-line: record.c keeps the skeleton
and calls, in order:

```
ano_record_uploads(cmd)                          // slot-upload flush + barriers
ano_record_shared_compute(cmd, entityCount)      // pass-table compute loop + fills + Hi-Z read barriers
   [prelude CB ends here under asyncLc]
ano_record_swapchain_acquire_barrier(cmd, imageIndex)
ano_shadow_record(cmd, entityCount, drawSlotCount)          // shadow/shadow_record.c
ano_record_tpsort(cmd, entityCount)
ano_record_views(cmd, entityCount, drawSlotCount)           // frame/record_views.c
ano_vk_text_record(...)                                     // existing
ano_record_composite(cmd, imageIndex)                       // frame/record_views.c
ano_record_hiz_tail(cmd)                                    // frame/hiz.c
ano_record_present_barrier(cmd, imageIndex)
```

All read rendererState/ctx globals like today; parameters stay minimal (cmd + the two counts
already computed in the skeleton). ano_ts stamps remain in the skeleton at the same five
boundaries, unconditional as today.

## structs.h split (phase 5)

structs.h stays the umbrella every current include site keeps using; domain types move into
headers it includes at the top. RendererState/PerFrameResources/ViewResources stay in structs.h
(they aggregate all domains). Moves:

- shadow/shadow_types.h: ANO_SHADOW_* defines, ShadowFrustumConfig, ShadowLightInfo,
  ShadowResources, ShadowCasterVolume, MoverBound
- light_types.h (root): LightType, LightData, LightRegistry, LightRowQuarantine, LightBuffer,
  light-row state enum
- buffer_types.h (root): TransformBuffer, MotionBuffer, InstanceDataBuffer,
  TransformStreamBuffer, SlotUpload, MaterialBuffer, CullView, CullUBO, CullingBuffers,
  CullUboBuffer, IndirectDrawBuffer, DeletionQueue types
- structs.h keeps: context/device/queue-family/swapchain/window/monitor types, MaterialData,
  cluster + view + Hi-Z + text defines, the frame/view/renderer aggregates

Result: structs.h ~600, each domain header under 300. Pure motion; no type changes.

## Phases

Each phase compiles and runs identically before moving on. Gate: ./build.sh 1 and 2 clean; debug
run on the desktop demo — validation counter 0, frame stats within session noise, one visual
sanity screenshot. One commit per phase (drafted by hand after manual testing, per project
convention).

- Phase 0 — plumbing. Add backend.h (extern ctx), drop static from ctx. Add empty frame/,
  shadow/, bridge/ dirs + headers. CMake untouched beyond nothing (no new TUs yet). Smallest
  possible diff to validate the linkage story.
- Phase 1 — vulkanMaster.c leaf subsystems (no record-path changes yet):
  slot_upload.c, light_registry.c, shadow/shadow_cache.c, shadow/shadow_casters.c,
  bridge/apply.c, bridge/producer.c, render_api.c, frame/profiling.c, frame/update.c
  (updateUniformBuffer moves here from instanceInit.c in the same motion), scene_buffers.c,
  shadow/shadow_resources.c. vulkanMaster.c drops to ~2400.
- Phase 2 — the record path: frame/passes.c, frame/hiz.c, shadow/shadow_record.c,
  frame/record_views.c, frame/record.c, frame/submit.c. vulkanMaster.c lands at ~720. This is
  the riskiest phase (barrier code motion) — keep each extraction its own build-and-run step.
- Phase 3 — instanceInit.c split into window/instance/device/swapchain/attachments/descriptors/
  commands/cleanup. instanceInit.h unchanged. instanceInit.c is deleted when empty.
- Phase 4 — pipeline.c split: layouts.c, pipelines/compute.c, pipelines/tonemap.c,
  pipelines/shadow_pipe.c; pipeline.c keeps utils + orchestration + cleanup.
- Phase 5 — structs.h domain headers (above).
- Phase 6 (optional) — text_raster.c: split the CPU compose side (ano_vk_text_set/set_runs,
  block set/clear, frame_refresh, pending_bounds, blocks_append, demo/OSD statics) into
  text_state.c (~200), leaving the GPU lane ~900. Only if it bothers anyone; the file is cohesive.

CMake: src/vulkan_backend/CMakeLists.txt target_sources gains each new TU in its phase;
src/render/ and src/render_bridge/ untouched.

## Order-sensitive invariants (must survive the motion verbatim)

- initVulkan sequence: async gates (asyncHiz -> asyncLc -> taskCull -> textOverlay -> asyncText)
  are decided BEFORE buffer creation (CONCURRENT sharing modes read them), before descriptor
  layouts/pipelines (task stage joins layouts + push ranges), before depth/Hi-Z resources (rest
  layouts differ async vs in-frame), and before createSyncObjects (timelines). ano_vk_init_shadow
  runs after ano_vk_init_pipelines (reuses the flat layout). Descriptor updaters run after
  createDescriptorSets; createCommandBuffer and createSyncObjects last.
- recordCommandBuffer order and every barrier stays byte-identical: uploads -> shared compute
  (fill+barrier structure per pass type) -> prelude end -> swapchain barrier -> shadow region
  (four phase barriers, two partitions per frustum inside one rendering instance) -> tpsort ->
  per-view (depthBarrierBefore passes, resolve-only-in-additive, picking on view 0) -> text ->
  composite -> Hi-Z tail (avenue-1 vs fallback, async vs in-frame restore) -> present barrier.
  ano_ts boundaries stay top-level and unconditional.
- drawFrame order: fence wait -> collect stats -> collect pick -> stream reclaim -> deletion
  queue flush -> acquire -> updateUniformBuffer -> updateCullingBuffers ->
  render_apply_commands -> text frame refresh -> CB resets -> record -> async CB records ->
  submits in exact order (graphics [or prelude+main], then lc compute, then hiz compute) ->
  present -> frameSubmitted/frameIndex/globalFrame advance. updateCullingBuffers must precede
  the command drain (light-count publish is deliberately one frame behind).
- render_apply_commands internal order: drain -> retired-slot collect/emit -> slots compact ->
  light registry collect -> compact -> publish light count -> stage_stream_frame.
- Push-constant stage masks at every vkCmdPushConstants call site must continue to match the
  layouts' ranges exactly (geometry | FRAGMENT | optional TASK).
- The g_framePasses array order is the depth/EQUAL contract (4a prepass CLEAR -> 4b two-sided
  LOAD -> 4 opaque EQUAL -> 4c two-sided opaque -> 4d masked LESS+write -> 5 transmission ->
  6 additive, resolve only in the last).

## Noticed while surveying, explicitly out of scope

Flagged for a later cleanup pass, NOT part of this refactor (which is motion-only):

- updateMeshTransforms (instanceInit.c:1695) and printUniformTransferState / printMatrix have
  only commented-out callers.
- updateTransformBuffer is a deprecated one-liner still called each frame; could inline into
  drawFrame.
- RenderEntity (structs.h:194) is legacy; last real consumer is updateMeshTransforms.
- requiredExtensions static in instanceInit.c carries a "should not be here" comment.
- vulkanMaster.h includes structs.h twice.
- The known clean-exit segfault (cleanupVulkan freeing uninitialized device slots,
  instanceInit.c:3332) is pre-existing and deferred; the cleanup.c motion is a natural moment to
  fix it, but that is a behavior change and needs its own sign-off.
