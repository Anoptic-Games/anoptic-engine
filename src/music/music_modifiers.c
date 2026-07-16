/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Humanize: two gauss per event always (tie zero after draws; stream stays aligned). Starts round(.,6).
// Echo: span mask grows as echoes land; masked repeat still decays velocity.
// Strum: groups by EXACT float start; tied-out half dur recomputed from rounded start (join bit-exact).
// Swing fmod safe (starts non-negative).

#include <math.h>
#include <string.h>

#include "music_modifiers.h"

static int clamp_velocity(double v)
{
    int r = (int)ano_music_round_int(v);
    return r < 1 ? 1 : r > 127 ? 127 : r;
}

AnoModifier ano_mod_swing(double amount)
{
    return (AnoModifier){ .kind = ANO_MOD_SWING, .u.swing = { amount } };
}

AnoModifier ano_mod_humanize(double tSigma, double vSigma)
{
    return (AnoModifier){ .kind = ANO_MOD_HUMANIZE, .u.humanize = { tSigma, vSigma } };
}

AnoModifier ano_mod_articulate(void)
{
    return (AnoModifier){ .kind = ANO_MOD_ARTICULATE, .u.articulate = { false, 0.0 } };
}

AnoModifier ano_mod_articulate_gate(double gate)
{
    return (AnoModifier){ .kind = ANO_MOD_ARTICULATE, .u.articulate = { true, gate } };
}

AnoModifier ano_mod_accent(void)
{
    return (AnoModifier){ .kind = ANO_MOD_ACCENT, .u.accent = { false, 0.0 } };
}

AnoModifier ano_mod_accent_depth(double depth)
{
    return (AnoModifier){ .kind = ANO_MOD_ACCENT, .u.accent = { true, depth } };
}

AnoModifier ano_mod_perform(double hairpin, double contour, double agogic, double lag)
{
    return (AnoModifier){ .kind = ANO_MOD_PERFORM,
                          .u.perform = { hairpin, contour, agogic, 0.05, lag } };
}

AnoModifier ano_mod_echo(void)
{
    return (AnoModifier){ .kind = ANO_MOD_ECHO, .u.echo = { 0.75, 0.55, 2, 24 } };
}

AnoModifier ano_mod_strum(double spread)
{
    return (AnoModifier){ .kind = ANO_MOD_STRUM, .u.strum = { spread } };
}

AnoModifier ano_mod_transpose(int octaves, int steps)
{
    return (AnoModifier){ .kind = ANO_MOD_TRANSPOSE, .u.transpose = { octaves, steps } };
}

static uint32_t apply_swing(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n,
                            AnoMeter meter)
{
    if (ano_meter_is_compound(meter))
        return n; // the feel is already ternary
    for (uint32_t i = 0; i < n; ++i) {
        double frac = fmod(ev[i].core.start, 1.0);
        double shift;
        if (fabs(frac - 0.5) < 1e-6)
            shift = m->u.swing.amount / 6.0;
        else if (fabs(frac - 0.25) < 1e-6 || fabs(frac - 0.75) < 1e-6)
            shift = m->u.swing.amount / 12.0;
        else
            shift = 0.0;
        if (shift != 0.0) {
            ev[i].core.start = ev[i].core.start + shift;
            double d = ev[i].core.dur - shift;
            ev[i].core.dur = d > ANO_MOD_MIN_DUR ? d : ANO_MOD_MIN_DUR;
        }
    }
    return n;
}

static uint32_t apply_humanize(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n,
                               const AnoHarmonicContext *ctx, AnoMeter meter,
                               AnoMusicRng *rng)
{
    double barStart = ctx->bar * ano_meter_bar_quarters(meter);
    double ts = m->u.humanize.tSigma, vs = m->u.humanize.vSigma;
    for (uint32_t i = 0; i < n; ++i) {
        // both draws happen for every event: the stream stays aligned
        double dt = ano_music_gauss(rng, 0.0, ts);
        if (dt < -2 * ts)
            dt = -2 * ts;
        if (dt > 2 * ts)
            dt = 2 * ts;
        double dv = ano_music_gauss(rng, 0.0, vs);
        if (ev[i].core.tie != ANO_MUSIC_TIE_NONE)
            dt = 0.0;
        if (ev[i].core.tie == ANO_MUSIC_TIE_IN || ev[i].core.tie == ANO_MUSIC_TIE_BOTH)
            dv = 0.0;
        double s = ev[i].core.start + dt;
        if (s < barStart)
            s = barStart;
        ev[i].core.start = ano_music_round(s, 6);
        ev[i].core.velocity = (uint8_t)clamp_velocity(ev[i].core.velocity + dv);
    }
    return n;
}

static uint32_t apply_articulate(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n,
                                 const AnoGenParams *params)
{
    double gate = m->u.articulate.hasGate ? m->u.articulate.gate : params->articulation;
    for (uint32_t i = 0; i < n; ++i) {
        if (ev[i].core.tie == ANO_MUSIC_TIE_OUT || ev[i].core.tie == ANO_MUSIC_TIE_BOTH)
            continue; // shortening a join half would open a gap in the note
        double d = ev[i].core.dur * gate;
        ev[i].core.dur = d > ANO_MOD_MIN_DUR ? d : ANO_MOD_MIN_DUR;
    }
    return n;
}

static uint32_t apply_accent(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n,
                             AnoMeter meter, const AnoGenParams *params)
{
    double depth = m->u.accent.hasDepth ? m->u.accent.depth
                                        : (double)params->accentDepth;
    double weights[ANO_METER_MAX_SLOTS];
    uint32_t wn = ano_meter_metric_weights(meter, weights);
    for (uint32_t i = 0; i < n; ++i) {
        int slot = ano_meter_slot_of(meter, ev[i].core.start);
        double w = slot >= 0 && slot < (int)wn ? weights[slot] : 1.0;
        ev[i].core.velocity = (uint8_t)clamp_velocity(
            ev[i].core.velocity + depth * (w - 2.5) / 3.0);
    }
    return n;
}

static uint32_t apply_perform(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n,
                              const AnoHarmonicContext *ctx, AnoMeter meter,
                              const AnoGenParams *params)
{
    const double hairpin = m->u.perform.hairpin, contour = m->u.perform.contour;
    const double agogic = m->u.perform.agogic, luftpause = m->u.perform.luftpause;
    int bars = ctx->phraseBars > 1 ? ctx->phraseBars : 1;
    double barQ = ano_meter_bar_quarters(meter);
    double phraseLen = bars * barQ;
    double phraseStart = (ctx->bar - ctx->phrasePos) * barQ;
    // the swell peaks at the planned apex when one exists, else mid-pre-cadence
    double crest;
    if (0 <= ctx->phraseApex && ctx->phraseApex < bars - 1)
        crest = (ctx->phraseApex + 0.5) / bars;
    else
        crest = (bars - 1.5 > 0.5 ? bars - 1.5 : 0.5) / bars;
    double barStart = ctx->bar * barQ;
    double cut = barStart + barQ - luftpause;
    double lag = m->u.perform.lag * (1.0 - 2.0 * params->noteDensity);
    for (uint32_t i = 0; i < n; ++i) {
        double frac = (ev[i].core.start - phraseStart) / phraseLen;
        frac = frac > 0.0 ? frac : 0.0;
        frac = frac < 1.0 ? frac : 1.0;
        double swell = frac <= crest ? frac / crest : (1.0 - frac) / (1.0 - crest);
        double velocity = ev[i].core.velocity * (1.0 + hairpin * (swell - 0.5))
                        + contour * (ev[i].core.pitch - params->registerCenter);
        double start = ev[i].core.start, dur = ev[i].core.dur;
        uint8_t tie = ev[i].core.tie;
        if (agogic != 0.0 && ctx->phrasePos == 0
            && ano_meter_slot_of(meter, ev[i].core.start) == 0
            && tie != ANO_MUSIC_TIE_OUT && tie != ANO_MUSIC_TIE_BOTH)
            dur *= 1.0 + agogic; // never stretch a join open
        if (lag != 0.0 && tie == ANO_MUSIC_TIE_NONE) {
            double s = start + lag;
            if (s < barStart)
                s = barStart;
            start = ano_music_round(s, 6);
        }
        // the anacrusis pickups live where the luftpause would carve
        if (luftpause != 0.0 && ctx->phrasePos == bars - 1
            && tie == ANO_MUSIC_TIE_NONE && strcmp(ev[i].role, "pickup") != 0
            && start < cut && cut < start + dur) {
            double d = cut - start;
            dur = d > ANO_MOD_MIN_DUR ? d : ANO_MOD_MIN_DUR;
        }
        ev[i].core.start = start;
        ev[i].core.dur = dur;
        ev[i].core.velocity = (uint8_t)clamp_velocity(velocity);
    }
    return n;
}

static uint32_t apply_echo(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n,
                           uint32_t cap)
{
    typedef struct Span { double s, e; int p; } Span;
    Span spans[192];
    uint32_t sn = 0;
    for (uint32_t i = 0; i < n && sn < 192u; ++i)
        spans[sn++] = (Span){ ev[i].core.start,
                              ev[i].core.start + ev[i].core.dur, ev[i].core.pitch };
    uint32_t out = n;
    for (uint32_t i = 0; i < n; ++i) {
        if (ev[i].core.tie != ANO_MUSIC_TIE_NONE)
            continue; // a chain is one note
        double velocity = ev[i].core.velocity;
        for (int k = 1; k <= m->u.echo.repeats; ++k) {
            velocity *= m->u.echo.decay;
            if (velocity < m->u.echo.minVelocity)
                break;
            double start = ano_music_round(ev[i].core.start + k * m->u.echo.delay, 6);
            double dur = m->u.echo.delay * 0.9;
            if (ev[i].core.dur < dur)
                dur = ev[i].core.dur;
            bool masked = false;
            for (uint32_t j = 0; j < sn && !masked; ++j)
                if (spans[j].p == ev[i].core.pitch && start < spans[j].e - 1e-9
                    && spans[j].s - 1e-9 < start + dur)
                    masked = true;
            if (masked)
                continue; // the running velocity keeps decaying
            if (out >= cap || sn >= 192u)
                continue;
            spans[sn++] = (Span){ start, start + dur, ev[i].core.pitch };
            ev[out] = ev[i];
            ev[out].core.start = start;
            ev[out].core.dur = dur;
            ev[out].core.velocity = (uint8_t)clamp_velocity(velocity);
            strncpy(ev[out].role, "echo", sizeof ev[out].role - 1);
            ev[out].role[4] = '\0';
            out++;
        }
    }
    // sort by (start, pitch), stable
    for (uint32_t i = 1; i < out; ++i) {
        AnoMusicEvent key = ev[i];
        uint32_t j = i;
        while (j > 0
               && (ev[j - 1].core.start > key.core.start
                   || (ev[j - 1].core.start == key.core.start
                       && ev[j - 1].core.pitch > key.core.pitch))) {
            ev[j] = ev[j - 1];
            --j;
        }
        ev[j] = key;
    }
    return out;
}

static uint32_t apply_strum(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n)
{
    AnoMusicEvent tmp[64];
    uint32_t out = 0;
    bool used[64] = { false };
    for (;;) {
        // the next unprocessed group = the smallest remaining start (exact ==)
        bool have = false;
        double start = 0.0;
        for (uint32_t i = 0; i < n; ++i)
            if (!used[i] && (!have || ev[i].core.start < start)) {
                have = true;
                start = ev[i].core.start;
            }
        if (!have)
            break;
        // gather the chord in original order, then stable-sort by pitch
        AnoMusicEvent chord[16];
        uint32_t cn = 0;
        for (uint32_t i = 0; i < n; ++i)
            if (!used[i] && ev[i].core.start == start) {
                used[i] = true;
                chord[cn++] = ev[i];
            }
        for (uint32_t i = 1; i < cn; ++i) {
            AnoMusicEvent key = chord[i];
            uint32_t j = i;
            while (j > 0 && chord[j - 1].core.pitch > key.core.pitch) {
                chord[j] = chord[j - 1];
                --j;
            }
            chord[j] = key;
        }
        for (uint32_t i = 0; i < cn; ++i) {
            double offset = cn > 1 ? m->u.strum.spread * i / (double)(cn - 1) : 0.0;
            uint8_t tie = chord[i].core.tie;
            if (tie == ANO_MUSIC_TIE_IN || tie == ANO_MUSIC_TIE_BOTH)
                offset = 0.0; // a continuation has no attack to stagger
            double s = ano_music_round(chord[i].core.start + offset, 6);
            // a tied-out end IS the join: recompute dur from the rounded start
            double dur = tie == ANO_MUSIC_TIE_OUT || tie == ANO_MUSIC_TIE_BOTH
                       ? chord[i].core.start + chord[i].core.dur - s
                       : chord[i].core.dur - offset;
            tmp[out] = chord[i];
            tmp[out].core.start = s;
            tmp[out].core.dur = dur > ANO_MOD_MIN_DUR ? dur : ANO_MOD_MIN_DUR;
            out++;
        }
    }
    memcpy(ev, tmp, out * sizeof tmp[0]);
    return out;
}

static uint32_t apply_transpose(const AnoModifier *m, AnoMusicEvent *ev, uint32_t n,
                                const AnoHarmonicContext *ctx)
{
    for (uint32_t i = 0; i < n; ++i) {
        int pitch = ev[i].core.pitch + 12 * m->u.transpose.octaves;
        if (m->u.transpose.steps)
            pitch = ano_diatonic_shift(ctx->scale, pitch, m->u.transpose.steps);
        pitch = pitch < 0 ? 0 : pitch > 127 ? 127 : pitch;
        ev[i].core.pitch = (uint8_t)pitch;
        int deg = ano_scale_degree_of(ctx->scale, pitch);
        ev[i].degree = deg > 0 ? (uint8_t)deg : 0;
    }
    return n;
}

uint32_t ano_mod_apply(const AnoModifier *mod, AnoMusicEvent *events,
                       uint32_t count, uint32_t cap,
                       const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, AnoMusicRng *rng)
{
    switch (mod->kind) {
    case ANO_MOD_SWING:      return apply_swing(mod, events, count, meter);
    case ANO_MOD_HUMANIZE:   return apply_humanize(mod, events, count, ctx, meter, rng);
    case ANO_MOD_ARTICULATE: return apply_articulate(mod, events, count, params);
    case ANO_MOD_ACCENT:     return apply_accent(mod, events, count, meter, params);
    case ANO_MOD_PERFORM:    return apply_perform(mod, events, count, ctx, meter, params);
    case ANO_MOD_ECHO:       return apply_echo(mod, events, count, cap);
    case ANO_MOD_STRUM:      return apply_strum(mod, events, count);
    case ANO_MOD_TRANSPOSE:  return apply_transpose(mod, events, count, ctx);
    }
    return count;
}

uint32_t ano_apply_chain(const AnoModifier *chain, uint32_t chainLen,
                         AnoMusicEvent *events, uint32_t count, uint32_t cap,
                         const AnoHarmonicContext *ctx, AnoMeter meter,
                         const AnoGenParams *params, AnoMusicRng *rng)
{
    for (uint32_t i = 0; i < chainLen; ++i)
        count = ano_mod_apply(&chain[i], events, count, cap, ctx, meter, params, rng);
    return count;
}

uint32_t ano_default_chain(uint8_t layer, bool perform, AnoModifier out[4])
{
    if (!perform) {
        switch (layer) {
        case ANO_MUSIC_PAD:
            out[0] = ano_mod_strum(0.05);
            out[1] = ano_mod_humanize(0.010, 3.0);
            return 2;
        case ANO_MUSIC_BASS:
            out[0] = ano_mod_humanize(0.008, 3.0);
            return 1;
        case ANO_MUSIC_MELODY:
        case ANO_MUSIC_COUNTER:
            out[0] = ano_mod_articulate();
            out[1] = ano_mod_accent();
            out[2] = ano_mod_humanize(0.015, 5.0);
            return 3;
        case ANO_MUSIC_ARP:
            out[0] = ano_mod_echo();
            return 1;
        default: // perc
            out[0] = ano_mod_humanize(0.006, 3.0);
            return 1;
        }
    }
    switch (layer) {
    case ANO_MUSIC_PAD:
        out[0] = ano_mod_perform(0.10, 0.0, 0.0, 0.0);
        out[1] = ano_mod_strum(0.05);
        out[2] = ano_mod_humanize(0.010, 3.0);
        return 3;
    case ANO_MUSIC_BASS:
        out[0] = ano_mod_perform(0.10, 0.0, 0.0, 0.0);
        out[1] = ano_mod_humanize(0.008, 3.0);
        return 2;
    case ANO_MUSIC_MELODY:
        out[0] = ano_mod_articulate();
        out[1] = ano_mod_accent();
        out[2] = ano_mod_perform(0.14, 0.4, 0.10, 0.02);
        out[3] = ano_mod_humanize(0.015, 5.0);
        return 4;
    case ANO_MUSIC_COUNTER:
        // subordinate: shallower hairpin, no agogic
        out[0] = ano_mod_articulate();
        out[1] = ano_mod_accent();
        out[2] = ano_mod_perform(0.12, 0.3, 0.0, 0.01);
        out[3] = ano_mod_humanize(0.015, 5.0);
        return 4;
    case ANO_MUSIC_ARP:
        out[0] = ano_mod_perform(0.10, 0.0, 0.0, 0.0);
        out[1] = ano_mod_echo();
        return 2;
    default: // perc keeps its groove dynamics
        out[0] = ano_mod_humanize(0.006, 3.0);
        return 1;
    }
}
