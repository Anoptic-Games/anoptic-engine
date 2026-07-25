/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_dsp_delay_read_frac's clamp ceiling vs a zero-length line. delay.h stored
// cap == maxDelay verbatim while the allocation floors at 2 slots, so the two disagreed the
// moment maxDelay was 0: the ceiling (float)(cap - 1u) wrapped to 4294967295.0f, every tap
// sailed past both clamps, and (uint32_t)delay converted an out-of-range float 〜 UB by
// C23 6.3.1.4p1, tripping UBSan's float-cast-overflow on the 5e9 / FLT_MAX / NaN probes and
// indexing off a garbage integer otherwise. maxDelay 0 is reachable: neither entry seam bounds
// sampleRate below (ano_audio.c:95 and audio_mixer.c:655 only default 0 to 48000), and
// audio_fx.c's fs-derived line lengths (fs * 0.0337f, fs * 0.200f) floor to 0 under 30 Hz.
// NaN bypassed both clamps at every line length, not just the degenerate ones.
// Controls pin the valid path 〜 in-range taps on a 48 kHz line must still return their exact
// interpolated ramp values and read_int must still return exact history 〜 so a fix that mutes,
// rounds, or refuses cannot pass. Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <float.h>

#include <anoptic_memory.h>

#include "audio/dsp/delay.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Control taps: inside every ceiling this primitive has ever had, so they pin behaviour that
// must not move. Guard taps: out of range both ways, plus both NaN signs.
static const float CONTROL_TAP[] = { 1.0f, 1.5f, 3.0f, 1000.5f, 47000.0f };
static const float GUARD_TAP[]   = { 0.0f, 1.5f, 3.0f, 1e9f, 5e9f, FLT_MAX, NAN, -NAN, -5.0f };
#define TAP_N(a) (sizeof (a) / sizeof *(a))

// Feeds value[step] = step + 1 for n steps. Caller keeps n > mask + 1, so no slot is still zero.
static void ramp_fill(AnoDspDelay *d, uint32_t n)
{
    for (uint32_t k = 0; k < n; ++k)
        ano_dsp_delay_write(d, (float)(k + 1u));
}

// Contract oracle for a ramp-filled line: the sample `delay` back is n + 1 - delay, so the
// interpolation between neighbours is exact and lands on n + 1 - delay for any fractional tap.
// delay is clamped into [1, mask - 1] per the header; a collapsed tap (0, on mask == 1 lines)
// indexes the slot mask + 1 back. Returns the value read_frac must return, bit for bit.
static float ramp_expect(uint32_t n, uint32_t mask, float delay)
{
    float dmax = (float)(mask - 1u);
    if (!(delay > 1.0f)) delay = 1.0f;
    if (delay > dmax)    delay = dmax;
    if (delay == 0.0f)   delay = (float)(mask + 1u); // 2-slot alias
    return (float)(n + 1u) - delay;
}

// One line: init, ramp, every tap finite and exactly on the oracle, read_int at both ends of
// its documented [1, mask] domain. Taps must fit the line: mask - 1 >= max tap for controls.
static void line_check(mi_heap_t *heap, uint32_t maxDelay, uint32_t expectMask,
                       const float *tap, size_t taps, const char *tag)
{
    AnoDspDelay d;
    CHECK(ano_dsp_delay_init(&d, heap, maxDelay), "delay init");
    if (!d.buf) return;
    CHECK(d.mask == expectMask, "pow2 allocation mask");

    uint32_t n = d.mask + 8u;
    ramp_fill(&d, n);

    for (size_t p = 0; p < taps; ++p) {
        float got = ano_dsp_delay_read_frac(&d, tap[p]);
        if (!isfinite(got)) {
            printf("FAIL: %s tap %g returned %g\n", tag, (double)tap[p], (double)got);
            failures++;
            continue;
        }
        float want = ramp_expect(n, d.mask, tap[p]);
        if (got != want) {
            printf("FAIL: %s tap %g gave %.6f, want %.6f\n",
                   tag, (double)tap[p], (double)got, (double)want);
            failures++;
        }
    }

    CHECK(ano_dsp_delay_read_int(&d, 1u) == (float)n, "read_int at 1");
    CHECK(ano_dsp_delay_read_int(&d, d.mask) == (float)(n + 1u - d.mask), "read_int at mask");
}

int main(void)
{
    mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
    CHECK(heap != NULL, "module heap created");
    if (!heap) return 1;

    // control: a real 48 kHz line 〜 in-range fractional taps interpolate exactly and the
    // integer reads return untouched history at both ends of the domain
    line_check(heap, 48000u, 65535u, CONTROL_TAP, TAP_N(CONTROL_TAP), "control 48000");

    // control: the shortest line the fx layer asks for at a sane rate still reads real history
    line_check(heap, 2u, 3u, CONTROL_TAP, 1u, "control maxDelay 2");

    printf("controls done: %d failure(s); firing the out-of-range taps\n", failures);
    fflush(stdout);

    // trigger: fs-derived lengths floor to 0 under 30 Hz. The line still owns 2 slots, so every
    // tap 〜 huge, negative, NaN 〜 must collapse into that buffer instead of converting a float
    // the ceiling never bounded.
    line_check(heap, 0u, 1u, GUARD_TAP, TAP_N(GUARD_TAP), "maxDelay 0");
    line_check(heap, 1u, 1u, GUARD_TAP, TAP_N(GUARD_TAP), "maxDelay 1");
    line_check(heap, 2u, 3u, GUARD_TAP, TAP_N(GUARD_TAP), "maxDelay 2");
    line_check(heap, 48000u, 65535u, GUARD_TAP, TAP_N(GUARD_TAP), "maxDelay 48000");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
