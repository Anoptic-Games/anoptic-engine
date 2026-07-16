/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

/* The console effect set (TECH_SPEC §12.2/§12.4). Defaults are the
 * prototype's console values and ship as tuning. Runs only on the mixer
 * thread (or the offline caller); no allocation, no locks after init. */

#include "audio_fx.h"

#include <math.h>
#include <string.h>

#include <anoptic_log.h>

#define FX_TAU_F 6.28318530717958647692f

// The public filter modes map straight onto the SVF's.
_Static_assert(ANO_AUDIO_FILTER_OFF == ANO_DSP_SVF_BYPASS
                   && ANO_AUDIO_FILTER_LOWPASS == ANO_DSP_SVF_LOWPASS
                   && ANO_AUDIO_FILTER_HIGHPASS == ANO_DSP_SVF_HIGHPASS
                   && ANO_AUDIO_FILTER_BANDPASS == ANO_DSP_SVF_BANDPASS,
               "AnoAudioFilterMode must mirror ANO_DSP_SVF_*");

static inline float fx_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void fx_smooth_init(AnoAudioSmooth *s, float v, float coef)
{
    ano_audio_smooth_snap(s, v);
    s->coef = coef;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool ano_audio_fx_init(AnoAudioFx *fx, uint32_t kind, mi_heap_t *heap,
                       uint32_t sampleRate, float coefBlock)
{
    memset(fx, 0, sizeof *fx);
    fx->kind = kind;
    fx->fs   = (float)sampleRate;
    const float fs = (float)sampleRate;

    switch ((AnoAudioEffectKind)kind) {
    case ANO_AUDIO_FX_NONE:
        return true;

    case ANO_AUDIO_FX_FILTER: {
        AnoAudioFxFilter *f = &fx->u.filter;
        f->mode = ANO_AUDIO_FILTER_OFF;
        fx_smooth_init(&f->cutoff, 1000.0f, coefBlock);
        fx_smooth_init(&f->q, 0.70710678f, coefBlock);
        return true;
    }

    case ANO_AUDIO_FX_EQ3: {
        AnoAudioFxEq3 *e = &fx->u.eq3;
        fx_smooth_init(&e->lowDb, 0.0f, coefBlock);
        fx_smooth_init(&e->lowF, 120.0f, coefBlock);
        fx_smooth_init(&e->midDb, 0.0f, coefBlock);
        fx_smooth_init(&e->midF, 1000.0f, coefBlock);
        fx_smooth_init(&e->midQ, 0.9f, coefBlock);
        fx_smooth_init(&e->highDb, 0.0f, coefBlock);
        fx_smooth_init(&e->highF, 6000.0f, coefBlock);
        return true;
    }

    case ANO_AUDIO_FX_DCBLOCK: {
        // pole for ~5 Hz corner
        fx->u.dc.R = 1.0f - FX_TAU_F * 5.0f / fs;
        return true;
    }

    case ANO_AUDIO_FX_DRIVE: {
        fx_smooth_init(&fx->u.drive.amount, 1.0f, coefBlock);
        fx_smooth_init(&fx->u.drive.trim, 1.0f, coefBlock);
        return true;
    }

    case ANO_AUDIO_FX_COMPRESSOR: {
        AnoAudioFxComp *c = &fx->u.comp;
        fx_smooth_init(&c->threshold, 0.30f, coefBlock);
        fx_smooth_init(&c->ratio, 2.5f, coefBlock);
        fx_smooth_init(&c->makeup, 1.0f, coefBlock);
        c->attackCoef  = ano_dsp_pole_ms(12.0f, fs);
        c->releaseCoef = ano_dsp_pole_ms(180.0f, fs);
        c->env  = 0.0f;
        c->gain = 1.0f;
        return true;
    }

    case ANO_AUDIO_FX_LIMITER: {
        AnoAudioFxLim *l = &fx->u.lim;
        fx_smooth_init(&l->ceiling, 0.92f, coefBlock);
        l->releaseCoef = ano_dsp_pole_ms(80.0f, fs);
        l->gain = 1.0f;
        l->lookahead = (uint32_t)(fs * 0.005f); // 5 ms window
        if (l->lookahead < 8u) l->lookahead = 8u;
        if (!ano_dsp_delay_init(&l->dl[0], heap, l->lookahead)
            || !ano_dsp_delay_init(&l->dl[1], heap, l->lookahead)
            || !ano_dsp_winmax_init(&l->wm, heap, l->lookahead))
            return false;
        return true;
    }

    case ANO_AUDIO_FX_CHORUS: {
        AnoAudioFxChorus *c = &fx->u.chorus;
        fx_smooth_init(&c->rate, 0.8f, coefBlock);
        fx_smooth_init(&c->depth, 4.0f, coefBlock);
        fx_smooth_init(&c->mix, 0.35f, coefBlock);
        uint32_t maxDelay = (uint32_t)(fs * 0.040f); // 15 ms center + 12 ms depth ceiling
        if (!ano_dsp_delay_init(&c->dl[0], heap, maxDelay)
            || !ano_dsp_delay_init(&c->dl[1], heap, maxDelay))
            return false;
        return true;
    }

    case ANO_AUDIO_FX_REVERB: {
        AnoAudioFxReverb *r = &fx->u.reverb;
        fx_smooth_init(&r->predelayMs, 20.0f, coefBlock);
        fx_smooth_init(&r->t60, 2.2f, coefBlock);
        fx_smooth_init(&r->dampHz, 4200.0f, coefBlock);
        fx_smooth_init(&r->mix, 1.0f, coefBlock); // return-bus usage: pure wet
        static const float lineMs[4] = { 33.7f, 45.3f, 57.7f, 68.9f }; // inharmonic (tuning)
        if (!ano_dsp_delay_init(&r->pre, heap, (uint32_t)(fs * 0.200f)))
            return false;
        if (!ano_dsp_allpass_init(&r->ap[0], heap, (uint32_t)(fs * 0.0059f), 0.70f)
            || !ano_dsp_allpass_init(&r->ap[1], heap, (uint32_t)(fs * 0.0083f), 0.70f))
            return false;
        for (int i = 0; i < 4; ++i) {
            r->lineSec[i] = lineMs[i] * 0.001f;
            r->lineLen[i] = (uint32_t)(fs * r->lineSec[i]);
            if (!ano_dsp_delay_init(&r->line[i], heap, r->lineLen[i]))
                return false;
        }
        ano_dsp_biquad_highshelf(&r->shelfC, 4500.0f, -4.0f, fs);
        return true;
    }

    case ANO_AUDIO_FX_PINGPONG: {
        AnoAudioFxPingpong *p = &fx->u.pp;
        fx_smooth_init(&p->timeMs, 300.0f, coefBlock);
        fx_smooth_init(&p->feedback, 0.42f, coefBlock);
        fx_smooth_init(&p->mix, 1.0f, coefBlock); // return-bus usage: pure wet
        uint32_t maxDelay = (uint32_t)(fs * 1.2f);
        if (!ano_dsp_delay_init(&p->dl[0], heap, maxDelay)
            || !ano_dsp_delay_init(&p->dl[1], heap, maxDelay))
            return false;
        return true;
    }

    case ANO_AUDIO_FX_WIDTH: {
        fx_smooth_init(&fx->u.width.amount, 1.0f, coefBlock);
        return true;
    }

    default:
        ano_log(ANO_WARN, "audio: unknown effect kind %u; slot left empty.", kind);
        fx->kind = ANO_AUDIO_FX_NONE;
        return true;
    }
}

// ---------------------------------------------------------------------------
// parameter dispatch (loud range handling lives here — finding 7)
// ---------------------------------------------------------------------------

void ano_audio_fx_set(AnoAudioFx *fx, uint32_t paramId, float value)
{
    if (paramId == ANO_AUDIO_P_BYPASS) {
        fx->bypass = value != 0.0f;
        return;
    }
    switch ((AnoAudioEffectKind)fx->kind) {

    case ANO_AUDIO_FX_FILTER: {
        AnoAudioFxFilter *f = &fx->u.filter;
        switch (paramId) {
        case ANO_AUDIO_P_FILTER_MODE: {
            uint32_t mode = (uint32_t)value;
            if (mode > ANO_AUDIO_FILTER_BANDPASS)
                mode = ANO_AUDIO_FILTER_OFF;
            if (f->mode == ANO_AUDIO_FILTER_OFF && mode != ANO_AUDIO_FILTER_OFF) {
                // entering: start at the target, from rest — no sweep from stale state
                ano_audio_smooth_snap(&f->cutoff, f->cutoff.target);
                ano_audio_smooth_snap(&f->q, f->q.target);
                memset(f->s, 0, sizeof f->s);
            }
            f->mode = mode;
            return;
        }
        case ANO_AUDIO_P_FILTER_CUTOFF: f->cutoff.target = fx_clampf(value, 20.0f, 20000.0f); return;
        case ANO_AUDIO_P_FILTER_Q:      f->q.target = fx_clampf(value, 0.1f, 12.0f); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_EQ3: {
        AnoAudioFxEq3 *e = &fx->u.eq3;
        switch (paramId) {
        case ANO_AUDIO_P_EQ_LOW_GAIN_DB:  e->lowDb.target = fx_clampf(value, -24.0f, 24.0f); return;
        case ANO_AUDIO_P_EQ_LOW_FREQ:     e->lowF.target = fx_clampf(value, 20.0f, 2000.0f); return;
        case ANO_AUDIO_P_EQ_MID_GAIN_DB:  e->midDb.target = fx_clampf(value, -24.0f, 24.0f); return;
        case ANO_AUDIO_P_EQ_MID_FREQ:     e->midF.target = fx_clampf(value, 100.0f, 10000.0f); return;
        case ANO_AUDIO_P_EQ_MID_Q:        e->midQ.target = fx_clampf(value, 0.1f, 12.0f); return;
        case ANO_AUDIO_P_EQ_HIGH_GAIN_DB: e->highDb.target = fx_clampf(value, -24.0f, 24.0f); return;
        case ANO_AUDIO_P_EQ_HIGH_FREQ:    e->highF.target = fx_clampf(value, 1000.0f, 20000.0f); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_DRIVE: {
        switch (paramId) {
        case ANO_AUDIO_P_DRIVE_AMOUNT: fx->u.drive.amount.target = fx_clampf(value, 0.1f, 16.0f); return;
        case ANO_AUDIO_P_DRIVE_TRIM:   fx->u.drive.trim.target = fx_clampf(value, 0.0f, 4.0f); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_COMPRESSOR: {
        AnoAudioFxComp *c = &fx->u.comp;
        switch (paramId) {
        case ANO_AUDIO_P_COMP_THRESHOLD:  c->threshold.target = fx_clampf(value, 0.01f, 1.0f); return;
        case ANO_AUDIO_P_COMP_RATIO:      c->ratio.target = fx_clampf(value, 1.0f, 20.0f); return;
        case ANO_AUDIO_P_COMP_ATTACK_MS:  c->attackCoef = ano_dsp_pole_ms(fx_clampf(value, 0.1f, 500.0f), fx->fs); return;
        case ANO_AUDIO_P_COMP_RELEASE_MS: c->releaseCoef = ano_dsp_pole_ms(fx_clampf(value, 1.0f, 2000.0f), fx->fs); return;
        case ANO_AUDIO_P_COMP_MAKEUP:     c->makeup.target = fx_clampf(value, 0.25f, 4.0f); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_LIMITER: {
        AnoAudioFxLim *l = &fx->u.lim;
        switch (paramId) {
        case ANO_AUDIO_P_LIM_CEILING:    l->ceiling.target = fx_clampf(value, 0.1f, 1.0f); return;
        case ANO_AUDIO_P_LIM_RELEASE_MS: l->releaseCoef = ano_dsp_pole_ms(fx_clampf(value, 1.0f, 1000.0f), fx->fs); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_CHORUS: {
        AnoAudioFxChorus *c = &fx->u.chorus;
        switch (paramId) {
        case ANO_AUDIO_P_CHORUS_RATE_HZ:  c->rate.target = fx_clampf(value, 0.01f, 8.0f); return;
        case ANO_AUDIO_P_CHORUS_DEPTH_MS: c->depth.target = fx_clampf(value, 0.1f, 12.0f); return;
        case ANO_AUDIO_P_CHORUS_MIX:      c->mix.target = fx_clampf(value, 0.0f, 1.0f); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_REVERB: {
        AnoAudioFxReverb *r = &fx->u.reverb;
        switch (paramId) {
        case ANO_AUDIO_P_REV_PREDELAY_MS: r->predelayMs.target = fx_clampf(value, 0.0f, 190.0f); return;
        case ANO_AUDIO_P_REV_T60_S:       r->t60.target = fx_clampf(value, 0.1f, 12.0f); return;
        case ANO_AUDIO_P_REV_DAMP_HZ:     r->dampHz.target = fx_clampf(value, 500.0f, 18000.0f); return;
        case ANO_AUDIO_P_REV_MIX:         r->mix.target = fx_clampf(value, 0.0f, 1.0f); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_PINGPONG: {
        AnoAudioFxPingpong *p = &fx->u.pp;
        switch (paramId) {
        case ANO_AUDIO_P_PP_TIME_MS:  p->timeMs.target = fx_clampf(value, 10.0f, 1100.0f); return;
        case ANO_AUDIO_P_PP_FEEDBACK: p->feedback.target = fx_clampf(value, 0.0f, 0.95f); return;
        case ANO_AUDIO_P_PP_MIX:      p->mix.target = fx_clampf(value, 0.0f, 1.0f); return;
        default: break;
        }
        break;
    }

    case ANO_AUDIO_FX_WIDTH: {
        if (paramId == ANO_AUDIO_P_WIDTH_AMOUNT) {
            fx->u.width.amount.target = fx_clampf(value, 0.0f, 2.0f);
            return;
        }
        break;
    }

    default:
        break;
    }
    ano_debug_log(ANO_WARN, "audio: FX_SET param %u does not belong to effect kind %u; dropped.",
                  paramId, fx->kind);
}

// ---------------------------------------------------------------------------
// processing
// ---------------------------------------------------------------------------

static void fx_filter(AnoAudioFxFilter *f, float *m, uint32_t frames, float fs)
{
    if (f->mode == ANO_AUDIO_FILTER_OFF)
        return;
    float fc = ano_audio_smooth_step(&f->cutoff);
    float q  = ano_audio_smooth_step(&f->q);
    ano_dsp_svf_coef(&f->c, fc, q, fs);
    ano_dsp_svf_flush(&f->s[0]);
    ano_dsp_svf_flush(&f->s[1]);
    for (uint32_t i = 0; i < frames; ++i) {
        m[2u * i]      = ano_dsp_svf_step(&f->c, &f->s[0], m[2u * i], f->mode);
        m[2u * i + 1u] = ano_dsp_svf_step(&f->c, &f->s[1], m[2u * i + 1u], f->mode);
    }
}

static void fx_eq3(AnoAudioFxEq3 *e, float *m, uint32_t frames, float fs)
{
    ano_dsp_biquad_lowshelf(&e->cl, ano_audio_smooth_step(&e->lowF),
                            ano_audio_smooth_step(&e->lowDb), fs);
    ano_dsp_biquad_peak(&e->cm, ano_audio_smooth_step(&e->midF),
                        ano_audio_smooth_step(&e->midQ),
                        ano_audio_smooth_step(&e->midDb), fs);
    ano_dsp_biquad_highshelf(&e->ch, ano_audio_smooth_step(&e->highF),
                             ano_audio_smooth_step(&e->highDb), fs);
    for (int ch = 0; ch < 2; ++ch) {
        ano_dsp_biquad_flush(&e->sl[ch]);
        ano_dsp_biquad_flush(&e->sm[ch]);
        ano_dsp_biquad_flush(&e->sh[ch]);
    }
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < 2u; ++ch) {
            float v = m[2u * i + ch];
            v = ano_dsp_biquad_step(&e->cl, &e->sl[ch], v);
            v = ano_dsp_biquad_step(&e->cm, &e->sm[ch], v);
            v = ano_dsp_biquad_step(&e->ch, &e->sh[ch], v);
            m[2u * i + ch] = v;
        }
    }
}

static void fx_dc(AnoAudioFxDc *d, float *m, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < 2u; ++ch) {
            float x = m[2u * i + ch];
            float y = x - d->x1[ch] + d->R * d->y1[ch];
            d->x1[ch] = x;
            d->y1[ch] = (y < 1.0e-20f && y > -1.0e-20f) ? 0.0f : y;
            m[2u * i + ch] = y;
        }
    }
}

static void fx_drive(AnoAudioFxDrive *d, float *m, uint32_t frames)
{
    float amt  = ano_audio_smooth_step(&d->amount);
    float trim = ano_audio_smooth_step(&d->trim);
    for (uint32_t i = 0; i < frames * 2u; ++i)
        m[i] = tanhf(m[i] * amt) * trim;
}

static void fx_comp(AnoAudioFxComp *c, float *m, uint32_t frames)
{
    float thr      = ano_audio_smooth_step(&c->threshold);
    float invRatio = 1.0f / ano_audio_smooth_step(&c->ratio);
    float mk       = ano_audio_smooth_step(&c->makeup);
    for (uint32_t i = 0; i < frames; ++i) {
        // feedback topology: last sample's gain shapes this sample; the
        // detector listens to the OUTPUT
        float g = c->gain * mk;
        float l = m[2u * i] * g;
        float r = m[2u * i + 1u] * g;
        m[2u * i]      = l;
        m[2u * i + 1u] = r;
        float al = l < 0.0f ? -l : l;
        float ar = r < 0.0f ? -r : r;
        float d  = al > ar ? al : ar;
        c->env += (d > c->env ? c->attackCoef : c->releaseCoef) * (d - c->env);
        if (c->env < 1.0e-20f) c->env = 0.0f;
        float target = ano_dsp_comp_gain(c->env, thr, invRatio);
        c->gain = fx_clampf(target, 0.05f, 1.0f);
    }
}

static void fx_limiter(AnoAudioFxLim *l, float *m, uint32_t frames)
{
    float ceil = ano_audio_smooth_step(&l->ceiling);
    for (uint32_t i = 0; i < frames; ++i) {
        float inL = m[2u * i], inR = m[2u * i + 1u];
        float a  = inL < 0.0f ? -inL : inL;
        float ar = inR < 0.0f ? -inR : inR;
        if (ar > a) a = ar;
        float mx = ano_dsp_winmax_push(&l->wm, a);
        float target = mx > ceil ? ceil / mx : 1.0f;
        if (target < l->gain)
            l->gain = target; // instant attack: the lookahead already absorbed it
        else
            l->gain += l->releaseCoef * (target - l->gain);
        float dl = ano_dsp_delay_read_int(&l->dl[0], l->lookahead);
        float dr = ano_dsp_delay_read_int(&l->dl[1], l->lookahead);
        ano_dsp_delay_write(&l->dl[0], inL);
        ano_dsp_delay_write(&l->dl[1], inR);
        m[2u * i]      = dl * l->gain;
        m[2u * i + 1u] = dr * l->gain;
    }
}

static void fx_chorus(AnoAudioFxChorus *c, float *m, uint32_t frames, float fs)
{
    float rate  = ano_audio_smooth_step(&c->rate);
    float depth = ano_audio_smooth_step(&c->depth) * 0.001f * fs; // samples
    float mix   = ano_audio_smooth_step(&c->mix);
    float center = 0.015f * fs;
    double step  = (double)rate / (double)fs;
    for (uint32_t i = 0; i < frames; ++i) {
        float ph = (float)c->phase * FX_TAU_F;
        float t0 = center + depth * sinf(ph);
        float t1 = center + depth * sinf(ph + 1.5707963f);
        ano_dsp_delay_write(&c->dl[0], m[2u * i]);
        ano_dsp_delay_write(&c->dl[1], m[2u * i + 1u]);
        float wl = ano_dsp_delay_read_frac(&c->dl[0], t0);
        float wr = ano_dsp_delay_read_frac(&c->dl[1], t1);
        m[2u * i]      += (wl - m[2u * i]) * mix;
        m[2u * i + 1u] += (wr - m[2u * i + 1u]) * mix;
        c->phase += step;
        if (c->phase >= 1.0)
            c->phase -= 1.0;
    }
}

static void fx_reverb(AnoAudioFxReverb *r, float *m, uint32_t frames, float fs)
{
    float pre  = ano_audio_smooth_step(&r->predelayMs) * 0.001f * fs;
    float t60  = ano_audio_smooth_step(&r->t60);
    float damp = ano_audio_smooth_step(&r->dampHz);
    float mix  = ano_audio_smooth_step(&r->mix);
    r->dampCoef = 1.0f - expf(-FX_TAU_F * damp / fs);
    for (int i = 0; i < 4; ++i) {
        r->lineGain[i] = powf(10.0f, -3.0f * r->lineSec[i] / t60);
        if (r->dampState[i] < 1.0e-20f && r->dampState[i] > -1.0e-20f)
            r->dampState[i] = 0.0f;
    }
    for (uint32_t i = 0; i < frames; ++i) {
        float dryL = m[2u * i], dryR = m[2u * i + 1u];
        // tank input: mono sum -> predelay -> two diffusers
        ano_dsp_delay_write(&r->pre, 0.5f * (dryL + dryR));
        float v = ano_dsp_delay_read_frac(&r->pre, pre < 1.0f ? 1.0f : pre);
        v = ano_dsp_allpass_step(&r->ap[0], v);
        v = ano_dsp_allpass_step(&r->ap[1], v);
        // Householder 4-line FDN with in-loop damping and per-line T60 gains
        float o[4], sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
            float raw = ano_dsp_delay_read_int(&r->line[k], r->lineLen[k]);
            r->dampState[k] += r->dampCoef * (raw - r->dampState[k]);
            o[k] = r->dampState[k];
            sum += o[k];
        }
        float half = 0.5f * sum;
        for (int k = 0; k < 4; ++k)
            ano_dsp_delay_write(&r->line[k], v + r->lineGain[k] * (o[k] - half));
        // decorrelated stereo taps -> output shelf
        float wl = 0.7f * (o[0] - o[2]);
        float wr = 0.7f * (o[1] - o[3]);
        wl = ano_dsp_biquad_step(&r->shelfC, &r->shelfS[0], wl);
        wr = ano_dsp_biquad_step(&r->shelfC, &r->shelfS[1], wr);
        m[2u * i]      = dryL + (wl - dryL) * mix;
        m[2u * i + 1u] = dryR + (wr - dryR) * mix;
    }
    ano_dsp_biquad_flush(&r->shelfS[0]);
    ano_dsp_biquad_flush(&r->shelfS[1]);
}

static void fx_pingpong(AnoAudioFxPingpong *p, float *m, uint32_t frames, float fs)
{
    float time = ano_audio_smooth_step(&p->timeMs) * 0.001f * fs;
    float fb   = ano_audio_smooth_step(&p->feedback);
    float mix  = ano_audio_smooth_step(&p->mix);
    for (uint32_t i = 0; i < frames; ++i) {
        float dryL = m[2u * i], dryR = m[2u * i + 1u];
        float outL = ano_dsp_delay_read_frac(&p->dl[0], time);
        float outR = ano_dsp_delay_read_frac(&p->dl[1], time);
        // the cross-feedback IS the ping-pong
        ano_dsp_delay_write(&p->dl[0], dryL + fb * outR);
        ano_dsp_delay_write(&p->dl[1], dryR + fb * outL);
        m[2u * i]      = dryL + (outL - dryL) * mix;
        m[2u * i + 1u] = dryR + (outR - dryR) * mix;
    }
}

static void fx_width(AnoAudioFxWidth *w, float *m, uint32_t frames)
{
    float amt = ano_audio_smooth_step(&w->amount);
    for (uint32_t i = 0; i < frames; ++i) {
        float mid  = 0.5f * (m[2u * i] + m[2u * i + 1u]);
        float side = 0.5f * (m[2u * i] - m[2u * i + 1u]) * amt;
        m[2u * i]      = mid + side;
        m[2u * i + 1u] = mid - side;
    }
}

void ano_audio_fx_process(AnoAudioFx *fx, float *stereo, uint32_t frames, uint32_t sampleRate)
{
    if (fx->kind == ANO_AUDIO_FX_NONE || fx->bypass)
        return;
    const float fs = (float)sampleRate;
    switch ((AnoAudioEffectKind)fx->kind) {
    case ANO_AUDIO_FX_FILTER:     fx_filter(&fx->u.filter, stereo, frames, fs); return;
    case ANO_AUDIO_FX_EQ3:        fx_eq3(&fx->u.eq3, stereo, frames, fs); return;
    case ANO_AUDIO_FX_DCBLOCK:    fx_dc(&fx->u.dc, stereo, frames); return;
    case ANO_AUDIO_FX_DRIVE:      fx_drive(&fx->u.drive, stereo, frames); return;
    case ANO_AUDIO_FX_COMPRESSOR: fx_comp(&fx->u.comp, stereo, frames); return;
    case ANO_AUDIO_FX_LIMITER:    fx_limiter(&fx->u.lim, stereo, frames); return;
    case ANO_AUDIO_FX_CHORUS:     fx_chorus(&fx->u.chorus, stereo, frames, fs); return;
    case ANO_AUDIO_FX_REVERB:     fx_reverb(&fx->u.reverb, stereo, frames, fs); return;
    case ANO_AUDIO_FX_PINGPONG:   fx_pingpong(&fx->u.pp, stereo, frames, fs); return;
    case ANO_AUDIO_FX_WIDTH:      fx_width(&fx->u.width, stereo, frames); return;
    default: return;
    }
}
