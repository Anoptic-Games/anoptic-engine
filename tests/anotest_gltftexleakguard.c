/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the failed-texture orphan at the glTF/texture composition seam (ano_GltfParser.c:277,
// docs/BUGS.md, Render / Vulkan backend / Interlink-Composition). parseGltf hears
// createTextureImage's false (:276) yet discharges nothing: the callee's post-create failure arms
// (texture.c:429-:433 transition; :446-:450 view, armed the day the tallied swapchain.c:428 /
// texture.c:437 fixes land) return false with the just-created VkImage and its texture-arena
// allocation already written through the out-params into loadedImages[t]/loadedAllocs[t]; adoption
// into the teardown registry is success-only (ano_vk_register_texture at :282, the sole route
// cleanupVulkan ever walks), no failure arm destroys anything, and :619-:621 free the host arrays
// holding the only copies of the handles 〜 one whole device texture per failed load, unreachable
// forever. Harness: compiles the REAL ano_GltfParser.c TU (real cgltf parse of a hand-built
// two-texture .gltf this test writes beside its CWD) against link-seam stubs; createTextureImage
// keeps a mint/adopt/destroy image ledger and models the callee's post-create arm as written
// today (image + alloc written, view and staging not 〜 the staging orphan is texture.c:426's own
// tally and is abstracted out here). Controls prove the all-success parse adopts every minted
// image and that the failed slot is neither registered nor bindless-bound (the status IS heard),
// so a reject-everything fix cannot pass. Fails until a heard false leaves zero un-adopted,
// un-destroyed images 〜 a parser-side destroy of the failed slot satisfies the ledger; if the
// callee is instead fixed to unwind internally, retire this with the entry. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/gltf/ano_GltfParser.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledgers 〜 every VkImage the loader seam mints, and where its ownership went */

#define MAX_IMGS 8
static VkImage  g_imgMinted[MAX_IMGS];
static bool     g_imgAdopted[MAX_IMGS];   // handed to the teardown registry (register_texture)
static bool     g_imgDestroyed[MAX_IMGS]; // vkDestroyImage'd by anyone
static uint32_t g_imgMintCount;

static VkBuffer g_stgMinted[MAX_IMGS];
static bool     g_stgLive[MAX_IMGS];
static uint32_t g_stgMintCount;

static uint32_t g_bindlessCalls;
static uint32_t g_texCalls;        // createTextureImage call ordinal, 1-based
static uint32_t g_failCall;        // ordinal whose post-create arm fails; 0 = never
static uint32_t g_doubleDestroys;  // never reset 〜 whole-run invariant
static uint32_t g_unknownDestroys; // never reset 〜 whole-run invariant

// Counts minted images with ownership discharged nowhere.
static uint32_t orphaned_images(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_imgMintCount; i++)
        if (!g_imgAdopted[i] && !g_imgDestroyed[i]) n++;
    return n;
}

// Counts staging buffers still live.
static uint32_t live_staging(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_stgMintCount; i++) if (g_stgLive[i]) n++;
    return n;
}

// Clears the per-run ledgers; whole-run invariants persist.
static void reset_ledger(void)
{
    memset(g_imgMinted, 0, sizeof g_imgMinted);
    memset(g_imgAdopted, 0, sizeof g_imgAdopted);
    memset(g_imgDestroyed, 0, sizeof g_imgDestroyed);
    memset(g_stgMinted, 0, sizeof g_stgMinted);
    memset(g_stgLive, 0, sizeof g_stgLive);
    g_imgMintCount = g_stgMintCount = g_bindlessCalls = g_texCalls = 0;
}


/* Link seams 〜 ano_GltfParser.c externs (real definitions live in vulkanMaster.c /
   texture.c / geometry.c, which this executable deliberately does not link) */

GpuAllocator  stagingAllocator;
RendererState rendererState;

// in: file path (ignored, no decode here); out: image/alloc always written after the create
// (texture.c:422), view + staging on success only (:445-:446). The injected failure models the
// post-create arm family as written: false with the live image left in the out-params.
bool createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage, GpuAllocation* textureImageAlloc, VkImageView* textureImageView, char* fileName, bool flag16, bool srgb, VkBuffer* outStagingBuffer)
{
    (void)ctx; (void)cmd; (void)fileName; (void)flag16; (void)srgb;
    g_texCalls++;
    if (g_imgMintCount >= MAX_IMGS || g_stgMintCount >= MAX_IMGS) { printf("FAIL: ledger overflow\n"); failures++; return false; }
    // createImage succeeded 〜 out-params hold the live image + arena allocation
    VkImage img = (VkImage)(uintptr_t)(0x2000u + g_imgMintCount);
    g_imgMinted[g_imgMintCount] = img;
    g_imgMintCount++;
    *textureImage = img;
    textureImageAlloc->memory = (VkDeviceMemory)(uintptr_t)0x77;
    textureImageAlloc->offset = 0;
    textureImageAlloc->size   = 1u << 20;
    textureImageAlloc->mapped = NULL;
    if (g_failCall == g_texCalls)
        return false; // transition/view arm: view and *outStagingBuffer never written
    *textureImageView = (VkImageView)(uintptr_t)(0x3000u + g_imgMintCount);
    VkBuffer stg = (VkBuffer)(uintptr_t)(0x1000u + g_stgMintCount);
    g_stgMinted[g_stgMintCount] = stg;
    g_stgLive[g_stgMintCount] = true;
    g_stgMintCount++;
    if (outStagingBuffer) *outStagingBuffer = stg;
    return true;
}

// Adoption into the teardown registry 〜 the one ownership discharge cleanupVulkan walks.
void ano_vk_register_texture(RenderPrimitives* primitives, TextureData data)
{
    (void)primitives;
    for (uint32_t i = 0; i < g_imgMintCount; i++)
        if (g_imgMinted[i] == data.textureImage) { g_imgAdopted[i] = true; return; }
    printf("FAIL: registry adopted an unminted image\n"); failures++;
}

// Bindless slot mint; records that the parser consumed the view as loaded.
uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler)
{
    (void)ctx; (void)bta; (void)view; (void)sampler;
    return ++g_bindlessCalls;
}

PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state)
{ (void)state; return (PbrFeatureFlags)0xFFFFFFFFu; }

void ano_vk_init_default_material_data(struct MaterialData* mat)
{ memset(mat, 0, sizeof *mat); }

AnoLodConfig ano_lod_config_default(uint32_t lodCount)
{ AnoLodConfig cfg = {0}; cfg.lodCount = lodCount ? lodCount : 1u; cfg.ratios[0] = 1.0f; return cfg; }

uint32_t geometry_pool_upload_chain(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                                    uint32_t transferFamily, VkQueue transferQueue,
                                    const Vertex* vertices, uint32_t vertexCount,
                                    const uint32_t* indices, uint32_t indexCount,
                                    const AnoLodConfig* config,
                                    uint32_t* out_lodBase, uint32_t* out_lodCount)
{
    (void)pool; (void)alloc; (void)device; (void)transferFamily; (void)transferQueue;
    (void)vertices; (void)vertexCount; (void)indices; (void)indexCount; (void)config;
    if (out_lodBase) *out_lodBase = 0u;
    if (out_lodCount) *out_lodCount = 1u;
    return 0u;
}

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx) { (void)ctx; return (VkCommandBuffer)(uintptr_t)0x54; }
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer) { (void)ctx; (void)commandBuffer; }
void gpu_alloc_reset(GpuAllocator* alloc) { (void)alloc; }
void multiplyMat4(mat4 result, const mat4 a, const mat4 b) { (void)result; (void)a; (void)b; }


/* Link seams 〜 the vk* entry points the parser TU (or a fixed parser) calls (loader not linked) */

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (buffer == VK_NULL_HANDLE) return; // spec no-op, the calloc'd-hole case
    for (uint32_t i = 0; i < g_stgMintCount; i++) {
        if (g_stgMinted[i] == buffer) {
            if (!g_stgLive[i]) g_doubleDestroys++;
            g_stgLive[i] = false;
            return;
        }
    }
    g_unknownDestroys++;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (image == VK_NULL_HANDLE) return;
    for (uint32_t i = 0; i < g_imgMintCount; i++) {
        if (g_imgMinted[i] == image) {
            if (g_imgDestroyed[i]) g_doubleDestroys++;
            g_imgDestroyed[i] = true;
            return;
        }
    }
    g_unknownDestroys++;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)imageView; (void)pAllocator; }


/* Fixture 〜 two needed textures behind one metallic-roughness material, no meshes */

static bool write_gltf(const char* path)
{
    static const char json[] =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"images\":[{\"uri\":\"anotest_gltfleak0.png\"},{\"uri\":\"anotest_gltfleak1.png\"}],"
        "\"textures\":[{\"source\":0},{\"source\":1}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{"
        "\"baseColorTexture\":{\"index\":0},"
        "\"metallicRoughnessTexture\":{\"index\":1}}}]}";
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(json, 1, sizeof json - 1, f);
    fclose(f);
    return true;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    static VulkanContext ctx; // zeroed; every seam stub ignores its handles
    char scene[] = "anotest_gltfleakguard_scene.gltf";
    CHECK(write_gltf(scene), "two-texture glTF fixture written");

    // control: the all-success parse adopts every minted image into the teardown registry and
    // its epilogue destroys every handed-out staging buffer, so a reject-everything or
    // destroy-everything fix cannot pass
    reset_ledger();
    g_failCall = 0;
    ModelAsset* asset = parseGltf(&ctx, scene);
    CHECK(asset != NULL, "all-success parse returns an asset");
    CHECK(g_texCalls == 2, "both needed textures reach the loader seam");
    CHECK(g_imgMintCount == 2, "loader minted both images");
    CHECK(g_imgAdopted[0] && g_imgAdopted[1], "success adopts both images into the registry");
    CHECK(g_bindlessCalls == 2, "success bindless-binds both views");
    CHECK(orphaned_images() == 0, "all-success parse orphans nothing");
    CHECK(live_staging() == 0, "parser epilogue destroys the handed-out staging buffers");

    // trigger: texture 1's load fails at the callee's post-create arm 〜 false comes back with
    // the live image + arena allocation already written into loadedImages[1]/loadedAllocs[1],
    // the parser drops the slot (textureLoaded[1] = false, :276) and frees the host arrays
    // (:619-:621) 〜 the only copies of the handles
    printf("trigger: post-create failure on the second texture load\n");
    reset_ledger();
    g_failCall = 2;
    asset = parseGltf(&ctx, scene);
    g_failCall = 0;
    CHECK(asset != NULL, "parse survives a failed texture by design");
    CHECK(g_texCalls == 2, "both textures reached the loader seam");
    CHECK(g_imgMintCount == 2, "failed load minted its image before the refusal");
    CHECK(g_imgAdopted[0], "surviving texture is adopted");
    CHECK(!g_imgAdopted[1] && g_bindlessCalls == 1, "failure status IS heard 〜 failed slot neither registered nor bindless-bound");
    if (orphaned_images() != 0)
        printf("  (image minted by the loader seam still un-adopted and un-destroyed after parseGltf 〜 loadedImages[1]/loadedAllocs[1] freed with the host arrays at ano_GltfParser.c:619-:621)\n");
    CHECK(orphaned_images() == 0, "a heard false leaves zero un-adopted, un-destroyed images");

    // whole-run ledger invariants 〜 a fix must not double-destroy or invent handles
    CHECK(g_doubleDestroys == 0, "no handle destroyed twice");
    CHECK(g_unknownDestroys == 0, "no unknown handle destroyed");

    remove(scene);

    if (failures) {
        printf("anotest_gltftexleakguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_gltftexleakguard: all passed\n");
    return 0;
}
