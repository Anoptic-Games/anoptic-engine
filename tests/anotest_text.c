/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_text.h -- module lifecycle, and the glyph-curve bake path:
 *   - lifecycle: version zeros before init, idempotent init, linked FreeType is the
 *     vendored 2.13+ submodule, clean shutdown / double shutdown / re-init;
 *   - white-box bake math (via src include of text/text_internal.h): binary16 pack is
 *     round-trip exact on representable values, RNE-accurate and monotone elsewhere,
 *     clamps overflow to inf; monotone quad splitting yields chained sandwich-monotone
 *     pieces (0/1/2-split cases); cubic->quad conversion preserves endpoints and stays
 *     within tolerance on a circle arc;
 *   - bake of Geist-Regular ASCII 32..126 against an independent decoder of the
 *     documented stream grammar: contiguous glyph extents, bit-exact contour closure,
 *     exact post-quantization monotone sandwich, directory bbox == decoded bbox,
 *     fill-right winding (chord shoelace: 'H' negative; 'O' outer negative + hole
 *     positive), per-glyph and total curve counts pinned to the FONT_RENDER.md audit
 *     oracle (1654 total, '@' 63, space 0), advance/metrics sanity, EINVAL/ENOMEM
 *     argument contract, and bit-identical determinism across two bakes;
 *   - the CPU reference rasterizer (step 3, the scalar mirror of the GPU shader)
 *     against FT_Render_Glyph ground truth on the same pixel grid at 64 px/em:
 *     per-glyph RMS and max coverage error, plus the unclamped-peak oracle proving
 *     the per-glyph clamp is load-bearing for BOTH overlap forms this font ships --
 *     separate overlapping contours ('# $ + f t') and self-overlapping single
 *     contours with winding-2 pockets ('H 8 @') -- while clean glyphs (incl. '%',
 *     whose audit flag was a bbox false positive) stay in [~1, 1.1);
 *   - the ghost-pixel sweep: every baked glyph at 64 and 200 px/em on a padded grid,
 *     asserting zero isolated coverage (>= 3/255 with an all-zero FT 3x3 around it)
 *     anywhere outside the ink -- the regression net for the solve_mono chord-fallback
 *     bands the on-screen demo exposed on 'v w ( 2';
 *   - the shaper (step 4 + v1 kerning): strict UTF-8 decode (overlong/surrogate/range/
 *     resync cases), golden layout over the Geist bake -- exact kerned pen advances,
 *     blank glyphs advancing without emitting, newline/CR handling, out-of-range gap,
 *     cap truncation vs total count, bitwise run continuation via penOut (split at a
 *     non-kerning boundary), measure extents, the 48-byte GPU ABI of AnoGlyphInstance;
 *   - color/style runs: same-size color splits inside kern pairs are position-bitwise
 *     vs the unsplit shape (empty runs invisible to the chain), size boundaries kill
 *     the kern and land per-run scale in the inverse, '\n' steps at its own run's
 *     height, mid-sequence boundaries can't split a codepoint, measure_runs extents;
 *   - the GPOS PairPos reader (v1): a hand-assembled synthetic table exercising
 *     first-subtable-wins, lookup accumulation, class-0 defaults, coverage/classdef
 *     formats 1+2, type-9 extension wrapping, absent slots, truncation fail-soft, and
 *     non-kern feature exclusion; plus the Geist oracle -- 2891 ASCII pairs summing to
 *     -63296 FUnits (fontTools audit), spot pairs bitwise, sorted-key invariant.
 * Requires resources/fonts/Geist staged next to the binary (tests/CMakeLists.txt).
 * Exit 0 == pass; failures print what broke. */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "anoptic_filesystem.h"
#include "anoptic_memory.h"
#include "anoptic_text.h"
#include "text/text_internal.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define FONT_PATH "resources/fonts/Geist/static/Geist-Regular.ttf"

// ---------------------------------------------------------------------------------------------
// Lifecycle.

static void expect_version_zero(const char *when)
{
    int maj = -1, min = -1, pat = -1;
    ano_text_version(&maj, &min, &pat);
    printf("%s: FreeType version reads %d.%d.%d\n", when, maj, min, pat);
    CHECK(maj == 0 && min == 0 && pat == 0, "version is all zeros without a live backend");
}

static void test_lifecycle(void)
{
    expect_version_zero("before init");
    CHECK(ano_text_init() == 0, "ano_text_init succeeds");
    CHECK(ano_text_init() == 0, "second init is an idempotent success");

    int maj = 0, min = 0, pat = 0;
    ano_text_version(&maj, &min, &pat);
    printf("after init: FreeType %d.%d.%d\n", maj, min, pat);
    CHECK(maj == 2, "FreeType major version is 2");
    CHECK(min >= 13, "FreeType minor version is >= 13 (vendored submodule generation)");
    ano_text_version(NULL, NULL, NULL); // NULL outputs are a documented no-op

    ano_text_shutdown();
    expect_version_zero("after shutdown");
    ano_text_shutdown(); // double shutdown must be harmless

    CHECK(ano_text_init() == 0, "re-init after shutdown succeeds");
    ano_text_version(&maj, &min, &pat);
    CHECK(maj == 2 && min >= 13, "re-initialized backend reports the same FreeType");
    ano_text_shutdown();
}

// ---------------------------------------------------------------------------------------------
// White-box: binary16 conversion.

static void test_half(void)
{
    // Exactly representable values round-trip bit-perfectly.
    const float exact[] = { 0.0f, 0.5f, 1.0f, -0.25f, 1.5f, -2.0f, 0.109375f, 65504.0f };
    for (size_t i = 0; i < sizeof exact / sizeof *exact; i++)
        CHECK(ano_half_unpack(ano_half_pack(exact[i])) == exact[i],
              "representable value round-trips exactly");

    // Coordinate-range values: relative error within one half ulp; order preserved.
    float prev = -10.0f;
    for (int k = 0; k <= 2000; k++)
    {
        float v  = (float)(k - 1000) / 333.0f; // [-3, 3], the em coordinate envelope
        float rt = ano_half_unpack(ano_half_pack(v));
        CHECK(fabsf(rt - v) <= fabsf(v) / 1024.0f + 1e-7f, "round-trip within half precision");
        CHECK(rt >= prev, "quantization is monotone");
        prev = rt;
    }

    CHECK(ano_half_pack(1e9f) == 0x7C00u, "overflow clamps to +inf");
    CHECK(ano_half_pack(-1e9f) == 0xFC00u, "overflow clamps to -inf");
    CHECK(ano_half_unpack(0x7C00u) > 3.0e38f, "+inf unpacks huge (sentinel is unmistakable)");
    // Subnormal half territory survives the round trip too (never hit by coordinates).
    float tiny = 3.0e-6f;
    CHECK(fabsf(ano_half_unpack(ano_half_pack(tiny)) - tiny) < 1e-7f, "subnormal path sane");
}

// ---------------------------------------------------------------------------------------------
// White-box: monotone splitting and cubic conversion.

static void check_monotone_quad(const AnoQuad *q, const char *msg)
{
    const double e = 1e-9;
    int ok = 1;
    for (int axis = 0; axis < 2; axis++)
    {
        const double *c = axis ? q->y : q->x;
        double lo = c[0] < c[2] ? c[0] : c[2];
        double hi = c[0] < c[2] ? c[2] : c[0];
        if (c[1] < lo - e || c[1] > hi + e)
            ok = 0;
    }
    CHECK(ok, msg);
}

static void test_quad_split(void)
{
    AnoQuad out[3];

    // Already monotone: passes through whole.
    AnoQuad mono = { .x = { 0.0, 0.2, 1.0 }, .y = { 0.0, 0.5, 1.0 } };
    CHECK(ano_quad_split_monotone(&mono, out) == 1, "monotone quad stays whole");

    // Line-as-quad (exact midpoint control): both axes' quadratic term is exactly zero.
    AnoQuad line = { .x = { 0.0, 1.0, 2.0 }, .y = { 0.25, 0.5, 0.75 } };
    CHECK(ano_quad_split_monotone(&line, out) == 1, "degenerate line quad never splits");

    // One y-extremum at t=0.5: two pieces sharing B(0.5) = (1,1).
    AnoQuad arch = { .x = { 0.0, 1.0, 2.0 }, .y = { 0.0, 2.0, 0.0 } };
    int n = ano_quad_split_monotone(&arch, out);
    CHECK(n == 2, "single-extremum quad splits in two");
    if (n == 2)
    {
        CHECK(fabs(out[0].x[2] - 1.0) < 1e-12 && fabs(out[0].y[2] - 1.0) < 1e-12,
              "split lands on the curve apex");
        CHECK(out[0].x[2] == out[1].x[0] && out[0].y[2] == out[1].y[0],
              "pieces chain exactly");
        check_monotone_quad(&out[0], "left piece is monotone");
        check_monotone_quad(&out[1], "right piece is monotone");
    }

    // Extrema on both axes: three chained monotone pieces.
    AnoQuad hook = { .x = { 0.0, 2.0, 1.0 }, .y = { 0.0, -1.0, 2.0 } };
    n = ano_quad_split_monotone(&hook, out);
    CHECK(n == 3, "two-extrema quad splits in three");
    for (int i = 0; i < n; i++)
        check_monotone_quad(&out[i], "each piece is monotone");
    for (int i = 1; i < n; i++)
        CHECK(out[i - 1].x[2] == out[i].x[0] && out[i - 1].y[2] == out[i].y[0],
              "three pieces chain exactly");
}

static void test_cubic(void)
{
    // Kappa cubic approximating a unit quarter circle.
    const double k = 0.5522847498307936;
    double px[4] = { 1.0, 1.0, k, 0.0 };
    double py[4] = { 0.0, k, 1.0, 1.0 };
    AnoQuad q[32];
    int n = ano_cubic_to_quads(px, py, 1e-3, q, 32);
    printf("cubic quarter-circle -> %d quads at 1e-3 em tolerance\n", n);
    CHECK(n >= 2 && n <= 32, "arc subdivides but respects the cap");
    CHECK(q[0].x[0] == 1.0 && q[0].y[0] == 0.0, "first endpoint exact");
    CHECK(q[n - 1].x[2] == 0.0 && q[n - 1].y[2] == 1.0, "last endpoint exact");
    for (int i = 1; i < n; i++)
        CHECK(q[i - 1].x[2] == q[i].x[0] && q[i - 1].y[2] == q[i].y[0], "quads chain exactly");
    for (int i = 0; i < n; i++)
        for (int s = 0; s <= 16; s++)
        {
            double t = s / 16.0, u = 1.0 - t;
            double x = u * u * q[i].x[0] + 2 * u * t * q[i].x[1] + t * t * q[i].x[2];
            double y = u * u * q[i].y[0] + 2 * u * t * q[i].y[1] + t * t * q[i].y[2];
            CHECK(fabs(sqrt(x * x + y * y) - 1.0) < 3e-3, "arc stays within tolerance of the circle");
        }
    CHECK(ano_cubic_to_quads(px, py, 1e-3, q, 0) == -1, "maxOut < 1 is rejected");
}

// ---------------------------------------------------------------------------------------------
// Bake validation: an independent decoder of the documented stream grammar.

typedef struct DecodedGlyph {
    float    minX, minY, maxX, maxY;
    double   shoelace[8];  // chord shoelace per contour (CCW positive)
    uint32_t contourCount;
    uint32_t curves;
} DecodedGlyph;

static float half_lo(uint32_t u) { return ano_half_unpack((uint16_t)(u & 0xFFFFu)); }
static float half_hi(uint32_t u) { return ano_half_unpack((uint16_t)(u >> 16)); }

// Walks one glyph's stream range; checks grammar, closure, and the monotone sandwich.
// Returns the stream index just past the glyph (== the next glyph's offset).
static uint32_t decode_glyph(const uint32_t *pts, const AnoGlyphEntry *e, DecodedGlyph *d)
{
    memset(d, 0, sizeof *d);
    d->minX = d->minY = 1e30f;
    d->maxX = d->maxY = -1e30f;
    uint32_t i = e->pointOffset;
    if (e->curveCount == 0)
        return i;

    uint32_t startBits = pts[i];
    CHECK(startBits != ANO_TEXT_POINT_SENTINEL, "glyph does not start with a sentinel");
    float p0x = half_lo(pts[i]), p0y = half_hi(pts[i]);
    uint32_t lastBits = startBits;
    i++;
    d->contourCount = 1;

    for (uint32_t c = 0; c < e->curveCount; c++)
    {
        if (pts[i] == ANO_TEXT_POINT_SENTINEL)
        {
            CHECK(lastBits == startBits, "contour closes bit-exactly at a sentinel");
            i++;
            startBits = pts[i];
            CHECK(startBits != ANO_TEXT_POINT_SENTINEL, "no doubled sentinel");
            p0x = half_lo(pts[i]);
            p0y = half_hi(pts[i]);
            lastBits = startBits;
            i++;
            if (d->contourCount < 8)
                d->contourCount++;
        }
        uint32_t u1 = pts[i++];
        uint32_t u2 = pts[i++];
        CHECK(u1 != ANO_TEXT_POINT_SENTINEL && u2 != ANO_TEXT_POINT_SENTINEL,
              "curve points are never sentinels");
        float p1x = half_lo(u1), p1y = half_hi(u1);
        float p2x = half_lo(u2), p2y = half_hi(u2);

        // Post-quantization monotone sandwich must hold EXACTLY (the bake clamps).
        CHECK(p1x >= fminf(p0x, p2x) && p1x <= fmaxf(p0x, p2x), "control inside endpoints (x)");
        CHECK(p1y >= fminf(p0y, p2y) && p1y <= fmaxf(p0y, p2y), "control inside endpoints (y)");

        float xs[3] = { p0x, p1x, p2x }, ys[3] = { p0y, p1y, p2y };
        for (int p = 0; p < 3; p++)
        {
            d->minX = fminf(d->minX, xs[p]);
            d->maxX = fmaxf(d->maxX, xs[p]);
            d->minY = fminf(d->minY, ys[p]);
            d->maxY = fmaxf(d->maxY, ys[p]);
        }
        d->shoelace[d->contourCount - 1] += (double)p0x * p2y - (double)p2x * p0y;

        p0x = p2x;
        p0y = p2y;
        lastBits = u2;
        d->curves++;
    }
    CHECK(lastBits == startBits, "final contour closes bit-exactly");
    return i;
}

static void validate_bake(const AnoFontBake *b)
{
    CHECK(b->glyphCount == 95, "ASCII 32..126 bakes 95 glyphs");
    CHECK(b->upem == 1000, "Geist reports upem 1000");
    CHECK(b->firstCodepoint == 32, "range echoed back");
    CHECK(b->pointCount > 0 && b->points != NULL, "point stream exists");
    CHECK(b->ascender > 0.0f && b->descender < 0.0f, "ascender above, descender below baseline");
    CHECK(b->lineHeight > b->ascender - b->descender - 0.5f && b->lineHeight > 0.0f,
          "line height sane");

    uint32_t total = 0;
    for (uint32_t i = 0; i < b->glyphCount; i++)
    {
        const AnoGlyphEntry *e = &b->glyphs[i];
        uint32_t next = (i + 1 < b->glyphCount) ? b->glyphs[i + 1].pointOffset : b->pointCount;
        CHECK(e->pointOffset <= next && next <= b->pointCount, "offsets are ordered");
        CHECK(e->flags == 0, "every ASCII codepoint exists in Geist");
        CHECK(e->advance > 0.0f && e->advance < 2.0f, "advance is positive and sane");

        DecodedGlyph d;
        uint32_t end = decode_glyph(b->points, e, &d);
        CHECK(end == next, "glyph stream extent is contiguous");
        CHECK(d.curves == e->curveCount, "decoder consumed exactly curveCount curves");
        if (e->curveCount > 0)
        {
            CHECK(d.minX == e->bboxMin[0] && d.minY == e->bboxMin[1]
                      && d.maxX == e->bboxMax[0] && d.maxY == e->bboxMax[1],
                  "directory bbox equals decoded bbox bit-exactly");
            CHECK(d.minX > -1.0f && d.maxX < 2.0f && d.minY > -1.0f && d.maxY < 2.0f,
                  "bbox within the em envelope");
        }
        total += e->curveCount;
    }

    // Audit oracle (FONT_RENDER.md section 3): counts measured on this exact font file
    // by an independent fontTools implementation (with composite glyphs decomposed --
    // ':',';','`','i','j' are components in Geist).
    printf("bake: %u monotone curves across ASCII (audit oracle 1654), %u stream points\n",
           total, b->pointCount);
    CHECK(total == 1654, "total monotone curve count matches the fontTools audit");
    if (total != 1654)
        for (uint32_t i = 0; i < b->glyphCount; i++)
            printf("  U+%04X '%c' curves=%u\n", b->firstCodepoint + i,
                   (char)(b->firstCodepoint + i), b->glyphs[i].curveCount);
    CHECK(b->glyphs[' ' - 32].curveCount == 0, "space is blank");
    CHECK(b->glyphs['@' - 32].curveCount == 63, "'@' matches the audit worst case");

    // Winding convention: fill-right => clockwise outers => negative chord shoelace.
    DecodedGlyph H, O;
    decode_glyph(b->points, &b->glyphs['H' - 32], &H);
    CHECK(H.contourCount == 1, "'H' is a single contour");
    CHECK(H.shoelace[0] < 0.0, "'H' winds clockwise (fill-right)");
    decode_glyph(b->points, &b->glyphs['O' - 32], &O);
    CHECK(O.contourCount == 2, "'O' is outer + hole");
    if (O.contourCount == 2)
    {
        double a = O.shoelace[0], h = O.shoelace[1];
        CHECK((a < 0.0) != (h < 0.0), "'O' contours wind oppositely");
        CHECK(a + h < 0.0, "'O' outer dominates (net clockwise ink)");
    }
}

// ---------------------------------------------------------------------------------------------
// Reference rasterizer vs FreeType ground truth.

#define REF_SCALE 64u      // px per em: big enough that curve detail matters
#define REF_DIM   160      // per-glyph buffer side; comfortably above any 64px glyph

// The unclamped-peak oracle, measured on this font. Coverage exceeding 1 comes in TWO
// forms, both neutralized by the per-glyph clamp: separate same-winding contours
// overlapping ('# $ + f t', the scouting audit's set minus '%' -- a bbox-proxy false
// positive, its ink never overlaps), and single contours that SELF-overlap ('H 8 @':
// Geist draws stems and crossbars as overlapping strokes joined by diagonal jogs,
// leaving winding-2 pockets; found by this test, invisible to the contour-pair audit).
// Note the paper's per-contour-evaluation fallback would NOT fix the self-overlap
// form; the clamp handles both. Clean glyphs must also REACH unity (solid interiors).
static const struct {
    char  g;
    float lo, hi;
} peakOracle[] = {
    { '#', 1.5f, 2.1f }, { '$', 1.5f, 2.1f }, { '+', 1.5f, 2.1f },
    { 'f', 1.5f, 2.1f }, { 't', 1.5f, 2.1f },                      // contour overlaps
    { 'H', 1.5f, 2.1f }, { '8', 1.4f, 2.1f }, { '@', 1.2f, 2.1f }, // self-overlaps
    { '%', 0.9f, 1.1f }, { 'A', 0.9f, 1.1f }, { 'g', 0.9f, 1.1f },
    { 'O', 0.9f, 1.1f }, { 'i', 0.9f, 1.1f },                      // clean
};

static void test_reference_raster(AnoFontId font, const AnoFontBake *b)
{
    static uint8_t truth[REF_DIM * REF_DIM];
    static uint8_t ours[REF_DIM * REF_DIM];

    double worstRms = 0.0;
    int    worstMax = 0;
    for (size_t p = 0; p < sizeof peakOracle / sizeof *peakOracle; p++)
    {
        uint32_t cp = (uint32_t)peakOracle[p].g;
        int fw = 0, fr = 0, fl = 0, ft = 0;
        int rc = ano_text_ref_ft_render(font, cp, REF_SCALE, truth, sizeof truth,
                                        &fw, &fr, &fl, &ft);
        CHECK(rc == 0, "FreeType ground-truth render succeeds");
        if (rc != 0)
            continue;
        CHECK(fw > 5 && fr > 10 && fw <= REF_DIM && fr <= REF_DIM,
              "ground-truth bitmap has sane dimensions");

        float maxSum = 0.0f;
        ano_text_raster_ref(b->points, &b->glyphs[cp - b->firstCodepoint], (float)REF_SCALE,
                            fl, ft, fw, fr, ours, &maxSum);

        double se = 0.0;
        int maxd = 0;
        for (int i = 0; i < fw * fr; i++)
        {
            int d = (int)ours[i] - (int)truth[i];
            se += (double)d * d;
            if (d < 0)
                d = -d;
            if (d > maxd)
                maxd = d;
        }
        double rms = sqrt(se / (fw * fr));
        printf("ref '%c': %2dx%2d rms=%5.2f max=%3d unclamped-peak=%.3f\n",
               peakOracle[p].g, fw, fr, rms, maxd, (double)maxSum);
        if (rms > worstRms)
            worstRms = rms;
        if (maxd > worstMax)
            worstMax = maxd;

        // Coverage agreement with FreeType (also an exact-area rasterizer). Observed
        // worst on this font: rms 2.71, max 53 -- the max sits on winding-2 pocket
        // boundaries where clamped coverage and the nonzero fill rule diverge for one
        // AA pixel. Thresholds leave margin for FP/platform variance only.
        CHECK(rms <= 4.0, "coverage RMS within threshold of FreeType");
        CHECK(maxd <= 64, "per-pixel coverage deviation bounded");

        CHECK(maxSum >= peakOracle[p].lo, "unclamped peak reaches its expected class");
        CHECK(maxSum <= peakOracle[p].hi, "unclamped peak bounded (no runaway winding)");
    }
    printf("ref worst: rms=%.2f max=%d over %zu probes\n", worstRms, worstMax,
           sizeof peakOracle / sizeof *peakOracle);
}

// ---------------------------------------------------------------------------------------------
// Ghost-pixel sweep: coverage leaking outside the ink.

// Regression net for the step-6 band artifact: solve_mono's former chord-fallback
// threshold mis-crossed shallow-but-genuine quads, so opposing stroke edges stopped
// cancelling and constant faint bands appeared right of concave edges ('v w ( 2') --
// entirely OUTSIDE the strokes, so RMS thresholds absorbed them and the affected glyphs
// sat outside the probe set. This sweep renders EVERY baked glyph on a grid padded past
// FreeType's tight bitmap and flags any pixel with coverage >= GHOST_MIN whose 3x3
// FreeType neighborhood is all zero: honest AA partials always touch ink, phantom
// coverage doesn't. Two scales, because the crossing error is a fixed em quantity while
// the window shrinks with 1/S: 200 px amplifies what 64 px shows ~3x (the old fallback
// fails here at >20/255).

#define GHOST_MIN 3   // flag isolated coverage at or above this (post-fix residual is 0)
#define GHOST_PAD 8   // grid margin past the FT bitmap; bands extend beyond the ink
#define GHOST_DIM 320 // per-glyph buffer side; fits any 200px glyph plus padding

static void test_ghost_pixels(AnoFontId font, const AnoFontBake *b)
{
    static uint8_t truth[GHOST_DIM * GHOST_DIM];
    static uint8_t ours[GHOST_DIM * GHOST_DIM];
    static const uint32_t scales[2] = { 64, 200 };

    for (int s = 0; s < 2; s++)
    {
        uint32_t S = scales[s];
        int ghosts = 0, worst = 0, worstCp = 0;
        for (uint32_t cp = b->firstCodepoint; cp < b->firstCodepoint + b->glyphCount; cp++)
        {
            const AnoGlyphEntry *g = &b->glyphs[cp - b->firstCodepoint];
            if (g->curveCount == 0)
                continue;
            int fw = 0, fr = 0, fl = 0, ft = 0;
            int rc = ano_text_ref_ft_render(font, cp, S, truth, sizeof truth,
                                            &fw, &fr, &fl, &ft);
            CHECK(rc == 0, "FreeType ground-truth render succeeds");
            if (rc != 0 || fw <= 0 || fr <= 0)
                continue;
            int ow = fw + 2 * GHOST_PAD, or_ = fr + 2 * GHOST_PAD;
            CHECK(ow <= GHOST_DIM && or_ <= GHOST_DIM, "padded ghost grid fits");
            if (ow > GHOST_DIM || or_ > GHOST_DIM)
                continue;
            ano_text_raster_ref(b->points, g, (float)S, fl - GHOST_PAD, ft + GHOST_PAD,
                                ow, or_, ours, NULL);

            for (int r = 0; r < or_; r++)
                for (int c = 0; c < ow; c++)
                {
                    int v = ours[r * ow + c];
                    if (v < GHOST_MIN)
                        continue;
                    // FT coordinates of this pixel; any ink in the 3x3 absolves it.
                    int fy = r - GHOST_PAD, fx = c - GHOST_PAD;
                    int ink = 0;
                    for (int dy = -1; dy <= 1 && !ink; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            int ny = fy + dy, nx = fx + dx;
                            if (ny < 0 || nx < 0 || ny >= fr || nx >= fw)
                                continue;
                            if (truth[ny * fw + nx] != 0)
                            {
                                ink = 1;
                                break;
                            }
                        }
                    if (!ink)
                    {
                        if (ghosts < 8)
                            printf("ghost '%c' @%upx (%d,%d) = %d\n", (char)cp, S, c, r, v);
                        ghosts++;
                        if (v > worst)
                        {
                            worst = v;
                            worstCp = (int)cp;
                        }
                    }
                }
        }
        printf("ghost sweep @%3upx: %d ghost pixels (worst %d on '%c')\n", S, ghosts,
               worst, ghosts ? (char)worstCp : ' ');
        CHECK(ghosts == 0, "no coverage outside the ink at any glyph");
    }
}

// ---------------------------------------------------------------------------------------------
// Shaper v0.

static void test_utf8(void)
{
    uint32_t n = 0;
    CHECK(ano_utf8_next("A", 1, &n) == 'A' && n == 1, "ascii decodes");
    CHECK(ano_utf8_next("\xC3\xA9", 2, &n) == 0xE9 && n == 2, "2-byte sequence");
    CHECK(ano_utf8_next("\xE2\x82\xAC", 3, &n) == 0x20AC && n == 3, "3-byte sequence");
    CHECK(ano_utf8_next("\xF0\x9F\x99\x82", 4, &n) == 0x1F642 && n == 4, "4-byte sequence");
    CHECK(ano_utf8_next("\xC0\x80", 2, &n) == 0xFFFD && n == 2, "overlong rejected, consumed");
    CHECK(ano_utf8_next("\xED\xA0\x80", 3, &n) == 0xFFFD && n == 3, "surrogate rejected");
    CHECK(ano_utf8_next("\xF4\x90\x80\x80", 4, &n) == 0xFFFD && n == 4, "past U+10FFFF rejected");
    CHECK(ano_utf8_next("\xC3", 1, &n) == 0xFFFD && n == 1, "truncated tail resyncs by one");
    CHECK(ano_utf8_next("\xC3\x41", 2, &n) == 0xFFFD && n == 1, "broken continuation resyncs by one");
    CHECK(ano_utf8_next("\xFF", 1, &n) == 0xFFFD && n == 1, "invalid lead byte");
    CHECK(ano_utf8_next("\x80", 1, &n) == 0xFFFD && n == 1, "stray continuation byte");
}

// ---------------------------------------------------------------------------------------------
// GPOS PairPos parser (shaper v1), white-box against a hand-assembled synthetic table.

// Byte cursor for big-endian table assembly; every section boundary is asserted so a
// layout mistake fails here, not as a confusing parser result.
static uint32_t put16(uint8_t *b, uint32_t off, uint32_t v)
{
    b[off] = (uint8_t)(v >> 8);
    b[off + 1] = (uint8_t)v;
    return off + 2;
}

static uint32_t put32(uint8_t *b, uint32_t off, uint32_t v)
{
    off = put16(b, off, v >> 16);
    return put16(b, off, v & 0xFFFFu);
}

// Layout under test -- 'latn' script, 'kern' feature with three lookups:
//   L0: fmt1 (pair 5,7 = -50) then fmt2 fallback (classes: (5|6, 7|8) = -30, covered
//       first glyph with class-0 second = -70) -- first-applying-subtable-wins;
//   L1: fmt2 (+10 for (5,7)) -- lookups accumulate;
//   L2: type-9 Extension wrapping fmt1 (pair 9,5 = -25).
// Coverage fmt1+fmt2 and ClassDef fmt1+fmt2 all appear at least once.
static void test_gpos_synthetic(void)
{
    enum { SL = 10, FL = 30, LL = 48, TOTAL = 238 };
    static uint8_t t[TOTAL];
    memset(t, 0, sizeof t);
    uint32_t o = 0;
    o = put16(t, o, 1); o = put16(t, o, 0);            // version 1.0
    o = put16(t, o, SL); o = put16(t, o, FL); o = put16(t, o, LL);
    CHECK(o == SL, "synthetic header lays out");

    o = put16(t, o, 1);                                 // ScriptList: 1 record
    o = put32(t, o, 0x6C61746Eu); o = put16(t, o, 8);   // 'latn' -> SL+8
    o = put16(t, o, 4); o = put16(t, o, 0);             // Script: defaultLangSys=4
    o = put16(t, o, 0); o = put16(t, o, 0xFFFF);        // LangSys: order, reqFeature
    o = put16(t, o, 1); o = put16(t, o, 0);             // 1 feature index: 0
    CHECK(o == FL, "synthetic script list lays out");

    o = put16(t, o, 1);                                 // FeatureList: 1 record
    o = put32(t, o, 0x6B65726Eu); o = put16(t, o, 8);   // 'kern' -> FL+8
    o = put16(t, o, 0); o = put16(t, o, 3);             // Feature: params, 3 lookups
    o = put16(t, o, 0); o = put16(t, o, 1); o = put16(t, o, 2);
    CHECK(o == LL, "synthetic feature list lays out");

    o = put16(t, o, 3);                                 // LookupList: 3 lookups
    o = put16(t, o, 8); o = put16(t, o, 96); o = put16(t, o, 150);
    // L0 @ LL+8: type 2, 2 subtables at +10 (fmt1) and +34 (fmt2).
    o = put16(t, o, 2); o = put16(t, o, 0); o = put16(t, o, 2);
    o = put16(t, o, 10); o = put16(t, o, 34);
    o = put16(t, o, 1); o = put16(t, o, 12);            // S0a: fmt1, cov @ +12
    o = put16(t, o, 0x4); o = put16(t, o, 0);
    o = put16(t, o, 1); o = put16(t, o, 18);            // 1 pairset @ +18
    o = put16(t, o, 1); o = put16(t, o, 1); o = put16(t, o, 5);  // cov fmt1: {5}
    o = put16(t, o, 1); o = put16(t, o, 7); o = put16(t, o, (uint32_t)(int32_t)-50);
    o = put16(t, o, 2); o = put16(t, o, 24);            // S0b: fmt2, cov @ +24
    o = put16(t, o, 0x4); o = put16(t, o, 0);
    o = put16(t, o, 34); o = put16(t, o, 44);           // cd1 @ +34, cd2 @ +44
    o = put16(t, o, 2); o = put16(t, o, 2);             // 2x2 classes
    o = put16(t, o, 0); o = put16(t, o, 0);             // matrix [0][*]
    o = put16(t, o, (uint32_t)(int32_t)-70); o = put16(t, o, (uint32_t)(int32_t)-30);
    o = put16(t, o, 2); o = put16(t, o, 1);             // cov fmt2: 1 range
    o = put16(t, o, 5); o = put16(t, o, 6); o = put16(t, o, 0);
    o = put16(t, o, 1); o = put16(t, o, 5);             // cd1 fmt1: start 5
    o = put16(t, o, 2); o = put16(t, o, 1); o = put16(t, o, 1); // classes {1,1}
    o = put16(t, o, 2); o = put16(t, o, 1);             // cd2 fmt2: 1 range
    o = put16(t, o, 7); o = put16(t, o, 8); o = put16(t, o, 1);
    CHECK(o == LL + 96, "synthetic lookup 0 lays out");
    // L1 @ LL+96: type 2, 1 subtable (fmt2, +10 for class pair (1,1)).
    o = put16(t, o, 2); o = put16(t, o, 0); o = put16(t, o, 1);
    o = put16(t, o, 8);
    o = put16(t, o, 2); o = put16(t, o, 24);            // S1: fmt2, cov @ +24
    o = put16(t, o, 0x4); o = put16(t, o, 0);
    o = put16(t, o, 30); o = put16(t, o, 38);           // cd1 @ +30, cd2 @ +38
    o = put16(t, o, 2); o = put16(t, o, 2);
    o = put16(t, o, 0); o = put16(t, o, 0);
    o = put16(t, o, 0); o = put16(t, o, 10);
    o = put16(t, o, 1); o = put16(t, o, 1); o = put16(t, o, 5);  // cov fmt1: {5}
    o = put16(t, o, 1); o = put16(t, o, 5); o = put16(t, o, 1); o = put16(t, o, 1);
    o = put16(t, o, 1); o = put16(t, o, 7); o = put16(t, o, 1); o = put16(t, o, 1);
    CHECK(o == LL + 150, "synthetic lookup 1 lays out");
    // L2 @ LL+150: type 9 Extension -> fmt1 (pair 9,5 = -25) at +8.
    o = put16(t, o, 9); o = put16(t, o, 0); o = put16(t, o, 1);
    o = put16(t, o, 8);
    o = put16(t, o, 1); o = put16(t, o, 2); o = put32(t, o, 8); // ext: type 2 @ +8
    o = put16(t, o, 1); o = put16(t, o, 12);            // inner fmt1, cov @ +12
    o = put16(t, o, 0x4); o = put16(t, o, 0);
    o = put16(t, o, 1); o = put16(t, o, 18);
    o = put16(t, o, 1); o = put16(t, o, 1); o = put16(t, o, 9);  // cov fmt1: {9}
    o = put16(t, o, 1); o = put16(t, o, 5); o = put16(t, o, (uint32_t)(int32_t)-25);
    CHECK(o == TOTAL, "synthetic table lays out to its full size");

    uint32_t slotGids[10];
    for (uint32_t i = 0; i < 10; i++)
        slotGids[i] = i;
    slotGids[3] = 0xFFFFFFFFu; // absent slot: must never touch the matrix
    int32_t dense[100] = { 0 };
    CHECK(ano_gpos_extract_kerns(t, TOTAL, slotGids, 10, dense) == 0,
          "synthetic table parses");
    CHECK(dense[5 * 10 + 7] == -40, "fmt1 wins over fmt2, lookups accumulate (-50 +10)");
    CHECK(dense[5 * 10 + 8] == -30, "fmt1 miss falls through to the fmt2 classes");
    CHECK(dense[5 * 10 + 9] == -70, "unlisted second glyph kerns as class 0");
    CHECK(dense[6 * 10 + 7] == -30, "coverage fmt2 range + classdef fmt1");
    CHECK(dense[9 * 10 + 5] == -25, "extension-wrapped PairPos applies");
    CHECK(dense[7 * 10 + 7] == 0 && dense[0] == 0, "uncovered pairs stay zero");
    int32_t rowcol3 = 0;
    for (uint32_t i = 0; i < 10; i++)
        rowcol3 |= dense[3 * 10 + i] | dense[i * 10 + 3];
    CHECK(rowcol3 == 0, "absent slots kern nothing");

    memset(dense, 0, sizeof dense);
    CHECK(ano_gpos_extract_kerns(t, 100, slotGids, 10, dense) != 0,
          "truncated table fails soft");

    static uint8_t noKern[TOTAL];
    memcpy(noKern, t, TOTAL);
    put32(noKern, FL + 2, 0x6D61726Bu); // feature tag 'kern' -> 'mark'
    memset(dense, 0, sizeof dense);
    CHECK(ano_gpos_extract_kerns(noKern, TOTAL, slotGids, 10, dense) == 0,
          "non-kern features parse to nothing");
    int32_t any = 0;
    for (uint32_t i = 0; i < 100; i++)
        any |= dense[i];
    CHECK(any == 0, "non-kern features contribute no pairs");
}

// The Geist kern oracle (fontTools audit of GPOS 'kern' for latn, ASCII x ASCII):
// 2891 nonzero pairs summing to -63296 font units, spot-checked on classic pairs.
static void test_kern_oracle(const AnoFontBake *b)
{
    CHECK(b->kernCount == 2891, "ASCII kern pair count matches the fontTools audit");
    int64_t sum = 0;
    bool sorted = true;
    for (uint32_t i = 0; i < b->kernCount; i++)
    {
        sum += lround((double)b->kerns[i].xAdvance * (double)b->upem);
        if (i > 0 && b->kerns[i - 1].key >= b->kerns[i].key)
            sorted = false;
    }
    CHECK(sum == -63296, "kern value sum matches the fontTools audit");
    CHECK(sorted, "pair table is strictly key-sorted (binary-search precondition)");

    double invUpem = 1.0 / (double)b->upem; // mirrors the bake's conversion exactly
    static const struct { char l, r; int32_t funits; } spot[] = {
        { 'A', 'V', -106 }, { 'V', 'A', -106 }, { 'T', 'o', -80 }, { 'L', 'T', -140 },
        { 'Y', 'o', -70 },  { 'r', '.', -60 },  { 'F', '.', -80 }, { 'A', 'v', -60 },
        { 'A', ' ', 0 },    { ' ', 'B', 0 },
    };
    for (size_t i = 0; i < sizeof spot / sizeof *spot; i++)
    {
        float expect = (float)((double)spot[i].funits * invUpem);
        CHECK(ano_text_kern(b, (uint32_t)(spot[i].l - 32), (uint32_t)(spot[i].r - 32)) == expect,
              "spot kern pair matches the audit bitwise");
    }
    CHECK(ano_text_kern(b, 1000, 0) == 0.0f && ano_text_kern(b, 0, 1000) == 0.0f,
          "out-of-range slots kern zero");
    CHECK(ano_text_kern(NULL, 0, 0) == 0.0f, "NULL bake kerns zero");
}

static void test_shaper(const AnoFontBake *b)
{
    const float S = 32.0f;
    const float org[2] = { 100.0f, 200.0f };
    const float col[4] = { 1.0f, 0.5f, 0.25f, 1.0f };
    AnoGlyphInstance inst[8];

    CHECK(ano_text_shape(b, "AV", 2, S, org, col, NULL, 0, NULL) == 2, "count mode needs no buffer");
    uint32_t n = ano_text_shape(b, "AV", 2, S, org, col, inst, 8, NULL);
    CHECK(n == 2, "AV emits two instances");
    CHECK(inst[0].origin[0] == org[0] && inst[0].origin[1] == org[1], "first glyph at the origin");
    float expX = org[0] + b->glyphs['A' - 32].advance * S; // mirrors the shaper's op order
    expX += ano_text_kern(b, 'A' - 32, 'V' - 32) * S;      // ...including the pair kern
    CHECK(inst[1].origin[0] == expX && inst[1].origin[1] == org[1], "kerned advance is exact");
    CHECK(inst[1].origin[0] < org[0] + b->glyphs['A' - 32].advance * S,
          "AV actually kerns tighter than the bare advance");
    CHECK(inst[0].glyphID == 'A' - 32 && inst[1].glyphID == 'V' - 32, "directory slots");
    CHECK(inst[0].inv[0] == 1.0f / S && inst[0].inv[1] == 0.0f && inst[0].inv[2] == 0.0f
              && inst[0].inv[3] == -1.0f / S,
          "v0 inverse is scale plus y-flip only");
    CHECK(inst[0].color[1] == 0.5f && inst[0].flags == 0, "color copied, flags clear");

    n = ano_text_shape(b, "A B", 3, S, org, col, inst, 8, NULL);
    CHECK(n == 2, "space emits nothing");
    expX = org[0] + b->glyphs['A' - 32].advance * S;
    expX += ano_text_kern(b, 'A' - 32, 0) * S;  // blanks join the pair chain (zero here)
    expX += b->glyphs[0].advance * S;           // slot 0 = space
    expX += ano_text_kern(b, 0, 'B' - 32) * S;
    CHECK(inst[1].origin[0] == expX, "space still advances the pen");

    float pen[2] = { 0 };
    n = ano_text_shape(b, "A\r\nB", 4, S, org, col, inst, 8, pen);
    CHECK(n == 2, "CRLF emits two glyphs");
    CHECK(inst[1].origin[0] == org[0] && inst[1].origin[1] == org[1] + b->lineHeight * S,
          "newline returns x to origin and steps one line down");
    CHECK(pen[0] == org[0] + b->glyphs['B' - 32].advance * S && pen[1] == inst[1].origin[1],
          "penOut lands after the last glyph");

    // Splitting a run and continuing from penOut is bitwise identical when the split
    // point doesn't kern (kerning never bridges calls -- split at a space, as the
    // header documents; Geist kerns 'A '/' B' zero, pinned by the kern oracle).
    AnoGlyphInstance whole[2], second[1];
    ano_text_shape(b, "A B", 3, S, org, col, whole, 2, NULL);
    ano_text_shape(b, "A ", 2, S, org, col, inst, 8, pen);
    ano_text_shape(b, "B", 1, S, pen, col, second, 1, NULL);
    CHECK(memcmp(&whole[1], &second[0], sizeof(AnoGlyphInstance)) == 0,
          "run continuation via penOut is exact");

    CHECK(ano_text_shape(b, "ABC", 3, S, org, col, inst, 1, NULL) == 3,
          "cap truncates writes, not the reported need");

    n = ano_text_shape(b, "A\xC3\xA9" "B", 4, S, org, col, inst, 8, NULL);
    CHECK(n == 2, "unbaked codepoint emits nothing");
    expX = org[0] + b->glyphs['A' - 32].advance * S;
    expX += ANO_TEXT_GAP_EM * S;
    CHECK(inst[1].origin[0] == expX, "unbaked codepoint leaves the documented gap");

    float w = -1.0f, h = -1.0f;
    ano_text_measure(b, "AB\nA", 4, S, &w, &h);
    float lineAB = b->glyphs['A' - 32].advance * S;
    lineAB += ano_text_kern(b, 'A' - 32, 'B' - 32) * S; // mirrors measure's op order
    lineAB += b->glyphs['B' - 32].advance * S;
    CHECK(w == lineAB, "measure width is the widest line (kerned)");
    CHECK(h == (2.0f * b->lineHeight) * S, "measure height covers both lines");
    ano_text_measure(b, "", 0, S, &w, &h);
    CHECK(w == 0.0f && h == 0.0f, "empty text measures zero");
}

static void test_shaper_runs(const AnoFontBake *b)
{
    const float S = 32.0f;
    const float org[2] = { 100.0f, 200.0f };
    const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    AnoGlyphInstance plain[8], styled[8];
    float penA[2], penB[2];

    // Same-size color splits landing INSIDE kerning pairs: one pen walks the whole
    // text, so positions/slots/penOut are bit-identical to the unsplit shape (the pair
    // chain bridges a color boundary) while each glyph wears its run's color.
    uint32_t n = ano_text_shape(b, "AV LT", 5, S, org, red, plain, 8, penA);
    const AnoTextRun split[3] = {
        { 1, S, { 1.0f, 0.0f, 0.0f, 1.0f } }, // "A
        { 3, S, { 0.0f, 0.0f, 1.0f, 1.0f } }, //  V L
        { 1, S, { 0.0f, 1.0f, 0.0f, 1.0f } }, //  T"
    };
    CHECK(ano_text_shape_runs(b, "AV LT", split, 3, org, styled, 8, penB) == n,
          "color-split count matches the unsplit shape");
    for (uint32_t i = 0; i < n; i++)
        CHECK(styled[i].origin[0] == plain[i].origin[0]
                  && styled[i].origin[1] == plain[i].origin[1]
                  && styled[i].glyphID == plain[i].glyphID && styled[i].inv[0] == plain[i].inv[0],
              "a same-size color run never moves a glyph");
    CHECK(penA[0] == penB[0] && penA[1] == penB[1], "penOut is split-invariant");
    CHECK(styled[0].color[0] == 1.0f && styled[1].color[2] == 1.0f && styled[2].color[2] == 1.0f
              && styled[3].color[1] == 1.0f,
          "each glyph wears its run's color");

    // An empty run, even at another size, can't break the bridge: the chain compares
    // against the size that SHAPED the previous glyph, not the previous run.
    const AnoTextRun hollow[3] = {
        { 1, S, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { 0, 2.0f * S, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { 1, S, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    CHECK(ano_text_shape_runs(b, "AV", hollow, 3, org, styled, 8, NULL) == 2
              && styled[1].origin[0] == plain[1].origin[0],
          "an empty run is invisible to the pair chain");

    // A SIZE boundary resets the chain: the right glyph lands at the bare advance and
    // both instances carry their own run's scale in the inverse.
    const AnoTextRun sized[2] = {
        { 1, S, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { 1, 2.0f * S, { 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    CHECK(ano_text_shape_runs(b, "AV", sized, 2, org, styled, 8, NULL) == 2, "sized AV shapes");
    CHECK(styled[1].origin[0] == org[0] + b->glyphs['A' - 32].advance * S,
          "a size boundary suppresses the pair kern");
    CHECK(styled[0].inv[0] == 1.0f / S && styled[1].inv[0] == 1.0f / (2.0f * S)
              && styled[1].inv[3] == -1.0f / (2.0f * S),
          "per-run size lands in the instance inverse");

    // '\n' steps by the lineHeight of the run holding the newline itself.
    const AnoTextRun nlFirst[2] = {
        { 2, S, { 1.0f, 0.0f, 0.0f, 1.0f } },          // "A\n"
        { 1, 2.0f * S, { 0.0f, 0.0f, 1.0f, 1.0f } },   // "B"
    };
    CHECK(ano_text_shape_runs(b, "A\nB", nlFirst, 2, org, styled, 8, NULL) == 2
              && styled[1].origin[1] == org[1] + b->lineHeight * S,
          "newline steps by its own run's line height");
    const AnoTextRun nlSecond[2] = {
        { 1, S, { 1.0f, 0.0f, 0.0f, 1.0f } },          // "A"
        { 2, 2.0f * S, { 0.0f, 0.0f, 1.0f, 1.0f } },   // "\nB"
    };
    CHECK(ano_text_shape_runs(b, "A\nB", nlSecond, 2, org, styled, 8, NULL) == 2
              && styled[1].origin[1] == org[1] + b->lineHeight * (2.0f * S),
          "a newline owned by the second run steps at ITS size");

    // A boundary inside a multi-byte sequence: the lead byte's run styles the whole
    // codepoint (here an unbaked e-acute's gap advance); the next run picks up after.
    const AnoTextRun straddle[2] = {
        { 2, S, { 1.0f, 0.0f, 0.0f, 1.0f } },          // "A" + the C3 lead byte
        { 2, 2.0f * S, { 0.0f, 0.0f, 1.0f, 1.0f } },   // the A9 tail + "B"
    };
    CHECK(ano_text_shape_runs(b, "A\xC3\xA9" "B", straddle, 2, org, styled, 8, NULL) == 2,
          "a straddled codepoint never splits");
    CHECK(styled[1].origin[0] == org[0] + b->glyphs['A' - 32].advance * S + ANO_TEXT_GAP_EM * S,
          "the gap advance takes the lead byte's size");
    CHECK(styled[1].color[2] == 1.0f && styled[1].inv[0] == 1.0f / (2.0f * S),
          "the next codepoint takes the next run's style");

    // Sizing call, cap truncation, and validation mirror ano_text_shape.
    CHECK(ano_text_shape_runs(b, "AV LT", split, 3, org, NULL, 0, NULL) == n,
          "count mode needs no buffer");
    CHECK(ano_text_shape_runs(b, "AV LT", split, 3, org, styled, 1, NULL) == n,
          "cap truncates writes, not the reported need");
    const AnoTextRun dead[1] = { { 2, 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f } } };
    CHECK(ano_text_shape_runs(b, "AV", dead, 1, org, styled, 8, NULL) == 0
              && ano_text_shape_runs(b, "AV", NULL, 1, org, styled, 8, NULL) == 0
              && ano_text_shape_runs(b, "AV", split, 0, org, styled, 8, NULL) == 0
              && ano_text_shape_runs(b, NULL, split, 3, org, styled, 8, NULL) == 0,
          "zero size, NULL runs, zero count, NULL text all reject");

    // measure_runs: widest kerned line, height as the sum of per-line steps (each line
    // at the size in effect at its end); uniform-run width matches ano_text_measure.
    const AnoTextRun mixed[2] = {
        { 3, S, { 1.0f, 0.0f, 0.0f, 1.0f } },          // "AB\n"
        { 1, 2.0f * S, { 0.0f, 0.0f, 1.0f, 1.0f } },   // "A"
    };
    float w = -1.0f, h = -1.0f;
    ano_text_measure_runs(b, "AB\nA", mixed, 2, &w, &h);
    float lineAB = b->glyphs['A' - 32].advance * S;
    lineAB += ano_text_kern(b, 'A' - 32, 'B' - 32) * S; // mirrors the core's op order
    lineAB += b->glyphs['B' - 32].advance * S;
    CHECK(w == fmaxf(lineAB, b->glyphs['A' - 32].advance * (2.0f * S)),
          "runs width is the widest line across sizes");
    CHECK(h == b->lineHeight * S + b->lineHeight * (2.0f * S),
          "runs height sums per-line steps at each line's ending size");
    const AnoTextRun uni[1] = { { 4, S, { 1.0f, 1.0f, 1.0f, 1.0f } } };
    float w2 = -1.0f, h2 = -1.0f;
    ano_text_measure(b, "AB\nA", 4, S, &w2, &h2);
    ano_text_measure_runs(b, "AB\nA", uni, 1, &w, &h);
    CHECK(w == w2, "uniform-run width is bit-identical to ano_text_measure");
    const AnoTextRun none[1] = { { 0, S, { 1.0f, 1.0f, 1.0f, 1.0f } } };
    ano_text_measure_runs(b, "", none, 1, &w, &h);
    CHECK(w == 0.0f && h == 0.0f, "empty runs measure zero");
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    test_lifecycle();
    test_half();
    test_quad_split();
    test_cubic();
    test_utf8();
    test_gpos_synthetic();

    CHECK(ano_fs_chdir_gamepath(), "chdir to the exe directory (staged font root)");
    CHECK(ano_text_init() == 0, "init for bake tests");

    CHECK(ano_text_font_load("resources/fonts/does-not-exist.ttf") == 0,
          "loading a missing file fails cleanly");
    AnoFontId geist = ano_text_font_load(FONT_PATH);
    CHECK(geist != 0, "Geist-Regular loads");

    mi_heap_t *heapA LOCALHEAPATTR = mi_heap_new();
    mi_heap_t *heapB LOCALHEAPATTR = mi_heap_new();
    CHECK(heapA != NULL && heapB != NULL, "bake heaps");

    AnoFontBake bake = { 0 };
    CHECK(ano_text_font_bake(0, 32, 126, heapA, &bake) == EINVAL, "bad handle rejected");
    CHECK(ano_text_font_bake(geist, 126, 32, heapA, &bake) == EINVAL, "reversed range rejected");
    CHECK(ano_text_font_bake(geist, 32, 126, NULL, &bake) == EINVAL, "NULL heap rejected");

    CHECK(ano_text_font_bake(geist, 32, 126, heapA, &bake) == 0, "ASCII bake succeeds");
    validate_bake(&bake);
    test_kern_oracle(&bake);
    test_reference_raster(geist, &bake);
    test_ghost_pixels(geist, &bake);
    test_shaper(&bake);
    test_shaper_runs(&bake);

    // Determinism: a second bake is bit-identical (double math, fixed iteration order).
    AnoFontBake again = { 0 };
    CHECK(ano_text_font_bake(geist, 32, 126, heapB, &again) == 0, "second bake succeeds");
    CHECK(again.pointCount == bake.pointCount && again.glyphCount == bake.glyphCount
              && again.kernCount == bake.kernCount,
          "second bake has identical shape");
    if (again.pointCount == bake.pointCount && again.glyphCount == bake.glyphCount
        && again.kernCount == bake.kernCount)
    {
        CHECK(memcmp(again.points, bake.points, bake.pointCount * 4u) == 0,
              "point streams are bit-identical");
        CHECK(memcmp(again.glyphs, bake.glyphs, bake.glyphCount * sizeof(AnoGlyphEntry)) == 0,
              "directories are bit-identical");
        CHECK(memcmp(again.kerns, bake.kerns, bake.kernCount * sizeof(AnoKernPair)) == 0,
              "kern tables are bit-identical");
    }

    ano_text_shutdown();

    if (failures == 0)
        printf("anotest_text: all checks passed\n");
    else
        printf("anotest_text: %d check(s) FAILED\n", failures);
    return failures ? 1 : 0;
}
