/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: ano_synth_score_begin anchor-capacity wrap. The header (anoptic_synth.h:81) says
// begin counts size allocations, and score_begin sizes the anchor array as
// tempoCount + 1u (ano_synth.c:202) for the implicit beat-0 seed 〜 at tempoCount UINT32_MAX
// the sum wraps to 0, the zero-count mi_heap_calloc returns a non-NULL minimal block
// (malloc(0) semantics), the NULL guard at :211 passes, and :213 writes the 24-byte seed
// anchor s->anchors[0] out of bounds while begin returns true with anchorCap 0
// (docs/BUGS.md, Synth / Interface-level). Controls pin the cap arithmetic on good input:
// tempoCount 1 holds exactly one added point, tempoCount 0 holds none, so a fix that rejects
// every begin cannot pass. Controls print and flush before the trigger since a padded or
// hardened allocator may abort inside begin itself (a crash is a valid failure signal).
// Deterministic proxy when the write lands silently: a begin that promised room for
// UINT32_MAX tempo points must take the first one, but anchorCap 0 makes clock_add's
// :164 fullness check reject it. Either begin rejects the wrap or the promise holds.
// Exit 0 == pass.

#include <stdio.h>
#include <stdint.h>

#include <anoptic_synth.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

int main(void)
{
    // control: tempoCount 1 〜 the declared point fits, the point past it does not
    AnoSynth *a = ano_synth_create(NULL);
    CHECK(a != NULL, "control synth created");
    if (!a) return 1;
    CHECK(ano_synth_score_begin(a, 4.0, 1, 1, 0), "begin with tempoCount 1 accepted");
    CHECK(ano_synth_score_tempo(a, 4.0, 90.0), "declared tempo point accepted");
    CHECK(!ano_synth_score_tempo(a, 8.0, 80.0), "point past declared capacity rejected");

    // control: tempoCount 0 〜 seed only, zero added points
    AnoSynth *b = ano_synth_create(NULL);
    CHECK(b != NULL, "zero-cap control synth created");
    if (!b) return 1;
    CHECK(ano_synth_score_begin(b, 4.0, 1, 0, 0), "begin with tempoCount 0 accepted");
    CHECK(!ano_synth_score_tempo(b, 4.0, 90.0), "added point on zero declared capacity rejected");

    printf("controls done: %d failure(s); triggering begin with tempoCount UINT32_MAX\n", failures);
    fflush(stdout);

    // trigger: anchorCap = UINT32_MAX + 1u wraps to 0; today begin OOB-writes the seed
    // anchor into a zero-size block and returns true. Contract: reject, or honor the promise.
    AnoSynth *c = ano_synth_create(NULL);
    CHECK(c != NULL, "trigger synth created");
    if (!c) return 1;
    bool ok = ano_synth_score_begin(c, 4.0, 1, UINT32_MAX, 0);
    if (ok)
        CHECK(ano_synth_score_tempo(c, 4.0, 90.0),
              "a begin that promised UINT32_MAX tempo points takes the first one");

    ano_synth_destroy(c);
    ano_synth_destroy(b);
    ano_synth_destroy(a);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
