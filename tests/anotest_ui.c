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

int main(int argc, char **argv)
{
    uint32_t soak = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 1;
    if (soak < 1)
        soak = 1;
    printf("anotest_ui: prim ABI, builder, reference evaluator (soak x%u)\n", soak);
    test_abi();
    test_builder();
    test_sdf(soak);
    test_coverage();
    test_clip();
    test_shadow();
    test_blend();
    if (failures) {
        printf("anotest_ui: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_ui: all passed\n");
    return 0;
}
