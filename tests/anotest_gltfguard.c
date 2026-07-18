/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: parseGltf's handling of a glTF whose file-sourced accessor/bufferView byte ranges
// lie. The loader goes straight from cgltf_parse_file/cgltf_load_buffers to per-element
// cgltf_accessor_read_float/read_index (ano_GltfParser.c:80/:96) without ever calling
// cgltf_validate 〜 and those helpers compute buffer->data + view->offset + accessor->offset +
// stride * index with NO bounds check; cgltf_validate is the library's only accessor-vs-view
// and view-vs-buffer byte-range gate (docs/BUGS.md, Render / Implementation, ano_GltfParser.c:30).
// So a POSITION accessor claiming 8 vertices over a 12-byte buffer reads 84 bytes past the
// loaded heap block and parseGltf hands back a non-NULL "successfully parsed" asset instead of
// rejecting the file like its own :25/:31 error paths do. The triggers keep the overrun small
// (<100 bytes past a live mimalloc block) so the lie is observed as a wrong return value, not
// as a crash inside this test. A control pins the valid-file path (counts, flatten, upload
// plumbing) so a reject-everything fix cannot pass. GPU entry points are stubbed: the parser's
// CPU path 〜 the code under test 〜 runs for real; no device, no window. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "render/gltf/ano_GltfParser.h"
#include "vulkan_backend/geometry.h"
#include "vulkan_backend/components.h"
#include "vulkan_backend/gpu_alloc.h"
#include "vulkan_backend/texture/texture.h"

#include "templates/scratch.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// ---------------------------------------------------------------------------
// Link-time environment: the parser's two globals plus stubs for every GPU
// entry point it calls. Uploads are recorded, never performed.
// ---------------------------------------------------------------------------

GpuAllocator  stagingAllocator;
RendererState rendererState;

static uint32_t g_uploadCalls = 0;
static uint32_t g_lastVertexCount = 0;
static uint32_t g_lastIndexCount = 0;

// Inputs: lodCount. Output: minimal single-level config (parser only forwards it).
AnoLodConfig ano_lod_config_default(uint32_t lodCount)
{
    AnoLodConfig cfg = {0};
    cfg.lodCount = lodCount ? lodCount : 1u;
    cfg.ratios[0] = 1.0f;
    return cfg;
}

// Records the counts the parser extracted; hands back a distinct chain base per call.
uint32_t geometry_pool_upload_chain(GeometryPool* pool, GpuAllocator* alloc, VkDevice device,
                                    uint32_t transferFamily, VkQueue transferQueue,
                                    const Vertex* vertices, uint32_t vertexCount,
                                    const uint32_t* indices, uint32_t indexCount,
                                    const AnoLodConfig* config,
                                    uint32_t* out_lodBase, uint32_t* out_lodCount)
{
    (void)pool; (void)alloc; (void)device; (void)transferFamily; (void)transferQueue;
    (void)vertices; (void)indices; (void)config;
    g_uploadCalls++;
    g_lastVertexCount = vertexCount;
    g_lastIndexCount  = indexCount;
    uint32_t base = 40u + g_uploadCalls;
    if (out_lodBase)  *out_lodBase = base;
    if (out_lodCount) *out_lodCount = 1u;
    return base;
}

PbrFeatureFlags ano_vk_get_active_pipelines_supported_features(const struct RendererState* state)
{
    (void)state;
    return PBR_FEATURE_NONE;
}

void ano_vk_init_default_material_data(struct MaterialData* mat)
{
    memset(mat, 0, sizeof *mat);
}

VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx)
{
    (void)ctx;
    return VK_NULL_HANDLE;
}

void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer)
{
    (void)ctx; (void)commandBuffer;
}

bool createTextureImage(VulkanContext* ctx, VkCommandBuffer cmd, VkImage* textureImage,
                        GpuAllocation* textureImageAlloc, VkImageView* textureImageView,
                        char* fileName, bool flag16, bool srgb, VkBuffer* outStagingBuffer)
{
    (void)ctx; (void)cmd; (void)textureImage; (void)textureImageAlloc; (void)textureImageView;
    (void)fileName; (void)flag16; (void)srgb; (void)outStagingBuffer;
    return false; // test scenes carry no textures; never reached
}

void ano_vk_register_texture(RenderPrimitives* primitives, TextureData data)
{
    (void)primitives; (void)data;
}

uint32_t bindless_register_texture(VulkanContext* ctx, BindlessTextureArray* bta,
                                   VkImageView view, VkSampler sampler)
{
    (void)ctx; (void)bta; (void)view; (void)sampler;
    return 0u;
}

void gpu_alloc_reset(GpuAllocator* alloc)
{
    (void)alloc;
}

// Column-major 4x4 multiply, result = a * b. Temp copy tolerates aliasing.
// model_flatten's only math dependency; the control uses identities so any
// consistent convention observes the same output.
void multiplyMat4(mat4 result, const mat4 a, const mat4 b)
{
    mat4 tmp;
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += a[k][r] * b[c][k];
            tmp[c][r] = s;
        }
    memcpy(result, tmp, sizeof(mat4));
}

// ---------------------------------------------------------------------------
// Fixture files. Buffers are inline base64 data URIs (all-zero payloads), so
// each scene is one self-contained .gltf in the scratch dir.
// ---------------------------------------------------------------------------

#define SCRATCH_DIR   ANO_TEST_OUTDIR "/anoscratch_gltfguard"
#define PATH_CONTROL  SCRATCH_DIR "/control.gltf"
#define PATH_ACCESSOR SCRATCH_DIR "/accessor_overrun.gltf"
#define PATH_VIEW     SCRATCH_DIR "/view_overrun.gltf"

// Valid scene: 18-byte buffer = 1 VEC3 float vertex (view 0, 12 B) + 3 u16 indices
// (view 1, 6 B). Passes cgltf_validate, so it must survive any fix.
static const char *JSON_CONTROL =
    "{\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{\"byteLength\":18,\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAA\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12},"
                     "{\"buffer\":0,\"byteOffset\":12,\"byteLength\":6}],"
    "\"accessors\":[{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":1,\"type\":\"VEC3\"},"
                   "{\"bufferView\":1,\"byteOffset\":0,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
    "\"nodes\":[{\"mesh\":0}],"
    "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

// Trigger: POSITION accessor claims 8 VEC3 floats (96 B) against a 12-byte view over a
// 12-byte buffer; the vertex loop reads 84 bytes past the loaded heap block.
static const char *JSON_ACCESSOR_OVERRUN =
    "{\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{\"byteLength\":12,\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAA\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":12}],"
    "\"accessors\":[{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\"},"
                   "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
    "\"nodes\":[{\"mesh\":0}],"
    "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

// Trigger: bufferView claims 60 bytes of a 12-byte buffer; the accessor (5 VEC3 = 60 B) fits
// the VIEW, isolating the missing view-vs-buffer gate. Reads 48 bytes past the block.
static const char *JSON_VIEW_OVERRUN =
    "{\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{\"byteLength\":12,\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAA\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":60}],"
    "\"accessors\":[{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":5,\"type\":\"VEC3\"},"
                   "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
    "\"nodes\":[{\"mesh\":0}],"
    "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";

// Inputs: path, contents. Output: true if fully written.
static bool write_fixture(const char *path, const char *json)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    size_t len = strlen(json);
    bool ok = fwrite(json, 1, len, f) == len;
    fclose(f);
    return ok;
}

int main(void)
{
    scratch_anchor_to_exe();
    scratch_make_dir(SCRATCH_DIR);

    if (!write_fixture(PATH_CONTROL, JSON_CONTROL) ||
        !write_fixture(PATH_ACCESSOR, JSON_ACCESSOR_OVERRUN) ||
        !write_fixture(PATH_VIEW, JSON_VIEW_OVERRUN)) {
        printf("FAIL: could not write fixture files\n");
        return 1;
    }

    VulkanContext ctx = {0};

    // control: a contract-clean file parses, uploads the real counts, and flattens
    {
        ModelAsset *good = parseGltf(&ctx, PATH_CONTROL);
        CHECK(good != NULL, "valid glTF parses to an asset");
        if (good) {
            CHECK(good->meshCount == 1, "control asset holds one mesh");
            CHECK(good->nodeCount == 1, "control asset holds one node");
            CHECK(g_uploadCalls == 1, "one primitive uploaded");
            CHECK(g_lastVertexCount == 1 && g_lastIndexCount == 3, "upload sees the file's true counts");

            mat4 ident = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
            AnoRenderableDesc desc[4];
            CHECK(model_flatten(good, ident, NULL, 0) == 1, "flatten sizes one primitive");
            CHECK(model_flatten(good, ident, desc, 4) == 1, "flatten fills one primitive");
            CHECK(desc[0].mesh_index == 41u, "flatten reports the uploaded chain base");
        }
    }

    // trigger: accessor byte range overruns its bufferView 〜 cgltf_validate's accessor gate
    {
        uint32_t uploadsBefore = g_uploadCalls;
        ModelAsset *bad = parseGltf(&ctx, PATH_ACCESSOR);
        CHECK(bad == NULL, "parseGltf rejects a POSITION accessor overrunning its bufferView");
        if (bad == NULL)
            CHECK(g_uploadCalls == uploadsBefore, "no geometry uploaded from out-of-bounds reads");
    }

    // trigger: bufferView byte range overruns its buffer 〜 the view-vs-buffer gate
    {
        uint32_t uploadsBefore = g_uploadCalls;
        ModelAsset *bad = parseGltf(&ctx, PATH_VIEW);
        CHECK(bad == NULL, "parseGltf rejects a bufferView overrunning its buffer");
        if (bad == NULL)
            CHECK(g_uploadCalls == uploadsBefore, "no geometry uploaded from out-of-bounds reads");
    }

    remove(PATH_CONTROL);
    remove(PATH_ACCESSOR);
    remove(PATH_VIEW);
    scratch_remove_dir(SCRATCH_DIR);

    if (failures) {
        printf("anotest_gltfguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_gltfguard: all passed\n");
    return 0;
}
