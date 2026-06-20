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

  ## Commit 1: 
  Get the window visibly working on MacOS. The Viking Room should be visibly rendered to the user, ask for user confirmation.

  Code-side this is done: the engine builds Release, completes init validation-clean on MoltenVK, and
  the render thread is live in `drawFrame`. Remaining: the user visually confirms the window shows the
  Viking Room (cannot be verified headless from here).

  Prerequisite — DONE. The bindless texture array was hardcoded to 4096 at pipeline.c:253 and never
  clamped to device limits. Apple M1 / MoltenVK caps update-after-bind samplers at 1024, so
  `vkCreatePipelineLayout` violated VUID-VkPipelineLayoutCreateInfo-descriptorType-03022 and
  -pSetLayouts-03036 — the cause of the two failing tests (`anotest_vk_compliance_layers`,
  `anotest_vk_sync`). Fixed: `ano_vk_init_material_layouts` queries
  `VkPhysicalDeviceDescriptorIndexingProperties` and clamps `maxTextures` to the min of the relevant
  update-after-bind sampler/sampled-image limits (a combined image sampler counts against both). Both
  tests are green; the run logs `maxTextures = 1024 (device update-after-bind limit 1024)`.

  ## Commit 2: 
  Verify ordering. Platform-specific setup may be needed, in which case we should inline the ifdef for now, and make a note that the platform-agnostic pattern in src/ should be followed in a later refactor.

  Follow-up surfaced by the P0 inversion: with the event pump on the main thread (macOS), the resize
  path now has the render thread call `glfwGetFramebufferSize`/`glfwWaitEvents` (recreateSwapChain,
  instanceInit.c:1008-1013) concurrently with `glfwPollEvents` on the main thread — concurrent access
  to one GLFWwindow, which GLFW does not guarantee safe. Steady-state (no resize) never triggers it,
  so it does not block bring-up, but resize handling on macOS needs the framebuffer-size query routed
  to the main thread. Belongs with the platform-agnostic ownership refactor noted above.

  ## Commit 3:
  List and systematically work through every validation error that comes up.

  ## Commit 4: 
  Ask user merge to main via PR, once parity across all platforms is achieved. So resolve any merge conflicts ahead of time.
