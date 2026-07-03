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
 *   - the v0 shaper (step 4): strict UTF-8 decode (overlong/surrogate/range/resync
 *     cases), golden layout over the Geist bake -- exact pen advances, blank glyphs
 *     advancing without emitting, newline/CR handling, out-of-range gap, cap
 *     truncation vs total count, bitwise run continuation via penOut, measure
 *     extents, and the 48-byte GPU ABI offsets of AnoGlyphInstance.
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
    CHECK(inst[1].origin[0] == expX && inst[1].origin[1] == org[1], "advance is exact");
    CHECK(inst[0].glyphID == 'A' - 32 && inst[1].glyphID == 'V' - 32, "directory slots");
    CHECK(inst[0].inv[0] == 1.0f / S && inst[0].inv[1] == 0.0f && inst[0].inv[2] == 0.0f
              && inst[0].inv[3] == -1.0f / S,
          "v0 inverse is scale plus y-flip only");
    CHECK(inst[0].color[1] == 0.5f && inst[0].flags == 0, "color copied, flags clear");

    n = ano_text_shape(b, "A B", 3, S, org, col, inst, 8, NULL);
    CHECK(n == 2, "space emits nothing");
    expX = org[0] + b->glyphs['A' - 32].advance * S;
    expX += b->glyphs[0].advance * S; // slot 0 = space
    CHECK(inst[1].origin[0] == expX, "space still advances the pen");

    float pen[2] = { 0 };
    n = ano_text_shape(b, "A\r\nB", 4, S, org, col, inst, 8, pen);
    CHECK(n == 2, "CRLF emits two glyphs");
    CHECK(inst[1].origin[0] == org[0] && inst[1].origin[1] == org[1] + b->lineHeight * S,
          "newline returns x to origin and steps one line down");
    CHECK(pen[0] == org[0] + b->glyphs['B' - 32].advance * S && pen[1] == inst[1].origin[1],
          "penOut lands after the last glyph");

    // Splitting a run at any point and continuing from penOut is bitwise identical.
    AnoGlyphInstance whole[2], second[1];
    ano_text_shape(b, "AB", 2, S, org, col, whole, 2, NULL);
    ano_text_shape(b, "A", 1, S, org, col, inst, 8, pen);
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
    lineAB += b->glyphs['B' - 32].advance * S;
    CHECK(w == lineAB, "measure width is the widest line");
    CHECK(h == (2.0f * b->lineHeight) * S, "measure height covers both lines");
    ano_text_measure(b, "", 0, S, &w, &h);
    CHECK(w == 0.0f && h == 0.0f, "empty text measures zero");
}

// ---------------------------------------------------------------------------------------------

int main(void)
{
    test_lifecycle();
    test_half();
    test_quad_split();
    test_cubic();
    test_utf8();

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
    test_reference_raster(geist, &bake);
    test_shaper(&bake);

    // Determinism: a second bake is bit-identical (double math, fixed iteration order).
    AnoFontBake again = { 0 };
    CHECK(ano_text_font_bake(geist, 32, 126, heapB, &again) == 0, "second bake succeeds");
    CHECK(again.pointCount == bake.pointCount && again.glyphCount == bake.glyphCount,
          "second bake has identical shape");
    if (again.pointCount == bake.pointCount && again.glyphCount == bake.glyphCount)
    {
        CHECK(memcmp(again.points, bake.points, bake.pointCount * 4u) == 0,
              "point streams are bit-identical");
        CHECK(memcmp(again.glyphs, bake.glyphs, bake.glyphCount * sizeof(AnoGlyphEntry)) == 0,
              "directories are bit-identical");
    }

    ano_text_shutdown();

    if (failures == 0)
        printf("anotest_text: all checks passed\n");
    else
        printf("anotest_text: %d check(s) FAILED\n", failures);
    return failures ? 1 : 0;
}
