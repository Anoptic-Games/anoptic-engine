# TODO

## P0 — macOS: GLFW must run on the main thread — DONE

The renderer ran on a spawned thread (commit 21c5f1d) that owned GLFW. On macOS this aborted in
Cocoa during window creation, before Vulkan device selection: AppKit forbids window and menu-bar
setup off the main thread. Worse, even past that abort the surface (CAMetalLayer) was created on the
render thread, so it was never wired to the visible NSView — the window showed no draw surface at all.

Fixed by adopting client-engine `9edaea8` ("render lives on main thread, everything else gets child
threads"): the whole render world — `initVulkan`, `glfwPollEvents`, `drawFrame`, `unInitVulkan`, and
crucially `createSurface` — now runs on the main thread (`main()`), so the CAMetalLayer attaches on
the thread that owns the window. The logic/ECS master (`anoLogicThreadMain`) is spun to a child thread
as the sole bridge producer. `initVulkan` runs synchronously before the producer starts, so the old
readiness handshake (`g_renderPhase`/`anoRenderIsReady`) and the `anoRenderThreadMain` entry point are
gone. Not `#ifdef`-gated: render-on-main is correct on every platform, mandatory only on macOS.

This supersedes the earlier window-ownership-split attempt (`anoRenderCreateWindow`/`PollWindow`/
`DestroyWindow`, idempotent window creation, `_Atomic framebufferResized`) — those were reverted.

Kept on top of `9edaea8`: the bindless sampler clamp (pipeline.c, below).

Verified on Apple M1 / MoltenVK with `ANO_FORCE_NO_MESH_SHADER=1` and validation forced on: the run
clears the Cocoa abort, completes `initVulkan` (device selected, both glTF assets parsed, textures
uploaded, "Instance creation complete!"), zero validation errors, and the main-thread render loop
sustains ~20-44% CPU in `drawFrame`. Visible-pixels confirmation by the user is the remaining gate.
Note: run from build/<cfg>/ — asset paths are CWD-relative and `build.sh` copies assets into the build dir.

## macOS bring-up verification (P0 unblocked)

- DONE. Device selection picks the MoltenVK GPU and takes the vertex fallback: the run logs
  `DeviceCount: 1`, `Enabling 3 device extensions (mesh shader: no)`.
- H2: no `firstInstance` validation error was observed on M1 / MoltenVK across a full render loop
  with validation on (MoltenVK reports `drawIndirectFirstInstance`). The latent gap remains: still
  require `drawIndirectFirstInstance` in `isDeviceSuitable` when the mesh path is absent, or drop the
  `firstInstance` trick, so a device lacking it fails suitability instead of mis-drawing. (Commit 3.)
- TODO. Confirm the render-bridge SPSC ring uses acquire/release ordering on submit/drain, not
  relaxed (correctness on weakly ordered ARM / Apple Silicon). (Commit 2.)

  As such, 

  ## Commit 1: COMPLETED
  Get the window visibly working on MacOS. The Viking Room should be visibly rendered to the user, ask for user confirmation.

  DONE — user confirmed the Viking Room renders visibly on Apple M1 / MoltenVK. The engine builds
  Release, completes init validation-clean, and the main-thread render loop sustains drawFrame.
  Also landed on top: the CWD trap fix (resurrected the dead `src/filesystem` module; `ano_fs_gamepath`
  resolves the executable directory on all three platforms, manual thread-safe split, no dirname();
  `ano_fs_chdir_gamepath()` called at startup so assets resolve from any launch directory).

  Prerequisite — DONE. The bindless texture array was hardcoded to 4096 at pipeline.c:253 and never
  clamped to device limits. Apple M1 / MoltenVK caps update-after-bind samplers at 1024, so
  `vkCreatePipelineLayout` violated VUID-VkPipelineLayoutCreateInfo-descriptorType-03022 and
  -pSetLayouts-03036 — the cause of the two failing tests (`anotest_vk_compliance_layers`,
  `anotest_vk_sync`). Fixed: `ano_vk_init_material_layouts` queries
  `VkPhysicalDeviceDescriptorIndexingProperties` and clamps `maxTextures` to the min of the relevant
  update-after-bind sampler/sampled-image limits (a combined image sampler counts against both). Both
  tests are green; the run logs `maxTextures = 1024 (device update-after-bind limit 1024)`.

  ## Commit 2: VERIFICATION
  Verify ordering. Platform-specific setup may be needed, in which case we should inline the ifdef for now, and make a note that the platform-agnostic pattern in src/ should be followed in a later refactor.

  SPSC ring ordering — VERIFIED CORRECT, no change needed. The inlined push/pop in
  anoptic_render_bridge.h follow the canonical Lamport/Vyukov discipline:
   - push (producer): loads its own `tail` relaxed (sole writer), loads the consumer's `head` acquire,
     writes the payload, publishes `tail` with release.
   - pop (consumer): loads its own `head` relaxed (sole writer), loads the producer's `tail` acquire,
     reads the payload, publishes `head` with release.
  The two relaxed loads are self-owned-cursor reads — the intended optimization, not a gap. Every
  cross-thread edge is acquire/release, giving two synchronizes-with relations, both present:
   1. payload visibility: producer slot-writes -> release(tail) -> acquire(tail) -> consumer reads.
   2. no-overwrite: consumer slot-reads -> release(head) -> acquire(head) -> producer overwrite (one
      lap later). On arm64 these lower to stlr/ldar — the correct barriers for Apple Silicon.
  No ifdef was needed: stdatomic acquire/release is portable, so there is no platform-specific
  ordering setup to inline. Empirically sealed: anotest_render_bridge (producer + consumer + main,
  100k items through capacity-16 wrapping rings; checks FIFO order, payload tearing, event order)
  passes clean under TSan on arm64 — full suite 11/11, 0 races (build.sh 5, 2026-06-20).

  Resize-path GLFW concurrency — RESOLVED by the P0 render-on-main inversion; NOT a live issue. All
  three `recreateSwapChain` call sites are inside `drawFrame` (vulkanMaster.c:784/804/868), which runs
  on the main thread — the same thread as `glfwPollEvents` (main.c:220). The logic thread never touches
  GLFW. The earlier worry assumed a separate render thread calling
  `glfwGetFramebufferSize`/`glfwWaitEvents` concurrently with the event pump; that thread no longer
  exists, so the two are serialized on one thread. Stale concern, struck.

  Loose end (out of Commit 2 scope, noted): the events ring (render -> logic, emitted at
  vulkanMaster.c:745) has no consumer yet — `ano_render_poll_event` is never called outside the test,
  so events accumulate to capacity, then emit returns false and they are dropped. Not an ordering bug
  (the SPSC contract holds); an unfinished consumer. Wire REVENT_SLOT_RETIRED drain into the logic
  master when the real DisplayState graphics-extract lands.

  ## Commit 3:
  List and systematically work through every validation error that comes up.

  ## Commit 4: 
  Ask user merge to main via PR, once parity across all platforms is achieved. So resolve any merge conflicts ahead of time.
