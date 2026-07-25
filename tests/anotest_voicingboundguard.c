/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Guard: ano_voice_chord's ingress must bound cfg->voices and stay total over degenerate
// inputs (docs/BUGS_DONE.md, Music). voices is a uint8_t in a module-private POD 〜 0 and > 6
// are constructible by any internal caller 〜 and pre-fix the walk wrote the option and
// candidate scratch and the caller's out[6] past their extents, the count==0 fallback
// read pcs behind a zero count, and the drop branch filled option rows before trimming.
// Controls pin the valid domain first, so a reject-everything fix fails before the
// triggers run. The canary tail turns the out[6] write into a plain-build detector.
// Exit 0 == pass.

#include <stdio.h>
#include <string.h>

#include "music/music_theory.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define CANARY    0x5AFEC0DE
#define OUT_SLOTS 16

// out[] wrapped in canaries: slots [6..15] must survive every call untouched.
static void arm(int *buf) { for (int i = 0; i < OUT_SLOTS; ++i) buf[i] = CANARY; }

static bool tail_intact(const int *buf)
{
    for (int i = 6; i < OUT_SLOTS; ++i)
        if (buf[i] != CANARY) return false;
    return true;
}

static bool ascending(const int *buf, uint32_t n)
{
    for (uint32_t i = 1; i < n; ++i)
        if (buf[i] <= buf[i - 1]) return false;
    return true;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // progress must survive the pre-fix crash

    int out[OUT_SLOTS];
    int ref[OUT_SLOTS];

    // control: the default config voices a triad 〜 a reject-everything fix fails here
    printf("control: default config places a 4-voice triad\n");
    const uint8_t triad[3] = { 0, 4, 7 };
    arm(out);
    double cost = 0.0;
    uint32_t n = ano_voice_chord(triad, 3, NULL, 0, NULL, out, &cost);
    CHECK(n == 4u, "control: default config returns 4 voices");
    CHECK(ascending(out, n), "control: voicing is strictly ascending");
    CHECK(n > 0 && out[0] >= 52 && out[n - 1] <= 79, "control: voicing stays inside lo..hi");
    CHECK(tail_intact(out), "control: nothing writes past out[5]");

    // control: voices == pcCount takes the exact-copy branch
    printf("control: voices == pcCount exact branch\n");
    AnoVoicingConfig five = ano_voicing_config_default();
    five.voices = 5;
    const uint8_t penta[5] = { 0, 2, 4, 7, 9 };
    arm(out);
    n = ano_voice_chord(penta, 5, NULL, 0, &five, out, NULL);
    CHECK(n == 5u, "control: five voices place");
    CHECK(ascending(out, n), "control: five-voice result ascending");
    CHECK(tail_intact(out), "control: five-voice call leaves the tail alone");

    // control: same inputs, same bytes
    printf("control: determinism\n");
    arm(out);
    arm(ref);
    uint32_t n1 = ano_voice_chord(triad, 3, NULL, 0, NULL, ref, NULL);
    uint32_t n2 = ano_voice_chord(triad, 3, NULL, 0, NULL, out, NULL);
    CHECK(n1 == n2 && memcmp(out, ref, sizeof out) == 0, "control: replay is byte-identical");

    // trigger: voices = 7 〜 one past MAX_VOICES; pre-fix the final copy writes out[6]
    printf("trigger: voices one past the extent\n");
    AnoVoicingConfig seven = ano_voicing_config_default();
    seven.voices = 7;
    seven.lo = 40;
    seven.hi = 90;
    seven.maxAdjacentGap = 50;
    const uint8_t diatonic[7] = { 0, 2, 4, 5, 7, 9, 11 };
    arm(out);
    n = ano_voice_chord(diatonic, 7, NULL, 0, &seven, out, NULL);
    CHECK(n <= 6u, "trigger: voice count is bounded by the signature extent");
    CHECK(tail_intact(out), "trigger: out[6..] survives an oversized voices");
    CHECK(n > 0 && ascending(out, n), "trigger: the clamped voicing still places and ascends");

    // trigger: voices = 255 〜 the full uint8_t range walks every scratch array pre-fix
    printf("trigger: voices at the type ceiling\n");
    AnoVoicingConfig ceiling = seven;
    ceiling.voices = 255;
    arm(out);
    n = ano_voice_chord(diatonic, 7, NULL, 0, &ceiling, out, NULL);
    CHECK(n <= 6u, "trigger: type-ceiling voices is bounded");
    CHECK(tail_intact(out), "trigger: out[6..] survives type-ceiling voices");

    // trigger: zero voices place nothing 〜 the documented 0-return, not a 256-empty-
    // candidate walk into cand[-1]
    printf("trigger: zero voices\n");
    AnoVoicingConfig zero = ano_voicing_config_default();
    zero.voices = 0;
    arm(out);
    n = ano_voice_chord(triad, 3, NULL, 0, &zero, out, NULL);
    CHECK(n == 0u, "trigger: zero voices returns 0");
    CHECK(out[0] == CANARY && tail_intact(out), "trigger: zero voices writes nothing");

    // trigger: zero pcs place nothing 〜 pre-fix the count==0 fallback copied pcs[0..3]
    // from a pointer with nothing behind it
    printf("trigger: zero pitch classes\n");
    arm(out);
    n = ano_voice_chord(NULL, 0, NULL, 0, NULL, out, NULL);
    CHECK(n == 0u, "trigger: zero pcs returns 0");
    CHECK(out[0] == CANARY && tail_intact(out), "trigger: zero pcs writes nothing");

    // trigger: a nine-pc cluster through the drop branch 〜 pre-fix the row fill wrote
    // eight entries before the trim; the bound now rides the write
    printf("trigger: pcCount past the row extent\n");
    const uint8_t cluster[9] = { 0, 1, 2, 4, 5, 7, 9, 10, 11 };
    AnoVoicingConfig six = ano_voicing_config_default();
    six.voices = 6;
    six.lo = 40;
    six.hi = 90;
    six.maxAdjacentGap = 50;
    arm(out);
    n = ano_voice_chord(cluster, 9, NULL, 0, &six, out, NULL);
    CHECK(n == 6u, "trigger: nine pcs voice six");
    CHECK(ascending(out, n), "trigger: nine-pc voicing ascends");
    CHECK(tail_intact(out), "trigger: nine-pc call leaves the tail alone");

    if (failures) {
        printf("anotest_voicingboundguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_voicingboundguard: all checks passed\n");
    return 0;
}
