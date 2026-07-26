/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the colour/data split across parseGltf's usage mark (ano_GltfParser.c:296-:344), its
// texture loop (:365-:434) and its material bake (:466-:730). One image sampled as baseColorTexture
// by one material and as metallicRoughnessTexture by another is the defect: uploaded sRGB and read
// through one slot, every data fetch comes back sampler-decoded and a stored 0.5 arrives as 0.214.
// The fix makes imageUsage a union, asks the constructor for BOTH interpretations over one
// allocation, registers each view in its own bindless slot, and bakes colorIndex into colour slots
// and dataIndex into data slots 〜 so the whole contract is: those two indices DIFFER. This compiles
// the REAL parser TU against link-seam stubs (no device, no loader), drives a hand-written
// five-texture scene covering colour-only, data-only, mixed-across-two-materials, mixed-within-one-
// material and never-constructed, and reads the baked rows back through a real materialBuffer.
// The refusal matrix runs both directions: whichever interpretation loses its bindless slot must
// hold ANO_BINDLESS_NONE, never the other's index and never slot 0 〜 slot 0 is the fallback
// texture, which is exactly what the 0xFF seeding at :219-:220 exists to keep it from becoming.
// Exit 0 == pass.

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


/* What the seams observed 〜 one row per fixture texture, keyed by the ordinal in its image URI */

#define TEX_COUNT     5
#define MAX_BINDLESS 16

static TextureUsageFlags g_usage[TEX_COUNT];   // the usage mask the constructor was asked for
static bool              g_built[TEX_COUNT];
static VkImageView       g_srgb[TEX_COUNT], g_unorm[TEX_COUNT];
static uint32_t          g_texCalls;

static VkImageView g_bindView[MAX_BINDLESS];   // the view each bindless registration named
static VkSampler   g_bindSampler[MAX_BINDLESS]; // and the sampler, since the descriptor is the pair
static uint32_t    g_bindCalls;
static uint32_t    g_refuseBind;               // registration ordinal answering NONE; 0 = never

// out: how many bindless registrations named this view. VK_NULL_HANDLE is never registered.
static uint32_t binds_of(VkImageView view)
{
    uint32_t n = 0;
    if (view == VK_NULL_HANDLE) return 0;
    for (uint32_t i = 0; i < g_bindCalls && i < MAX_BINDLESS; i++)
        if (g_bindView[i] == view) n++;
    return n;
}

// in: a resolved texture path ending in "_t<digit>.png". out: the fixture ordinal, TEX_COUNT if the
// tail does not parse 〜 which fails the run rather than silently mis-attributing a call.
static uint32_t path_ordinal(const char* path)
{
    const char* tail = strrchr(path, '_');
    if (!tail || tail[1] != 't' || tail[2] < '0' || tail[2] > '9') return TEX_COUNT;
    return (uint32_t)(tail[2] - '0');
}


/* Link seams 〜 ano_GltfParser.c's externs (the real definitions live in vulkanMaster.c /
   texture.c / geometry.c, which this executable deliberately does not link) */

GpuAllocator  stagingAllocator;
RendererState rendererState;

// in: usage picks the views. out: BUILT, with srgbView non-null iff COLOR was asked for and
// unormView non-null iff DATA was 〜 one mutable-format image carrying both, never two uploads.
// Handles are ordinal-encoded so the bindless log can be read back per texture per domain.
AnoTextureResult createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, TexturePackage* pkg,
                                    const char* fileName, bool flag16,
                                    TextureUsageFlags usage, bool keepStaging)
{
    (void)ctx; (void)cmd; (void)flag16;
    g_texCalls++;
    if (!pkg) return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);
    *pkg = (TexturePackage){0};
    if (usage == TEXTURE_USE_NONE) return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_INVALID);

    uint32_t t = path_ordinal(fileName);
    if (t >= TEX_COUNT) {
        printf("FAIL: constructor called with an unrecognised path: %s\n", fileName);
        failures++;
        return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_SOURCE);
    }

    g_usage[t] = usage;
    g_built[t] = true;
    g_srgb[t]  = (usage & TEXTURE_USE_COLOR) ? (VkImageView)(uintptr_t)(0x5000u + t) : VK_NULL_HANDLE;
    g_unorm[t] = (usage & TEXTURE_USE_DATA)  ? (VkImageView)(uintptr_t)(0x6000u + t) : VK_NULL_HANDLE;

    pkg->image = (VkImage)(uintptr_t)(0x2000u + t);
    pkg->srgbView = g_srgb[t];
    pkg->unormView = g_unorm[t];
    pkg->staging = keepStaging ? (VkBuffer)(uintptr_t)(0x1000u + t) : VK_NULL_HANDLE;
    pkg->alloc = (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x77, .size = 1u << 20 };
    pkg->mipLevels = 1; pkg->width = 4; pkg->height = 4;
    return ANO_RESULT(AnoTextureResult, ANO_TEXTURE_BUILT);
}

bool ano_vk_register_texture(RenderPrimitives* primitives, TextureData data)
{ (void)primitives; (void)data; return true; }

// out: the slot the view now occupies, or ANO_BINDLESS_NONE on the injected refusal. Grants are the
// 1-based call ordinal, so slot 0 〜 the fallback texture 〜 is never handed out and an index that
// aliased onto it is distinguishable from every real grant.
uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta, VkImageView view, VkSampler sampler)
{
    (void)ctx; (void)bta;
    if (g_bindCalls < MAX_BINDLESS) {
        g_bindView[g_bindCalls] = view;
        g_bindSampler[g_bindCalls] = sampler;
    }
    g_bindCalls++;
    if (g_refuseBind == g_bindCalls) return ANO_BINDLESS_NONE;
    return g_bindCalls;
}

PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state)
{ (void)state; return (PbrFeatureFlags)0xFFFFFFFFu; }

// The real default writes the no-texture sentinel into every texture field (components.c:139), so
// a slot the bake never touches reads as absent rather than as bindless slot 0.
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

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx) { (void)ctx; return (VkCommandBuffer)(uintptr_t)0x54; }
void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer) { (void)ctx; (void)commandBuffer; }
void gpu_alloc_reset(GpuAllocator* alloc) { (void)alloc; }
void multiplyMat4(mat4 result, const mat4 a, const mat4 b) { (void)result; (void)a; (void)b; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)buffer; (void)pAllocator; }
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)image; (void)pAllocator; }
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)imageView; (void)pAllocator; }


/* Fixture 〜 five textures over four materials, one primitive each so every material bakes a row:
     t0  colour-only        (baseColorTexture of material B)
     t1  data-only          (metallicRoughnessTexture of material A)
     t2  mixed ACROSS two   (baseColorTexture of A, metallicRoughnessTexture of B)
     t3  mixed WITHIN one   (both slots of material C)
     t4  never constructed  (its image carries no uri) 〜 both slots of material D
   Material A is row 0 and material B is row 1: A's colour index and B's data index name the same
   image through different interpretations, and the whole bug is whether they differ. */

#define SCENE "anotest_gltftexdomainguard_scene.gltf"
#define SHARED "anotest_gltftexdomainguard_shared.gltf"

#define ROW_A 0u
#define ROW_B 1u
#define ROW_C 2u
#define ROW_D 3u

static const char* JSON_SCENE =
    "{\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{\"byteLength\":18,\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAA\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12},"
                     "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":6}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
                   "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"images\":[{\"uri\":\"anotest_gltfdom_t0.png\"},{\"uri\":\"anotest_gltfdom_t1.png\"},"
                "{\"uri\":\"anotest_gltfdom_t2.png\"},{\"uri\":\"anotest_gltfdom_t3.png\"},"
                "{\"name\":\"no-uri\"}],"
    "\"textures\":[{\"source\":0},{\"source\":1},{\"source\":2},{\"source\":3},{\"source\":4}],"
    "\"materials\":["
        "{\"name\":\"A\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":2},"
                                                  "\"metallicRoughnessTexture\":{\"index\":1}}},"
        "{\"name\":\"B\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
                                                  "\"metallicRoughnessTexture\":{\"index\":2}}},"
        "{\"name\":\"C\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":3},"
                                                  "\"metallicRoughnessTexture\":{\"index\":3}}},"
        "{\"name\":\"D\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":4},"
                                                  "\"metallicRoughnessTexture\":{\"index\":4}}}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0},"
                                 "{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":1},"
                                 "{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":2},"
                                 "{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":3}]}],"
    "\"nodes\":[{\"mesh\":0}],"
    "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

// FOUR textures over ONE image: tex 0 colour and tex 1 data (so the image's mask unions to both),
// tex 3 colour again, and tex 2 colour under a texture-level CLAMP sampler declaration 〜 differing
// sampler declarations must not split one image's slots, because the image is what carries them.
static const char* JSON_SHARED =
    "{\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{\"byteLength\":18,\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAA\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12},"
                     "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":6}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
                   "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"images\":[{\"uri\":\"anotest_gltfdom_t0.png\"}],"
    "\"samplers\":[{\"wrapS\":33071}],"
    "\"textures\":[{\"source\":0},{\"source\":0},{\"source\":0,\"sampler\":0},{\"source\":0}],"
    "\"materials\":["
        "{\"name\":\"A\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}},"
        "{\"name\":\"B\",\"pbrMetallicRoughness\":{\"metallicRoughnessTexture\":{\"index\":1}}},"
        "{\"name\":\"C\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":2}}},"
        "{\"name\":\"D\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":3}}}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0},"
                                 "{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":1},"
                                 "{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":2},"
                                 "{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":3}]}],"
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


/* Material rows 〜 colorIndex/dataIndex are private scratch freed with the parser's scope; the
   baked SSBO row is the only surface they reach. */

#define MAT_ROWS 8
static MaterialData g_rows[MAX_FRAMES_IN_FLIGHT][MAT_ROWS];

// Points the material buffer at real host storage and clears every per-run observation.
static void arm_run(void)
{
    memset(g_rows, 0, sizeof g_rows);
    for (size_t f = 0; f < MAX_FRAMES_IN_FLIGHT; ++f)
        rendererState.materialBuffer.mapped[f] = g_rows[f];
    rendererState.materialBuffer.capacity = MAT_ROWS;
    rendererState.materialBuffer.count = 0;

    memset(g_usage, 0, sizeof g_usage);
    memset(g_built, 0, sizeof g_built);
    memset(g_srgb, 0, sizeof g_srgb);
    memset(g_unorm, 0, sizeof g_unorm);
    memset(g_bindView, 0, sizeof g_bindView);
    memset(g_bindSampler, 0, sizeof g_bindSampler);
    rendererState.textureSampler = (VkSampler)(uintptr_t)0x900u; // the engine's one sampler
    g_texCalls = g_bindCalls = 0;
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

    // the split itself: usage masks, per-domain view registration, and the baked indices
    {
        arm_run();
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "the four-material scene parses");
        CHECK(g_texCalls == 4, "the four URI-bearing textures reach the constructor");

        CHECK(g_usage[0] == TEXTURE_USE_COLOR, "a texture read only as baseColorTexture asks for COLOR alone");
        CHECK(g_usage[1] == TEXTURE_USE_DATA, "a texture read only as metallicRoughnessTexture asks for DATA alone");
        CHECK(g_usage[2] == (TEXTURE_USE_COLOR | TEXTURE_USE_DATA),
              "a texture read in both domains ACROSS two materials asks for both 〜 the mask is a union, not a last writer");
        CHECK(g_usage[3] == (TEXTURE_USE_COLOR | TEXTURE_USE_DATA),
              "a texture read in both domains WITHIN one material asks for both");
        CHECK(!g_built[4], "a texture with no image uri is never constructed");

        CHECK(g_bindCalls == 6, "six registrations 〜 one per single-domain texture, two per mixed one");
        CHECK(binds_of(g_srgb[0]) == 1 && g_unorm[0] == VK_NULL_HANDLE,
              "the colour-only texture drives exactly one registration");
        CHECK(binds_of(g_unorm[1]) == 1 && g_srgb[1] == VK_NULL_HANDLE,
              "the data-only texture drives exactly one registration");
        CHECK(g_srgb[2] != g_unorm[2] && binds_of(g_srgb[2]) == 1 && binds_of(g_unorm[2]) == 1,
              "the mixed texture drives TWO registrations with two DISTINCT view handles");
        CHECK(g_srgb[3] != g_unorm[3] && binds_of(g_srgb[3]) == 1 && binds_of(g_unorm[3]) == 1,
              "mixed within one material likewise registers both views");

        // the inequality that IS the bug: one image, two interpretations, two indices
        uint32_t colourOfMixed = g_rows[0][ROW_A].baseColorTexture;
        uint32_t dataOfMixed   = g_rows[0][ROW_B].metallicRoughnessTexture;
        CHECK(colourOfMixed != ANO_BINDLESS_NONE, "material A's baseColorTexture carries a real colour index");
        CHECK(dataOfMixed != ANO_BINDLESS_NONE, "material B's metallicRoughnessTexture carries a real data index");
        if (colourOfMixed == dataOfMixed)
            printf("  (both interpretations of the shared image baked index %" PRIu32
                   " 〜 every data read of it comes back sampler-decoded)\n", colourOfMixed);
        CHECK(colourOfMixed != dataOfMixed,
              "the colour and data interpretations of ONE image bake DIFFERENT bindless indices");

        CHECK(g_rows[0][ROW_A].metallicRoughnessTexture != ANO_BINDLESS_NONE,
              "material A's data slot carries the data-only texture");
        CHECK(g_rows[0][ROW_B].baseColorTexture != ANO_BINDLESS_NONE,
              "material B's colour slot carries the colour-only texture");
        CHECK(g_rows[0][ROW_C].baseColorTexture != g_rows[0][ROW_C].metallicRoughnessTexture,
              "mixed within one material bakes two different indices into that material's two slots");
        CHECK(g_rows[0][ROW_D].baseColorTexture == ANO_BINDLESS_NONE &&
              g_rows[0][ROW_D].metallicRoughnessTexture == ANO_BINDLESS_NONE,
              "a texture never constructed leaves BOTH cells at ANO_BINDLESS_NONE");
        free(asset);
    }

    // refusal matrix, data side: the mixed texture's SECOND registration is refused
    {
        arm_run();
        g_refuseBind = 4; // t0 colour, t1 data, t2 colour, [t2 data]
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "refused data slot: the parse survives");
        uint32_t colour = g_rows[0][ROW_A].baseColorTexture;
        uint32_t dat    = g_rows[0][ROW_B].metallicRoughnessTexture;
        CHECK(g_bindView[3] == g_unorm[2], "refused data slot: the refusal landed on the data view");
        CHECK(colour != ANO_BINDLESS_NONE && colour != 0u,
              "refused data slot: the colour interpretation keeps its real index");
        CHECK(dat == ANO_BINDLESS_NONE,
              "refused data slot: the data interpretation holds ANO_BINDLESS_NONE");
        CHECK(dat != colour, "refused data slot: the refusal never aliases onto the colour index");
        CHECK(dat != 0u, "refused data slot: the refusal never aliases onto slot 0, the fallback texture");
        free(asset);
    }

    // refusal matrix, colour side: the mirror case, the mixed texture's FIRST registration refused
    {
        arm_run();
        g_refuseBind = 3; // t0 colour, t1 data, [t2 colour], t2 data
        ModelAsset* asset = parseGltf(&ctx, SCENE);
        CHECK(asset != NULL, "refused colour slot: the parse survives");
        uint32_t colour = g_rows[0][ROW_A].baseColorTexture;
        uint32_t dat    = g_rows[0][ROW_B].metallicRoughnessTexture;
        CHECK(g_bindView[2] == g_srgb[2], "refused colour slot: the refusal landed on the colour view");
        CHECK(colour == ANO_BINDLESS_NONE,
              "refused colour slot: the colour interpretation holds ANO_BINDLESS_NONE");
        CHECK(dat == ANO_BINDLESS_NONE && g_bindCalls == 3,
              "refused colour slot: the halt is taken before the data registration 〜 the array that refused one view refuses the next");
        CHECK(colour != 0u && dat != 0u,
              "refused colour slot: neither cell aliases onto slot 0, the fallback texture");
        free(asset);
    }

    // Consolidation: FOUR textures over ONE image. Keying construction by texture built one VkImage,
    // one arena span and one mip chain per texture, and split an image reached through two textures
    // into two of everything 〜 which is the very case the mutable-format work exists to merge.
    {
        arm_run();
        if (!write_fixture(SHARED, JSON_SHARED)) {
            printf("FAIL: could not write the shared-image fixture\n");
            return 1;
        }
        ModelAsset* asset = parseGltf(&ctx, SHARED);
        CHECK(asset != NULL, "the shared-image scene parses");

        CHECK(g_texCalls == 1,
              "ONE image behind four textures is constructed ONCE 〜 one decode, one VkImage, one arena span, one mip chain");
        CHECK(g_usage[0] == (TEXTURE_USE_COLOR | TEXTURE_USE_DATA),
              "the image's mask unions the roles of every texture naming it, so it carries both views");

        uint32_t a = g_rows[0][ROW_A].baseColorTexture;            // tex 0, default sampler, colour
        uint32_t b = g_rows[0][ROW_B].metallicRoughnessTexture;    // tex 1, default sampler, data
        uint32_t c = g_rows[0][ROW_C].baseColorTexture;            // tex 2, CLAMP sampler, colour
        uint32_t d = g_rows[0][ROW_D].baseColorTexture;            // tex 3, default sampler, colour

        CHECK(a != ANO_BINDLESS_NONE && b != ANO_BINDLESS_NONE
              && c != ANO_BINDLESS_NONE && d != ANO_BINDLESS_NONE,
              "every texture over the shared image bakes a real index");
        CHECK(a == c && a == d, "every material reading the shared image as colour bakes the SAME index, whatever sampler its texture declares");
        CHECK(a != b, "the colour and data interpretations of the shared image stay distinct");
        CHECK(g_bindCalls == 2, "at most one colour index and one data index per image 〜 the array is bump-only, so a duplicate slot is a permanent loss");
        CHECK(binds_of(g_srgb[0]) == 1 && binds_of(g_unorm[0]) == 1 && g_bindSampler[0] == rendererState.textureSampler, "each of the image's two views is registered once, under the engine's one sampler");
        free(asset);
        remove(SHARED);
    }

    remove(SCENE);

    if (failures) {
        printf("anotest_gltftexdomainguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_gltftexdomainguard: all passed\n");
    return 0;
}
