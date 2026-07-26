/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_vk_register_texture ownership seam.
// Harness: real components.c with realloc/free mapped to countdown shims. No GPU.
// Cases: healthy batch, refused 0->8 mint, refused 8->16 growth. Exit 0 == pass.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_backend/components.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)


/* Allocator seam 〜 components.c's realloc/free tokens land here, routed to libc */

static int      g_failReallocAt;  // Nth seam realloc fails; 0 = healthy
static uint32_t g_liveBlocks;     // registry heap blocks outstanding

// in: p, n as realloc
// out: libc realloc, or NULL when countdown hits zero
void* anotest_seam_realloc(void* p, size_t n)
{
	if (g_failReallocAt > 0 && --g_failReallocAt == 0) return NULL;
	void* q = realloc(p, n);
	if (q && !p) g_liveBlocks++;
	return q;
}

// in: p as free
void anotest_seam_free(void* p)
{
	if (p) g_liveBlocks--;
	free(p);
}


/* Mint ledger 〜 every fake GPU object offered to the registration seam */

#define MAX_TEX 32
static VkImage     g_img[MAX_TEX];
static VkImageView g_srgb[MAX_TEX];
static VkImageView g_unorm[MAX_TEX];
static bool        g_imgLive[MAX_TEX];
static bool        g_srgbLive[MAX_TEX];
static bool        g_unormLive[MAX_TEX];
static bool        g_bindless[MAX_TEX];  // published to the bindless set by the caller model
static TextureData g_handed[MAX_TEX];    // the record exactly as it was offered
static uint32_t    g_texCount;
static uint32_t    g_doubleDestroys;     // whole-run invariant
static uint32_t    g_unknownDestroys;    // whole-run invariant

// Clears the mint ledger between scenarios.
static void ledger_reset(void)
{
	memset(g_imgLive, 0, sizeof g_imgLive);
	memset(g_srgbLive, 0, sizeof g_srgbLive);
	memset(g_unormLive, 0, sizeof g_unormLive);
	memset(g_bindless, 0, sizeof g_bindless);
	g_texCount = 0;
}

// out: minted handles still live
static uint32_t live_handles(void)
{
	uint32_t n = 0;
	for (uint32_t t = 0; t < g_texCount; t++) {
		if (g_imgLive[t]) n++;
		if (g_srgbLive[t]) n++;
		if (g_unormLive[t]) n++;
	}
	return n;
}

// in: v as view handle. Discharges its ledger entry.
static void destroy_view(VkImageView v)
{
	if (v == VK_NULL_HANDLE) return;
	for (uint32_t t = 0; t < g_texCount; t++) {
		if (g_srgb[t] == v)  { if (!g_srgbLive[t]) g_doubleDestroys++;  g_srgbLive[t] = false;  return; }
		if (g_unorm[t] == v) { if (!g_unormLive[t]) g_doubleDestroys++; g_unormLive[t] = false; return; }
	}
	g_unknownDestroys++;
}

// in: m as image handle. Discharges its ledger entry.
static void destroy_image(VkImage m)
{
	if (m == VK_NULL_HANDLE) return;
	for (uint32_t t = 0; t < g_texCount; t++) {
		if (g_img[t] == m) { if (!g_imgLive[t]) g_doubleDestroys++; g_imgLive[t] = false; return; }
	}
	g_unknownDestroys++;
}

// in: i as refused texture index. Caller discharge on false.
static void model_discharge(uint32_t i)
{
	destroy_view(g_unorm[i]);
	destroy_view(g_srgb[i]);
	destroy_image(g_img[i]);
}

// in: prims as registry. out: true if record entered.
// Mint image + both views, offer record, publish bindless only on grant.
static bool model_load_texture(RenderPrimitives* prims)
{
	uint32_t i = g_texCount++;
	g_img[i]   = (VkImage)(uintptr_t)(0xA000u + i);
	g_srgb[i]  = (VkImageView)(uintptr_t)(0xB000u + i);
	g_unorm[i] = (VkImageView)(uintptr_t)(0xC000u + i);
	g_imgLive[i] = g_srgbLive[i] = g_unormLive[i] = true;

	TextureData td = {
		.usageCount = 77u,  // registry must zero
		.textureImage = g_img[i],
		.textureImageAlloc = { .memory = (VkDeviceMemory)(uintptr_t)(0xD000u + i),
		                       .offset = 256u * i, .size = 4096u + i, .mapped = NULL },
		.srgbView = g_srgb[i],
		.unormView = g_unorm[i],
	};
	g_handed[i] = td;

	if (!ano_vk_register_texture(prims, td)) { model_discharge(i); return false; }

	g_bindless[i] = true; // bindless_register_texture(view, sampler)
	return true;
}

// in: got as resident record, want as offered record.
// out: true iff fields match and usageCount is zero.
static bool record_matches(const TextureData* got, const TextureData* want)
{
	return got->usageCount == 0u &&
	       got->textureImage == want->textureImage &&
	       got->srgbView == want->srgbView &&
	       got->unormView == want->unormView &&
	       got->textureImageAlloc.memory == want->textureImageAlloc.memory &&
	       got->textureImageAlloc.offset == want->textureImageAlloc.offset &&
	       got->textureImageAlloc.size   == want->textureImageAlloc.size &&
	       got->textureImageAlloc.mapped == want->textureImageAlloc.mapped;
}

// in: prims. Destroy resident views+images, then ano_vk_cleanup_primitives.
static void model_cleanup(RenderPrimitives* prims)
{
	for (uint32_t i = 0; i < prims->textureCount; i++) {
		destroy_view(prims->textureBuffers[i].unormView);
		destroy_view(prims->textureBuffers[i].srgbView);
		destroy_image(prims->textureBuffers[i].textureImage);
	}
	ano_vk_cleanup_primitives(prims);
}

// in: where as label. Report live minted handles.
static void report_orphans(const char* where)
{
	for (uint32_t t = 0; t < g_texCount; t++) {
		if (!g_imgLive[t] && !g_srgbLive[t] && !g_unormLive[t]) continue;
		printf("  orphan: %s texture %" PRIu32 " image %p srgb %p unorm %p 〜 bindless-published: %s\n",
		       where, t, (void*)g_img[t], (void*)g_srgb[t], (void*)g_unorm[t], g_bindless[t] ? "yes" : "no");
	}
}


int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	// control: nine healthy registrations
	static RenderPrimitives good;
	bool allTook = true;
	for (int i = 0; i < 9; i++) if (!model_load_texture(&good)) allTook = false;
	CHECK(allTook, "control: every healthy registration answers true");
	CHECK(good.textureCount == 9u, "control: all nine records resident");
	CHECK(good.textureCapacity >= 9u, "control: capacity grew to cover the batch");
	bool recordsOk = true;
	for (uint32_t i = 0; i < good.textureCount && i < g_texCount; i++)
		if (!record_matches(&good.textureBuffers[i], &g_handed[i])) recordsOk = false;
	CHECK(recordsOk, "control: records land in order, whole, with only usageCount rewritten to zero");
	model_cleanup(&good);
	report_orphans("control");
	CHECK(live_handles() == 0, "control: teardown discharges every minted handle");
	CHECK(good.textureBuffers == NULL && good.textureCount == 0u && good.textureCapacity == 0u, "control: cleanup frees the array and zeroes the counts");
	CHECK(g_liveBlocks == 0u, "control: registry heap balanced");

	// trigger: first mint refuses
	printf("trigger: the 0->8 registry mint refuses\n");
	ledger_reset();
	static RenderPrimitives barren;
	g_failReallocAt = 1;
	bool took = model_load_texture(&barren);
	CHECK(!took, "refused mint: the refusal is reported to the caller");
	CHECK(barren.textureBuffers == NULL, "refused mint: the registry array is still unallocated");
	CHECK(barren.textureCount == 0u && barren.textureCapacity == 0u, "refused mint: count and capacity are unchanged");
	CHECK(!g_bindless[0], "refused mint: the texture is never bindless-published");
	model_cleanup(&barren);
	report_orphans("refused mint");
	CHECK(live_handles() == 0, "refused mint: the caller's discharge covers the whole record");
	CHECK(g_liveBlocks == 0u, "refused mint: no registry block leaked");

	// trigger: 8->16 growth refuses on ninth registration
	printf("trigger: the 8->16 registry growth refuses on the ninth registration\n");
	ledger_reset();
	static RenderPrimitives world;
	g_failReallocAt = 2; // realloc #1: 0->8 mint; #2: 8->16 growth
	uint32_t refused = 0;
	for (int i = 0; i < 9; i++) if (!model_load_texture(&world)) refused++;
	CHECK(g_failReallocAt == 0, "refused growth: the injection was consumed");
	CHECK(refused == 1, "refused growth: exactly the ninth registration is refused");
	CHECK(world.textureCount == 8u, "refused growth: textureCount is unchanged");
	CHECK(world.textureCapacity == 8u, "refused growth: textureCapacity is unchanged");
	bool prefixOk = world.textureBuffers != NULL;
	for (uint32_t i = 0; i < world.textureCount && i < g_texCount; i++)
		if (!record_matches(&world.textureBuffers[i], &g_handed[i])) prefixOk = false;
	CHECK(prefixOk, "refused growth: the resident prefix is intact and readable");
	CHECK(!g_bindless[8], "refused growth: the refused texture is never bindless-published");
	model_cleanup(&world);
	report_orphans("refused growth");
	CHECK(live_handles() == 0, "refused growth: nothing orphans 〜 the caller discharged what the registry refused");
	CHECK(world.textureBuffers == NULL && world.textureCount == 0u && world.textureCapacity == 0u, "refused growth: cleanup frees the array and zeroes the counts");
	CHECK(g_liveBlocks == 0u, "refused growth: registry heap balanced");

	// whole-run ledger invariants
	CHECK(g_doubleDestroys == 0, "no handle destroyed twice");
	CHECK(g_unknownDestroys == 0, "no unminted handle destroyed");

	if (failures) {
		printf("anotest_texregisterguard: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("anotest_texregisterguard: all passed\n");
	return 0;
}
