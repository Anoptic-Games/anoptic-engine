/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the perc compound-meter slot bound. ano_generate_perc (music_perc.c:121) writes the
// grouped-kick pickup kick[slots - 2] into bool kick[ANO_METER_MAX_SLOTS] whenever noteDensity
// > 0.75, but ano_meter_slots exceeds the 32-slot cap for meters past 8 quarters 〜 9/4 is 36 〜
// so the write lands at kick[34], past the stack array, and the readback loop reads kick[32..35]
// off the frame (docs/BUGS.md, Music / Implementation). The same 32-wide shape breaks the hat
// lane: AnoGroove.hatDrops is a u32 slot bitmask and music_perc.c:154 computes hatDrops >> s for
// s up to 35, a shift past the width that wraps to s & 31 on x86/arm, so mask bits for slots 0..3
// silently drop the hats at 32..35 (ano_make_groove plants the same wrap at :79). Controls pin
// 4/4 and 12/8 (the widest in-cap compound bar): kicks in range incl. the legit slots-2 pickup,
// mask drops honored, so neither a reject-compound nor an ignore-the-mask fix can pass. Trigger
// is differential and fix-agnostic: two identical 9/4 bars at density 0.8, mask 0 vs mask bits
// {0,2} 〜 the mask may only remove the two in-range hats, never the hats at 32/34 a u32 mask
// cannot name; today the wrapped shift eats both, and each 9/4 call also executes the kick[34]
// stack write (a crash here is the same bug). Deterministic 〜 the groove path draws no RNG,
// fill/crash lanes gated off. Exit 0 == pass.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "music/music_perc.h"
#include "music/music_det.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

static AnoPercResult out;

// Inputs: emitted bar, grid slot, drum pitch. Output: true if that drum sounds on that slot.
static bool has_drum_at(const AnoPercResult *r, int slot, uint8_t pitch)
{
    for (uint32_t i = 0; i < r->eventCount && i < 48u; ++i)
        if (r->events[i].core.pitch == pitch
            && (int)(r->events[i].core.start / ANO_MUSIC_GRID + 0.5) == slot)
            return true;
    return false;
}

// Inputs: emitted bar, drum pitch. Output: how many events carry that drum.
static uint32_t count_drum(const AnoPercResult *r, uint8_t pitch)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->eventCount && i < 48u; ++i)
        if (r->events[i].core.pitch == pitch)
            n++;
    return n;
}

// Inputs: emitted bar, slot count. Output: true if every event sits on a slot inside [0, slots).
static bool slots_in_range(const AnoPercResult *r, int slots)
{
    for (uint32_t i = 0; i < r->eventCount && i < 48u; ++i) {
        int s = (int)(r->events[i].core.start / ANO_MUSIC_GRID + 0.5);
        if (s < 0 || s >= slots)
            return false;
    }
    return true;
}

// One groove-pinned perc bar into `out`: bar 0 of an 8-bar phrase (OPEN slot 〜 no fill draw),
// no prior fill, hyperFill 0, ghostCount 0 〜 the groove path draws zero RNG, so the bar is a
// pure function of meter, density, and the hat-drop mask.
static void run_bar(AnoMeter meter, double density, uint32_t hatMask)
{
    AnoHarmonicContext ctx = { 0 };
    AnoGenParams params = ano_gen_params_default();
    params.noteDensity = density;
    AnoPercConfig cfg = ano_perc_config_default();
    AnoGroove groove = { 0 };
    groove.hatDrops = hatMask;
    AnoPhrasePos pos = { .phrase = 0, .pos = 0, .bars = 8, .kind = ANO_SEG_REGULAR };
    AnoMusicRng rng;
    ano_music_rng_seed(&rng, 42);
    memset(&out, 0, sizeof out);
    ano_generate_perc(&ctx, meter, &params, pos, false, &cfg, &rng, &groove, 0.0, &out);
}

int main(void)
{
    const uint8_t KICK = ANO_DRUM_PITCHES[ANO_DRUM_KICK];
    const uint8_t CHAT = ANO_DRUM_PITCHES[ANO_DRUM_CHAT];

    // control: 4/4 at density 0.8 〜 events in range, mask bits 0/2 drop exactly those hats
    run_bar((AnoMeter){ 4, 4 }, 0.8, 0x5u);
    CHECK(out.eventCount > 0 && out.eventCount <= 48u, "4/4: bar emits within the result cap");
    CHECK(slots_in_range(&out, 16), "4/4: every event on a slot inside the bar");
    CHECK(!has_drum_at(&out, 0, CHAT), "4/4: mask bit 0 drops the slot-0 hat");
    CHECK(!has_drum_at(&out, 2, CHAT), "4/4: mask bit 2 drops the slot-2 hat");
    CHECK(has_drum_at(&out, 1, CHAT), "4/4: unmasked slot-1 hat sounds");
    CHECK(has_drum_at(&out, 0, KICK), "4/4: downbeat kick sounds");

    // control: 12/8 is the widest in-cap compound bar (24 slots) 〜 the grouped-kick set incl.
    // the slots-2 pickup is intended behavior, all of it in range
    run_bar((AnoMeter){ 12, 8 }, 0.8, 0x5u);
    CHECK(out.eventCount > 0 && out.eventCount <= 48u, "12/8: bar emits within the result cap");
    CHECK(slots_in_range(&out, 24), "12/8: every event on a slot inside the bar");
    CHECK(has_drum_at(&out, 0, KICK), "12/8: downbeat kick sounds");
    CHECK(has_drum_at(&out, 22, KICK), "12/8: the slots-2 pickup kick sounds in range");
    CHECK(!has_drum_at(&out, 0, CHAT), "12/8: mask bit 0 drops the slot-0 hat");
    CHECK(has_drum_at(&out, 1, CHAT), "12/8: unmasked slot-1 hat sounds");

    // trigger: 9/4 is 36 slots against 32-wide structures. density 0.8 > 0.75 executes the
    // kick[34] write past the stack set in BOTH runs (a crash below is this bug). Differential:
    // the only legal effect of mask bits {0,2} vs mask 0 is losing the two in-range hats 〜 a
    // u32 mask cannot name slots >= 32, so whatever the fixed hat lane does past slot 31 it must
    // do identically in both runs; today the wrapped shift eats the hats at 32 and 34.
    run_bar((AnoMeter){ 9, 4 }, 0.8, 0x0u);
    CHECK(out.eventCount > 0 && out.eventCount <= 48u, "9/4 unmasked: bar emits within the result cap");
    bool hat32Free = has_drum_at(&out, 32, CHAT);
    bool hat34Free = has_drum_at(&out, 34, CHAT);
    uint32_t chatsFree = count_drum(&out, CHAT);
    CHECK(has_drum_at(&out, 0, CHAT), "9/4 unmasked: slot-0 hat sounds");
    CHECK(has_drum_at(&out, 2, CHAT), "9/4 unmasked: slot-2 hat sounds");

    run_bar((AnoMeter){ 9, 4 }, 0.8, 0x5u);
    CHECK(out.eventCount > 0 && out.eventCount <= 48u, "9/4 masked: bar emits within the result cap");
    CHECK(!has_drum_at(&out, 0, CHAT), "9/4 masked: mask bit 0 drops the in-range slot-0 hat");
    CHECK(!has_drum_at(&out, 2, CHAT), "9/4 masked: mask bit 2 drops the in-range slot-2 hat");
    CHECK(has_drum_at(&out, 32, CHAT) == hat32Free, "9/4: mask bit 0 must not alias the slot-32 hat");
    CHECK(has_drum_at(&out, 34, CHAT) == hat34Free, "9/4: mask bit 2 must not alias the slot-34 hat");
    CHECK(chatsFree - count_drum(&out, CHAT) == 2u, "9/4: the mask removes exactly its two in-range hats");

    if (failures) {
        printf("anotest_percmeterguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_percmeterguard: all passed\n");
    return 0;
}
