/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the unwind contract of createTextureImage and createTextureImageFromPixels. A package
// constructor acquires up to four objects 〜 staging buffer, image, sRGB view, UNORM view 〜 and
// every refusal below the first of them must leave *pkg inert, discharge exactly what that call
// acquired, and answer the AnoTextureResult code the loader classifies on: SOURCE is this asset's
// problem and the next texture may still load, DEVICE is the whole load's problem because gpu_alloc
// is monotonic, INVALID is the call itself. A miscoded arm is therefore a real defect, not a label.
// The arm this guard exists for did not exist before the two-view package: mixed usage with the
// SECOND createImageView refusing, where an already-live sRGB view must be discharged by an unwind
// no caller can see. keepStaging is true on every refusal arm, so a hand-out that fires on failure
// shows up as a live buffer with an inert package.
// Harness: compiles the REAL texture.c TU and satisfies its link seams with stubs that ledger every
// buffer, image and view handle they issue 〜 no GPU device, no loader. Real stbi_load decodes a
// real TGA this test writes beside its CWD. Every injection is contract-faithful: vkCreateImage and
// vkBindImageMemory refuse with OOM, gpu_alloc answers the empty allocation, createImageView answers
// VK_NULL_HANDLE, and beginSingleTimeCommands answers VK_NULL_HANDLE on its Nth call, which is
// transient-pool exhaustion and reaches the transition, copy and mip-chain arms when cmd is
// VK_NULL_HANDLE. Controls prove both clean shapes 〜 keepStaging false retires the buffer
// callee-side, keepStaging true hands it out live for the caller to retire 〜 so a reject-everything
// or destroy-everything implementation cannot pass. The create-time mutable-format plan is asserted
// off the same stubs: a mixed mask sets MUTABLE_FORMAT and attaches the two-entry format list, a
// single-role mask sets neither, and the view count matches the mask. Whole-run invariants: nothing
// destroyed twice, nothing destroyed that was never minted, nothing live when a scenario ends.
// Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/texture/texture.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Ledgers 〜 one per handle family the constructors acquire. Vulkan handles are a pointer or a
   uint64_t depending on the target; both fit uint64_t, which is what a ledger stores. */

#define MAX_OBJ 32

typedef struct Ledger {
	const char* what;
	uint64_t    handle[MAX_OBJ];
	bool        live[MAX_OBJ];
	uint32_t    count;
} Ledger;

#define HBITS(h) ((uint64_t)(uintptr_t)(h))

static Ledger g_bufs   = { .what = "staging buffer" };
static Ledger g_images = { .what = "image" };
static Ledger g_views  = { .what = "image view" };

static void*    g_shadow[MAX_OBJ];   // host mapping behind each staging buffer, by mint slot
static uint32_t g_doubleDestroys;    // whole-run invariant
static uint32_t g_unknownDestroys;   // whole-run invariant
static uint32_t g_leaked;            // whole-run invariant: handles alive when a scenario ended

// in: led, base 〜 the first handle value this family issues.
// out: a fresh live handle, or 0 when the ledger is full.
static uint64_t ledger_mint(Ledger* led, uint64_t base)
{
	if (led->count >= MAX_OBJ) { printf("FAIL: %s ledger overflow\n", led->what); failures++; return 0; }
	uint64_t h = base + led->count;
	led->handle[led->count] = h;
	led->live[led->count] = true;
	led->count++;
	return h;
}

// in: led, h. Discharges h; a second discharge or an unminted handle trips a whole-run invariant.
static void ledger_kill(Ledger* led, uint64_t h)
{
	if (h == 0) return; // every vkDestroy* is a spec no-op on VK_NULL_HANDLE
	for (uint32_t i = 0; i < led->count; i++) {
		if (led->handle[i] == h) {
			if (!led->live[i]) g_doubleDestroys++;
			led->live[i] = false;
			return;
		}
	}
	g_unknownDestroys++;
}

// out: how many of led's handles are still live.
static uint32_t ledger_live(const Ledger* led)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < led->count; i++) if (led->live[i]) n++;
	return n;
}

// in: where 〜 the scenario just finished.
// out: how many handles it left live. Reports each family, releases the staging shadows and clears
// the mint ledgers; the run invariants above persist.
static uint32_t ledgers_settle(const char* where)
{
	Ledger* all[3] = { &g_bufs, &g_images, &g_views };
	uint32_t total = 0;
	for (uint32_t l = 0; l < 3; l++) {
		uint32_t n = ledger_live(all[l]);
		if (n) printf("  leak: %s 〜 %" PRIu32 " %s(s) still live\n", where, n, all[l]->what);
		total += n;
	}
	g_leaked += total;
	for (uint32_t i = 0; i < g_bufs.count; i++) { free(g_shadow[i]); g_shadow[i] = NULL; }
	g_bufs   = (Ledger){ .what = "staging buffer" };
	g_images = (Ledger){ .what = "image" };
	g_views  = (Ledger){ .what = "image view" };
	return total;
}


/* Failure injection 〜 each switch spells one refusal a driver or the arena really produces */

static bool     g_failCreateImage;     // vkCreateImage answers OUT_OF_DEVICE_MEMORY
static bool     g_failGpuAlloc;        // the texture arena answers the empty allocation
static bool     g_failBindImageMemory; // vkBindImageMemory answers OUT_OF_DEVICE_MEMORY
static uint32_t g_viewRefuseAt;        // Nth createImageView answers VK_NULL_HANDLE; 0 = healthy
static uint32_t g_viewCalls;
static uint32_t g_beginRefuseAt;       // Nth beginSingleTimeCommands answers VK_NULL_HANDLE; 0 = healthy
static uint32_t g_beginCalls;

// Disarms every switch and rewinds the call counters.
static void injection_reset(void)
{
	g_failCreateImage = g_failGpuAlloc = g_failBindImageMemory = false;
	g_viewRefuseAt = g_viewCalls = g_beginRefuseAt = g_beginCalls = 0;
}

/* What the last vkCreateImage was asked for. The mutable-format plan is create-time only, so this
   is its whole observable surface without a device. */

static VkImageCreateFlags g_lastFlags;
static bool               g_lastHadFormatList;
static uint32_t           g_lastFormatCount;
static VkFormat           g_lastFormats[8];


/* Link seams 〜 texture.c externs (the real definitions live in vulkanMaster.c / instance/, which
   this executable deliberately does not link) */

GpuAllocator textureAllocator;
GpuAllocator stagingAllocator;

// in: size. out: a ledgered buffer handle over a host-backed shadow mapping.
bool createDataBuffer(VulkanContext* ctx, GpuAllocator* allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, GpuAllocation* allocation)
{
	(void)ctx; (void)allocator; (void)usage; (void)properties;
	uint32_t slot = g_bufs.count;
	void* shadow = malloc((size_t)size);
	uint64_t h = ledger_mint(&g_bufs, 0x1000u);
	if (h == 0) { free(shadow); return false; }
	g_shadow[slot] = shadow;
	*allocation = (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x51, .offset = 0, .size = size, .mapped = shadow };
	*buffer = (VkBuffer)(uintptr_t)h;
	return shadow != NULL;
}

// Satisfies the request unless the arena-exhaustion arm is injected.
GpuAllocation gpu_alloc(GpuAllocator* alloc, VkMemoryRequirements reqs, VkMemoryPropertyFlags props)
{
	(void)alloc; (void)props;
	if (g_failGpuAlloc) return (GpuAllocation){0};
	return (GpuAllocation){ .memory = (VkDeviceMemory)(uintptr_t)0x53, .offset = 0, .size = reqs.size, .mapped = NULL };
}

// out: a transient CB, or VK_NULL_HANDLE on the armed call 〜 a transient pool running dry.
VkCommandBuffer beginSingleTimeCommands(VulkanContext* ctx)
{
	(void)ctx;
	if (++g_beginCalls == g_beginRefuseAt) return VK_NULL_HANDLE;
	return (VkCommandBuffer)(uintptr_t)0x54;
}

void endSingleTimeCommands(VulkanContext* ctx, VkCommandBuffer commandBuffer) { (void)ctx; (void)commandBuffer; }
bool hasStencilComponent(VkFormat format) { return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT; }

// out: a ledgered view handle, or VK_NULL_HANDLE on the armed call. Every grant is distinct, so the
// two interpretations of one image can never alias each other.
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
	(void)device; (void)image; (void)format; (void)aspectFlags; (void)mipLevels;
	if (++g_viewCalls == g_viewRefuseAt) return VK_NULL_HANDLE;
	return (VkImageView)(uintptr_t)ledger_mint(&g_views, 0x2000u);
}


/* Link seams 〜 the vk* entry points texture.c calls (loader not linked) */

// Records the create-time format plan before honouring any injection, so a refused create is still
// observable. Out-param stays undefined on error, per spec.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{
	(void)device; (void)pAllocator;
	g_lastFlags = pCreateInfo->flags;
	g_lastHadFormatList = false;
	g_lastFormatCount = 0;
	memset(g_lastFormats, 0, sizeof g_lastFormats);
	for (const VkBaseInStructure* p = pCreateInfo->pNext; p != NULL; p = p->pNext) {
		if (p->sType != VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) continue;
		const VkImageFormatListCreateInfo* list = (const VkImageFormatListCreateInfo*)p;
		g_lastHadFormatList = true;
		g_lastFormatCount = list->viewFormatCount;
		for (uint32_t i = 0; i < list->viewFormatCount && i < 8; i++) g_lastFormats[i] = list->pViewFormats[i];
	}
	if (g_failCreateImage) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	*pImage = (VkImage)(uintptr_t)ledger_mint(&g_images, 0x3000u);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{ (void)device; (void)image; pMemoryRequirements->size = 1u << 20; pMemoryRequirements->alignment = 256; pMemoryRequirements->memoryTypeBits = 1; }

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{ (void)device; (void)image; (void)memory; (void)memoryOffset; return g_failBindImageMemory ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
{ (void)commandBuffer; (void)srcStageMask; (void)dstStageMask; (void)dependencyFlags; (void)memoryBarrierCount; (void)pMemoryBarriers; (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers; (void)imageMemoryBarrierCount; (void)pImageMemoryBarriers; }

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{ (void)commandBuffer; (void)srcBuffer; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions; }

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
{ (void)commandBuffer; (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout; (void)regionCount; (void)pRegions; (void)filter; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
{ (void)physicalDevice; (void)format; memset(pFormatProperties, 0, sizeof *pFormatProperties); pFormatProperties->optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT; }

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pAllocator; ledger_kill(&g_bufs, HBITS(buffer)); }

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pAllocator; ledger_kill(&g_images, HBITS(image)); }

// Ledger-backed on purpose: a view the unwind forgets is invisible against a no-op stub, and the
// two-view package is exactly where that forgetting became possible.
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)pAllocator; ledger_kill(&g_views, HBITS(imageView)); }

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
{ (void)device; (void)descriptorWriteCount; (void)pDescriptorWrites; (void)descriptorCopyCount; (void)pDescriptorCopies; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler)
{ (void)device; (void)pCreateInfo; (void)pAllocator; *pSampler = (VkSampler)(uintptr_t)0x61; return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator)
{ (void)device; (void)sampler; (void)pAllocator; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties)
{ (void)physicalDevice; memset(pProperties, 0, sizeof *pProperties); }


/* Fixtures and package helpers */

// Writes a w x h uncompressed 32-bit true-color TGA (top-left origin) for the real stbi_load.
static bool write_tga(const char* path, int w, int h)
{
	unsigned char hdr[18] = {0};
	hdr[2]  = 2; // uncompressed true-color
	hdr[12] = (unsigned char)(w & 0xFF); hdr[13] = (unsigned char)((w >> 8) & 0xFF);
	hdr[14] = (unsigned char)(h & 0xFF); hdr[15] = (unsigned char)((h >> 8) & 0xFF);
	hdr[16] = 32;   // bits per pixel
	hdr[17] = 0x28; // top-left origin, 8 alpha bits
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	fwrite(hdr, 1, sizeof hdr, f);
	for (int i = 0; i < w * h; i++) {
		unsigned char px[4] = { (unsigned char)i, 0x40, 0x80, 0xFF }; // BGRA in-file
		fwrite(px, 1, sizeof px, f);
	}
	fclose(f);
	return true;
}

// Writes a blob no stbi decoder claims, so the decode seam refuses on content rather than on access.
static bool write_junk(const char* path)
{
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	fwrite("anoptic-not-an-image", 1, 20, f);
	fclose(f);
	return true;
}

// Fills pkg with a non-zero pattern, so "inert" proves the callee wrote its own total zero rather
// than the caller's zeroing showing through.
static void poison(TexturePackage* pkg) { memset(pkg, 0xA5, sizeof *pkg); }

// out: true iff nothing was published into pkg.
static bool pkg_inert(const TexturePackage* p)
{
	return p->image == VK_NULL_HANDLE && p->srgbView == VK_NULL_HANDLE && p->unormView == VK_NULL_HANDLE &&
	       p->staging == VK_NULL_HANDLE && p->alloc.memory == VK_NULL_HANDLE && p->alloc.offset == 0 &&
	       p->alloc.size == 0 && p->alloc.mapped == NULL &&
	       p->mipLevels == 0 && p->width == 0 && p->height == 0;
}

// in: ctx, pkg 〜 the caller's obligation over a BUILT package: discharge every handle it owns.
// Each deallocator is a spec no-op on VK_NULL_HANDLE, so an absent view costs nothing.
static void discharge(VulkanContext* ctx, TexturePackage* pkg)
{
	vkDestroyBuffer(ctx->device, pkg->staging, NULL);
	vkDestroyImageView(ctx->device, pkg->unormView, NULL);
	vkDestroyImageView(ctx->device, pkg->srgbView, NULL);
	vkDestroyImage(ctx->device, pkg->image, NULL);
	*pkg = (TexturePackage){0};
}

// in: r, pkg from a refused construction; want 〜 the code the arm owes; name 〜 the arm.
// The whole refusal contract in one place: right code, inert package, empty ledgers.
static void expect_refusal(AnoTextureResult r, const TexturePackage* pkg, AnoTextureResultCode want, const char* name)
{
	char msg[192];
	if (r.code != want) printf("  (%s answered code %d, wanted %d)\n", name, (int)r.code, (int)want);
	snprintf(msg, sizeof msg, "%s: answers its own code", name);
	CHECK(r.code == want, msg);
	snprintf(msg, sizeof msg, "%s: leaves *pkg inert", name);
	CHECK(pkg_inert(pkg), msg);
	snprintf(msg, sizeof msg, "%s: discharges everything it acquired", name);
	CHECK(ledgers_settle(name) == 0, msg);
}


int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	static VulkanContext ctx; // zeroed; every seam stub ignores its handles
	VkCommandBuffer borrowed = (VkCommandBuffer)(uintptr_t)0x70;
	TexturePackage pkg;
	AnoTextureResult r;

	char sq[]   = "anotest_texunwind_4x4.tga";
	char junk[] = "anotest_texunwind_junk.tga";
	CHECK(write_tga(sq, 4, 4), "square TGA written");
	CHECK(write_junk(junk), "undecodable fixture written");

	static unsigned char px[4 * 4 * 4];
	for (uint32_t i = 0; i < sizeof px; i++) px[i] = (unsigned char)(0xA0 ^ i);

	/* Controls 〜 both clean shapes, so neither rejecting everything nor destroying everything passes */

	// keepStaging false with no borrow: the buffer is retired callee-side, one image behind one view
	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, VK_NULL_HANDLE, &pkg, sq, false, TEXTURE_USE_COLOR, false);
	CHECK(r.code == ANO_TEXTURE_BUILT, "control: colour-only file load is BUILT");
	CHECK(pkg.staging == VK_NULL_HANDLE && g_bufs.count == 1 && ledger_live(&g_bufs) == 0, "control: keepStaging=false retires the staging buffer callee-side");
	CHECK(pkg.image != VK_NULL_HANDLE && ledger_live(&g_images) == 1, "control: one live image is published");
	CHECK(pkg.srgbView != VK_NULL_HANDLE && pkg.unormView == VK_NULL_HANDLE, "control: colour-only builds the sRGB view alone");
	CHECK(g_views.count == 1 && ledger_live(&g_views) == 1, "control: colour-only mints exactly one view");
	CHECK((g_lastFlags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) == 0 && !g_lastHadFormatList, "control: colour-only asks for neither MUTABLE_FORMAT nor a format list");
	CHECK(pkg.width == 4 && pkg.height == 4 && pkg.mipLevels == 3, "control: the package carries the decoded extents and its mip count");
	discharge(&ctx, &pkg);
	CHECK(ledgers_settle("colour-only control") == 0, "control: the caller's discharge balances every ledger");

	// keepStaging true with a borrow: the copy that CB carries reads the buffer until the caller
	// submits, so the buffer comes out live and only the caller may retire it
	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR, true);
	CHECK(r.code == ANO_TEXTURE_BUILT, "control: borrowed-CB load is BUILT");
	CHECK(pkg.staging != VK_NULL_HANDLE && ledger_live(&g_bufs) == 1, "control: keepStaging=true hands the staging buffer out live");
	CHECK(g_beginCalls == 0, "control: a borrowed CB mints no transient command buffers");
	discharge(&ctx, &pkg);
	CHECK(ledgers_settle("keepStaging control") == 0, "control: the caller retires the handed-out staging buffer");

	/* Structural 〜 the create-time format plan and the view count follow the usage mask */

	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_DATA, true);
	CHECK(r.code == ANO_TEXTURE_BUILT, "data-only file load is BUILT");
	CHECK(pkg.unormView != VK_NULL_HANDLE && pkg.srgbView == VK_NULL_HANDLE, "data-only builds the UNORM view alone");
	CHECK(g_views.count == 1, "data-only mints exactly one view");
	CHECK((g_lastFlags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) == 0 && !g_lastHadFormatList, "data-only asks for neither MUTABLE_FORMAT nor a format list");
	discharge(&ctx, &pkg);
	CHECK(ledgers_settle("data-only") == 0, "data-only balances every ledger");

	// One mutable-format image over one allocation, carrying two distinct views and the explicit
	// two-entry alias list the driver plans against instead of the whole compatibility class
	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR | TEXTURE_USE_DATA, true);
	CHECK(r.code == ANO_TEXTURE_BUILT, "mixed-usage file load is BUILT");
	CHECK(pkg.srgbView != VK_NULL_HANDLE && pkg.unormView != VK_NULL_HANDLE, "mixed usage builds both views");
	CHECK(pkg.srgbView != pkg.unormView, "mixed usage's two views are distinct handles");
	CHECK(g_images.count == 1 && g_bufs.count == 1, "mixed usage uploads one image once, not one per view");
	CHECK((g_lastFlags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) != 0, "mixed usage sets MUTABLE_FORMAT");
	// The colour-only control above takes three levels off this same 4x4 source; a blit chain filters in one format's space, so any level below the top would be wrong through one of the two views.
	CHECK(pkg.mipLevels == 1, "mixed usage publishes a single mip level, so both views read exact samples");
	CHECK(g_lastHadFormatList && g_lastFormatCount == 2, "mixed usage attaches a two-entry format list");
	bool listOk = g_lastHadFormatList && g_lastFormatCount == 2 &&
		((g_lastFormats[0] == VK_FORMAT_R8G8B8A8_SRGB  && g_lastFormats[1] == VK_FORMAT_R8G8B8A8_UNORM) ||
		 (g_lastFormats[0] == VK_FORMAT_R8G8B8A8_UNORM && g_lastFormats[1] == VK_FORMAT_R8G8B8A8_SRGB));
	CHECK(listOk, "the format list names exactly the sRGB and the UNORM interpretation");
	discharge(&ctx, &pkg);
	CHECK(ledgers_settle("mixed usage") == 0, "mixed usage balances every ledger");

	// The sibling pixel face carries the same shapes over a caller-supplied buffer
	injection_reset(); poison(&pkg);
	r = createTextureImageFromPixels(&ctx, VK_NULL_HANDLE, &pkg, px, 4, 4, TEXTURE_USE_COLOR | TEXTURE_USE_DATA, false);
	CHECK(r.code == ANO_TEXTURE_BUILT, "control: pixel upload is BUILT");
	CHECK(g_bufs.count == 1 && ledger_live(&g_bufs) == 0, "control: the pixel face retires its staging buffer callee-side");
	CHECK(pkg.mipLevels == 1 && pkg.width == 4 && pkg.height == 4, "control: the pixel face publishes a single-level chain");
	CHECK(pkg.srgbView != VK_NULL_HANDLE && pkg.unormView != VK_NULL_HANDLE && pkg.srgbView != pkg.unormView, "control: the pixel face builds both distinct views");
	CHECK((g_lastFlags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) != 0 && g_lastFormatCount == 2, "control: the pixel face plans the same mutable-format image");
	discharge(&ctx, &pkg);
	CHECK(ledgers_settle("pixel control") == 0, "control: the pixel face balances every ledger");

	/* Refusal arms 〜 every one owes its code, an inert package, and a total discharge */

	printf("arms: keepStaging is true throughout, so a hand-out that fires on failure shows as a live buffer\n");

	// Inside createImageShared: the image never exists, exists unbacked, or is refused its binding
	injection_reset(); poison(&pkg); g_failCreateImage = true;
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "vkCreateImage refusal");

	injection_reset(); poison(&pkg); g_failGpuAlloc = true;
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "texture arena refusal");

	injection_reset(); poison(&pkg); g_failBindImageMemory = true;
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "vkBindImageMemory refusal");

	injection_reset(); poison(&pkg); g_failGpuAlloc = true;
	r = createTextureImageFromPixels(&ctx, borrowed, &pkg, px, 4, 4, TEXTURE_USE_DATA, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "pixel face, texture arena refusal");

	// The colour view refuses first, with the image and the staging buffer already live
	injection_reset(); poison(&pkg); g_viewRefuseAt = 1;
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR, true);
	CHECK(g_views.count == 0 && ledger_live(&g_images) == 0 && ledger_live(&g_bufs) == 0, "colour-view refusal mints no view and keeps nothing else");
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "colour view refusal");

	injection_reset(); poison(&pkg); g_viewRefuseAt = 1;
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_DATA, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "data view refusal");

	// The arm this guard exists for: mixed usage, second view refused. The sRGB view is already
	// live and belongs to nobody 〜 no caller ever sees it 〜 so only the unwind can discharge it.
	printf("arm: mixed usage with the second view refused 〜 the already-live sRGB view must not survive\n");
	injection_reset(); poison(&pkg); g_viewRefuseAt = 2;
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR | TEXTURE_USE_DATA, true);
	CHECK(g_views.count == 1, "second-view refusal leaves exactly one view minted");
	CHECK(ledger_live(&g_views) == 0, "the already-live sRGB view is discharged by the unwind");
	CHECK(ledger_live(&g_images) == 0 && ledger_live(&g_bufs) == 0, "the image and the staging buffer go with it");
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "mixed usage, second view refused");

	injection_reset(); poison(&pkg); g_viewRefuseAt = 2;
	r = createTextureImageFromPixels(&ctx, borrowed, &pkg, px, 4, 4, TEXTURE_USE_COLOR | TEXTURE_USE_DATA, true);
	CHECK(g_views.count == 1 && ledger_live(&g_views) == 0, "pixel face: its already-live sRGB view is discharged too");
	expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, "pixel face, second view refused");

	// Transient-pool exhaustion. No borrow, so each helper mints its own CB and the Nth refusal picks
	// the arm: 1 the pre-copy transition, 2 the copy, 3 the mip chain (the pixel face's third is its
	// post-copy transition instead).
	for (uint32_t n = 1; n <= 3; n++) {
		char name[64];
		snprintf(name, sizeof name, "transient CB refusal #%" PRIu32 " (file)", n);
		injection_reset(); poison(&pkg); g_beginRefuseAt = n;
		r = createTextureImage(&ctx, VK_NULL_HANDLE, &pkg, sq, false, TEXTURE_USE_COLOR | TEXTURE_USE_DATA, true);
		CHECK(g_beginCalls == n, "the refusal lands on the armed mint and nothing is minted after it");
		expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, name);
	}
	for (uint32_t n = 1; n <= 3; n++) {
		char name[64];
		snprintf(name, sizeof name, "transient CB refusal #%" PRIu32 " (pixels)", n);
		injection_reset(); poison(&pkg); g_beginRefuseAt = n;
		r = createTextureImageFromPixels(&ctx, VK_NULL_HANDLE, &pkg, px, 4, 4, TEXTURE_USE_DATA, true);
		CHECK(g_beginCalls == n, "the refusal lands on the armed mint and nothing is minted after it");
		expect_refusal(r, &pkg, ANO_TEXTURE_DEVICE, name);
	}

	// Source refusals 〜 this asset's problem, so the loader may keep going
	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, borrowed, &pkg, "anotest_texunwind_absent.tga", false, TEXTURE_USE_COLOR, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_SOURCE, "missing source file");

	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, borrowed, &pkg, junk, false, TEXTURE_USE_COLOR, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_SOURCE, "undecodable source file");

	injection_reset(); poison(&pkg);
	r = createTextureImageFromPixels(&ctx, borrowed, &pkg, NULL, 4, 4, TEXTURE_USE_COLOR, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_SOURCE, "null pixel source");

	injection_reset(); poison(&pkg);
	r = createTextureImageFromPixels(&ctx, borrowed, &pkg, px, 0, 4, TEXTURE_USE_COLOR, true);
	expect_refusal(r, &pkg, ANO_TEXTURE_SOURCE, "zero-extent pixel source");

	// Contract refusals 〜 the call itself is malformed, so nothing is attempted at all
	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_NONE, true);
	CHECK(g_bufs.count == 0 && g_images.count == 0 && g_views.count == 0, "no-usage refusal acquires nothing");
	expect_refusal(r, &pkg, ANO_TEXTURE_INVALID, "no usage bits (file)");

	injection_reset(); poison(&pkg);
	r = createTextureImageFromPixels(&ctx, borrowed, &pkg, px, 4, 4, TEXTURE_USE_NONE, true);
	CHECK(g_bufs.count == 0 && g_images.count == 0 && g_views.count == 0, "no-usage refusal acquires nothing");
	expect_refusal(r, &pkg, ANO_TEXTURE_INVALID, "no usage bits (pixels)");

	// Accept-form: a bit outside the two the constructor knows must refuse, not slip past on the strength of the COLOR bit beside it.
	injection_reset(); poison(&pkg);
	r = createTextureImage(&ctx, borrowed, &pkg, sq, false, TEXTURE_USE_COLOR | 0x4u, true);
	CHECK(g_bufs.count == 0 && g_images.count == 0 && g_views.count == 0, "unknown-bit refusal acquires nothing");
	expect_refusal(r, &pkg, ANO_TEXTURE_INVALID, "unknown usage bit (file)");

	injection_reset(); poison(&pkg);
	r = createTextureImageFromPixels(&ctx, borrowed, &pkg, px, 4, 4, TEXTURE_USE_COLOR | 0x4u, true);
	CHECK(g_bufs.count == 0 && g_images.count == 0 && g_views.count == 0, "unknown-bit refusal acquires nothing");
	expect_refusal(r, &pkg, ANO_TEXTURE_INVALID, "unknown usage bit (pixels)");

	injection_reset();
	r = createTextureImage(&ctx, borrowed, NULL, sq, false, TEXTURE_USE_COLOR, true);
	CHECK(r.code == ANO_TEXTURE_INVALID, "null destination (file) answers INVALID");
	r = createTextureImageFromPixels(&ctx, borrowed, NULL, px, 4, 4, TEXTURE_USE_COLOR, true);
	CHECK(r.code == ANO_TEXTURE_INVALID, "null destination (pixels) answers INVALID");
	CHECK(ledgers_settle("null destination") == 0, "a null destination acquires nothing");

	/* Whole-run invariants 〜 an unwind must not overshoot the way a missed view undershoots */

	CHECK(g_doubleDestroys == 0, "no handle destroyed twice");
	CHECK(g_unknownDestroys == 0, "no handle destroyed that was never minted");
	CHECK(g_leaked == 0, "no handle left live by any scenario");

	ledgers_settle("exit");
	remove(sq);
	remove(junk);

	if (failures) {
		printf("anotest_texunwindguard: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("anotest_texunwindguard: all passed\n");
	return 0;
}
