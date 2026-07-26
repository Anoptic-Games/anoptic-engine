/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: parseGltf's texture custody chain (ano_GltfParser.c:365-:450). Compiles the REAL parser
// TU against link-seam stubs 〜 no device, no loader 〜 and drives a hand-written two-texture scene
// through a stubbed createTextureImage that keeps a mint/adopt/destroy ledger over every handle it
// hands out. Four questions. Which AnoTextureResult codes the parser discharges for: none, because
// BUILT transfers the package and every other code already unwound inside the callee
// (texture.c:497), so a parser-side destroy there would be a double free. Which code latches
// haltLoad: DEVICE alone, since gpu_alloc is monotonic and every later attempt burns another span.
// What a refused registry adoption costs: the image and BOTH views, no bindless call, both index
// cells left at ANO_BINDLESS_NONE. And WHEN those destroys land 〜 the point of the whole deferred
// epilogue. The refused image is one the unsubmitted batch has already copied and blitted into, so
// destroying it before endSingleTimeCommands submits is the use-after-free
// VUID-vkDestroyImage-image-01000 fires on; every seam here stamps a monotonic call clock and the
// first parser-side discharge must outrank the batch submit. The index cells are read back through
// the baked material rows, the only surface parseGltf publishes them on. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/gltf/ano_GltfParser.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/geometry.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/texture/texture.h"

#include "templates/scratch.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledgers 〜 every handle the loader seam mints, and where its ownership went */

#define MAX_IMGS  8
#define MAX_VIEWS 16

// One monotonic clock over every seam. Ordering, not just counting: the epilogue's discharges have
// to outrank the batch submit, and only a sequence number can say so.
static uint32_t g_seq;
static uint32_t g_seqBatchEnd;      // endSingleTimeCommands
static uint32_t g_seqFirstKill;     // first destroy issued above the constructor seam

static VkImage  g_imgMinted[MAX_IMGS];
static bool     g_imgAdopted[MAX_IMGS];   // handed to the teardown registry (register_texture)
static bool     g_imgDestroyed[MAX_IMGS];
static uint32_t g_imgMintCount;

static VkImageView g_viewMinted[MAX_VIEWS];
static bool        g_viewDestroyed[MAX_VIEWS];
static uint32_t    g_viewMintCount;

static VkBuffer g_stgMinted[MAX_IMGS];
static bool     g_stgLive[MAX_IMGS];
static uint32_t g_stgMintCount;

// Destroys issued under the constructor stub are the callee's own unwind, not the parser's.
static bool     g_inCallee;
static uint32_t g_killImg, g_killView, g_killBuf;   // parser-side discharges, per kind

static uint32_t g_texCalls;         // createTextureImage ordinal, 1-based
static uint32_t g_regCalls;         // ano_vk_register_texture ordinal, 1-based
static uint32_t g_bindlessCalls;

static uint32_t             g_failCall;       // constructor ordinal that refuses; 0 = never
static AnoTextureResultCode g_failCode;
static uint32_t             g_refuseAdopt;    // register ordinal answering false; 0 = never
static uint32_t             g_refuseBind;     // bindless ordinal answering NONE; 0 = never

static uint32_t g_doubleKills;   // never reset 〜 whole-run invariant
static uint32_t g_unknownKills;  // never reset 〜 whole-run invariant

// Counts minted images whose ownership was discharged nowhere.
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

// Clears the per-run ledgers and the clock; whole-run invariants persist.
static void reset_ledger(void)
{
    memset(g_imgMinted, 0, sizeof g_imgMinted);
    memset(g_imgAdopted, 0, sizeof g_imgAdopted);
    memset(g_imgDestroyed, 0, sizeof g_imgDestroyed);
    memset(g_viewMinted, 0, sizeof g_viewMinted);
    memset(g_viewDestroyed, 0, sizeof g_viewDestroyed);
    memset(g_stgMinted, 0, sizeof g_stgMinted);
    memset(g_stgLive, 0, sizeof g_stgLive);
    g_imgMintCount = g_viewMintCount = g_stgMintCount = 0;
    g_seq = g_seqBatchEnd = g_seqFirstKill = 0;
    g_killImg = g_killView = g_killBuf = 0;
    g_texCalls = g_regCalls = g_bindlessCalls = 0;
}

// out: a fresh ledgered handle, VK_NULL_HANDLE once the ledger is full (which fails the run).
static VkImage mint_image(void)
{
    if (g_imgMintCount >= MAX_IMGS) { printf("FAIL: image ledger overflow\n"); failures++; return VK_NULL_HANDLE; }
    VkImage h = (VkImage)(uintptr_t)(0x2000u + g_imgMintCount);
    g_imgMinted[g_imgMintCount++] = h;
    return h;
}

static VkImageView mint_view(void)
{
    if (g_viewMintCount >= MAX_VIEWS) { printf("FAIL: view ledger overflow\n"); failures++; return VK_NULL_HANDLE; }
    VkImageView h = (VkImageView)(uintptr_t)(0x3000u + g_viewMintCount);
    g_viewMinted[g_viewMintCount++] = h;
    return h;
}

static VkBuffer mint_staging(void)
{
    if (g_stgMintCount >= MAX_IMGS) { printf("FAIL: staging ledger overflow\n"); failures++; return VK_NULL_HANDLE; }
    VkBuffer h = (VkBuffer)(uintptr_t)(0x1000u + g_stgMintCount);
    g_stgMinted[g_stgMintCount] = h;
    g_stgLive[g_stgMintCount] = true;
    g_stgMintCount++;
    return h;
}


/* Link seams 〜 ano_GltfParser.c's externs (the real definitions live in vulkanMaster.c /
   texture.c / geometry.c, which this executable deliberately does not link) */

GpuAllocator  stagingAllocator;
RendererState rendererState;

// in: usage picks the views, keepStaging asks for the staging buffer, g_failCall/g_failCode inject
// one refusal by call ordinal. out: BUILT publishes a whole package; every other code discharges
// what it acquired ITSELF and leaves *pkg inert, exactly as texture.c:497's fail label does 〜 so a
// parser-side destroy on a refusal shows up here as a double destroy.
AnoTextureResult createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
                                    const char* fileName, bool flag16,
                                    TextureUsageFlags usage, bool keepStaging)
{
    (void)cmd; (void)fileName; (void)flag16;
    g_seq++;
    g_texCalls++;
    if (!pkg) return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);
    *pkg = (TexturePackage){0};
    if (usage == TEXTURE_USE_NONE) return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);

    VkImage     image = mint_image();
    VkImageView srgb  = (usage & TEXTURE_USE_COLOR) ? mint_view() : VK_NULL_HANDLE;
    VkImageView unorm = (usage & TEXTURE_USE_DATA)  ? mint_view() : VK_NULL_HANDLE;
    VkBuffer    staging = mint_staging();

    if (g_failCall == g_texCalls) {
        g_inCallee = true;
        vkDestroyImageView(ctx->device, unorm, NULL);
        vkDestroyImageView(ctx->device, srgb, NULL);
        vkDestroyImage(ctx->device, image, NULL);
        vkDestroyBuffer(ctx->device, staging, NULL);
        g_inCallee = false;
        return ANO_RESULT(AnoTextureResult, g_failCode);
    }

    if (keepStaging) {
        pkg->staging = staging;
    } else {
        g_inCallee = true;
        vkDestroyBuffer(ctx->device, staging, NULL);
        g_inCallee = false;
    }
    pkg->image = image;
    pkg->srgbView = srgb;
    pkg->unormView = unorm;
    pkg->alloc = (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x77, .size = 1u << 20 };
    pkg->mipLevels = 1; pkg->width = 4; pkg->height = 4;
    return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_BUILT);
}

// Adoption into the teardown registry 〜 the one ownership discharge cleanupVulkan walks.
// out: false leaves every handle in data with the caller.
bool ano_vk_register_texture(RenderPrimitives* primitives, TextureData data)
{
    (void)primitives;
    g_seq++;
    g_regCalls++;
    if (g_refuseAdopt == g_regCalls) return false;
    for (uint32_t i = 0; i < g_imgMintCount; i++)
        if (g_imgMinted[i] == data.textureImage) { g_imgAdopted[i] = true; return true; }
    printf("FAIL: registry adopted an unminted image\n"); failures++;
    return true;
}

// Grants start at 1: slot 0 is the fallback texture, so an index that aliased onto it would be
// distinguishable here from a real grant.
uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler)
{
    (void)ctx; (void)bta; (void)view; (void)sampler;
    g_seq++;
    if (++g_bindlessCalls == g_refuseBind) return ANO_BINDLESS_NONE;
    return g_bindlessCalls;
}

// This guard is about custody, not sampling: one handle for every request keeps the descriptor
// pair constant so the bindless call count reflects views alone.
PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state)
{ (void)state; return (PbrFeatureFlags)0xFFFFFFFFu; }

// The real default writes the no-texture sentinel into every texture field (components.c:139).
void ano_vk_init_default_material_data(struct MaterialData* mat)
{
    memset(mat, 0, sizeof *mat);
    mat->baseColorTexture = ANO_BINDLESS_NONE;
    mat->metallicRoughnessTexture = ANO_BINDLESS_NONE;
    mat->normalTexture = ANO_BINDLESS_NONE;
    mat->occlusionTexture = ANO_BINDLESS_NONE;
    mat->emissiveTexture = ANO_BINDLESS_NONE;
}

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

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx)
{ (void)ctx; g_seq++; return (VkCommandBuffer)(uintptr_t)0x54; }

// The batch submit and its fence wait. Nothing the batch referenced may be destroyed before this.
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer)
{ (void)ctx; (void)commandBuffer; g_seqBatchEnd = ++g_seq; }

void gpu_alloc_reset(GpuAllocator* alloc) { (void)alloc; }
void multiplyMat4(mat4 result, const mat4 a, const mat4 b) { (void)result; (void)a; (void)b; }


/* Link seams 〜 the vk* entry points the parser TU calls (the loader is not linked) */

// Records one discharge on the clock, attributed to whoever issued it.
static void note_kill(void)
{
    uint32_t now = ++g_seq;
    if (g_inCallee) return;
    if (!g_seqFirstKill) g_seqFirstKill = now;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (buffer == VK_NULL_HANDLE) return; // spec no-op, the calloc'd-hole case
    note_kill();
    if (!g_inCallee) g_killBuf++;
    for (uint32_t i = 0; i < g_stgMintCount; i++) {
        if (g_stgMinted[i] == buffer) {
            if (!g_stgLive[i]) g_doubleKills++;
            g_stgLive[i] = false;
            return;
        }
    }
    g_unknownKills++;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (image == VK_NULL_HANDLE) return;
    note_kill();
    if (!g_inCallee) g_killImg++;
    for (uint32_t i = 0; i < g_imgMintCount; i++) {
        if (g_imgMinted[i] == image) {
            if (g_imgDestroyed[i]) g_doubleKills++;
            g_imgDestroyed[i] = true;
            return;
        }
    }
    g_unknownKills++;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{
    (void)device; (void)pAllocator;
    if (imageView == VK_NULL_HANDLE) return;
    note_kill();
    if (!g_inCallee) g_killView++;
    for (uint32_t i = 0; i < g_viewMintCount; i++) {
        if (g_viewMinted[i] == imageView) {
            if (g_viewDestroyed[i]) g_doubleKills++;
            g_viewDestroyed[i] = true;
            return;
        }
    }
    g_unknownKills++;
}


/* Fixture 〜 texture 0 sampled in BOTH domains (so a refusal must leave two cells NONE), texture 1
   in colour only (so "was texture 1 attempted at all" is readable off its own material row). One
   18-byte buffer feeds one vertex and three zero indices, twice, so both material rows bake. */

#define SCENE "anotest_gltftexleakguard_scene.gltf"

static const char* JSON_SCENE =
    "{\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{\"byteLength\":18,\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAA\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12},"
                     "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":6}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
                   "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"images\":[{\"uri\":\"anotest_gltfleak_t0.png\"},{\"uri\":\"anotest_gltfleak_t1.png\"}],"
    "\"textures\":[{\"source\":0},{\"source\":1}],"
    "\"materials\":[{\"name\":\"mixed\",\"pbrMetallicRoughness\":{"
                     "\"baseColorTexture\":{\"index\":0},\"metallicRoughnessTexture\":{\"index\":0}}},"
                   "{\"name\":\"colour\",\"pbrMetallicRoughness\":{"
                     "\"baseColorTexture\":{\"index\":1}}}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0},"
                                 "{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":1}]}],"
    "\"nodes\":[{\"mesh\":0}],"
    "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

// Inputs: path, contents. Output: true if fully written.
static bool write_fixture(const char* path, const char* json)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t len = strlen(json);
    bool ok = fwrite(json, 1, len, f) == len;
    fclose(f);
    return ok;
}


/* Material rows 〜 the parser's colorIndex/dataIndex cells are private scratch, freed with the
   scope; the baked SSBO row is the only place they surface. */

#define MAT_ROWS 8
static MaterialData g_rows[MAX_FRAMES_IN_FLIGHT][MAT_ROWS];

// Points the material buffer at real host storage and clears the run's injections.
static void arm_run(void)
{
    memset(g_rows, 0, sizeof g_rows);
    for (size_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
        rendererState.materialBuffer.mapped[f] = g_rows[f];
    rendererState.materialBuffer.capacity = MAT_ROWS;
    rendererState.materialBuffer.count = 0;
    reset_ledger();
    g_failCall = 0;
    g_failCode = ANO_TEXTURE_BUILT;
    g_refuseAdopt = 0;
    g_refuseBind = 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    scratch_anchor_to_exe();

    if (!write_fixture(SCENE, JSON_SCENE)) {
        printf("FAIL: could not write the glTF fixture\n");
        return 1;
    }

    static VulkanContext ctx; // zeroed; every seam stub ignores its handles

    // control: the all-success parse adopts every minted image, bindless-binds all three views
    // (texture 0 in both domains, texture 1 in colour), destroys each handed-out staging buffer
    // exactly once and destroys NOTHING the registry now owns
    {
        arm_run();
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "clean parse returns an asset");
        CHECK(g_texCalls == 2, "both needed textures reach the constructor");
        CHECK(g_imgMintCount == 2 && g_viewMintCount == 3, "two images, three views minted");
        CHECK(g_imgAdopted[0] && g_imgAdopted[1], "both images adopted into the teardown registry");
        CHECK(g_bindlessCalls == 3, "all three views bindless-bound");
        CHECK(g_killBuf == 2 && live_staging() == 0, "the epilogue destroys each staging buffer exactly once");
        CHECK(g_killImg == 0 && g_killView == 0, "a clean run destroys no image and no view 〜 the registry owns them");
        CHECK(orphaned_images() == 0, "a clean run orphans nothing");
        CHECK(g_seqFirstKill > g_seqBatchEnd, "staging buffers die after the batch submits");
        CHECK(g_rows[0][0].baseColorTexture == 1u && g_rows[0][0].metallicRoughnessTexture == 2u,
              "the mixed texture bakes its colour index into the colour slot and its data index into the data slot");
        CHECK(g_rows[0][1].baseColorTexture == 3u, "the colour-only texture bakes its own slot");
        free(asset);
    }

    // ANO_TEXTURE_SOURCE: an unusable source file is this asset's problem alone. The callee already
    // unwound, so the parser discharges nothing, and texture 1 is still attempted.
    {
        arm_run();
        g_failCall = 1; g_failCode = ANO_TEXTURE_SOURCE;
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "SOURCE: the parse survives an unusable texture source");
        CHECK(g_texCalls == 2, "SOURCE: the load continues 〜 texture 1 is still attempted");
        CHECK(g_killImg == 0 && g_killView == 0, "SOURCE: the parser discharges no image or view of the refused package");
        CHECK(g_killBuf == 1, "SOURCE: only the surviving texture's staging buffer is destroyed");
        CHECK(g_doubleKills == 0, "SOURCE: nothing the callee already discharged is destroyed again");
        CHECK(orphaned_images() == 0, "SOURCE: ledger balanced");
        CHECK(g_rows[0][0].baseColorTexture == ANO_BINDLESS_NONE &&
              g_rows[0][0].metallicRoughnessTexture == ANO_BINDLESS_NONE,
              "SOURCE: the refused texture leaves both cells at ANO_BINDLESS_NONE");
        CHECK(g_rows[0][1].baseColorTexture == 1u, "SOURCE: the next texture still takes a real slot");
        free(asset);
    }

    // ANO_TEXTURE_INVALID: a contract violation is likewise local 〜 same discharge, load continues.
    {
        arm_run();
        g_failCall = 1; g_failCode = ANO_TEXTURE_INVALID;
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "INVALID: the parse survives");
        CHECK(g_texCalls == 2, "INVALID: the load continues");
        CHECK(g_killImg == 0 && g_killView == 0, "INVALID: the parser discharges nothing of the refused package");
        CHECK(orphaned_images() == 0, "INVALID: ledger balanced");
        CHECK(g_rows[0][1].baseColorTexture == 1u, "INVALID: the next texture still takes a real slot");
        free(asset);
    }

    // ANO_TEXTURE_DEVICE: the arena is monotonic, so the first refusal proves every later one.
    // haltLoad latches and texture 1 is never even attempted.
    {
        arm_run();
        g_failCall = 1; g_failCode = ANO_TEXTURE_DEVICE;
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "DEVICE: the parse survives");
        CHECK(g_texCalls == 1, "DEVICE: haltLoad latches 〜 texture 1 never reaches the constructor");
        CHECK(g_bindlessCalls == 0, "DEVICE: nothing bindless-bound");
        CHECK(g_killImg == 0 && g_killView == 0 && g_killBuf == 0, "DEVICE: the parser discharges nothing");
        CHECK(orphaned_images() == 0, "DEVICE: ledger balanced");
        CHECK(g_rows[0][0].baseColorTexture == ANO_BINDLESS_NONE &&
              g_rows[0][0].metallicRoughnessTexture == ANO_BINDLESS_NONE &&
              g_rows[0][1].baseColorTexture == ANO_BINDLESS_NONE,
              "DEVICE: every cell stays at ANO_BINDLESS_NONE");
        free(asset);
    }

    // Refused registry adoption on the mixed texture. The parser still holds an image the
    // unsubmitted batch has copied and blitted into, so it discharges image AND both views 〜 in
    // the epilogue, after endSingleTimeCommands. Destroying them inside the loop is
    // VUID-vkDestroyImage-image-01000.
    {
        arm_run();
        g_refuseAdopt = 1;
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "refused adoption: the parse survives");
        CHECK(g_regCalls == 1 && g_texCalls == 1, "refused adoption: haltLoad latches 〜 texture 1 never reaches the constructor");
        CHECK(g_bindlessCalls == 0, "refused adoption: no bindless call for an unadopted texture");
        CHECK(g_killImg == 1 && g_imgDestroyed[0], "refused adoption: the image is destroyed");
        CHECK(g_killView == 2 && g_viewDestroyed[0] && g_viewDestroyed[1], "refused adoption: BOTH views are destroyed");
        CHECK(g_killBuf == 1 && live_staging() == 0, "refused adoption: the staging buffer is still destroyed");
        CHECK(orphaned_images() == 0, "refused adoption: ledger balanced");
        CHECK(g_seqBatchEnd != 0, "refused adoption: the batch was submitted");
        if (g_seqFirstKill && g_seqFirstKill < g_seqBatchEnd)
            printf("  (a handle the unsubmitted batch still references was destroyed at seq %" PRIu32
                   ", before endSingleTimeCommands at seq %" PRIu32 ")\n", g_seqFirstKill, g_seqBatchEnd);
        CHECK(g_seqFirstKill > g_seqBatchEnd, "refused adoption: every discharge lands AFTER the batch submits");
        CHECK(g_rows[0][0].baseColorTexture == ANO_BINDLESS_NONE &&
              g_rows[0][0].metallicRoughnessTexture == ANO_BINDLESS_NONE,
              "refused adoption: both index cells stay at ANO_BINDLESS_NONE");
        CHECK(g_rows[0][1].baseColorTexture == ANO_BINDLESS_NONE,
              "refused adoption: the skipped texture's cell stays at ANO_BINDLESS_NONE");
        free(asset);
    }

    // Refused bindless slot on the first image. The array is bump-only and never un-refuses, so no
    // second interpretation or image is offered. The image was already adopted, so it stays
    // registry-owned and is NOT discharged here.
    {
        arm_run();
        g_refuseBind = 1;
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "refused slot: the parse survives");
        CHECK(g_texCalls == 1, "refused slot: haltLoad latches 〜 image 1 never reaches createTextureImage");
        CHECK(g_bindlessCalls == 1, "refused slot: no registration follows the first permanent refusal");
        CHECK(g_imgAdopted[0] && g_killImg == 0 && g_killView == 0 && orphaned_images() == 0, "refused slot: the refusing image stays registry-owned 〜 the parser discharges nothing");
        CHECK(g_rows[0][0].baseColorTexture == ANO_BINDLESS_NONE &&
              g_rows[0][0].metallicRoughnessTexture == ANO_BINDLESS_NONE &&
              g_rows[0][1].baseColorTexture == ANO_BINDLESS_NONE,
              "refused slot: every unavailable interpretation bakes NONE, never slot 0");
        free(asset);
    }

    // whole-run ledger invariants 〜 no handle destroyed twice, none invented
    CHECK(g_doubleKills == 0, "no handle destroyed twice");
    CHECK(g_unknownKills == 0, "no unknown handle destroyed");

    remove(SCENE);

    if (failures) {
        printf("anotest_gltftexleakguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_gltftexleakguard: all passed\n");
    return 0;
}
