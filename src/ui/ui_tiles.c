/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// UI per-tile prim lists: CPU-coarse stage of the scaling ladder (ui-render.md §3.7).
// Dense tilesX*tilesY grid of 8px tiles; each prim scatters into overlapping tiles
// (counting sort) so the GPU walks only a tile's prims. Runs at compose cadence.
// Solid bit = prim fully covers the tile 〜 GPU skips SDF, takes flat fill. Pure, any thread.
//
// PERF TODO (ui-render.md §3.7.3-4, deferred, measurement-gated):
//  - opaque-truncation: opaque solid OVER resets its tile list to just it (bounds list depth)
//  - sparse dispatch: non-empty tiles + compact active-tile list (skip empty dense tiles)
//  - GPU binning: move scatter to compute for dense dynamic vector content

#include "anoptic_ui.h"

#include <math.h>


/* AABB */

// In: prim (identity inv, v0). Out: padded pixel AABB (half+1px AA; SHADOW +3*sigma+1px).
// Matches ui_box_hits / ui_pending_bounds so tiles cover exactly what the brute cull accepts.
void ano_ui_prim_aabb(const AnoUiPrim *p, float outMin[2], float outMax[2])
{
    float pad = p->kind == ANO_UI_SHADOW ? 3.0f * p->param[0] + 1.0f : 1.0f;
    outMin[0] = p->origin[0] - p->half[0] - pad;
    outMin[1] = p->origin[1] - p->half[1] - pad;
    outMax[0] = p->origin[0] + p->half[0] + pad;
    outMax[1] = p->origin[1] + p->half[1] + pad;
}

// Opaque RRECT fill fully covering tile [tx0,ty0)-(tx1,ty1)? True only when the tile sits
// inside the coverage-1 core (inset by largest corner radius + 0.5px AA half-window) 〜
// exact classification, never an approximation.
static bool prim_solid_over(const AnoUiPrim *p, float tx0, float ty0, float tx1, float ty1)
{
    if (p->kind != ANO_UI_RRECT || p->param[0] != 0.0f)
        return false; // shadows/images/paths/rings are never flat-solid
    float maxR = p->radii[0];
    for (int i = 1; i < 4; i++)
        if (p->radii[i] > maxR)
            maxR = p->radii[i];
    float insetX = p->half[0] - maxR - 0.5f;
    float insetY = p->half[1] - maxR - 0.5f;
    if (insetX <= 0.0f || insetY <= 0.0f)
        return false;
    return tx0 >= p->origin[0] - insetX && tx1 <= p->origin[0] + insetX
           && ty0 >= p->origin[1] - insetY && ty1 <= p->origin[1] + insetY;
}


/* Tile Build */

// In: scene, grid origin (overlay px) + tile counts, caller buffers.
// Out: offsets (tilesX*tilesY+1, prefix-summed so tile t owns [offsets[t],offsets[t+1]))
// and the prim-index entry stream (ascending = painter order, solid bit set per tile).
// cursor = tilesX*tilesY scratch. Returns entry count; *ok false if a cap is too small.
uint32_t ano_ui_tile_build(const AnoUiScene *s, int32_t ox, int32_t oy,
                           uint32_t tilesX, uint32_t tilesY,
                           uint32_t *offsets, uint32_t offsetsCap,
                           uint32_t *entries, uint32_t entryCap,
                           uint32_t *cursor, bool *ok)
{
    uint32_t nTiles = tilesX * tilesY;
    *ok = true;
    if (nTiles + 1 > offsetsCap || tilesX == 0 || tilesY == 0)
    {
        *ok = false;
        return 0;
    }
    for (uint32_t t = 0; t <= nTiles; t++)
        offsets[t] = 0;

    // Pass 1: count each prim into offsets[tile+1] (shifted for the prefix sum).
    for (uint32_t i = 0; i < s->primCount; i++)
    {
        float mn[2], mx[2];
        ano_ui_prim_aabb(&s->prims[i], mn, mx);
        int32_t tx0 = (int32_t)floorf((mn[0] - (float)ox) / 8.0f);
        int32_t tx1 = (int32_t)floorf((mx[0] - (float)ox) / 8.0f);
        int32_t ty0 = (int32_t)floorf((mn[1] - (float)oy) / 8.0f);
        int32_t ty1 = (int32_t)floorf((mx[1] - (float)oy) / 8.0f);
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 >= (int32_t)tilesX) tx1 = (int32_t)tilesX - 1;
        if (ty1 >= (int32_t)tilesY) ty1 = (int32_t)tilesY - 1;
        for (int32_t ty = ty0; ty <= ty1; ty++)
            for (int32_t tx = tx0; tx <= tx1; tx++)
                offsets[(uint32_t)ty * tilesX + (uint32_t)tx + 1]++;
    }

    // Prefix sum: offsets[t] becomes tile t's start, offsets[nTiles] the total.
    for (uint32_t t = 0; t < nTiles; t++)
        offsets[t + 1] += offsets[t];
    uint32_t total = offsets[nTiles];
    if (total > entryCap)
    {
        *ok = false;
        return 0;
    }

    // Pass 2: scatter prim indices (ascending) into each tile, tagging solid coverage.
    for (uint32_t t = 0; t < nTiles; t++)
        cursor[t] = offsets[t];
    for (uint32_t i = 0; i < s->primCount; i++)
    {
        const AnoUiPrim *p = &s->prims[i];
        float mn[2], mx[2];
        ano_ui_prim_aabb(p, mn, mx);
        int32_t tx0 = (int32_t)floorf((mn[0] - (float)ox) / 8.0f);
        int32_t tx1 = (int32_t)floorf((mx[0] - (float)ox) / 8.0f);
        int32_t ty0 = (int32_t)floorf((mn[1] - (float)oy) / 8.0f);
        int32_t ty1 = (int32_t)floorf((mx[1] - (float)oy) / 8.0f);
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 >= (int32_t)tilesX) tx1 = (int32_t)tilesX - 1;
        if (ty1 >= (int32_t)tilesY) ty1 = (int32_t)tilesY - 1;
        for (int32_t ty = ty0; ty <= ty1; ty++)
            for (int32_t tx = tx0; tx <= tx1; tx++)
            {
                float px0 = (float)(ox + tx * 8), py0 = (float)(oy + ty * 8);
                bool solid = prim_solid_over(p, px0, py0, px0 + 8.0f, py0 + 8.0f);
                uint32_t e = i | (solid ? ANO_UI_ENTRY_SOLID : 0u);
                entries[cursor[(uint32_t)ty * tilesX + (uint32_t)tx]++] = e;
            }
    }
    return total;
}
