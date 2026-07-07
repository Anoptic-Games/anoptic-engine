/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for anoptic_ui.h -- the UI prim ABI, builder, and reference evaluator
 * (docs/ui/ui-render.md §7 step 2, the pre-GPU validation gate):
 *   - ABI: runtime echo of the static_assert'd std430 layout (96/48/48/32 B);
 *   - builder: field packing goldens, radii clamp (negative, over-cap), sigma floor,
 *     full-array refusal (ANO_UI_REF_NONE, no mutation), clip rect/rounded sentinel;
 *   - rrect SDF: Euclidean exactness on edge/corner rays (d == t), sign agreement
 *     with a double-precision geometric inside predicate over a jittered grid;
 *   - rrect/border/clip coverage vs a 64x64-supersampled double oracle per pixel
 *     window: straight-edge windows are exact (the ramp IS the half-plane box
 *     filter); corner-zone windows carry the documented approximation, bounds
 *     pinned from the first measured run and asserted since;
 *   - shadow: Wallace closed form vs ground truth = 4x-supersampled rrect mask
 *     convolved with a separable discrete Gaussian (double), rect and rounded
 *     cases; erf approx + 4-sample quadrature + 3-sigma truncation all land inside
 *     the pinned envelope;
 *   - blend semantics: painter's order (later on top), ADD accumulates rgb without
 *     occluding, PATH/GLYPHS kinds contribute zero in this lane, evaluation purity.
 * Oracles are all in double; the evaluator mirrors future GLSL in float.
 * Deterministic (fixed seed); argv[1] scales the jittered-probe soak. Exit 0 = pass. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anoptic_ui.h"
#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// ---------------------------------------------------------------------------------------------
// Double-precision geometry oracles.

// Point-in-rounded-rect, coordinates relative to center, per-corner radii (tl,tr,br,bl).
static bool inside_rrect(double x, double y, double hx, double hy, const double r[4])
{
    if (fabs(x) > hx || fabs(y) > hy)
        return false;
    double rr = x >= 0.0 ? (y >= 0.0 ? r[2] : r[1]) : (y >= 0.0 ? r[3] : r[0]);
    if (rr <= 0.0)
        return true;
    double ax = fabs(x), ay = fabs(y);
    double cx = hx - rr, cy = hy - rr;
    if (ax <= cx || ay <= cy)
        return true;
    double dx = ax - cx, dy = ay - cy;
    return dx * dx + dy * dy <= rr * rr;
}

// Ring predicate: inside the rrect but not inside its w-erosion (erosion of an exact
// rounded box = rounded box with half-w extents and max(r-w, 0) radii).
static bool inside_ring(double x, double y, double hx, double hy, const double r[4], double w)
{
    if (!inside_rrect(x, y, hx, hy, r))
        return false;
    double ri[4];
    for (int i = 0; i < 4; i++)
        ri[i] = r[i] - w > 0.0 ? r[i] - w : 0.0;
    return !inside_rrect(x, y, hx - w, hy - w, ri);
}

// 64x64-supersampled coverage of a predicate over the unit window [px,px+1)x[py,py+1),
// coordinates relative to the shape center at (cx,cy).
#define SS 64
typedef bool (*inside_fn)(double x, double y, void *u);

static double oracle_cov(inside_fn f, void *u, double px, double py, double cx, double cy)
{
    int hits = 0;
    for (int sy = 0; sy < SS; sy++)
        for (int sx = 0; sx < SS; sx++)
            if (f(px + (sx + 0.5) / SS - cx, py + (sy + 0.5) / SS - cy, u))
                hits++;
    return (double)hits / (SS * SS);
}

typedef struct { double hx, hy; double r[4]; double w; } RrShape;
static bool pred_fill(double x, double y, void *u)
{
    RrShape *s = u;
    return inside_rrect(x, y, s->hx, s->hy, s->r);
}
static bool pred_ring(double x, double y, void *u)
{
    RrShape *s = u;
    return inside_ring(x, y, s->hx, s->hy, s->r, s->w);
}

// ---------------------------------------------------------------------------------------------
// ABI echo.

static void test_abi(void)
{
    CHECK(sizeof(AnoUiPrim) == 96 && sizeof(AnoUiClip) == 48
              && sizeof(AnoUiPaint) == 48 && sizeof(AnoUiStop) == 32,
          "ABI sizes");
    CHECK(offsetof(AnoUiPrim, origin) == 16 && offsetof(AnoUiPrim, kind) == 24
              && offsetof(AnoUiPrim, flags) == 28 && offsetof(AnoUiPrim, half) == 32
              && offsetof(AnoUiPrim, param) == 40 && offsetof(AnoUiPrim, radii) == 48
              && offsetof(AnoUiPrim, color) == 64 && offsetof(AnoUiPrim, paintRef) == 80
              && offsetof(AnoUiPrim, aux1) == 92,
          "ABI prim offsets");
    CHECK(offsetof(AnoUiClip, rrCenter) == 16 && offsetof(AnoUiClip, rrRadii) == 32,
          "ABI clip offsets");
}

// ---------------------------------------------------------------------------------------------
// Builder goldens.

static void test_builder(void)
{
    AnoUiPrim prims[4];
    AnoUiClip clips[2];
    AnoUiBuilder b;
    ano_ui_builder_init(&b, prims, 4, clips, 2, NULL, 0, NULL, 0);
    CHECK(b.primCount == 0 && b.clipCount == 0 && b.paintCap == 0, "builder init zeroes");

    // Packing + radii clamp: negatives to 0, over-cap to min(half) (=20 here).
    float rad[4] = { -5.0f, 50.0f, 10.0f, 3.0f };
    float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    uint32_t i0 = ano_ui_rrect(&b, (float[2]){ 10, 20 }, (float[2]){ 50, 60 }, rad, red,
                               -2.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    CHECK(i0 == 0 && b.primCount == 1, "rrect claims slot 0");
    CHECK(prims[0].origin[0] == 30.0f && prims[0].origin[1] == 40.0f
              && prims[0].half[0] == 20.0f && prims[0].half[1] == 20.0f,
          "rrect center/half from min/max");
    CHECK(prims[0].inv[0] == 1.0f && prims[0].inv[1] == 0.0f && prims[0].inv[3] == 1.0f,
          "identity inv");
    CHECK(prims[0].radii[0] == 0.0f && prims[0].radii[1] == 20.0f
              && prims[0].radii[2] == 10.0f && prims[0].radii[3] == 3.0f,
          "radii clamp: negative to 0, over-cap to min(half)");
    CHECK(prims[0].param[0] == 0.0f, "negative border width clamps to fill");
    CHECK(prims[0].kind == ANO_UI_RRECT && prims[0].paintRef == ANO_UI_REF_NONE, "kind + refs");

    // Shadow: sigma floor, uniform corner capped at min(half) (=10 on a 60x20 box).
    uint32_t i1 = ano_ui_shadow(&b, (float[2]){ 0, 0 }, (float[2]){ 60, 20 }, 999.0f, 0.0f,
                                red, ANO_UI_REF_NONE, 0);
    CHECK(i1 == 1 && prims[1].kind == ANO_UI_SHADOW, "shadow claims slot 1");
    CHECK(prims[1].radii[0] == 10.0f && prims[1].radii[3] == 10.0f, "shadow corner capped, uniform");
    CHECK(prims[1].param[0] == 1e-3f, "sigma floor");

    // Image + glyphs aux packing.
    uint32_t i2 = ano_ui_image(&b, (float[2]){ 0, 0 }, (float[2]){ 32, 32 }, NULL, 7, -1.0f,
                               red, ANO_UI_REF_NONE, 0);
    CHECK(i2 == 2 && prims[2].aux0 == 7 && prims[2].param[0] == 0.0f, "image tex index + lod clamp");
    uint32_t i3 = ano_ui_glyphs(&b, (float[2]){ 0, 0 }, (float[2]){ 100, 20 }, 40, 12, red,
                                ANO_UI_REF_NONE, 0);
    CHECK(i3 == 3 && prims[3].aux0 == 40 && prims[3].aux1 == 12, "glyphs range packing");

    // Full prim array refuses without mutation.
    AnoUiPrim before = prims[3];
    uint32_t iFull = ano_ui_path(&b, (float[2]){ 0, 0 }, (float[2]){ 1, 1 }, 0, 0, red,
                                 ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    CHECK(iFull == ANO_UI_REF_NONE && b.primCount == 4, "full prim array refused");
    CHECK(memcmp(&before, &prims[3], sizeof before) == 0, "refusal mutates nothing");

    // Clips: rect-only sentinel, rounded term packs + clamps.
    uint32_t c0 = ano_ui_clip(&b, (float[2]){ 5, 5 }, (float[2]){ 95, 45 }, NULL, NULL, NULL);
    CHECK(c0 == 0 && clips[0].rrHalf[0] < 0.0f, "rect-only clip sentinel");
    float crad[4] = { 4, 4, 4, 4 };
    uint32_t c1 = ano_ui_clip(&b, (float[2]){ 0, 0 }, (float[2]){ 10, 10 },
                              (float[2]){ 2, 2 }, (float[2]){ 8, 8 }, crad);
    CHECK(c1 == 1 && clips[1].rrHalf[0] == 3.0f && clips[1].rrRadii[0] == 3.0f,
          "rounded clip packs + clamps radii to min(half)");
    CHECK(ano_ui_clip(&b, (float[2]){ 0, 0 }, (float[2]){ 1, 1 }, NULL, NULL, NULL)
              == ANO_UI_REF_NONE,
          "full clip array refused");

    AnoUiScene s = ano_ui_scene(&b);
    CHECK(s.primCount == 4 && s.clipCount == 2, "scene view counts");
}

// ---------------------------------------------------------------------------------------------
// SDF exactness: rays hit d == t, sign agrees with the predicate everywhere off-boundary.

static void test_sdf(uint32_t soak)
{
    const float half[2] = { 30.0f, 20.0f };
    const float radii[4] = { 0.0f, 5.0f, 12.0f, 20.0f };
    const double dradii[4] = { 0.0, 5.0, 12.0, 20.0 };

    // Straight-edge rays (points chosen on the edges' straight spans).
    for (int k = 0; k < 4; k++) {
        float t = (float[]){ -5.0f, 0.0f, 3.0f, 17.0f }[k];
        float d = ano_ui_ref_sd_rrect((float[2]){ 30.0f + t, 0.0f }, half, radii);
        CHECK(fabsf(d - t) < 1e-4f, "right-edge ray d == t");
        d = ano_ui_ref_sd_rrect((float[2]){ 0.0f, -20.0f - t }, half, radii);
        CHECK(fabsf(d - t) < 1e-4f, "top-edge ray d == t");
    }
    // Corner-arc ray through the br corner (r = 12), circle center (18, 8).
    for (int k = 0; k < 5; k++) {
        float t = (float[]){ -6.0f, -2.0f, 0.0f, 4.0f, 15.0f }[k];
        float s = 0.70710678f;
        float p[2] = { 18.0f + s * (12.0f + t), 8.0f + s * (12.0f + t) };
        float d = ano_ui_ref_sd_rrect(p, half, radii);
        CHECK(fabsf(d - t) < 1e-3f, "br corner-arc ray d == t");
    }
    // Sharp tl corner (r = 0): outside diagonal distance is Euclidean to the point.
    {
        float d = ano_ui_ref_sd_rrect((float[2]){ -33.0f, -24.0f }, half, radii);
        CHECK(fabsf(d - 5.0f) < 1e-4f, "sharp-corner point distance");
    }
    // Jittered sign sweep vs the predicate.
    test_rng rng = rng_make(0x0517AB1Eu);
    uint64_t probes = 0, skipped = 0;
    for (uint32_t it = 0; it < soak; it++) {
        for (double y = -26.0; y <= 26.0; y += 0.5) {
            for (double x = -36.0; x <= 36.0; x += 0.5) {
                double jx = x + (rng_below(&rng, 1000) / 1000.0 - 0.5) * 0.25;
                double jy = y + (rng_below(&rng, 1000) / 1000.0 - 0.5) * 0.25;
                float d = ano_ui_ref_sd_rrect((float[2]){ (float)jx, (float)jy }, half, radii);
                if (fabsf(d) < 5e-3f) {
                    skipped++;
                    continue;
                }
                bool in = inside_rrect(jx, jy, 30.0, 20.0, dradii);
                if ((d < 0.0f) != in) {
                    printf("FAIL: SDF sign mismatch at (%.3f, %.3f), d=%f in=%d\n", jx, jy, d, in);
                    failures++;
                    return;
                }
                probes++;
            }
        }
    }
    printf("  sdf sign sweep: %llu probes, %llu boundary-skipped\n",
           (unsigned long long)probes, (unsigned long long)skipped);
}

// ---------------------------------------------------------------------------------------------
// Coverage vs supersampled oracle. Windows classify as corner-zone (center within
// r + 2.5 px of a box corner point) or edge-class; edge-class must be exact.

typedef struct {
    double edgeMax, cornerMax, rms;
    uint64_t n;
} CovStats;

// Truth source per window: supersampled predicate, or a closed form where one exists
// (the 64x64 oracle has a 1/64 quantum, so arbitrary-fraction edges need the exact form).
typedef double (*truth_fn)(void *u, int px, int py);

typedef struct { inside_fn f; void *u; double cx, cy; } SsTruth;
static double truth_ss(void *u, int px, int py)
{
    SsTruth *t = u;
    return oracle_cov(t->f, t->u, px, py, t->cx, t->cy);
}

typedef struct { double minx, miny, maxx, maxy; } RectTruth; // absolute canvas coords
static double truth_rect(void *u, int px, int py)
{
    RectTruth *t = u;
    double ox = fmax(0.0, fmin(px + 1.0, t->maxx) - fmax((double)px, t->minx));
    double oy = fmax(0.0, fmin(py + 1.0, t->maxy) - fmax((double)py, t->miny));
    return ox * oy;
}

// Corner zone: window center within (corner radius + border + 3.5) px of a box corner
// point — the border term folds the eroded shape's inner corners into the zone.
static void cov_canvas(const AnoUiScene *s, truth_fn truth, void *u, double cx, double cy,
                       const double half[2], const double radii[4], double border,
                       int x0, int y0, int x1, int y1, CovStats *st)
{
    st->edgeMax = st->cornerMax = st->rms = 0.0;
    st->n = 0;
    double sum2 = 0.0;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float out[4];
            ano_ui_ref_eval(s, (float)px, (float)py, out);
            double err = fabs(out[3] - truth(u, px, py)); // alpha carries coverage (color a=1)
            double wx = px + 0.5 - cx, wy = py + 0.5 - cy;
            bool corner = false;
            static const int rmap[4] = { 0, 1, 3, 2 }; // quadrant bits -> (tl,tr,bl,br) radius slot
            for (int q = 0; q < 4; q++) {
                double qx = (q & 1) ? half[0] : -half[0];
                double qy = (q & 2) ? half[1] : -half[1];
                double dx = wx - qx, dy = wy - qy;
                if (sqrt(dx * dx + dy * dy) < radii[rmap[q]] + border + 3.5)
                    corner = true;
            }
            if (corner)
                st->cornerMax = fmax(st->cornerMax, err);
            else
                st->edgeMax = fmax(st->edgeMax, err);
            sum2 += err * err;
            st->n++;
        }
    }
    st->rms = sqrt(sum2 / (double)st->n);
}

static void test_coverage(void)
{
    AnoUiPrim prims[1];
    AnoUiBuilder b;
    float white[4] = { 1, 1, 1, 1 };

    struct {
        const char *name;
        float min[2], max[2];
        float radii[4];
        float border;
        bool analytic; // exact rect truth (arbitrary fractions); else 1/64-aligned SS truth
        double edgeGate, cornerGate;
    } cases[] = {
        // Gates pinned from the measured run of 2026-07-07 (printed below each time):
        // rect analytic edge ~1e-7; r=8 corner 0.0375; mixed corner 0.1250 (its r=0 tip);
        // ring corner 0.1250 (inner sharp corners of the erosion).
        { "rect r=0 frac", { 10.3f, 10.7f }, { 50.6f, 40.2f }, { 0, 0, 0, 0 }, 0.0f, true,
          2e-3, 0.15 },
        { "rrect r=8", { 10.0f, 10.0f }, { 70.0f, 50.0f }, { 8, 8, 8, 8 }, 0.0f, false,
          2e-3, 0.05 },
        { "rrect mixed", { 10.5f, 10.25f }, { 70.5f, 50.25f }, { 0, 4, 10, 16 }, 0.0f, false,
          2e-3, 0.15 },
        { "ring w=3 mixed", { 10.5f, 10.25f }, { 70.5f, 50.25f }, { 0, 4, 10, 16 }, 3.0f, false,
          4e-3, 0.20 },
    };
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        ano_ui_builder_init(&b, prims, 1, NULL, 0, NULL, 0, NULL, 0);
        ano_ui_rrect(&b, cases[c].min, cases[c].max, cases[c].radii, white, cases[c].border,
                     ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
        AnoUiScene s = ano_ui_scene(&b);
        double cx = prims[0].origin[0], cy = prims[0].origin[1];
        double half[2] = { prims[0].half[0], prims[0].half[1] };
        double radii[4] = { prims[0].radii[0], prims[0].radii[1], prims[0].radii[2],
                            prims[0].radii[3] };
        RrShape shape = { half[0], half[1], { radii[0], radii[1], radii[2], radii[3] },
                          cases[c].border };
        SsTruth ss = { cases[c].border > 0.0f ? pred_ring : pred_fill, &shape, cx, cy };
        RectTruth rt = { cases[c].min[0], cases[c].min[1], cases[c].max[0], cases[c].max[1] };
        CovStats st;
        cov_canvas(&s, cases[c].analytic ? truth_rect : truth_ss,
                   cases[c].analytic ? (void *)&rt : (void *)&ss, cx, cy, half, radii,
                   cases[c].border, (int)cases[c].min[0] - 3, (int)cases[c].min[1] - 3,
                   (int)cases[c].max[0] + 4, (int)cases[c].max[1] + 4, &st);
        printf("  cov %-14s edgeMax %.5f cornerMax %.5f rms %.5f (%llu windows)\n",
               cases[c].name, st.edgeMax, st.cornerMax, st.rms, (unsigned long long)st.n);
        CHECK(st.edgeMax <= cases[c].edgeGate, "edge-class windows exact");
        CHECK(st.cornerMax <= cases[c].cornerGate, "corner-zone windows bounded");
    }
}

// Clip: rect clip cutting through a fill's interior stays exact away from the clip's
// own corners; a rounded clip term multiplies in with the same corner caveat.
static void test_clip(void)
{
    AnoUiPrim prims[1];
    AnoUiClip clips[1];
    AnoUiBuilder b;
    float white[4] = { 1, 1, 1, 1 };
    ano_ui_builder_init(&b, prims, 1, clips, 1, NULL, 0, NULL, 0);
    uint32_t clip = ano_ui_clip(&b, (float[2]){ 20.4f, 15.6f }, (float[2]){ 60.7f, 45.3f },
                                NULL, NULL, NULL);
    ano_ui_rrect(&b, (float[2]){ 10, 10 }, (float[2]){ 70, 50 }, NULL, white, 0.0f,
                 ANO_UI_REF_NONE, clip, 0);
    AnoUiScene s = ano_ui_scene(&b);
    // Sample rows crossing the clip edges but away from clip corners; the prim fully
    // covers these windows, so truth = the exact window/clip-rect overlap (closed form —
    // the SS oracle's 1/64 quantum would mask exactness at these fractions).
    RectTruth rt = { 20.4, 15.6, 60.7, 45.3 };
    double worst = 0.0;
    for (int py = 25; py < 35; py++)
        for (int px = 15; px < 66; px++) {
            float out[4];
            ano_ui_ref_eval(&s, (float)px, (float)py, out);
            worst = fmax(worst, fabs(out[3] - truth_rect(&rt, px, py)));
        }
    printf("  clip rect interior rows worst %.7f\n", worst);
    CHECK(worst <= 2e-3, "rect clip exact through prim interior");
}

// ---------------------------------------------------------------------------------------------
// Gradient paints: a solid rrect filled by each gradient kind; deep-interior windows
// have coverage 1, so the evaluator's output there IS the resolved fill. Truth is an
// independent analytic t plus a two-stop clamp-and-lerp, so the check exercises the
// builder's xform setup, the parameter map, and the stop interpolation end to end.

static void grad2_truth(const float c0[4], const float c1[4], double t, float out[4])
{
    double u = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t; // CSS pad
    for (int k = 0; k < 4; k++)
        out[k] = (float)(c0[k] + (c1[k] - c0[k]) * u);
}

static void test_gradient(void)
{
    AnoUiPrim prims[1];
    AnoUiPaint paints[1];
    AnoUiStop stops[2], in[2];
    AnoUiBuilder b;
    float white[4] = { 1, 1, 1, 1 };
    // Alpha-1 stops so premultiplied == straight and the lerp oracle stays simple.
    float c0[4] = { 0.05f, 0.10f, 0.20f, 1.0f };
    float c1[4] = { 0.80f, 0.60f, 0.30f, 1.0f };
    for (int k = 0; k < 4; k++) { in[0].color[k] = c0[k]; in[1].color[k] = c1[k]; }
    in[0].t = 0.0f; in[1].t = 1.0f;
    float mn[2] = { 20, 20 }, mx[2] = { 120, 100 };
    float r10[4] = { 10, 10, 10, 10 };

    // Deep interior of the box (windows fully inside, coverage exactly 1).
    const int ix0 = 45, iy0 = 45, ix1 = 95, iy1 = 75;

    struct { const char *name; int kind; float a[2], b2, ang; } cases[] = {
        { "linear", 0, { 30, 110 }, 0, 0 },   // t along x: 0 at x=30, 1 at x=110
        { "radial", 1, { 70, 60 }, 45, 0 },    // center (70,60), radius 45
        { "conic",  2, { 70, 60 }, 0, 0.0f },  // center (70,60), start angle 0
    };
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        ano_ui_builder_init(&b, prims, 1, NULL, 0, paints, 1, stops, 2);
        uint32_t g = ANO_UI_REF_NONE;
        if (cases[c].kind == 0)
            g = ano_ui_paint_linear(&b, (float[2]){ cases[c].a[0], 0 },
                                    (float[2]){ cases[c].a[1], 0 }, in, 2);
        else if (cases[c].kind == 1)
            g = ano_ui_paint_radial(&b, cases[c].a, cases[c].b2, in, 2);
        else
            g = ano_ui_paint_conic(&b, cases[c].a, cases[c].ang, in, 2);
        CHECK(g == 0u, "gradient paint pushed at index 0");
        ano_ui_rrect(&b, mn, mx, r10, white, 0.0f, g, ANO_UI_REF_NONE, 0);
        AnoUiScene s = ano_ui_scene(&b);
        double worst = 0.0;
        for (int py = iy0; py < iy1; py++)
            for (int px = ix0; px < ix1; px++) {
                float out[4], want[4];
                ano_ui_ref_eval(&s, (float)px, (float)py, out);
                double gx = px + 0.5, gy = py + 0.5, t;
                if (cases[c].kind == 0)
                    t = (gx - cases[c].a[0]) / (cases[c].a[1] - cases[c].a[0]);
                else if (cases[c].kind == 1)
                    t = sqrt((gx - cases[c].a[0]) * (gx - cases[c].a[0])
                             + (gy - cases[c].a[1]) * (gy - cases[c].a[1])) / cases[c].b2;
                else
                    t = atan2(gy - cases[c].a[1], gx - cases[c].a[0]) / 6.283185307179586 + 0.5;
                grad2_truth(c0, c1, t, want);
                for (int k = 0; k < 4; k++)
                    worst = fmax(worst, fabs(out[k] - want[k]));
            }
        printf("  gradient %-7s interior worst %.7f\n", cases[c].name, worst);
        CHECK(worst <= 1e-5, "gradient fill matches analytic");
    }

    // Base modulation: a tint on the fill prim scales the resolved paint component-wise.
    ano_ui_builder_init(&b, prims, 1, NULL, 0, paints, 1, stops, 2);
    uint32_t g = ano_ui_paint_linear(&b, (float[2]){ 30, 0 }, (float[2]){ 110, 0 }, in, 2);
    float tint[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    ano_ui_rrect(&b, mn, mx, r10, tint, 0.0f, g, ANO_UI_REF_NONE, 0);
    AnoUiScene s = ano_ui_scene(&b);
    double mworst = 0.0;
    for (int py = iy0; py < iy1; py++)
        for (int px = ix0; px < ix1; px++) {
            float out[4], want[4];
            ano_ui_ref_eval(&s, (float)px, (float)py, out);
            grad2_truth(c0, c1, (px + 0.5 - 30) / 80.0, want);
            for (int k = 0; k < 4; k++)
                mworst = fmax(mworst, fabs(out[k] - want[k] * 0.5f));
        }
    printf("  gradient modulate  worst %.7f\n", mworst);
    CHECK(mworst <= 1e-5, "base tint modulates the gradient fill");

    // Fail-closed: a paintRef past the table is transparent (a truncated table must not
    // leak). The builder never emits this; force it to mirror the shader's bound check.
    prims[0].paintRef = 7u; // paintCount is 1
    float out[4];
    ano_ui_ref_eval(&s, 70.0f, 60.0f, out);
    CHECK(out[0] == 0.0f && out[3] == 0.0f, "out-of-range paint fails closed");
}

// ---------------------------------------------------------------------------------------------
// Path fill: an axis-aligned rectangle baked as a 4-line path must reproduce the exact
// box coverage (curve_area is exact on straight edges), which validates the baker, the
// monotone sweep, the binary16 pack round-trip, and the winding auto-orientation. A
// reversed winding must still fill; an oppositely wound inner contour must punch a hole.

static void test_path(void)
{
    AnoUiPrim prims[1];
    uint32_t curves[512];
    AnoUiBuilder b;
    float white[4] = { 1, 1, 1, 1 };
    // Integer, small-half-extent coords so binary16 is effectively exact.
    RectTruth rt = { 20, 20, 100, 80 };

    // Clockwise-on-screen rectangle (4 lines, auto-closed).
    AnoUiPathSeg cw[] = {
        { ANO_UI_SEG_MOVE, { 20, 20, 0, 0 } },
        { ANO_UI_SEG_LINE, { 100, 20, 0, 0 } },
        { ANO_UI_SEG_LINE, { 100, 80, 0, 0 } },
        { ANO_UI_SEG_LINE, { 20, 80, 0, 0 } },
    };
    ano_ui_builder_init(&b, prims, 1, NULL, 0, NULL, 0, NULL, 0);
    ano_ui_builder_curves(&b, curves, 512);
    uint32_t idx = ano_ui_path_fill(&b, cw, 4, white, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    CHECK(idx == 0u, "path prim emitted");
    AnoUiScene s = ano_ui_scene(&b);
    double iworst = 0.0, eworst = 0.0;
    for (int py = 30; py < 70; py++)
        for (int px = 30; px < 90; px++) {
            float out[4];
            ano_ui_ref_eval(&s, (float)px, (float)py, out);
            iworst = fmax(iworst, fabs(out[3] - 1.0));
        }
    // Edge windows away from the corners: exact box coverage.
    for (int px = 30; px < 90; px++) {
        float top[4], bot[4];
        ano_ui_ref_eval(&s, (float)px, 19.0f, top); // straddles y=20
        ano_ui_ref_eval(&s, (float)px, 80.0f, bot); // straddles y=80
        eworst = fmax(eworst, fabs(top[3] - truth_rect(&rt, px, 19)));
        eworst = fmax(eworst, fabs(bot[3] - truth_rect(&rt, px, 80)));
    }
    printf("  path rect interior %.6f  edge %.6f  (%u words, %u seg)\n",
           iworst, eworst, b.curveCount, prims[0].aux1);
    CHECK(iworst <= 2e-3, "path rect interior filled");
    CHECK(eworst <= 4e-3, "path rect straight edges ~exact");

    // Reversed winding fills identically (auto-orientation).
    AnoUiPathSeg ccw[] = {
        { ANO_UI_SEG_MOVE, { 20, 20, 0, 0 } },
        { ANO_UI_SEG_LINE, { 20, 80, 0, 0 } },
        { ANO_UI_SEG_LINE, { 100, 80, 0, 0 } },
        { ANO_UI_SEG_LINE, { 100, 20, 0, 0 } },
    };
    ano_ui_builder_init(&b, prims, 1, NULL, 0, NULL, 0, NULL, 0);
    ano_ui_builder_curves(&b, curves, 512);
    ano_ui_path_fill(&b, ccw, 4, white, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    AnoUiScene s2 = ano_ui_scene(&b);
    float c[4];
    ano_ui_ref_eval(&s2, 60.0f, 50.0f, c);
    CHECK(fabs(c[3] - 1.0) <= 2e-3, "reversed winding still fills");

    // Hole: outer CW + inner oppositely wound -> covered between, empty inside the inner.
    AnoUiPathSeg holed[] = {
        { ANO_UI_SEG_MOVE, { 20, 20, 0, 0 } },
        { ANO_UI_SEG_LINE, { 100, 20, 0, 0 } },
        { ANO_UI_SEG_LINE, { 100, 80, 0, 0 } },
        { ANO_UI_SEG_LINE, { 20, 80, 0, 0 } },
        { ANO_UI_SEG_MOVE, { 45, 38, 0, 0 } }, // inner, reversed winding
        { ANO_UI_SEG_LINE, { 45, 62, 0, 0 } },
        { ANO_UI_SEG_LINE, { 75, 62, 0, 0 } },
        { ANO_UI_SEG_LINE, { 75, 38, 0, 0 } },
    };
    ano_ui_builder_init(&b, prims, 1, NULL, 0, NULL, 0, NULL, 0);
    ano_ui_builder_curves(&b, curves, 512);
    ano_ui_path_fill(&b, holed, 8, white, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    AnoUiScene s3 = ano_ui_scene(&b);
    float between[4], inside[4];
    ano_ui_ref_eval(&s3, 28.0f, 50.0f, between); // in the ring
    ano_ui_ref_eval(&s3, 60.0f, 50.0f, inside);   // in the hole
    printf("  path hole ring %.4f  hole %.4f\n", between[3], inside[3]);
    CHECK(fabs(between[3] - 1.0) <= 2e-3, "filled between outer and hole");
    CHECK(inside[3] <= 2e-3, "inner contour punches a hole");
}

// ---------------------------------------------------------------------------------------------
// Shadow vs blurred-mask ground truth: 4x supersampled rrect mask, separable discrete
// Gaussian (radius 4 sigma, double), box-downsampled; evaluator sampled at pixel centers.

static void shadow_case(const char *name, double hw, double hh, double corner, double sigma,
                        double maxGate, double rmsGate)
{
    const int ss = 4;
    int pad = (int)ceil(4.0 * sigma) + 2;
    int W = (int)(2 * hw) + 2 * pad, H = (int)(2 * hh) + 2 * pad;
    int sw = W * ss, sh = H * ss;
    double cx = W / 2.0, cy = H / 2.0;
    double *mask = malloc((size_t)sw * sh * sizeof(double));
    double *tmp = malloc((size_t)sw * sh * sizeof(double));
    double r4[4] = { corner, corner, corner, corner };
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++)
            mask[(size_t)y * sw + x] =
                inside_rrect((x + 0.5) / ss - cx, (y + 0.5) / ss - cy, hw, hh, r4) ? 1.0 : 0.0;
    // Separable Gaussian at supersample resolution, kernel normalized over its support.
    double sigss = sigma * ss;
    int kr = (int)ceil(4.0 * sigss);
    double *kern = malloc((size_t)(2 * kr + 1) * sizeof(double));
    double ksum = 0.0;
    for (int i = -kr; i <= kr; i++)
        ksum += kern[i + kr] = exp(-(double)i * i / (2.0 * sigss * sigss));
    for (int i = 0; i <= 2 * kr; i++)
        kern[i] /= ksum;
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            double v = 0.0;
            for (int i = -kr; i <= kr; i++) {
                int xx = x + i;
                if (xx >= 0 && xx < sw)
                    v += mask[(size_t)y * sw + xx] * kern[i + kr];
            }
            tmp[(size_t)y * sw + x] = v;
        }
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            double v = 0.0;
            for (int i = -kr; i <= kr; i++) {
                int yy = y + i;
                if (yy >= 0 && yy < sh)
                    v += tmp[(size_t)yy * sw + x] * kern[i + kr];
            }
            mask[(size_t)y * sw + x] = v;
        }
    // Compare per output pixel.
    double worst = 0.0, sum2 = 0.0;
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++) {
            double truth = 0.0;
            for (int sy = 0; sy < ss; sy++)
                for (int sx = 0; sx < ss; sx++)
                    truth += mask[(size_t)(py * ss + sy) * sw + px * ss + sx];
            truth /= ss * ss;
            float v = ano_ui_ref_shadow((float[2]){ (float)(px + 0.5 - cx),
                                                    (float)(py + 0.5 - cy) },
                                        (float[2]){ (float)hw, (float)hh }, (float)corner,
                                        (float)sigma);
            CHECK(v >= 0.0f && v <= 1.001f, "shadow value range");
            double err = fabs(v - truth);
            worst = fmax(worst, err);
            sum2 += err * err;
        }
    double rms = sqrt(sum2 / ((double)W * H));
    printf("  shadow %-16s max %.5f (%.2f/255) rms %.5f (%llu px)\n", name, worst, worst * 255.0,
           rms, (unsigned long long)((size_t)W * H));
    CHECK(worst <= maxGate, "shadow max error inside pinned envelope");
    CHECK(rms <= rmsGate, "shadow rms inside pinned envelope");
    free(kern);
    free(tmp);
    free(mask);
}

static void test_shadow(void)
{
    // Gates pinned from the measured run of 2026-07-07: max 0.0133 / 0.0105 / 0.0183
    // (3.4, 2.7, 4.7 per 255), rms 0.0035 / 0.0031 / 0.0039 — erf approx + 4-sample
    // quadrature + 3-sigma truncation, all inside a ~5/255 envelope.
    shadow_case("rect s=2", 30.0, 20.0, 0.0, 2.0, 0.020, 0.005);
    shadow_case("rect s=8", 30.0, 20.0, 0.0, 8.0, 0.020, 0.005);
    shadow_case("rrect r=8 s=4", 30.0, 20.0, 8.0, 4.0, 0.025, 0.005);

    // Inner-shadow sanity: zero outside the shape, zero deep inside, positive at the rim.
    AnoUiPrim prims[1];
    AnoUiBuilder b;
    float ink[4] = { 0, 0, 0, 1 };
    ano_ui_builder_init(&b, prims, 1, NULL, 0, NULL, 0, NULL, 0);
    ano_ui_shadow(&b, (float[2]){ 0, 0 }, (float[2]){ 60, 40 }, 4.0f, 3.0f, ink,
                  ANO_UI_REF_NONE, ANO_UI_FLAG_INNER);
    AnoUiScene s = ano_ui_scene(&b);
    float out[4];
    ano_ui_ref_eval(&s, 80.0f, 20.0f, out);
    CHECK(out[3] < 1e-4f, "inner shadow zero outside");
    ano_ui_ref_eval(&s, 29.5f, 19.5f, out);
    CHECK(out[3] < 2e-2f, "inner shadow near zero deep inside (3-sigma truncation residue)");
    ano_ui_ref_eval(&s, 1.5f, 19.5f, out);
    CHECK(out[3] > 0.2f, "inner shadow positive at the rim");
}

// ---------------------------------------------------------------------------------------------
// Blend semantics + kind skips + purity.

static void test_blend(void)
{
    AnoUiPrim prims[4];
    AnoUiBuilder b;
    float red[4] = { 1, 0, 0, 1 }, green[4] = { 0, 1, 0, 1 }, glow[4] = { 0.4f, 0.2f, 0, 0.4f };
    ano_ui_builder_init(&b, prims, 4, NULL, 0, NULL, 0, NULL, 0);
    ano_ui_rrect(&b, (float[2]){ 0, 0 }, (float[2]){ 40, 40 }, NULL, red, 0.0f,
                 ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    ano_ui_rrect(&b, (float[2]){ 20, 20 }, (float[2]){ 60, 60 }, NULL, green, 0.0f,
                 ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
    AnoUiScene s = ano_ui_scene(&b);
    float base[4], out[4];
    ano_ui_ref_eval(&s, 30.0f, 30.0f, base); // deep inside both
    CHECK(base[0] == 0.0f && base[1] == 1.0f && base[3] == 1.0f, "painter's order: later on top");
    ano_ui_ref_eval(&s, 10.0f, 10.0f, out); // only the first
    CHECK(out[0] == 1.0f && out[1] == 0.0f, "painter's order: base visible outside overlap");

    // ADD lights rgb without occluding; PATH/GLYPHS contribute zero in this lane.
    ano_ui_shadow(&b, (float[2]){ 10, 10 }, (float[2]){ 50, 50 }, 4.0f, 3.0f, glow,
                  ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
    ano_ui_glyphs(&b, (float[2]){ 0, 0 }, (float[2]){ 60, 60 }, 0, 8, red, ANO_UI_REF_NONE, 0);
    s = ano_ui_scene(&b);
    float lit[4];
    ano_ui_ref_eval(&s, 30.0f, 30.0f, lit);
    CHECK(lit[0] > base[0] + 0.3f && lit[1] > base[1] + 0.15f && lit[3] == base[3],
          "ADD accumulates rgb, alpha untouched, GLYPHS skipped");

    // Purity: identical inputs, identical bits.
    float again[4];
    ano_ui_ref_eval(&s, 30.0f, 30.0f, again);
    CHECK(memcmp(lit, again, sizeof lit) == 0, "evaluation purity");
}

// ---------------------------------------------------------------------------------------------
// Per-tile lists: the tiled walk must be BIT-IDENTICAL to the brute painter's-order
// evaluation. Prims outside a pixel's tile contribute coverage 0 (they are outside their
// influence box), and a solid entry's forced coverage 1 is exact, so equality is exact.

static void demo_build(AnoUiPrim *prims, AnoUiClip *clips, AnoUiPaint *paints,
                       AnoUiStop *stops, uint32_t *curves, AnoUiBuilder *b);

// Tile-grid a scene from its prim-influence union, then compare tiled vs brute at every
// pixel the grid covers (plus a margin, where both must read empty). Returns worst delta.
static double tiles_check(const AnoUiScene *s, const char *name)
{
    float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
    for (uint32_t i = 0; i < s->primCount; i++) {
        float mn[2], mx[2];
        ano_ui_prim_aabb(&s->prims[i], mn, mx);
        lo[0] = fminf(lo[0], mn[0]); lo[1] = fminf(lo[1], mn[1]);
        hi[0] = fmaxf(hi[0], mx[0]); hi[1] = fmaxf(hi[1], mx[1]);
    }
    int32_t ox = (int32_t)floorf(lo[0] / 8.0f) * 8, oy = (int32_t)floorf(lo[1] / 8.0f) * 8;
    uint32_t tilesX = (uint32_t)(((int32_t)ceilf(hi[0]) - ox + 7) / 8);
    uint32_t tilesY = (uint32_t)(((int32_t)ceilf(hi[1]) - oy + 7) / 8);
    uint32_t nTiles = tilesX * tilesY;
    uint32_t *offsets = malloc((size_t)(nTiles + 1) * 4);
    uint32_t *cursor = malloc((size_t)nTiles * 4);
    uint32_t entryCap = 1u << 20;
    uint32_t *entries = malloc((size_t)entryCap * 4);
    bool ok = false;
    uint32_t nEntries = ano_ui_tile_build(s, ox, oy, tilesX, tilesY, offsets, nTiles + 1,
                                          entries, entryCap, cursor, &ok);
    CHECK(ok, "tile build fits caps");
    uint32_t nSolid = 0;
    for (uint32_t k = 0; k < nEntries; k++)
        if (entries[k] & ANO_UI_ENTRY_SOLID) nSolid++;
    double worst = 0.0;
    for (int32_t py = oy - 4; py < oy + (int32_t)tilesY * 8 + 4; py++)
        for (int32_t px = ox - 4; px < ox + (int32_t)tilesX * 8 + 4; px++) {
            float a[4], t[4];
            ano_ui_ref_eval(s, (float)px, (float)py, a);
            ano_ui_ref_eval_tiled(s, ox, oy, tilesX, tilesY, offsets, entries, px, py, t);
            for (int k = 0; k < 4; k++)
                worst = fmax(worst, fabs((double)a[k] - (double)t[k]));
        }
    printf("  tiles %-8s %ux%u  %u entries (%u solid)  worst |tiled-brute| %.9f\n",
           name, tilesX, tilesY, nEntries, nSolid, worst);
    free(offsets); free(cursor); free(entries);
    return worst;
}

static void test_tiles(uint32_t soak)
{
    // Demo scene: every prim kind, a clip, a gradient, a path.
    AnoUiPrim prims[32];
    AnoUiClip clips[4];
    AnoUiPaint paints[8];
    AnoUiStop stops[16];
    uint32_t curves[256];
    AnoUiBuilder b;
    demo_build(prims, clips, paints, stops, curves, &b);
    AnoUiScene s = ano_ui_scene(&b);
    // The demo has shadows; the tiled path clips a shadow at its 3-sigma AABB exactly as
    // the GPU brute cull (ui_box_hits) does, while the uncalled reference keeps the whole
    // Gaussian tail — so the only difference is that sub-1/255 tail, not an assignment bug.
    CHECK(tiles_check(&s, "demo") <= 2e-3, "demo tiled matches brute within the shadow-cull tail");

    // Shadow-free random rrects (fills, rings, blends) on fractional tile-straddling
    // positions must be EXACTLY equal: outside its AABB an rrect contributes coverage 0,
    // and a solid entry's forced coverage 1 is exact.
    test_rng rng = rng_make(0x7113ED00u);
    for (uint32_t it = 0; it < 1u + soak; it++) {
        AnoUiPrim rp[24];
        AnoUiBuilder rb;
        ano_ui_builder_init(&rb, rp, 24, NULL, 0, NULL, 0, NULL, 0);
        uint32_t n = 6 + rng_below(&rng, 14);
        for (uint32_t i = 0; i < n; i++) {
            float x = 20.0f + (float)rng_below(&rng, 2000) * 0.1f;
            float y = 20.0f + (float)rng_below(&rng, 1500) * 0.1f;
            float w = 10.0f + (float)rng_below(&rng, 600) * 0.1f;
            float h = 10.0f + (float)rng_below(&rng, 400) * 0.1f;
            float rr = (float)rng_below(&rng, 12);
            float col[4] = { 0.5f, 0.4f, 0.6f, 0.3f + (float)rng_below(&rng, 70) * 0.01f };
            float radii[4] = { rr, rr, rr, rr };
            ano_ui_rrect(&rb, (float[2]){ x, y }, (float[2]){ x + w, y + h }, radii, col,
                         rng_below(&rng, 4) == 0 ? 2.0f : 0.0f, ANO_UI_REF_NONE,
                         ANO_UI_REF_NONE, rng_below(&rng, 3) == 0 ? ANO_UI_BLEND_ADD : 0u);
        }
        AnoUiScene rs = ano_ui_scene(&rb);
        if (tiles_check(&rs, "random") != 0.0)
            CHECK(false, "random rrects tiled == brute (bit-identical)");
    }
}

// ---------------------------------------------------------------------------------------------
// Standing demo scene: determinism golden + the GPU screenshot harness. The GPU path
// (ANO_UI_OPAQUE) renders the same scene over an opaque black backdrop straight into
// the sRGB swapchain; the reference mimics both quantizers (UNORM8 linear overlay,
// then the sRGB encode) so the compare envelope is the text lane's.

#define DEMO_ORIGIN_X 48.0f
#define DEMO_ORIGIN_Y 120.0f

static void demo_build(AnoUiPrim *prims, AnoUiClip *clips, AnoUiPaint *paints,
                       AnoUiStop *stops, uint32_t *curves, AnoUiBuilder *b)
{
    ano_ui_builder_init(b, prims, 32, clips, 4, paints, 8, stops, 16);
    ano_ui_builder_curves(b, curves, 256);
    ano_ui_demo_scene(b, DEMO_ORIGIN_X, DEMO_ORIGIN_Y);
}

static void test_demo(void)
{
    AnoUiPrim p1[32], p2[32];
    AnoUiClip c1[4], c2[4];
    AnoUiPaint pa1[8], pa2[8];
    AnoUiStop st1[16], st2[16];
    uint32_t cv1[256], cv2[256];
    AnoUiBuilder b1, b2;
    demo_build(p1, c1, pa1, st1, cv1, &b1);
    demo_build(p2, c2, pa2, st2, cv2, &b2);
    CHECK(b1.primCount >= 12 && b1.clipCount == 2 && b1.paintCount == 1 && b1.curveCount > 0,
          "demo scene counts");
    CHECK(b1.primCount == b2.primCount
              && memcmp(p1, p2, b1.primCount * sizeof(AnoUiPrim)) == 0
              && memcmp(c1, c2, b1.clipCount * sizeof(AnoUiClip)) == 0
              && b1.paintCount == b2.paintCount
              && memcmp(pa1, pa2, b1.paintCount * sizeof(AnoUiPaint)) == 0
              && b1.stopCount == b2.stopCount
              && memcmp(st1, st2, b1.stopCount * sizeof(AnoUiStop)) == 0
              && b1.curveCount == b2.curveCount
              && memcmp(cv1, cv2, b1.curveCount * sizeof(uint32_t)) == 0,
          "demo scene bitwise-stable");
    for (uint32_t i = 0; i < b1.primCount; i++)
        CHECK(p1[i].clipRef == ANO_UI_REF_NONE || p1[i].clipRef < b1.clipCount,
              "demo clip refs valid");
    printf("  demo scene: %u prims, %u clips, %u paints, %u curve words, bitwise-stable\n",
           b1.primCount, b1.clipCount, b1.paintCount, b1.curveCount);
}

// One swapchain byte triple for the demo scene over opaque black at pixel (px,py):
// eval -> clamp -> UNORM8 overlay quantize -> sRGB encode -> byte.
static void demo_pixel(const AnoUiScene *s, int px, int py, uint8_t out[3])
{
    float acc[4];
    ano_ui_ref_eval(s, (float)px, (float)py, acc);
    for (int c = 0; c < 3; c++) {
        float lin = fminf(fmaxf(acc[c], 0.0f), 1.0f);
        lin = roundf(lin * 255.0f) / 255.0f;
        float e = lin <= 0.0031308f ? 12.92f * lin : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;
        out[c] = (uint8_t)lroundf(fminf(fmaxf(e, 0.0f), 1.0f) * 255.0f);
    }
}

// --dump W H out.ppm : write the reference canvas (P6) for eyeballing.
static int demo_dump(int argc, char **argv)
{
    if (argc < 5) {
        printf("usage: anotest_ui --dump W H out.ppm\n");
        return 2;
    }
    int w = atoi(argv[2]), h = atoi(argv[3]);
    AnoUiPrim prims[32];
    AnoUiClip clips[4];
    AnoUiPaint paints[8];
    AnoUiStop stops[16];
    uint32_t curves[256];
    AnoUiBuilder b;
    demo_build(prims, clips, paints, stops, curves, &b);
    AnoUiScene s = ano_ui_scene(&b);
    FILE *f = fopen(argv[4], "wb");
    if (!f)
        return 2;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int py = 0; py < h; py++)
        for (int px = 0; px < w; px++) {
            uint8_t rgb[3];
            demo_pixel(&s, px, py, rgb);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("wrote %dx%d reference canvas to %s\n", w, h, argv[4]);
    return 0;
}

static int ppm_token(FILE *f) // next int, skipping whitespace and # comments
{
    int c, v = 0;
    do {
        c = fgetc(f);
        if (c == '#')
            while (c != '\n' && c != EOF)
                c = fgetc(f);
    } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        c = fgetc(f);
    }
    return v;
}

// --compare shot.ppm : RMS/max of an ANO_UI_OPAQUE screenshot against the reference.
static int demo_compare(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: anotest_ui --compare shot.ppm\n");
        return 2;
    }
    FILE *f = fopen(argv[2], "rb");
    if (!f) {
        printf("cannot open %s\n", argv[2]);
        return 2;
    }
    if (fgetc(f) != 'P' || fgetc(f) != '6') {
        printf("not a P6 ppm\n");
        fclose(f);
        return 2;
    }
    int w = ppm_token(f), h = ppm_token(f), maxv = ppm_token(f);
    if (maxv != 255) {
        printf("unsupported maxval %d\n", maxv);
        fclose(f);
        return 2;
    }
    AnoUiPrim prims[32];
    AnoUiClip clips[4];
    AnoUiPaint paints[8];
    AnoUiStop stops[16];
    uint32_t curves[256];
    AnoUiBuilder b;
    demo_build(prims, clips, paints, stops, curves, &b);
    AnoUiScene s = ano_ui_scene(&b);
    uint8_t *row = malloc((size_t)w * 3);
    double sum2 = 0.0;
    int worst = 0, wx = 0, wy = 0;
    for (int py = 0; py < h; py++) {
        if (fread(row, 3, (size_t)w, f) != (size_t)w) {
            printf("short read at row %d\n", py);
            free(row);
            fclose(f);
            return 2;
        }
        for (int px = 0; px < w; px++) {
            uint8_t rgb[3];
            demo_pixel(&s, px, py, rgb);
            for (int c = 0; c < 3; c++) {
                int d = abs((int)row[(size_t)px * 3 + c] - (int)rgb[c]);
                sum2 += (double)d * d;
                if (d > worst) {
                    worst = d;
                    wx = px;
                    wy = py;
                }
            }
        }
    }
    free(row);
    fclose(f);
    double rms = sqrt(sum2 / ((double)w * h * 3.0));
    printf("compare %dx%d: rms %.4f/255 max %d/255 at (%d,%d)\n", w, h, rms, worst, wx, wy);
    return rms <= 1.0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--dump") == 0)
        return demo_dump(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "--compare") == 0)
        return demo_compare(argc, argv);
    uint32_t soak = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 1;
    if (soak < 1)
        soak = 1;
    printf("anotest_ui: prim ABI, builder, reference evaluator (soak x%u)\n", soak);
    test_abi();
    test_builder();
    test_sdf(soak);
    test_coverage();
    test_clip();
    test_gradient();
    test_path();
    test_tiles(soak);
    test_shadow();
    test_blend();
    test_demo();
    if (failures) {
        printf("anotest_ui: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_ui: all passed\n");
    return 0;
}
