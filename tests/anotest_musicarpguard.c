/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Coverage: the arp emission slot bound. ano_generate_arp (music_arp.c:102) streams one event
// per meter slot into AnoArpResult.events[ANO_METER_MAX_SLOTS] with no bound, but ano_meter_slots
// exceeds the 32-slot cap for any meter longer than 8 quarters 〜 9/4 is 36 〜 and ano_music_create
// accepts such meters unvalidated (docs/BUGS.md, Music / Implementation). The metric-weights twin
// at music_ir.c:63 clamps exactly this; the arp does not. Controls pin the live path in 4/4 and
// 12/8 〜 a real bar comes back full, in range, canary untouched 〜 so a reject-everything fix
// cannot pass. Trigger: a 9/4 bar at density 1.0 (step 1, skip probability exactly 0, so all 36
// slots emit deterministically with no RNG dependence) must stay inside the 32-entry array; today
// the tail writes stomp the canary past the result struct, and the 33rd write lands on eventCount
// itself, recycling the tail into events[0..2] and reporting a 3-event bar.
// Deterministic, no threads, no files. Exit 0 == pass.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "music/music_arp.h"
#include "music/music_det.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

// Result plus a canary fence directly after it: any emission past events[] lands here.
typedef struct Fence
{
    AnoArpResult r;
    uint8_t      canary[1024];
} Fence;

static Fence fence; // static: the spill stays contained, not on main's frame

static bool canary_intact(void)
{
    for (size_t i = 0; i < sizeof fence.canary; ++i)
        if (fence.canary[i] != 0x5A)
            return false;
    return true;
}

// One arp bar: C triad, density 1.0 (step 1, skipProb 0 〜 every slot emits, zero RNG draws land).
static void run_bar(AnoMeter meter)
{
    AnoHarmonicContext ctx = { 0 };
    ctx.scale = (AnoScale){ 0, 0 }; // C ionian
    ctx.chordPcs[0] = 0;
    ctx.chordPcs[1] = 4;
    ctx.chordPcs[2] = 7;
    ctx.chordPcCount = 3;
    strcpy(ctx.chordSym, "C");

    AnoGenParams params = ano_gen_params_default();
    params.noteDensity = 1.0;

    AnoArpConfig cfg = ano_arp_config_default();
    AnoMusicRng rng;
    ano_music_rng_seed(&rng, 42);

    memset(fence.canary, 0x5A, sizeof fence.canary);
    ano_generate_arp(&ctx, meter, &params, ANO_ARP_UP, &cfg, &rng,
                     false, 0, &fence.r);
}

int main(void)
{
    // control: 4/4 at density 1.0 fills all 16 slots, in register, canary untouched
    run_bar((AnoMeter){ 4, 4 });
    CHECK(canary_intact(), "4/4: no writes past the result struct");
    CHECK(fence.r.eventCount == 16, "4/4: 16 slots emit 16 events");
    CHECK(fence.r.events[0].core.start == 0.0, "4/4: slot 0 event first");
    for (uint32_t i = 0; i < fence.r.eventCount && i < 16; ++i) {
        CHECK(fence.r.events[i].core.pitch >= 72 && fence.r.events[i].core.pitch <= 96,
              "4/4: pitch within the configured pool span");
        CHECK(fence.r.events[i].core.layer == ANO_MUSIC_ARP, "4/4: arp layer");
    }

    // control: 12/8 is the widest bar the cap comment promises (24 slots), still clean
    run_bar((AnoMeter){ 12, 8 });
    CHECK(canary_intact(), "12/8: no writes past the result struct");
    CHECK(fence.r.eventCount == 24, "12/8: 24 slots emit 24 events");

    // trigger: 9/4 is 36 slots into a 32-entry array 〜 the emission must respect the cap
    run_bar((AnoMeter){ 9, 4 });
    CHECK(canary_intact(), "9/4: no writes past the result struct");
    CHECK(fence.r.eventCount <= ANO_METER_MAX_SLOTS, "9/4: count stays within events[] capacity");
    CHECK(fence.r.eventCount == 0 || fence.r.events[0].core.start == 0.0,
          "9/4: slot 0 event not recycled by the spilled tail");

    if (failures) {
        printf("anotest_musicarpguard: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_musicarpguard: all passed\n");
    return 0;
}
