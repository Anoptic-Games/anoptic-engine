/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* Coverage for the Phase 3 DSP library and console (src/audio/dsp headers,
 * src/audio/audio_fx.c, the fx-chain + send routing in the mixer):
 *  - unit oracles on the primitives: sliding-window max vs the naive max
 *    (exact), peak-follower attack/decay properties, linear ramp exactness,
 *    biquad shelf gains by sine probe, SVF pass/stop-band by sine probe;
 *  - effect properties via offline renders: the limiter NEVER exceeds its
 *    ceiling, the DC blocker kills DC, WIDTH 0 collapses to mono bit-exactly,
 *    the compressor reduces peaks, sends ring a reverb tail that then decays,
 *    a zero send is silent where a live one is not, the ping-pong echoes at
 *    its delay time, chorus changes the signal without changing its level;
 *  - THE Phase 3 exit gate: the full console topology (strips with EQ+filter,
 *    two send returns carrying reverb and ping-pong, a master chain of
 *    drive -> compressor -> limiter -> DC block) instantiates from config and
 *    renders noise byte-identically across deliberate heap churn.
 * All offline: deterministic, CI-safe. argv[1] scales soak rounds. Exit 0 == pass. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <anoptic_audio.h>
#include <anoptic_memory.h>

#include "audio/dsp/svf.h"
#include "audio/dsp/biquad.h"
#include "audio/dsp/dynamics.h"

#include "templates/rng.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); failures++; } \
} while (0)

#define RATE 48000u

static float peak_of(const float *buf, uint64_t samples)
{
    float peak = 0.0f;
    for (uint64_t i = 0; i < samples; i++) {
        float a = buf[i] < 0.0f ? -buf[i] : buf[i];
        if (a > peak) peak = a;
    }
    return peak;
}

// RMS over the stereo frame window [f0, f1).
static float rms_win(const float *buf, uint64_t f0, uint64_t f1)
{
    double acc = 0.0;
    for (uint64_t i = f0 * 2u; i < f1 * 2u; i++)
        acc += (double)buf[i] * (double)buf[i];
    return (float)sqrt(acc / (double)((f1 - f0) * 2u));
}

// --- section 1: primitive unit oracles ---

static void test_primitives(void)
{
    // sliding-window max == naive max, exactly, on random data
    {
        mi_heap_t *heap LOCALHEAPATTR = mi_heap_new();
        enum { N = 5000, WIN = 64 };
        AnoDspWinMax wm;
        CHECK(ano_dsp_winmax_init(&wm, heap, WIN), "winmax init");
        test_rng rng = rng_make(0x11u);
        static float x[N];
        bool exact = true;
        for (int i = 0; i < N; i++) {
            x[i] = (float)rng_below(&rng, 100000u) / 100000.0f;
            float got = ano_dsp_winmax_push(&wm, x[i]);
            float want = 0.0f;
            for (int k = i - WIN + 1 < 0 ? 0 : i - WIN + 1; k <= i; k++)
                if (x[k] > want) want = x[k];
            if (got != want) exact = false;
        }
        CHECK(exact, "window max matches naive max exactly");
    }

    // follower: instant attack, monotone exponential decay
    {
        AnoDspFollower f = { .env = 0.0f, .decay = 0.999f };
        CHECK(ano_dsp_follower_step(&f, 0.8f) == 0.8f, "follower attack is instant");
        float prev = f.env;
        bool mono = true;
        for (int i = 0; i < 100; i++) {
            float e = ano_dsp_follower_step(&f, 0.0f);
            if (e > prev) mono = false;
            prev = e;
        }
        CHECK(mono && prev < 0.8f && prev > 0.7f, "follower decay monotone at its rate");
    }

    // ramp reaches its target in exactly N steps
    {
        AnoDspRamp r = { .y = 0.0f };
        ano_dsp_ramp_to(&r, 1.0f, 100);
        float last = 0.0f;
        for (int i = 0; i < 100; i++)
            last = ano_dsp_ramp_step(&r);
        CHECK(fabsf(last - 1.0f) < 1e-5f, "ramp lands on target at N");
        CHECK(ano_dsp_ramp_step(&r) == last, "ramp holds after N");
    }

    // biquad low shelf: +12 dB deep below the corner, ~0 dB far above
    {
        AnoDspBiquad c;
        AnoDspBiquadState s = {0};
        ano_dsp_biquad_lowshelf(&c, 200.0f, 12.0f, (float)RATE);
        double acc = 0.0;
        int n = 0;
        for (int i = 0; i < 48000; i++) {
            float in = sinf(6.2831853f * 30.0f * (float)i / (float)RATE);
            float out = ano_dsp_biquad_step(&c, &s, in);
            if (i > 4800) { acc += (double)out * out; n++; }
        }
        float gainDb = 10.0f * log10f((float)(acc / n) / 0.5f);
        CHECK(gainDb > 11.0f && gainDb < 13.0f, "low shelf boosts its band ~+12 dB");
        memset(&s, 0, sizeof s);
        acc = 0.0; n = 0;
        for (int i = 0; i < 48000; i++) {
            float in = sinf(6.2831853f * 8000.0f * (float)i / (float)RATE);
            float out = ano_dsp_biquad_step(&c, &s, in);
            if (i > 4800) { acc += (double)out * out; n++; }
        }
        gainDb = 10.0f * log10f((float)(acc / n) / 0.5f);
        CHECK(gainDb > -1.0f && gainDb < 1.0f, "low shelf leaves the top ~flat");
    }

    // SVF lowpass: pass band ~unity, stop band well down
    {
        AnoDspSvfCoef c;
        AnoDspSvfState s = {0};
        ano_dsp_svf_coef(&c, 1000.0f, 0.7071f, (float)RATE);
        double acc = 0.0;
        int n = 0;
        for (int i = 0; i < 48000; i++) {
            float in = sinf(6.2831853f * 100.0f * (float)i / (float)RATE);
            float out = ano_dsp_svf_step(&c, &s, in, ANO_DSP_SVF_LOWPASS);
            if (i > 4800) { acc += (double)out * out; n++; }
        }
        float gainDb = 10.0f * log10f((float)(acc / n) / 0.5f);
        CHECK(gainDb > -1.0f && gainDb < 1.0f, "SVF LP pass band ~unity");
        memset(&s, 0, sizeof s);
        acc = 0.0; n = 0;
        for (int i = 0; i < 48000; i++) {
            float in = sinf(6.2831853f * 8000.0f * (float)i / (float)RATE);
            float out = ano_dsp_svf_step(&c, &s, in, ANO_DSP_SVF_LOWPASS);
            if (i > 4800) { acc += (double)out * out; n++; }
        }
        gainDb = 10.0f * log10f((float)(acc / n) / 0.5f);
        CHECK(gainDb < -20.0f, "SVF LP stop band attenuates > 20 dB");
    }
}

// --- section 2: effect properties via offline renders ---

#define CLICK_FRAMES 4800u
static float g_click[CLICK_FRAMES]; // mono decaying noise burst
static float g_dc[256];             // constant offset (loopable)
static float g_stereo[512 * 2u];    // uncorrelated L/R (loopable)

static void make_material(void)
{
    test_rng rng = rng_make(0xD59u);
    for (uint32_t i = 0; i < CLICK_FRAMES; i++) {
        float n = ((float)rng_below(&rng, 65536u) / 32768.0f) - 1.0f;
        g_click[i] = n * expf(-(float)i / 400.0f);
    }
    for (uint32_t i = 0; i < 256; i++)
        g_dc[i] = 0.7f;
    for (uint32_t i = 0; i < 512; i++) {
        g_stereo[2u * i]      = sinf(6.2831853f * (float)i * 3.0f / 512.0f);
        g_stereo[2u * i + 1u] = sinf(6.2831853f * (float)i * 7.0f / 512.0f);
    }
}

// Render `frames` with one bus layout, a fixed buffer set, and a short event list.
static bool render_case(const AnoAudioBusDesc *layout, uint32_t busCount,
                        const AnoAudioOfflineEvent *events, uint32_t eventCount,
                        float *out, uint64_t frames)
{
    const AnoAudioOfflineBuffer buffers[] = {
        { .buffer_id = 1, .channels = 1, .frames = CLICK_FRAMES, .data = g_click },
        { .buffer_id = 2, .channels = 1, .frames = 256, .data = g_dc },
        { .buffer_id = 3, .channels = 2, .frames = 512, .data = g_stereo },
    };
    AnoAudioOfflineDesc desc = {
        .sampleRate = RATE,
        .blockFrames = 512,
        .busCount = busCount,
        .busLayout = layout,
        .events = events,
        .eventCount = eventCount,
        .buffers = buffers,
        .bufferCount = 3,
    };
    return ano_audio_render_offline(&desc, out, frames);
}

static void test_effects(void)
{
    enum { F = 48000 }; // one second
    float *out = mi_malloc((size_t)F * 2u * sizeof(float));
    float *alt = mi_malloc((size_t)F * 2u * sizeof(float));
    if (!out || !alt) {
        CHECK(false, "case buffers failed to allocate");
        return;
    }

    // limiter: a 2x-hot tone through a 0.5 ceiling NEVER exceeds it
    {
        const AnoAudioBusDesc layout[2] = {
            [0] = { .fx = { ANO_AUDIO_FX_LIMITER } },
            [1] = { .parent = 0 },
        };
        const AnoAudioOfflineEvent ev[] = {
            { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 0, .fxSlot = 0,
                .paramId = ANO_AUDIO_P_LIM_CEILING, .value = 0.5f } },
            { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
                .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 1, .gain = 2.0f, .freqHz = 220.0f } } },
        };
        CHECK(render_case(layout, 2, ev, 2, out, F), "limiter case renders");
        // the ceiling retarget glides (~30 ms); the property holds once settled
        float p = peak_of(out + 9600 * 2, (uint64_t)(F - 9600) * 2u);
        CHECK(p <= 0.501f, "limiter never exceeds its settled ceiling");
        CHECK(p > 0.40f, "limiter is limiting, not silencing");
    }

    // DC blocker: a constant-offset loop decays to ~zero
    {
        const AnoAudioBusDesc layout[2] = {
            [0] = { 0 },
            [1] = { .parent = 0, .fx = { ANO_AUDIO_FX_DCBLOCK } },
        };
        const AnoAudioOfflineEvent ev[] = {
            { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
                .desc = { .kind = ANO_AUDIO_SOURCE_BUFFER, .buffer_id = 2, .bus = 1,
                          .flags = ANO_AUDIO_SOURCE_LOOP, .gain = 1.0f } } },
        };
        CHECK(render_case(layout, 2, ev, 1, out, F), "dc case renders");
        CHECK(rms_win(out, F - 9600, F) < 0.01f, "DC blocker kills the offset");
    }

    // width 0: uncorrelated stereo collapses to identical channels
    {
        const AnoAudioBusDesc layout[2] = {
            [0] = { 0 },
            [1] = { .parent = 0, .fx = { ANO_AUDIO_FX_WIDTH } },
        };
        const AnoAudioOfflineEvent ev[] = {
            { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 1, .fxSlot = 0,
                .paramId = ANO_AUDIO_P_WIDTH_AMOUNT, .value = 0.0f } },
            { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
                .desc = { .kind = ANO_AUDIO_SOURCE_BUFFER, .buffer_id = 3, .bus = 1,
                          .flags = ANO_AUDIO_SOURCE_LOOP, .gain = 0.5f } } },
        };
        CHECK(render_case(layout, 2, ev, 2, out, F), "width case renders");
        // the amount retarget glides then snaps to exactly 0; check the settled half
        bool mono = true;
        for (uint64_t i = F / 2u; i < (uint64_t)F; i++)
            if (out[2u * i] != out[2u * i + 1u]) { mono = false; break; }
        CHECK(mono, "WIDTH 0 collapses to mono bit-exactly once settled");
    }

    // compressor: hot tone's peak comes down vs the uncompressed render
    {
        const AnoAudioBusDesc comp[2] = {
            [0] = { 0 },
            [1] = { .parent = 0, .fx = { ANO_AUDIO_FX_COMPRESSOR } },
        };
        const AnoAudioBusDesc flat[2] = { [0] = { 0 }, [1] = { .parent = 0 } };
        const AnoAudioOfflineEvent ev[] = {
            { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 1, .fxSlot = 0,
                .paramId = ANO_AUDIO_P_COMP_THRESHOLD, .value = 0.2f } },
            { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 1, .fxSlot = 0,
                .paramId = ANO_AUDIO_P_COMP_RATIO, .value = 4.0f } },
            { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
                .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 1, .gain = 0.8f, .freqHz = 330.0f } } },
        };
        CHECK(render_case(comp, 2, ev, 3, out, F), "comp case renders");
        CHECK(render_case(flat, 2, ev, 3, alt, F), "flat case renders");
        float pc = peak_of(out + F, F);      // steady state (second half)
        float pf = peak_of(alt + F, F);
        CHECK(pc < pf * 0.7f, "compressor reduces steady-state peaks > 3 dB");
    }

    // sends + reverb: the tail rings after the source dies, then decays;
    // a zero send leaves the return silent
    {
        AnoAudioBusDesc layout[3] = {
            [0] = { 0 },
            [1] = { .parent = 0, .fx = { ANO_AUDIO_FX_REVERB } },
            [2] = { .parent = 0, .sendTarget = { 1 }, .sendLevel = { 0.6f } },
        };
        const AnoAudioOfflineEvent ev[] = {
            { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
                .desc = { .kind = ANO_AUDIO_SOURCE_BUFFER, .buffer_id = 1, .bus = 2, .gain = 0.8f } } },
        };
        CHECK(render_case(layout, 3, ev, 1, out, F), "reverb case renders");
        // click + release die well before 0.6 s; the tail is pure reverb
        float early = rms_win(out, 28800, 33600); // 0.60-0.70 s
        float late  = rms_win(out, 43200, 48000); // 0.90-1.00 s
        CHECK(early > 1.0e-4f, "reverb tail rings after the source dies");
        CHECK(late < early, "reverb tail decays");
        layout[2].sendLevel[0] = 0.0f;
        CHECK(render_case(layout, 3, ev, 1, alt, F), "dry case renders");
        CHECK(rms_win(alt, 28800, 48000) < 1.0e-6f, "zero send leaves the return silent");
    }

    // ping-pong: a click echoes at its delay time
    {
        const AnoAudioBusDesc layout[3] = {
            [0] = { 0 },
            [1] = { .parent = 0, .fx = { ANO_AUDIO_FX_PINGPONG } },
            [2] = { .parent = 0, .sendTarget = { 1 }, .sendLevel = { 0.8f } },
        };
        const AnoAudioOfflineEvent ev[] = {
            { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 1, .fxSlot = 0,
                .paramId = ANO_AUDIO_P_PP_TIME_MS, .value = 250.0f } },
            { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
                .desc = { .kind = ANO_AUDIO_SOURCE_BUFFER, .buffer_id = 1, .bus = 2, .gain = 0.8f } } },
        };
        CHECK(render_case(layout, 3, ev, 2, out, F), "pingpong case renders");
        float echo  = rms_win(out, 12000, 16800); // 0.25-0.35 s: first echo window
        float quiet = rms_win(out, 8400, 11000);  // 0.175-0.23 s: between click and echo
        CHECK(echo > quiet * 2.0f, "ping-pong echoes at its delay time");
    }

    // chorus: changes the waveform, preserves the level
    {
        const AnoAudioBusDesc wet[2] = {
            [0] = { 0 },
            [1] = { .parent = 0, .fx = { ANO_AUDIO_FX_CHORUS } },
        };
        const AnoAudioBusDesc dry[2] = { [0] = { 0 }, [1] = { .parent = 0 } };
        const AnoAudioOfflineEvent ev[] = {
            { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
                .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 1, .gain = 0.4f, .freqHz = 440.0f } } },
        };
        CHECK(render_case(wet, 2, ev, 1, out, F), "chorus case renders");
        CHECK(render_case(dry, 2, ev, 1, alt, F), "chorus dry case renders");
        CHECK(memcmp(out, alt, (size_t)F * 2u * sizeof(float)) != 0, "chorus changes the signal");
        float rw = rms_win(out, 4800, F);
        float rd = rms_win(alt, 4800, F);
        CHECK(rw > rd * 0.5f && rw < rd * 2.0f, "chorus preserves the level ballpark");
    }

    mi_free(out);
    mi_free(alt);
}

// --- section 3: the console golden (the Phase 3 exit gate) ---

static void churn_heap(test_rng *rng, uint32_t rounds)
{
    enum { SLOTS = 256 };
    void *ptr[SLOTS] = {0};
    for (uint32_t r = 0; r < rounds; r++) {
        for (uint32_t i = 0; i < SLOTS; i++) {
            uint32_t slot = rng_below(rng, SLOTS);
            if (ptr[slot]) {
                mi_free(ptr[slot]);
                ptr[slot] = NULL;
            } else {
                size_t bytes = 1u + rng_below(rng, 8192u);
                ptr[slot] = mi_malloc(bytes);
                if (ptr[slot])
                    memset(ptr[slot], (int)(rng_next(rng) & 0xFFu), bytes);
            }
        }
    }
    for (uint32_t i = 0; i < SLOTS; i++)
        if (ptr[i]) mi_free(ptr[i]);
}

static bool render_console(float *out, uint64_t frames)
{
    // master chain <- reverb return (1) + delay return (2) <- music (3) + sfx (4)
    const AnoAudioBusDesc layout[5] = {
        [0] = { .fx = { ANO_AUDIO_FX_DRIVE, ANO_AUDIO_FX_COMPRESSOR,
                        ANO_AUDIO_FX_LIMITER, ANO_AUDIO_FX_DCBLOCK } },
        [1] = { .parent = 0, .gain = 0.8f, .fx = { ANO_AUDIO_FX_REVERB } },
        [2] = { .parent = 0, .gain = 0.7f, .fx = { ANO_AUDIO_FX_PINGPONG } },
        [3] = { .parent = 0, .fx = { ANO_AUDIO_FX_EQ3, ANO_AUDIO_FX_CHORUS },
                .sendTarget = { 1, 2 }, .sendLevel = { 0.30f, 0.15f } },
        [4] = { .parent = 0, .fx = { ANO_AUDIO_FX_FILTER, ANO_AUDIO_FX_WIDTH },
                .sendTarget = { 1 }, .sendLevel = { 0.25f } },
    };
    const AnoAudioOfflineEvent ev[] = {
        { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 3, .fxSlot = 0,
            .paramId = ANO_AUDIO_P_EQ_LOW_GAIN_DB, .value = 3.0f } },
        { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 4, .fxSlot = 0,
            .paramId = ANO_AUDIO_P_FILTER_CUTOFF, .value = 5000.0f } },
        { .frame = 0, .cmd = { .kind = ACMD_FX_SET, .bus = 4, .fxSlot = 0,
            .paramId = ANO_AUDIO_P_FILTER_MODE, .value = (float)ANO_AUDIO_FILTER_LOWPASS } },
        { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 1,
            .desc = { .kind = ANO_AUDIO_SOURCE_TONE, .bus = 3, .gain = 0.4f, .freqHz = 220.0f } } },
        { .frame = 0, .cmd = { .kind = ACMD_SOURCE_PLAY, .source_id = 2,
            .desc = { .kind = ANO_AUDIO_SOURCE_BUFFER, .buffer_id = 1, .bus = 4, .gain = 0.7f,
                      .flags = ANO_AUDIO_SOURCE_LOOP, .rate = 0.9f } } },
        { .frame = 12000, .cmd = { .kind = ACMD_BUS_SET, .bus = 3,
            .fields = ANO_AUDIO_FIELD_SEND0, .send = { 0.6f, 0.0f } } },
        { .frame = 24000, .cmd = { .kind = ACMD_FX_SET, .bus = 4, .fxSlot = 0,
            .paramId = ANO_AUDIO_P_FILTER_CUTOFF, .value = 1200.0f } },
        { .frame = 36000, .cmd = { .kind = ACMD_SOURCE_STOP, .source_id = 1 } },
    };
    const AnoAudioOfflineBuffer buffers[] = {
        { .buffer_id = 1, .channels = 1, .frames = CLICK_FRAMES, .data = g_click },
    };
    AnoAudioOfflineDesc desc = {
        .sampleRate = RATE,
        .blockFrames = 512,
        .busCount = 5,
        .busLayout = layout,
        .events = ev,
        .eventCount = (uint32_t)(sizeof ev / sizeof ev[0]),
        .buffers = buffers,
        .bufferCount = 1,
    };
    return ano_audio_render_offline(&desc, out, frames);
}

static void test_console_golden(uint32_t soak)
{
    enum { F = 48000 };
    float *golden = mi_malloc((size_t)F * 2u * sizeof(float));
    float *again  = mi_malloc((size_t)F * 2u * sizeof(float));
    if (!golden || !again) {
        CHECK(false, "console buffers failed to allocate");
        return;
    }
    CHECK(render_console(golden, F), "console renders");
    float p = peak_of(golden, (uint64_t)F * 2u);
    CHECK(p > 0.05f && p <= 0.92f, "console output audible and under the limiter ceiling");

    test_rng rng = rng_make(0x60D5u);
    for (uint32_t round = 0; round < soak; round++) {
        churn_heap(&rng, 64);
        memset(again, 0x5A, (size_t)F * 2u * sizeof(float));
        CHECK(render_console(again, F), "console re-renders");
        CHECK(memcmp(golden, again, (size_t)F * 2u * sizeof(float)) == 0,
              "console render byte-identical on a churned heap");
    }
    mi_free(golden);
    mi_free(again);
}

int main(int argc, char **argv)
{
    uint32_t soak = 1;
    if (argc > 1) {
        int s = atoi(argv[1]);
        if (s > 0) soak = (uint32_t)s;
    }
    make_material();
    test_primitives();
    test_effects();
    test_console_golden(soak);

    if (failures) {
        printf("anotest_audiodsp: %d FAILURE(S)\n", failures);
        return 1;
    }
    printf("anotest_audiodsp: all passed (soak x%u)\n", soak);
    return 0;
}
