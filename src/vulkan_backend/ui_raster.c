/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI overlay lane: groundwork stub (docs/ui/ui-render.md §7 step 1). The prim ABI
// and its reference evaluator live in src/ui; steps 3-4 grow this TU into the
// buffer/descriptor/dispatch owner alongside text_raster.c.

#include "vulkan_backend/ui_raster.h"

#include "anoptic_logging.h"
#include "anoptic_ui.h"

// In: ctx/state after ano_vk_text_init (the UI lane shares the text overlay image).
// Out: true always; failure would log and clear state->uiOverlay, never abort boot.
bool ano_vk_ui_init(VulkanContext* ctx, RendererState* state)
{
    (void)ctx;
    if (!state->uiOverlay)
        return true;
    // Nothing to create yet; announce the lane so gate wiring is observable.
    ano_log(ANO_INFO, "UI overlay: groundwork stub (prim ABI %u B, clip %u B)",
            (unsigned)sizeof(AnoUiPrim), (unsigned)sizeof(AnoUiClip));
    return true;
}

void ano_vk_ui_destroy(VulkanContext* ctx, RendererState* state)
{
    (void)ctx;
    (void)state;
}
