/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/*
 * synth_voices.c
 * Patches as data, voices as kernels. Every numeric constant below is tuning
 * lifted verbatim from the prototype's patch set (musicgen/synth/patches.py);
 * the topology per class is fixed (finding 4: a sounding voice keeps its
 * patch, no per-voice branching beyond the class switch). Filter keytracking
 * bakes at allocation: kt = clamp((f/261.63)^amount, 0.5, 2.5). Amplitude is
 * (velocity/127)^1.5, applied by the caller at spawn. Drum transients use
 * fixed per-recipe noise seeds — every kick is bit-identical by design; the
 * generation side's Humanize supplies variation.
 */

#include <math.h>
#include <string.h>

#include "synth_internal.h"
#include "../audio/dsp/wavetable.h"

// constant-power pan: p in [-1, 1] -> L/R gains
static void pan_gains(float p, float *gl, float *gr)
{
    float th = (p + 1.0f) * 0.78539816339f; // pi/4
    *gl = cosf(th);
    *gr = sinf(th);
}

static float keytrack(float freq, float amount)
{
    float v = powf(freq / 261.63f, amount);
    return v < 0.5f ? 0.5f : v > 2.5f ? 2.5f : v;
}

// signalflow resonance [0,1) -> SVF Q (zeta = 1 - res)
static float q_from_res(float res)
{
    if (res > 0.98f) res = 0.98f;
    return 1.0f / (2.0f * (1.0f - res));
}

static float midi_hz(uint8_t pitch)
{
    return 440.0f * exp2f(((float)pitch - 69.0f) / 12.0f);
}

// ---------------------------------------------------------------------------
// Baked banks
// ---------------------------------------------------------------------------

// 4 single-cycle frames, dark -> bright: harmonics 1..23 at 1/h^(2.3 - 0.45f),
// even harmonics fading in as 0.35 + 0.2f; each frame normalized to 0.9 peak.
void ano_synth_bake_wavetable(float *bank)
{
    for (uint32_t f = 0; f < ANO_SYNTH_WT_FRAMES; ++f) {
        float *wave = bank + (size_t)f * ANO_SYNTH_WT_LEN;
        float rolloff = 2.3f - 0.45f * (float)f;
        float evenAmp = 0.35f + 0.2f * (float)f;
        for (uint32_t n = 0; n < ANO_SYNTH_WT_LEN; ++n) {
            double ph = (double)n / ANO_SYNTH_WT_LEN;
            double v = 0.0;
            for (uint32_t h = 1; h <= 23; ++h) {
                double a = 1.0 / pow((double)h, rolloff);
                if ((h & 1u) == 0u) a *= evenAmp;
                v += a * sin(2.0 * 3.14159265358979 * h * ph);
            }
            wave[n] = (float)v;
        }
        float peak = 0.0f;
        for (uint32_t n = 0; n < ANO_SYNTH_WT_LEN; ++n) {
            float a = fabsf(wave[n]);
            if (a > peak) peak = a;
        }
        if (peak > 0.0f) {
            float g = 0.9f / peak;
            for (uint32_t n = 0; n < ANO_SYNTH_WT_LEN; ++n)
                wave[n] *= g;
        }
    }
}

// The sampler's "recording": a deterministic 1.6 s bell at root C5 (MIDI 72),
// inharmonic partials (ratio, amp, decay) + a 12 ms noise chiff, 0.8 peak.
void ano_synth_bake_bell(float *out, uint64_t frames, float sampleRate)
{
    static const float part[4][3] = {
        { 1.00f, 1.00f, 1.9f }, { 2.76f, 0.60f, 3.2f },
        { 5.40f, 0.25f, 4.8f }, { 8.93f, 0.12f, 7.0f },
    };
    float f0 = 440.0f * exp2f((72.0f - 69.0f) / 12.0f);
    for (uint64_t i = 0; i < frames; ++i) {
        float t = (float)i / sampleRate;
        float v = 0.0f;
        for (int p = 0; p < 4; ++p)
            v += part[p][1] * sinf(ANO_DSP_TWO_PI * f0 * part[p][0] * t) * expf(-part[p][2] * t);
        out[i] = v;
    }
    AnoDspRng rng;
    ano_dsp_rng_seed(&rng, 0xBE11u);
    uint64_t chiff = (uint64_t)(0.012f * sampleRate);
    if (chiff > frames) chiff = frames;
    for (uint64_t i = 0; i < chiff; ++i) {
        float fade = 1.0f - (float)i / (float)chiff;
        out[i] += ano_dsp_noise(&rng) * fade * 0.4f;
    }
    float peak = 0.0f;
    for (uint64_t i = 0; i < frames; ++i) {
        float a = fabsf(out[i]);
        if (a > peak) peak = a;
    }
    if (peak > 0.0f) {
        float g = 0.8f / peak;
        for (uint64_t i = 0; i < frames; ++i)
            out[i] *= g;
    }
}

// ---------------------------------------------------------------------------
// Spawn: resolve (layer, patch) to a configured voice
// ---------------------------------------------------------------------------

// Layer defaults when instruments[] holds 0 (or a reserved texture patch).
static uint32_t default_patch(uint32_t layer)
{
    switch (layer) {
    case ANO_MUSIC_PAD:     return ANO_SYNTH_PATCH_WARM;
    case ANO_MUSIC_BASS:    return ANO_SYNTH_PATCH_ROUND;
    case ANO_MUSIC_MELODY:  return ANO_SYNTH_PATCH_SOFT;
    case ANO_MUSIC_COUNTER: return ANO_SYNTH_PATCH_MELLOW;
    case ANO_MUSIC_ARP:     return ANO_SYNTH_PATCH_PLUCK;
    default:                return ANO_SYNTH_PATCH_DEFAULT;
    }
}

static void spawn_pad(AnoSynth *s, AnoSynthVoice *v, float dur, bool bright)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_PAD;
    float detune = bright ? 1.009f : 1.004f;
    float f[3] = { v->freq, v->freq * detune, v->freq / detune };
    static const float brightPan[3] = { 0.0f, -0.7f, 0.7f };
    for (int i = 0; i < 3; ++i) {
        v->u.pad.ph[i] = 0.0f;
        v->u.pad.dt[i] = f[i] / fs;
        float gl, gr;
        pan_gains(bright ? brightPan[i] : 0.0f, &gl, &gr);
        v->u.pad.og[i][0] = gl * (1.0f / 3.0f);
        v->u.pad.og[i][1] = gr * (1.0f / 3.0f);
    }
    float attack = fminf(bright ? 0.15f : 0.5f, dur * 0.35f);
    ano_dsp_asr_init(&v->env, attack, fmaxf(dur - attack, 0.05f), 0.8f, 1.5f, fs);
    v->cutMult = (bright ? 1.7f : 1.0f) * keytrack(v->freq, 0.2f);
    v->resQ    = q_from_res(bright ? 0.32f : 0.15f);
    v->total   = (uint64_t)((dur + 0.8f) * fs);
    v->panL = v->panR = 1.0f; // pan lives in og
}

static void spawn_wtpad(AnoSynth *s, AnoSynthVoice *v, float dur)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_WTPAD;
    v->u.wt.ph = 0.0f;
    v->u.wt.dt = v->freq / fs;
    v->u.wt.morphAmp = fminf(1.0f, v->amp * 1.4f);
    ano_dsp_asr_init(&v->u.wt.morph, fminf(1.2f, fmaxf(0.3f, dur * 0.6f)),
                     fmaxf(dur - 1.2f, 0.05f), 1.0f, 1.2f, fs);
    float attack = fminf(0.4f, dur * 0.35f);
    ano_dsp_asr_init(&v->env, attack, fmaxf(dur - attack, 0.05f), 0.9f, 1.5f, fs);
    v->cutMult = 1.2f * keytrack(v->freq, 0.2f);
    v->resQ    = q_from_res(0.12f);
    v->amp    *= 0.8f;
    v->total   = (uint64_t)((dur + 0.9f) * fs);
    pan_gains(0.0f, &v->panL, &v->panR);
}

static void spawn_bass(AnoSynth *s, AnoSynthVoice *v, float dur, bool driven)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_BASS;
    v->u.bass.sawPh = v->u.bass.subPh = 0.0f;
    v->u.bass.drive = driven ? 2.2f : 0.0f;
    v->u.bass.sweepBase  = driven ? 0.25f : 0.35f;
    v->u.bass.sweepScale = driven ? 1.05f : 0.65f;
    ano_dsp_asr_init(&v->u.bass.fenv, 0.001f, 0.05f, 0.25f, 3.0f, fs);
    ano_dsp_asr_init(&v->env, 0.004f, fmaxf(dur - 0.004f, 0.03f), 0.1f, 2.0f, fs);
    v->cutMult = (driven ? 1.4f : 1.0f) * keytrack(v->freq, 0.3f);
    v->resQ    = q_from_res(driven ? 0.3f : 0.2f);
    v->total   = (uint64_t)((dur + 0.1f) * fs);
    pan_gains(0.0f, &v->panL, &v->panR);
}

// osc type codes for lead slots
enum { LO_SINE = 0, LO_TRI, LO_SAW, LO_SQUARE };

static void spawn_lead(AnoSynth *s, AnoSynthVoice *v, float dur, uint32_t patch)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_LEAD;
    v->u.lead.ph1 = v->u.lead.ph2 = v->u.lead.vibPh = 0.0f;
    v->u.lead.tri1 = v->u.lead.tri2 = (AnoDspTri){0};
    float attack, release, mult, res, pan, vibRate, vibDelay, vibDepth;
    if (patch == ANO_SYNTH_PATCH_HARD) {
        v->u.lead.t1 = LO_SAW;    v->u.lead.a1 = 0.6f;
        v->u.lead.t2 = LO_SQUARE; v->u.lead.a2 = 0.3f;
        vibRate = 6.2f; vibDelay = 0.15f; vibDepth = 0.009f;
        attack = 0.006f; release = 0.18f; mult = 1.4f; res = 0.2f; pan = 0.12f;
    } else if (patch == ANO_SYNTH_PATCH_MELLOW) {
        v->u.lead.t1 = LO_TRI;  v->u.lead.a1 = 0.8f;
        v->u.lead.t2 = LO_SINE; v->u.lead.a2 = 0.2f;
        vibRate = 4.8f; vibDelay = 0.5f; vibDepth = 0.004f;
        attack = 0.035f; release = 0.22f; mult = 0.8f; res = 0.1f; pan = -0.14f;
    } else { // SOFT
        v->u.lead.t1 = LO_TRI; v->u.lead.a1 = 0.7f;
        v->u.lead.t2 = LO_SAW; v->u.lead.a2 = 0.25f;
        vibRate = 5.5f; vibDelay = 0.35f; vibDepth = 0.006f;
        attack = 0.02f; release = 0.18f; mult = 1.0f; res = 0.1f; pan = 0.12f;
    }
    v->u.lead.vibRate  = vibRate;
    v->u.lead.vibDepth = vibDepth;
    v->u.lead.vibDelay = (uint64_t)(vibDelay * fs);
    ano_dsp_asr_init(&v->env, attack, fmaxf(dur - attack, 0.03f), release, 1.8f, fs);
    v->cutMult = mult * keytrack(v->freq, 0.4f);
    v->resQ    = q_from_res(res);
    v->total   = (uint64_t)((dur + release) * fs);
    pan_gains(pan, &v->panL, &v->panR);
}

static void spawn_sampler(AnoSynth *s, AnoSynthVoice *v, float dur, uint8_t pitch)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_SAMPLER;
    v->u.smp.cur  = 0.0;
    v->u.smp.rate = exp2(((double)pitch - 72.0) / 12.0);
    float natural = (float)((double)s->bellFrames / fs / v->u.smp.rate);
    float total   = fminf(natural, dur + 1.2f);
    ano_dsp_asr_init(&v->env, 0.001f, fmaxf(total - 0.4f, 0.02f), 0.4f, 2.0f, fs);
    v->cutMult = 1.5f * keytrack(midi_hz(pitch), 0.2f);
    v->resQ    = q_from_res(0.0f);
    v->total   = (uint64_t)(total * fs);
    pan_gains(0.08f, &v->panL, &v->panR);
}

static void spawn_fm(AnoSynth *s, AnoSynthVoice *v, float dur, bool glass)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_FM;
    v->u.fm.carPh = v->u.fm.modPh = 0.0f;
    v->u.fm.ratio = glass ? 7.003f : 3.007f;
    v->u.fm.index = glass ? 2.6f : 1.8f;
    ano_dsp_asr_init(&v->u.fm.menv, 0.001f, 0.0f, fminf(dur, glass ? 0.5f : 0.35f), 4.0f, fs);
    float sustain = fmaxf(dur * 0.5f, 0.02f);
    float release = fminf(dur, glass ? 0.5f : 0.3f);
    ano_dsp_asr_init(&v->env, 0.002f, sustain, release, 3.0f, fs);
    v->total = (uint64_t)((0.002f + sustain + release) * fs);
    pan_gains(-0.2f, &v->panL, &v->panR);
}

static void spawn_chime(AnoSynth *s, AnoSynthVoice *v, float dur, uint8_t pitch)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_CHIME;
    static const float ratios[5] = { 1.0f, 2.76f, 5.40f, 8.93f, 11.34f };
    static const float decays[5] = { 1.0f, 1.7f, 2.6f, 3.8f, 5.2f };
    float ring = fminf(dur + 1.6f, 2.6f);
    for (int i = 0; i < 5; ++i) {
        v->u.chime.ph[i] = 0.0f;
        ano_dsp_asr_init(&v->u.chime.penv[i], 0.001f, 0.0f,
                         fmaxf(ring / decays[i], 0.05f), 3.0f, fs);
    }
    (void)ratios; // read in the step kernel
    ano_dsp_asr_init(&v->u.chime.chiff, 0.0005f, 0.0f, 0.02f, 3.0f, fs);
    ano_dsp_svf_coef(&v->u.chime.bpc, 5200.0f, q_from_res(0.4f), fs);
    v->u.chime.bps = (AnoDspSvfState){0};
    ano_dsp_rng_seed(&v->u.chime.rng, 0xC41Eu);
    ano_dsp_asr_init(&v->env, 0.0f, ring + 0.1f, 0.0f, 1.0f, fs); // partials shape decay
    v->cutMult = 1.3f * keytrack(v->freq, 0.2f);
    v->resQ    = q_from_res(0.05f);
    v->amp    *= 0.6f;
    v->total   = (uint64_t)((ring + 0.1f) * fs);
    float pan = (float)((pitch * 7) % 11) / 11.0f * 1.2f - 0.6f;
    pan_gains(pan, &v->panL, &v->panR);
}

// GM pitch -> drum recipe. Everything here has a fixed spectrum: filter coefs
// bake now, cutoff staging never touches percussion.
static void spawn_drum(AnoSynth *s, AnoSynthVoice *v, uint8_t pitch)
{
    float fs = (float)s->sampleRate;
    v->cls = ANO_SYNTH_VC_DRUM;
    v->u.drum.ph = 0.0f;
    v->u.drum.s1 = v->u.drum.s2 = (AnoDspSvfState){0};
    float total, pan;
    switch (pitch) {
    case 36: // kick: 44 -> 129 Hz sweep + band-passed click
        v->u.drum.kind = ANO_SYNTH_DRUM_KICK;
        ano_dsp_asr_init(&v->u.drum.e2, 0.0005f, 0.0f, 0.09f, 4.0f, fs);
        ano_dsp_asr_init(&v->u.drum.e3, 0.0005f, 0.0f, 0.012f, 3.0f, fs);
        ano_dsp_asr_init(&v->env, 0.001f, 0.02f, 0.22f, 3.0f, fs);
        ano_dsp_svf_coef(&v->u.drum.c1, 3500.0f, q_from_res(0.4f), fs);
        ano_dsp_rng_seed(&v->u.drum.rng, 0xD1C4u);
        total = 0.30f; pan = 0.0f;
        break;
    case 38: // snare: band-passed rattle + 195 Hz tone
        v->u.drum.kind = ANO_SYNTH_DRUM_SNARE;
        ano_dsp_asr_init(&v->env, 0.001f, 0.01f, 0.16f, 3.0f, fs);      // rattle
        ano_dsp_asr_init(&v->u.drum.e3, 0.001f, 0.0f, 0.08f, 3.0f, fs); // tone
        ano_dsp_svf_coef(&v->u.drum.c1, 1900.0f, q_from_res(0.3f), fs);
        ano_dsp_rng_seed(&v->u.drum.rng, 0xD5A2u);
        total = 0.22f; pan = 0.04f;
        break;
    case 42: case 46: { // hats: high-passed noise, closed/open decay
        bool open = pitch == 46;
        v->u.drum.kind = open ? ANO_SYNTH_DRUM_OHAT : ANO_SYNTH_DRUM_CHAT;
        float decay = open ? 0.28f : 0.045f;
        ano_dsp_asr_init(&v->env, 0.001f, open ? 0.005f : 0.0f, decay, 3.0f, fs);
        ano_dsp_svf_coef(&v->u.drum.c1, 7800.0f, q_from_res(0.2f), fs);
        ano_dsp_rng_seed(&v->u.drum.rng, 0xDCA7u);
        total = decay + 0.03f; pan = -0.22f;
        break;
    }
    case 45: case 47: case 50: { // toms: swept sine + thump
        v->u.drum.kind = ANO_SYNTH_DRUM_TOM;
        v->freq = pitch == 45 ? 105.0f : pitch == 47 ? 135.0f : 170.0f;
        ano_dsp_asr_init(&v->u.drum.e2, 0.001f, 0.0f, 0.18f, 3.0f, fs);
        ano_dsp_asr_init(&v->u.drum.e3, 0.0005f, 0.0f, 0.02f, 3.0f, fs);
        ano_dsp_asr_init(&v->env, 0.001f, 0.02f, 0.30f, 2.5f, fs);
        ano_dsp_rng_seed(&v->u.drum.rng, 0xD703u);
        total = 0.36f; pan = 0.0f;
        break;
    }
    case 49: // crash: high-passed wash + band-passed shimmer
        v->u.drum.kind = ANO_SYNTH_DRUM_CRASH;
        ano_dsp_asr_init(&v->env, 0.002f, 0.05f, 1.3f, 2.5f, fs);
        ano_dsp_svf_coef(&v->u.drum.c1, 5200.0f, q_from_res(0.1f), fs);
        ano_dsp_svf_coef(&v->u.drum.c2, 9000.0f, q_from_res(0.6f), fs);
        ano_dsp_rng_seed(&v->u.drum.rng, 0xDC4Au);
        ano_dsp_rng_seed(&v->u.drum.rng2, 0xDC4Bu);
        total = 1.45f; pan = 0.15f;
        break;
    case 70: // shaker
        v->u.drum.kind = ANO_SYNTH_DRUM_SHAKER;
        ano_dsp_asr_init(&v->env, 0.015f, 0.0f, 0.06f, 1.5f, fs);
        ano_dsp_svf_coef(&v->u.drum.c1, 6300.0f, q_from_res(0.5f), fs);
        ano_dsp_rng_seed(&v->u.drum.rng, 0xD5ACu);
        total = 0.10f; pan = -0.3f;
        break;
    default: // rim, and the fallback for unmapped percussion
        v->u.drum.kind = ANO_SYNTH_DRUM_RIM;
        ano_dsp_asr_init(&v->env, 0.0005f, 0.0f, 0.045f, 3.0f, fs);
        ano_dsp_svf_coef(&v->u.drum.c1, 4500.0f, q_from_res(0.6f), fs);
        ano_dsp_rng_seed(&v->u.drum.rng, 0xD814u);
        total = 0.06f; pan = 0.1f;
        break;
    }
    v->total = (uint64_t)(total * fs);
    pan_gains(pan, &v->panL, &v->panR);
}

bool ano_synth_voice_spawn(AnoSynth *s, AnoSynthVoice *v, const AnoSynthNote *n)
{
    memset(v, 0, sizeof *v);
    v->active = true;
    v->layer  = n->ev.layer;
    v->freq   = midi_hz(n->ev.pitch);
    v->amp    = powf((float)n->ev.velocity / 127.0f, 1.5f);
    float dur = n->durS;

    if (n->ev.layer == ANO_MUSIC_PERC) {
        spawn_drum(s, v, n->ev.pitch);
        return true;
    }

    uint32_t patch = s->instruments[n->ev.layer];
    if (patch == ANO_SYNTH_PATCH_DEFAULT || patch >= ANO_SYNTH_PATCH_COUNT
        || patch == ANO_SYNTH_PATCH_BREEZE || patch == ANO_SYNTH_PATCH_WHISTLE
        || patch == ANO_SYNTH_PATCH_BAD_GROUND) // reserved textures fall back
        patch = default_patch(n->ev.layer);

    switch (patch) {
    case ANO_SYNTH_PATCH_WARM:   spawn_pad(s, v, dur, false); return true;
    case ANO_SYNTH_PATCH_BRIGHT: spawn_pad(s, v, dur, true); return true;
    case ANO_SYNTH_PATCH_MORPH:  spawn_wtpad(s, v, dur); return true;
    case ANO_SYNTH_PATCH_ROUND:  spawn_bass(s, v, dur, false); return true;
    case ANO_SYNTH_PATCH_DRIVEN: spawn_bass(s, v, dur, true); return true;
    case ANO_SYNTH_PATCH_SOFT:
    case ANO_SYNTH_PATCH_HARD:
    case ANO_SYNTH_PATCH_MELLOW: spawn_lead(s, v, dur, patch); return true;
    case ANO_SYNTH_PATCH_KEYS:   spawn_sampler(s, v, dur, n->ev.pitch); return true;
    case ANO_SYNTH_PATCH_PLUCK:  spawn_fm(s, v, dur, false); return true;
    case ANO_SYNTH_PATCH_GLASS:  spawn_fm(s, v, dur, true); return true;
    case ANO_SYNTH_PATCH_CHIMES: spawn_chime(s, v, dur, n->ev.pitch); return true;
    default:
        v->active = false;
        return false;
    }
}

// ---------------------------------------------------------------------------
// Per-span coefficient refresh (classes whose cutoff rides the shared bus)
// ---------------------------------------------------------------------------

void ano_synth_voice_span_coef(AnoSynth *s, AnoSynthVoice *v, const float *staged)
{
    float fs = (float)s->sampleRate;
    switch (v->cls) {
    case ANO_SYNTH_VC_PAD:
    case ANO_SYNTH_VC_WTPAD:
    case ANO_SYNTH_VC_LEAD:
    case ANO_SYNTH_VC_SAMPLER:
    case ANO_SYNTH_VC_CHIME:
        ano_dsp_svf_coef(&v->fc, staged[v->layer] * v->cutMult, v->resQ, fs);
        ano_dsp_svf_flush(&v->f0);
        ano_dsp_svf_flush(&v->f1);
        return;
    case ANO_SYNTH_VC_BASS: // per-sample sweep; just keep the state denormal-free
        ano_dsp_svf_flush(&v->f0);
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// One sample per class
// ---------------------------------------------------------------------------

static float lead_osc(uint8_t type, AnoDspTri *tri, float phase, float dt)
{
    switch (type) {
    case LO_TRI:    return ano_dsp_triangle(tri, phase, dt);
    case LO_SAW:    return ano_dsp_saw(phase, dt);
    case LO_SQUARE: return ano_dsp_square(phase, dt);
    default:        return ano_dsp_sine(phase);
    }
}

void ano_synth_voice_step(AnoSynth *s, AnoSynthVoice *v, const float *staged,
                          float *l, float *r)
{
    float fs = (float)s->sampleRate;
    switch (v->cls) {

    case ANO_SYNTH_VC_PAD: {
        float sl = 0.0f, sr = 0.0f;
        for (int i = 0; i < 3; ++i) {
            float p = v->u.pad.ph[i];
            float smp = ano_dsp_saw(p, v->u.pad.dt[i]);
            ano_dsp_phase_step(&v->u.pad.ph[i], v->u.pad.dt[i]);
            sl += smp * v->u.pad.og[i][0];
            sr += smp * v->u.pad.og[i][1];
        }
        float e = ano_dsp_asr_step(&v->env) * v->amp;
        *l += ano_dsp_svf_step(&v->fc, &v->f0, sl, ANO_DSP_SVF_LOWPASS) * e;
        *r += ano_dsp_svf_step(&v->fc, &v->f1, sr, ANO_DSP_SVF_LOWPASS) * e;
        return;
    }

    case ANO_SYNTH_VC_WTPAD: {
        AnoDspWavetable wt = { s->wtBank, ANO_SYNTH_WT_LEN, ANO_SYNTH_WT_FRAMES };
        float morph = ano_dsp_asr_step(&v->u.wt.morph) * v->u.wt.morphAmp;
        float smp = ano_dsp_wavetable_read(&wt, v->u.wt.ph, morph);
        ano_dsp_phase_step(&v->u.wt.ph, v->u.wt.dt);
        float e = ano_dsp_asr_step(&v->env) * v->amp;
        float o = ano_dsp_svf_step(&v->fc, &v->f0, smp, ANO_DSP_SVF_LOWPASS) * e;
        *l += o * v->panL;
        *r += o * v->panR;
        return;
    }

    case ANO_SYNTH_VC_BASS: {
        float dt = v->freq / fs;
        float body = ano_dsp_saw(v->u.bass.sawPh, dt) * 0.6f
                   + ano_dsp_sine(v->u.bass.subPh) * 0.5f;
        ano_dsp_phase_step(&v->u.bass.sawPh, dt);
        ano_dsp_phase_step(&v->u.bass.subPh, 0.5f * dt);
        if (v->u.bass.drive > 0.0f)
            body = tanhf(body * v->u.bass.drive) * 0.8f;
        float fe  = ano_dsp_asr_step(&v->u.bass.fenv);
        float cut = staged[ANO_MUSIC_BASS]
                  * (v->u.bass.sweepBase + fe * v->u.bass.sweepScale) * v->cutMult;
        ano_dsp_svf_coef(&v->fc, cut, v->resQ, fs);
        float e = ano_dsp_asr_step(&v->env) * v->amp;
        float o = ano_dsp_svf_step(&v->fc, &v->f0, body, ANO_DSP_SVF_LOWPASS) * e;
        *l += o * v->panL;
        *r += o * v->panR;
        return;
    }

    case ANO_SYNTH_VC_LEAD: {
        float ramp = v->u.lead.vibDelay
                   ? fminf((float)v->age / (float)v->u.lead.vibDelay, 1.0f) : 1.0f;
        float vib = 1.0f + ano_dsp_sine(v->u.lead.vibPh) * ramp * v->u.lead.vibDepth;
        ano_dsp_phase_step(&v->u.lead.vibPh, v->u.lead.vibRate / fs);
        float dt = v->freq * vib / fs;
        float smp = lead_osc(v->u.lead.t1, &v->u.lead.tri1, v->u.lead.ph1, dt) * v->u.lead.a1
                  + lead_osc(v->u.lead.t2, &v->u.lead.tri2, v->u.lead.ph2, dt) * v->u.lead.a2;
        ano_dsp_phase_step(&v->u.lead.ph1, dt);
        ano_dsp_phase_step(&v->u.lead.ph2, dt);
        float e = ano_dsp_asr_step(&v->env) * v->amp;
        float o = ano_dsp_svf_step(&v->fc, &v->f0, smp, ANO_DSP_SVF_LOWPASS) * e;
        *l += o * v->panL;
        *r += o * v->panR;
        return;
    }

    case ANO_SYNTH_VC_SAMPLER: {
        double cur = v->u.smp.cur;
        uint64_t i0 = (uint64_t)cur;
        float smp = 0.0f;
        if (i0 + 1u < s->bellFrames) {
            float frac = (float)(cur - (double)i0);
            smp = s->bell[i0] + (s->bell[i0 + 1u] - s->bell[i0]) * frac;
        }
        v->u.smp.cur = cur + v->u.smp.rate;
        float e = ano_dsp_asr_step(&v->env) * v->amp;
        float o = ano_dsp_svf_step(&v->fc, &v->f0, smp, ANO_DSP_SVF_LOWPASS) * e;
        *l += o * v->panL;
        *r += o * v->panR;
        return;
    }

    case ANO_SYNTH_VC_FM: {
        float me  = ano_dsp_asr_step(&v->u.fm.menv);
        float mod = ano_dsp_sine(v->u.fm.modPh) * v->freq * v->u.fm.index * me;
        ano_dsp_phase_step(&v->u.fm.modPh, v->freq * v->u.fm.ratio / fs);
        float smp = ano_dsp_sine(v->u.fm.carPh);
        ano_dsp_phase_step(&v->u.fm.carPh, (v->freq + mod) / fs);
        float e = ano_dsp_asr_step(&v->env) * v->amp;
        *l += smp * e * v->panL;
        *r += smp * e * v->panR;
        return;
    }

    case ANO_SYNTH_VC_CHIME: {
        static const float ratios[5] = { 1.0f, 2.76f, 5.40f, 8.93f, 11.34f };
        static const float gains[5]  = { 1.0f, 0.6f, 0.35f, 0.2f, 0.12f };
        float body = 0.0f;
        for (int i = 0; i < 5; ++i) {
            body += gains[i] * ano_dsp_sine(v->u.chime.ph[i])
                  * ano_dsp_asr_step(&v->u.chime.penv[i]);
            ano_dsp_phase_step(&v->u.chime.ph[i], v->freq * ratios[i] / fs);
        }
        float chiff = ano_dsp_svf_step(&v->u.chime.bpc, &v->u.chime.bps,
                                       ano_dsp_noise(&v->u.chime.rng), ANO_DSP_SVF_BANDPASS)
                    * ano_dsp_asr_step(&v->u.chime.chiff) * 0.3f;
        (void)ano_dsp_asr_step(&v->env); // lifecycle only; partials shape decay
        float o = ano_dsp_svf_step(&v->fc, &v->f0, body + chiff, ANO_DSP_SVF_LOWPASS) * v->amp;
        *l += o * v->panL;
        *r += o * v->panR;
        return;
    }

    case ANO_SYNTH_VC_DRUM: {
        float o = 0.0f;
        switch (v->u.drum.kind) {
        case ANO_SYNTH_DRUM_KICK: {
            float pe = ano_dsp_asr_step(&v->u.drum.e2);
            float body = ano_dsp_sine(v->u.drum.ph);
            ano_dsp_phase_step(&v->u.drum.ph, (44.0f + pe * 85.0f) / fs);
            float click = ano_dsp_svf_step(&v->u.drum.c1, &v->u.drum.s1,
                                           ano_dsp_noise(&v->u.drum.rng), ANO_DSP_SVF_BANDPASS)
                        * ano_dsp_asr_step(&v->u.drum.e3) * 0.5f;
            o = (body + click) * ano_dsp_asr_step(&v->env) * v->amp * 1.2f;
            break;
        }
        case ANO_SYNTH_DRUM_SNARE: {
            float rattle = ano_dsp_svf_step(&v->u.drum.c1, &v->u.drum.s1,
                                            ano_dsp_noise(&v->u.drum.rng), ANO_DSP_SVF_BANDPASS)
                         * ano_dsp_asr_step(&v->env) * 0.8f;
            float tone = ano_dsp_sine(v->u.drum.ph) * ano_dsp_asr_step(&v->u.drum.e3) * 0.4f;
            ano_dsp_phase_step(&v->u.drum.ph, 195.0f / fs);
            o = (rattle + tone) * v->amp;
            break;
        }
        case ANO_SYNTH_DRUM_TOM: {
            float pe = ano_dsp_asr_step(&v->u.drum.e2);
            float body = ano_dsp_sine(v->u.drum.ph);
            ano_dsp_phase_step(&v->u.drum.ph, v->freq * (1.0f + pe * 0.55f) / fs);
            float thump = ano_dsp_noise(&v->u.drum.rng)
                        * ano_dsp_asr_step(&v->u.drum.e3) * 0.2f;
            o = (body + thump) * ano_dsp_asr_step(&v->env) * v->amp;
            break;
        }
        case ANO_SYNTH_DRUM_CRASH: {
            float wash = ano_dsp_svf_step(&v->u.drum.c1, &v->u.drum.s1,
                                          ano_dsp_noise(&v->u.drum.rng), ANO_DSP_SVF_HIGHPASS);
            float shim = ano_dsp_svf_step(&v->u.drum.c2, &v->u.drum.s2,
                                          ano_dsp_noise(&v->u.drum.rng2), ANO_DSP_SVF_BANDPASS) * 0.5f;
            o = (wash + shim) * ano_dsp_asr_step(&v->env) * v->amp * 0.6f;
            break;
        }
        case ANO_SYNTH_DRUM_CHAT:
        case ANO_SYNTH_DRUM_OHAT:
            o = ano_dsp_svf_step(&v->u.drum.c1, &v->u.drum.s1,
                                 ano_dsp_noise(&v->u.drum.rng), ANO_DSP_SVF_HIGHPASS)
              * ano_dsp_asr_step(&v->env) * v->amp * 0.7f;
            break;
        case ANO_SYNTH_DRUM_SHAKER:
            o = ano_dsp_svf_step(&v->u.drum.c1, &v->u.drum.s1,
                                 ano_dsp_noise(&v->u.drum.rng), ANO_DSP_SVF_BANDPASS)
              * ano_dsp_asr_step(&v->env) * v->amp * 0.6f;
            break;
        default: // RIM
            o = ano_dsp_svf_step(&v->u.drum.c1, &v->u.drum.s1,
                                 ano_dsp_noise(&v->u.drum.rng), ANO_DSP_SVF_BANDPASS)
              * ano_dsp_asr_step(&v->env) * v->amp;
            break;
        }
        *l += o * v->panL;
        *r += o * v->panR;
        return;
    }

    default:
        return;
    }
}
