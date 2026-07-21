/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Theory linter. Stable insertion sorts (equal keys keep emission order; not qsort).
// Contract: rule/bar multiset, not violation order.

#include "music_verify.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "music_perc.h"

#define LINT_MAX_LINE  1024 // merged melodic line
#define LINT_MAX_GROUP 16   // simultaneous pad voices
#define LINT_MAX_PAT   64   // percussion hits / arp onsets in one bar
#define LINT_MAX_BARS  1024

/* Roles */

// Scale-outside licenses: echo, motif, imitation, doubling (tighter whitelist).
static const char *const CHROMATIC_ROLES[] = {
    "approach", "borrowed", "chromatic", "echo", "motif", "doubling", "imitation",
};
// Non-chord licenses. pedal/suspension must also discharge (lint_obligations).
static const char *const EXTRA_NONCHORD[] = {
    "passing", "neighbor", "pedal", "appoggiatura", "suspension",
};

static bool role_is(const AnoMusicEvent *e, const char *r)
{
    return strcmp(e->role, r) == 0;
}

static bool is_chromatic_role(const char *r)
{
    for (uint32_t i = 0; i < sizeof CHROMATIC_ROLES / sizeof CHROMATIC_ROLES[0]; ++i)
        if (strcmp(r, CHROMATIC_ROLES[i]) == 0)
            return true;
    return false;
}

static bool is_licensed_nonchord(const char *r)
{
    if (is_chromatic_role(r))
        return true;
    for (uint32_t i = 0; i < sizeof EXTRA_NONCHORD / sizeof EXTRA_NONCHORD[0]; ++i)
        if (strcmp(r, EXTRA_NONCHORD[i]) == 0)
            return true;
    return false;
}

/* Report */

AnoLintLimits ano_lint_limits_default(void)
{
    // static: an object with static storage has its PADDING zeroed, and this
    // struct is copied into the engine, whose bytes are its snapshot.
    static const AnoLintLimits k = {
        .maxVoiceMove = 7,
        .padLo = 52, .padHi = 79,
        .bassLo = 26, .bassHi = 55,
        .melodyLo = 54, .melodyHi = 90, // register_center map range [66,78] +/- 12
        .melodyStrongChordRatio = 0.8,
        .leapResolutionRatio = 0.9,
        .leapSemitones = 5,
        .counterLo = 55, .counterHi = 79, // C5: the tenor gap (G3..G5)
        .counterConsonanceRatio = 0.7,
        .counterOverlapRatio = 0.4,
        .drumPitches = ANO_DRUM_PITCHES,
        .drumPitchCount = ANO_DRUM_COUNT,
    };
    return k;
}

void ano_lint_report_reset(AnoLintReport *r)
{
    r->count = 0;
    r->dropped = 0;
}

const char *ano_violation_str(const AnoViolation *v, char *buf, uint32_t cap)
{
    if (v->bar < 0)
        snprintf(buf, cap, "[%s] piece: %s", v->rule, v->message);
    else
        snprintf(buf, cap, "[%s] bar %d: %s", v->rule, v->bar + 1, v->message);
    return buf;
}

static void vio(AnoLintReport *r, const char *rule, int bar, const char *fmt, ...)
{
    if (r->count >= ANO_LINT_MAX_VIOLATIONS) {
        r->dropped++;
        return;
    }
    AnoViolation *v = &r->v[r->count++];
    v->rule = rule;
    v->bar = bar;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v->message, sizeof v->message, fmt, ap);
    va_end(ap);
}

/* Lookups */

// Subject bar vs lookahead (horizon+). horizon < 0 judges everything.
static bool judged(int bar, int horizon)
{
    return horizon < 0 || bar < horizon;
}

// Harmonic position from the grid slot displaced FROM (not post-jitter start).
// Holds while modifiers move < half grid (0.125 beats); none do.
static int grid_pos(double start, AnoMeter meter, double *offset)
{
    double slot = nearbyint(start / ANO_MUSIC_GRID) * ANO_MUSIC_GRID;
    int bar = ano_meter_bar_of(meter, slot);
    if (offset)
        *offset = slot - bar * ano_meter_bar_quarters(meter);
    return bar;
}

// Last context for a bar wins. Full list (reaches lookahead).
static const AnoHarmonicContext *ctx_of(const AnoHarmonicContext *cx, uint32_t n, int bar)
{
    const AnoHarmonicContext *hit = NULL;
    for (uint32_t i = 0; i < n; ++i)
        if (cx[i].bar == bar)
            hit = &cx[i];
    return hit;
}

static bool pc_in(const uint8_t *pcs, uint32_t n, int pitch)
{
    for (uint32_t i = 0; i < n; ++i)
        if (pcs[i] == pitch % 12)
            return true;
    return false;
}

static bool int_in(const int *a, uint32_t n, int v)
{
    for (uint32_t i = 0; i < n; ++i)
        if (a[i] == v)
            return true;
    return false;
}

static const char *pname(int midi, char *buf)
{
    return ano_pitch_name(midi, false, buf, 8);
}

static const char *sname(AnoScale s, char *buf)
{
    return ano_scale_name(s, buf, 24);
}

static const char *lname(const AnoMusicEvent *e)
{
    return ANO_LAYER_NAMES[e->core.layer];
}

static const char *csym(const AnoHarmonicContext *c)
{
    return c->chordSym[0] ? c->chordSym : "the chord";
}

// Chord at event start: D3 timeline segment or downbeat.
static uint32_t pcs_at(const AnoHarmonicContext *c, double offset, uint8_t out[5])
{
    if (c->chordSpanCount) {
        AnoChord now = ano_ctx_chord_at(c, offset);
        if (now.valid)
            return ano_chord_voiced_pcs(now, c->scale, out);
    }
    memcpy(out, c->chordPcs, c->chordPcCount);
    return c->chordPcCount;
}

/* Stable sorts */

typedef bool (*EvLess)(const AnoMusicEvent *a, const AnoMusicEvent *b);

static void sort_evp(const AnoMusicEvent **a, uint32_t n, EvLess less)
{
    for (uint32_t i = 1; i < n; ++i) {
        const AnoMusicEvent *k = a[i];
        uint32_t j = i;
        while (j > 0 && less(k, a[j - 1])) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = k;
    }
}

static bool by_start(const AnoMusicEvent *a, const AnoMusicEvent *b)
{
    return a->core.start < b->core.start;
}

static bool by_start_pitch(const AnoMusicEvent *a, const AnoMusicEvent *b)
{
    if (a->core.start != b->core.start)
        return a->core.start < b->core.start;
    return a->core.pitch < b->core.pitch;
}

static void sort_ints(int *a, uint32_t n)
{
    for (uint32_t i = 1; i < n; ++i) {
        int k = a[i];
        uint32_t j = i;
        while (j > 0 && k < a[j - 1]) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = k;
    }
}

// Collect the events of one layer, in emission order.
static uint32_t collect(const AnoMusicEvent *ev, uint32_t n, int layer,
                        const AnoMusicEvent **out, uint32_t cap)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < n && m < cap; ++i)
        if (ev[i].core.layer == layer)
            out[m++] = &ev[i];
    return m;
}

// Event sounding at t (last started <= t, not ended). NULL if rest.
static const AnoMusicEvent *sounding(const AnoMusicEvent **evs, uint32_t n, double t)
{
    const AnoMusicEvent *cur = NULL;
    for (uint32_t i = 0; i < n; ++i) {
        if (evs[i]->core.start > t + 1e-9)
            break;
        if (t < evs[i]->core.start + evs[i]->core.dur - 1e-9)
            cur = evs[i];
    }
    return cur;
}

/* M0: grid, scale, annotations */

static void lint_events(const AnoMusicEvent *ev, uint32_t n,
                        const AnoHarmonicContext *cx, uint32_t ncx,
                        AnoMeter meter, AnoLintStage stage, AnoLintReport *out)
{
    char nb[8];
    for (uint32_t i = 0; i < n; ++i) {
        const AnoMusicEvent *e = &ev[i];
        int bar = grid_pos(e->core.start, meter, NULL);

        if (stage == ANO_LINT_PRE) {
            const double vals[2] = { e->core.start, e->core.dur };
            const char *names[2] = { "start", "dur" };
            for (int k = 0; k < 2; ++k) {
                double q = vals[k] / ANO_MUSIC_GRID;
                if (fabs(q - nearbyint(q)) > 1e-9)
                    vio(out, "grid", bar, "%s=%g off the %g-beat grid (%s)",
                        names[k], vals[k], ANO_MUSIC_GRID, lname(e));
            }
        }

        if (e->core.layer == ANO_MUSIC_PERC)
            continue; // drums are unpitched; scale rules do not apply

        const AnoHarmonicContext *c = ctx_of(cx, ncx, bar);
        if (!c) {
            if (!role_is(e, "echo")) // echo tails may ring past the last bar
                vio(out, "context", bar, "no HarmonicContext covers %s %s",
                    lname(e), pname(e->core.pitch, nb));
            continue;
        }
        // Chord members are licensed by the chord itself (chordPcs reflect
        // borrowing); anything else chromatic needs a licensing role.
        bool member = c->chordPcCount && pc_in(c->chordPcs, c->chordPcCount, e->core.pitch);
        if (!ano_scale_contains(c->scale, e->core.pitch) && !member
            && !is_chromatic_role(e->role))
            vio(out, "scale", bar,
                "%s (%s) not in %s, not a member of %s, and role '%s' does not "
                "license chromaticism",
                pname(e->core.pitch, nb), lname(e), sname(c->scale, (char[24]){ 0 }),
                csym(c), e->role);
        // an echo keeps its source bar's annotations; the harmony (and even
        // the mode) may have moved on underneath it
        if (e->degree != 0 && ano_scale_degree_of(c->scale, e->core.pitch) != e->degree
            && !role_is(e, "echo"))
            vio(out, "degree", bar, "%s annotated ^%d but is ^%d in %s",
                pname(e->core.pitch, nb), e->degree,
                ano_scale_degree_of(c->scale, e->core.pitch),
                sname(c->scale, (char[24]){ 0 }));
    }
}

/* M1: pad voicing, bass */

static void lint_pad(const AnoMusicEvent *ev, uint32_t n,
                     const AnoHarmonicContext *cx, uint32_t ncx, AnoMeter meter,
                     const AnoLintLimits *L, AnoLintStage stage, AnoLintReport *out)
{
    const AnoMusicEvent *pads[LINT_MAX_LINE];
    uint32_t np = collect(ev, n, ANO_MUSIC_PAD, pads, LINT_MAX_LINE);
    sort_evp(pads, np, by_start_pitch);

    char a[8], b[8];
    // A C3 entry hosted by the pad rides ABOVE the voicing: the range still
    // applies, the voicing rules do not 〜 it is a figure, not a voice.
    const AnoMusicEvent *grp[LINT_MAX_LINE];
    uint32_t ng = 0;
    for (uint32_t i = 0; i < np; ++i) {
        if (role_is(pads[i], "imitation")) {
            int p = pads[i]->core.pitch;
            if (!(L->padLo <= p && p <= L->padHi))
                vio(out, "pad-range", ano_meter_bar_of(meter, pads[i]->core.start),
                    "%s outside pad range [%s, %s]", pname(p, a),
                    pname(L->padLo, b), pname(L->padHi, (char[8]){ 0 }));
            continue;
        }
        grp[ng++] = pads[i];
    }

    // Voicing analysis (unison doubling, voice movement) needs simultaneous
    // chords 〜 Strum staggers starts, so these rules are pre-modifier only.
    bool voicing = stage == ANO_LINT_PRE;
    int prev[LINT_MAX_GROUP];
    uint32_t prevN = 0;
    bool hasPrev = false;

    for (uint32_t i = 0; i < ng;) {
        double start = grp[i]->core.start;
        uint32_t j = i, cnt = 0;
        int pitches[LINT_MAX_GROUP];
        while (j < ng && grp[j]->core.start == start) {
            if (cnt < LINT_MAX_GROUP)
                pitches[cnt++] = grp[j]->core.pitch;
            ++j;
        }
        double offset;
        int bar = grid_pos(start, meter, &offset);

        if (voicing) {
            for (uint32_t k = 1; k < cnt; ++k)
                if (pitches[k] == pitches[k - 1]) {
                    vio(out, "unison", bar, "pad voicing doubles a unison: %s",
                        pname(pitches[k], a));
                    break;
                }
        }
        for (uint32_t k = 0; k < cnt; ++k)
            if (!(L->padLo <= pitches[k] && pitches[k] <= L->padHi))
                vio(out, "pad-range", bar, "%s outside pad range [%s, %s]",
                    pname(pitches[k], a), pname(L->padLo, b),
                    pname(L->padHi, (char[8]){ 0 }));

        const AnoHarmonicContext *c = ctx_of(cx, ncx, bar);
        if (c && c->chordPcCount) {
            uint8_t pcs[5];
            uint32_t npcs = pcs_at(c, offset, pcs); // D3: the segment in force
            for (uint32_t k = i; k < j; ++k)
                if (!pc_in(pcs, npcs, grp[k]->core.pitch)
                    && !is_licensed_nonchord(grp[k]->role))
                    vio(out, "chord-tone", bar, "pad %s is not a member of %s",
                        pname(grp[k]->core.pitch, a), csym(c));
        }
        // a lone pitch is a figure note (a C2 comping strike, an ornament's
        // resolution), not a voicing 〜 voice-leading is judged between chords
        if (voicing && hasPrev && prevN == cnt && cnt >= 2) {
            for (uint32_t k = 0; k < cnt; ++k)
                if (abs(pitches[k] - prev[k]) > L->maxVoiceMove)
                    vio(out, "voice-move", bar,
                        "pad voice %u leaps %d semitones (%s -> %s), max %d", k,
                        abs(pitches[k] - prev[k]), pname(prev[k], a),
                        pname(pitches[k], b), L->maxVoiceMove);
        }
        memcpy(prev, pitches, cnt * sizeof pitches[0]);
        prevN = cnt;
        hasPrev = true;
        i = j;
    }
}

static void lint_bass(const AnoMusicEvent *ev, uint32_t n,
                      const AnoHarmonicContext *cx, uint32_t ncx, AnoMeter meter,
                      const AnoLintLimits *L, AnoLintReport *out)
{
    char a[8];
    for (uint32_t i = 0; i < n; ++i) {
        const AnoMusicEvent *e = &ev[i];
        if (e->core.layer != ANO_MUSIC_BASS)
            continue;
        double offset;
        int bar = grid_pos(e->core.start, meter, &offset);
        int p = e->core.pitch;
        if (!(L->bassLo <= p && p <= L->bassHi))
            vio(out, "bass-range", bar, "%s outside bass range [%s, %s]",
                pname(p, a), pname(L->bassLo, (char[8]){ 0 }),
                pname(L->bassHi, (char[8]){ 0 }));
        const AnoHarmonicContext *c = ctx_of(cx, ncx, bar);
        if (!c || !c->chordPcCount)
            continue;
        if (offset == 0.0) { // the downbeat, as WRITTEN (a humanized onset still is one)
            // a pedal (held bass under shifting harmony) is a licensed non-root
            // beat-1 bass; it carries a termination obligation instead
            if (p % 12 != c->chordPcs[0] && !is_licensed_nonchord(e->role))
                vio(out, "bass-root", bar,
                    "beat-1 bass %s is not the bass pc of %s (pc %d)", pname(p, a),
                    csym(c), c->chordPcs[0]);
        } else if (!pc_in(c->chordPcs, c->chordPcCount, p)
                   && !is_licensed_nonchord(e->role)) {
            vio(out, "bass-chord-tone", bar,
                "bass %s not in %s and role '%s' does not license it", pname(p, a),
                csym(c), e->role);
        }
    }
}

/* Melodic line (tie chain = one note) */

static void lint_melody(const AnoMusicEvent *ev, uint32_t n,
                        const AnoHarmonicContext *cx, uint32_t ncx, AnoMeter meter,
                        const AnoLintLimits *L, AnoLintReport *out)
{
    // merge_ties over the melody alone: chains never cross layers, so this is
    // melody projection of whole-stream merge
    AnoMusicEvent line[LINT_MAX_LINE];
    uint32_t nl = 0;
    for (uint32_t i = 0; i < n && nl < LINT_MAX_LINE; ++i)
        if (ev[i].core.layer == ANO_MUSIC_MELODY)
            line[nl++] = ev[i];
    nl = ano_merge_ties(line, nl, line, LINT_MAX_LINE);
    if (!nl)
        return;

    const AnoMusicEvent *mel[LINT_MAX_LINE];
    for (uint32_t i = 0; i < nl; ++i)
        mel[i] = &line[i];
    sort_evp(mel, nl, by_start);

    char a[8], b[8];
    for (uint32_t i = 0; i < nl; ++i) {
        // the doubled line (C1) sits up to a 6th under the surface, so its
        // floor extends by that interval; the surface note itself is bounded
        int floor_ = role_is(mel[i], "doubling") ? L->melodyLo - 9 : L->melodyLo;
        int p = mel[i]->core.pitch;
        if (!(floor_ <= p && p <= L->melodyHi))
            vio(out, "melody-range", ano_meter_bar_of(meter, mel[i]->core.start),
                "%s outside melody range [%s, %s]", pname(p, a),
                pname(L->melodyLo, b), pname(L->melodyHi, (char[8]){ 0 }));
    }

    // motif/doubling excluded from surface-line heuristics.
    const AnoMusicEvent *tune[LINT_MAX_LINE];
    uint32_t nt = 0;
    for (uint32_t i = 0; i < nl; ++i)
        if (!role_is(mel[i], "motif") && !role_is(mel[i], "doubling"))
            tune[nt++] = mel[i];

    int strong[ANO_METER_MAX_SLOTS];
    uint32_t ns = ano_meter_strong_slots(meter, strong);

    uint32_t onStrong = 0, chordal = 0;
    for (uint32_t i = 0; i < nt; ++i) {
        if (!int_in(strong, ns, ano_meter_slot_of(meter, tune[i]->core.start)))
            continue;
        const AnoHarmonicContext *c =
            ctx_of(cx, ncx, ano_meter_bar_of(meter, tune[i]->core.start));
        // the cadence bar is a deliberate embellished approach (appoggiatura ->
        // run -> resolution), not chord-tone outlining
        if (!c || !c->chordPcCount || c->cadenceSlot == ANO_CTX_SLOT_CADENCE)
            continue;
        onStrong++;
        chordal += pc_in(c->chordPcs, c->chordPcCount, tune[i]->core.pitch);
    }
    if (onStrong) {
        double ratio = (double)chordal / onStrong;
        if (ratio < L->melodyStrongChordRatio)
            vio(out, "melody-strong-beats", -1,
                "only %u/%u strong-beat melody notes are chord tones (%.2f < %.2f)",
                chordal, onStrong, ratio, L->melodyStrongChordRatio);
    }

    uint32_t leaps = 0, resolved = 0;
    for (uint32_t i = 0; i + 2 < nt; ++i) {
        const AnoMusicEvent *x = tune[i], *y = tune[i + 1], *z = tune[i + 2];
        double xEnd = x->core.start + x->core.dur, yEnd = y->core.start + y->core.dur;
        if (y->core.start - xEnd > 2.0 || z->core.start - yEnd > 2.0)
            continue; // a rest breaks the line; no recovery expected
        int interval = (int)y->core.pitch - (int)x->core.pitch;
        if (abs(interval) <= L->leapSemitones)
            continue;
        leaps++;
        int back = (int)z->core.pitch - (int)y->core.pitch;
        if (back != 0 && (back > 0) != (interval > 0) && abs(back) <= 2)
            resolved++;
    }
    if (leaps && (double)resolved / leaps < L->leapResolutionRatio)
        vio(out, "melody-leaps", -1,
            "only %u/%u leaps beyond a P4 recover by an opposite step (%.2f < %.2f)",
            resolved, leaps, (double)resolved / leaps, L->leapResolutionRatio);
}

/* D1 tie coherence */

// in/both must continue same-layer same-pitch out. Orphan out legal; orphan in illegal.
static void lint_ties(const AnoMusicEvent *ev, uint32_t n, AnoMeter meter,
                      AnoLintReport *out)
{
    char a[8];
    for (uint32_t i = 0; i < n; ++i) {
        const AnoMusicEvent *e = &ev[i];
        if (e->core.tie != ANO_MUSIC_TIE_IN && e->core.tie != ANO_MUSIC_TIE_BOTH)
            continue;
        bool hosted = false;
        for (uint32_t j = 0; j < n && !hosted; ++j) {
            const AnoMusicEvent *o = &ev[j];
            if (o == e || o->core.layer != e->core.layer || o->core.pitch != e->core.pitch)
                continue;
            if (o->core.tie != ANO_MUSIC_TIE_OUT && o->core.tie != ANO_MUSIC_TIE_BOTH)
                continue;
            hosted = fabs(o->core.start + o->core.dur - e->core.start) < 1e-9;
        }
        if (!hosted)
            vio(out, "tie", ano_meter_bar_of(meter, e->core.start),
                "%s (%s) ties in from nothing 〜 no same-pitch note ties out into "
                "its start",
                pname(e->core.pitch, a), lname(e));
    }
}

/* C1 doubling */

// doubling: simultaneous melody 3rd/6th above; chord on strong / scale on weak.
static void lint_doubling(const AnoMusicEvent *ev, uint32_t n,
                          const AnoHarmonicContext *cx, uint32_t ncx, AnoMeter meter,
                          AnoLintReport *out)
{
    int strong[ANO_METER_MAX_SLOTS];
    uint32_t ns = ano_meter_strong_slots(meter, strong);
    char a[8], b[8];

    for (uint32_t i = 0; i < n; ++i) {
        const AnoMusicEvent *d = &ev[i];
        if (d->core.layer != ANO_MUSIC_MELODY || !role_is(d, "doubling"))
            continue;
        int bar = ano_meter_bar_of(meter, d->core.start);

        const AnoMusicEvent *src = NULL;
        for (uint32_t j = 0; j < n && !src; ++j) {
            const AnoMusicEvent *m = &ev[j];
            if (m->core.layer != ANO_MUSIC_MELODY || role_is(m, "doubling"))
                continue;
            if (fabs(m->core.start - d->core.start) < 1e-9 && m->core.pitch > d->core.pitch)
                src = m;
        }
        if (!src) {
            vio(out, "doubling", bar,
                "doubling %s has no simultaneous melody note above it",
                pname(d->core.pitch, a));
            continue;
        }
        int ic = ano_interval_class(d->core.pitch, src->core.pitch);
        if (ic != 3 && ic != 4 && ic != 8 && ic != 9) {
            vio(out, "doubling", bar,
                "doubling %s is not a 3rd/6th below the melody's %s",
                pname(d->core.pitch, a), pname(src->core.pitch, b));
            continue;
        }
        const AnoHarmonicContext *c = ctx_of(cx, ncx, bar);
        if (!c || !c->chordPcCount)
            continue;
        if (int_in(strong, ns, ano_meter_slot_of(meter, d->core.start))) {
            if (!pc_in(c->chordPcs, c->chordPcCount, d->core.pitch))
                vio(out, "doubling", bar,
                    "strong-slot doubling %s is not a member of %s",
                    pname(d->core.pitch, a), csym(c));
        } else {
            AnoScale ms = c->chord.valid ? ano_chord_scale_for(c->chord, c->scale) : c->scale;
            if (!ano_scale_contains(ms, d->core.pitch)
                && !pc_in(c->chordPcs, c->chordPcCount, d->core.pitch))
                vio(out, "doubling", bar,
                    "doubling %s is neither a %s tone nor a member of %s",
                    pname(d->core.pitch, a), sname(ms, (char[24]){ 0 }), csym(c));
        }
    }
}

/* C5 species */

// Counter in tenor gap, never above melody; strong = chord + consonant; no perfects.
static void lint_counter(const AnoMusicEvent *ev, uint32_t n,
                         const AnoHarmonicContext *cx, uint32_t ncx, AnoMeter meter,
                         const AnoLintLimits *L, AnoLintReport *out)
{
    const AnoMusicEvent *ctr[LINT_MAX_LINE], *mel[LINT_MAX_LINE], *bas[LINT_MAX_LINE];
    uint32_t nc = 0, nm = 0, nb = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const AnoMusicEvent *e = &ev[i];
        if (e->core.layer == ANO_MUSIC_COUNTER && !role_is(e, "echo") && nc < LINT_MAX_LINE)
            ctr[nc++] = e;
        else if (e->core.layer == ANO_MUSIC_MELODY && !role_is(e, "doubling")
                 && !role_is(e, "echo") && nm < LINT_MAX_LINE)
            mel[nm++] = e;
        else if (e->core.layer == ANO_MUSIC_BASS && nb < LINT_MAX_LINE)
            bas[nb++] = e;
    }
    if (!nc)
        return;
    sort_evp(ctr, nc, by_start);
    sort_evp(mel, nm, by_start);
    sort_evp(bas, nb, by_start);

    int strong[ANO_METER_MAX_SLOTS];
    uint32_t ns = ano_meter_strong_slots(meter, strong);
    double barq = ano_meter_bar_quarters(meter);
    char a[8], b[8];

    uint32_t consonant = 0, total = 0, overlap = 0, weak = 0;
    bool hasVm = false, hasVb = false;
    double vmT = 0.0, vbT = 0.0;
    int vmC = 0, vmM = 0, vbC = 0, vbB = 0;

    for (uint32_t i = 0; i < nc; ++i) {
        const AnoMusicEvent *e = ctr[i];
        int bar = ano_meter_bar_of(meter, e->core.start);
        int slot = ano_meter_slot_of(meter, e->core.start);
        int p = e->core.pitch;

        if (!(L->counterLo <= p && p <= L->counterHi))
            vio(out, "counter-range", bar, "%s outside counter range [%s, %s]",
                pname(p, a), pname(L->counterLo, b), pname(L->counterHi, (char[8]){ 0 }));
        const AnoMusicEvent *m = sounding(mel, nm, e->core.start);
        if (m && p > m->core.pitch)
            vio(out, "counter-crossing", bar,
                "counter %s crosses above the melody's %s", pname(p, a),
                pname(m->core.pitch, b));

        if (slot != 0) {
            weak++;
            for (uint32_t j = 0; j < nm; ++j)
                if (ano_meter_bar_of(meter, mel[j]->core.start) == bar
                    && ano_meter_slot_of(meter, mel[j]->core.start) == slot) {
                    overlap++;
                    break;
                }
        }
        if (!int_in(strong, ns, slot))
            continue;

        const AnoHarmonicContext *c = ctx_of(cx, ncx, bar);
        if (c && c->chordPcCount && !pc_in(c->chordPcs, c->chordPcCount, p))
            vio(out, "counter-chord-tone", bar,
                "strong-slot counter %s is not a member of %s", pname(p, a), csym(c));

        if (m) {
            total++;
            consonant += ano_is_consonant(p, m->core.pitch);
            if (hasVm && e->core.start - vmT <= barq + 1e-9) {
                if (ano_forbidden_parallel(vmC, vmM, p, m->core.pitch))
                    vio(out, "counter-parallel", bar,
                        "consecutive perfects between counter and melody: %s/%s -> %s/%s",
                        pname(vmC, a), pname(vmM, b), pname(p, (char[8]){ 0 }),
                        pname(m->core.pitch, (char[8]){ 0 }));
                else if (slot == 0 && ano_forbidden_direct(vmC, vmM, p, m->core.pitch, 2))
                    vio(out, "counter-direct", bar,
                        "direct perfect between counter and melody into the downbeat");
            }
            vmT = e->core.start;
            vmC = p;
            vmM = m->core.pitch;
            hasVm = true;
        }
        const AnoMusicEvent *bs = sounding(bas, nb, e->core.start);
        if (bs) {
            if (hasVb && e->core.start - vbT <= barq + 1e-9) {
                if (ano_forbidden_parallel(vbB, vbC, bs->core.pitch, p))
                    vio(out, "counter-parallel", bar,
                        "consecutive perfects between counter and bass: %s/%s -> %s/%s",
                        pname(vbC, a), pname(vbB, b), pname(p, (char[8]){ 0 }),
                        pname(bs->core.pitch, (char[8]){ 0 }));
                else if (slot == 0
                         && ano_forbidden_direct(vbB, vbC, bs->core.pitch, p, 2))
                    vio(out, "counter-direct", bar,
                        "direct perfect between counter and bass into the downbeat");
            }
            vbT = e->core.start;
            vbC = p;
            vbB = bs->core.pitch;
            hasVb = true;
        }
    }

    if (total >= 8 && (double)consonant / total < L->counterConsonanceRatio)
        vio(out, "counter-consonance", -1,
            "only %u/%u strong-beat counter notes are consonant with the melody "
            "(%.2f < %.2f)",
            consonant, total, (double)consonant / total, L->counterConsonanceRatio);
    if (weak >= 10 && (double)overlap / weak > L->counterOverlapRatio)
        vio(out, "counter-overlap", -1,
            "%u/%u off-downbeat counter onsets coincide with melody onsets "
            "(%.2f > %.2f) 〜 the counter should move in the melody's holes",
            overlap, weak, (double)overlap / weak, L->counterOverlapRatio);
}

/* Drum map, cadences */

static void lint_perc(const AnoMusicEvent *ev, uint32_t n, AnoMeter meter,
                      const AnoLintLimits *L, AnoLintReport *out)
{
    for (uint32_t i = 0; i < n; ++i) {
        if (ev[i].core.layer != ANO_MUSIC_PERC)
            continue;
        bool known = false;
        for (uint32_t k = 0; k < L->drumPitchCount && !known; ++k)
            known = L->drumPitches[k] == ev[i].core.pitch;
        if (!known)
            vio(out, "drum-map", ano_meter_bar_of(meter, ev[i].core.start),
                "perc pitch %d not in the drum map", ev[i].core.pitch);
    }
}

static void lint_cadences(const AnoHarmonicContext *cx, uint32_t ncx, int horizon,
                          AnoLintReport *out)
{
    // cadence bar: the policy's arrival degree; pre-cadence: its approach
    static const int8_t ARRIVE[3][2] = { { 1, -1 }, { 5, -1 }, { 6, -1 } };
    static const int8_t APPROACH[3][2] = { { 5, 7 }, { 2, 4 }, { 5, 7 } };
    static const char *const PNAME[3] = { "authentic", "half", "deceptive" };

    for (uint32_t i = 0; i < ncx; ++i) {
        const AnoHarmonicContext *c = &cx[i];
        if (!judged(c->bar, horizon))
            continue;
        if (c->cadenceSlot == ANO_CTX_SLOT_NONE || !c->chord.valid
            || c->cadencePolicy == ANO_CADENCE_NONE)
            continue;
        if (c->chord.applied)
            continue; // a secondary dominant is a valid (chromatic) pre-cadence;
                      // the tonicize obligation checks its resolution instead
        const int8_t *allowed = c->cadenceSlot == ANO_CTX_SLOT_CADENCE
                                    ? ARRIVE[c->cadencePolicy]
                                    : APPROACH[c->cadencePolicy];
        // a D3 split bar is judged by the segment APPROACHING the cadence 〜
        // the harmony in force when the barline arrives
        AnoChord judged = c->chordSpanCount ? c->chords[c->chordSpanCount - 1].chord
                                            : c->chord;
        if (judged.degree != allowed[0] && (allowed[1] < 0 || judged.degree != allowed[1]))
            vio(out, "cadence", c->bar,
                "%s (%s) realized degree %d, expected one of (%d%s%d)",
                c->cadenceSlot == ANO_CTX_SLOT_CADENCE ? "cadence" : "pre-cadence",
                PNAME[c->cadencePolicy], judged.degree, allowed[0],
                allowed[1] < 0 ? "" : ", ", allowed[1] < 0 ? allowed[0] : allowed[1]);
    }
}

/* M14 obligations */

static void lint_obligations(const AnoMusicEvent *ev, uint32_t n,
                             const AnoHarmonicContext *cx, uint32_t ncx, int horizon,
                             AnoMeter meter, AnoLintReport *out)
{
    char a[8];
    for (uint32_t i = 0; i < n; ++i) {
        const AnoMusicEvent *e = &ev[i];
        bool susp = role_is(e, "suspension");
        // an appoggiatura carries the same resolution obligation, unprepared 〜
        // but only in the pad: the melody's own leap / strong-beat rules govern
        // its melodic appoggiaturas, which pass through non-chord tones mid-run
        bool appog = role_is(e, "appoggiatura") && e->core.layer == ANO_MUSIC_PAD;
        if (!susp && !appog)
            continue;
        int bar = ano_meter_bar_of(meter, e->core.start);
        double end = e->core.start + e->core.dur;

        if (susp) {
            bool prepared = false;
            for (uint32_t j = 0; j < n && !prepared; ++j) {
                const AnoMusicEvent *o = &ev[j];
                prepared = o != e && o->core.layer == e->core.layer
                           && o->core.pitch == e->core.pitch
                           && fabs(o->core.start + o->core.dur - e->core.start) < 1e-9;
            }
            if (!prepared)
                vio(out, "suspension-prep", bar,
                    "%s (%s) suspension is unprepared (no held tone of the same "
                    "pitch precedes it)",
                    pname(e->core.pitch, a), lname(e));
        }

        bool resolved = false;
        for (uint32_t j = 0; j < n && !resolved; ++j) {
            const AnoMusicEvent *o = &ev[j];
            if (o == e || o->core.layer != e->core.layer)
                continue;
            if (fabs(o->core.start - end) > 1e-9)
                continue;
            int step = (int)e->core.pitch - (int)o->core.pitch;
            if (step < 1 || step > 2)
                continue;
            const AnoHarmonicContext *rc =
                ctx_of(cx, ncx, ano_meter_bar_of(meter, o->core.start));
            resolved = rc && rc->chordPcCount
                       && pc_in(rc->chordPcs, rc->chordPcCount, o->core.pitch);
        }
        if (!resolved)
            vio(out, susp ? "suspension" : "appoggiatura", bar,
                "%s (%s) %s does not resolve down by step to a chord tone at beat %g",
                pname(e->core.pitch, a), lname(e), susp ? "suspension" : "appoggiatura",
                ano_meter_beat_in_bar(meter, end));
    }

    // a pedal run (contiguous same-pitch bass) terminates at a cadence: its
    // last held bar is the cadence, or the cadence chord arrives right after
    const AnoMusicEvent *ped[LINT_MAX_LINE];
    uint32_t npe = 0;
    for (uint32_t i = 0; i < n && npe < LINT_MAX_LINE; ++i)
        if (role_is(&ev[i], "pedal"))
            ped[npe++] = &ev[i];
    sort_evp(ped, npe, by_start);

    for (uint32_t i = 0; i < npe;) {
        uint32_t j = i;
        while (j + 1 < npe && ped[j + 1]->core.pitch == ped[i]->core.pitch
               && ano_meter_bar_of(meter, ped[j + 1]->core.start)
                          - ano_meter_bar_of(meter, ped[j]->core.start)
                      <= 1)
            ++j;
        int first = ano_meter_bar_of(meter, ped[i]->core.start);
        int last = ano_meter_bar_of(meter, ped[j]->core.start);
        bool lands = false;
        for (int bb = last; bb <= last + 1 && !lands; ++bb) {
            const AnoHarmonicContext *c = ctx_of(cx, ncx, bb);
            lands = c && c->cadenceSlot == ANO_CTX_SLOT_CADENCE;
        }
        if (!lands)
            vio(out, "pedal", first,
                "%s pedal (bars %d..%d) does not terminate at a cadence",
                pname(ped[i]->core.pitch, a), first + 1, last + 1);
        i = j + 1;
    }

    // obligations are PLANTED inside the window; ctx_of reaches past it, so a
    // promise made in the last rendered bar can still be kept by a lookahead
    for (uint32_t i = 0; i < ncx; ++i) {
        const AnoHarmonicContext *c = &cx[i];
        if (!judged(c->bar, horizon))
            continue;
        const AnoHarmonicContext *nx = ctx_of(cx, ncx, c->bar + 1);
        if (c->obligation == ANO_OBL_TONICIZE) {
            if (!nx || !nx->chord.valid || nx->chord.degree != c->obligationTarget)
                vio(out, "tonicize", c->bar,
                    "secondary dominant %s does not resolve to degree %d",
                    c->chordSym[0] ? c->chordSym : "(?)", c->obligationTarget);
        } else if (c->obligation == ANO_OBL_CADENTIAL64) {
            // B1: the 6/4 is a promise 〜 a root-position dominant must follow;
            // a D3 split bar may discharge it WITHIN the bar (the mid-pulse V)
            bool inBar = false;
            for (uint32_t k = 0; k < c->chordSpanCount; ++k)
                if (c->chords[k].off > 0 && c->chords[k].chord.degree == 5
                    && c->chords[k].chord.inversion == 0)
                    inBar = true;
            if (!inBar
                && (!nx || !nx->chord.valid || nx->chord.degree != 5
                    || nx->chord.inversion != 0))
                vio(out, "cadential64", c->bar,
                    "cadential 6/4 %s does not discharge onto a root-position V",
                    c->chordSym[0] ? c->chordSym : "(?)");
        }
    }

    // B4: a lament ground (contiguous "lament" bars) must reach the dominant 〜
    // its own last chord is degree 5, or the bar after it is. The bass must
    // ARRIVE on 5: root position only.
    int lam[LINT_MAX_BARS];
    uint32_t nlam = 0;
    for (uint32_t i = 0; i < ncx && nlam < LINT_MAX_BARS; ++i)
        if (cx[i].obligation == ANO_OBL_LAMENT && judged(cx[i].bar, horizon))
            lam[nlam++] = cx[i].bar;
    sort_ints(lam, nlam);

    for (uint32_t i = 0; i < nlam;) {
        uint32_t j = i;
        while (j + 1 < nlam && lam[j + 1] == lam[j] + 1)
            ++j;
        const AnoHarmonicContext *last = ctx_of(cx, ncx, lam[j]);
        const AnoHarmonicContext *nx = ctx_of(cx, ncx, lam[j] + 1);
        bool reaches = false;
        for (int k = 0; k < 2; ++k) {
            const AnoHarmonicContext *c = k ? nx : last;
            if (c && c->chord.valid && c->chord.degree == 5 && c->chord.inversion == 0)
                reaches = true;
        }
        if (!reaches)
            vio(out, "lament", lam[i],
                "lament ground (bars %d..%d) never reaches the dominant", lam[i] + 1,
                lam[j] + 1);
        i = j + 1;
    }
}

/* lint() */

// Over capacity -> "capacity" violation (truncated lint must not pass clean).
static void lint_capacity(const AnoMusicEvent *ev, uint32_t n, AnoLintReport *out)
{
    uint32_t per[ANO_MUSIC_LAYER_COUNT] = { 0 };
    for (uint32_t i = 0; i < n; ++i)
        if (ev[i].core.layer < ANO_MUSIC_LAYER_COUNT)
            per[ev[i].core.layer]++;
    for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l)
        if (per[l] > LINT_MAX_LINE)
            vio(out, "capacity", -1,
                "%u %s events exceed the linter's %d-event line buffer 〜 the "
                "layer rules would silently see a truncated line",
                per[l], ANO_LAYER_NAMES[l], LINT_MAX_LINE);
}

void ano_lint(const AnoMusicEvent *events, uint32_t n,
              const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
              AnoMeter meter, AnoLintStage stage, const AnoLintLimits *limits,
              AnoLintReport *out)
{
    AnoLintLimits def = ano_lint_limits_default();
    const AnoLintLimits *L = limits ? limits : &def;

    lint_capacity(events, n, out);
    lint_events(events, n, contexts, nctx, meter, stage, out);
    lint_pad(events, n, contexts, nctx, meter, L, stage, out);
    lint_bass(events, n, contexts, nctx, meter, L, out);
    if (stage == ANO_LINT_PRE) {
        // slot-based melodic and obligation analysis assumes the unmodified grid
        lint_melody(events, n, contexts, nctx, meter, L, out);
        lint_obligations(events, n, contexts, nctx, horizon, meter, out);
        lint_doubling(events, n, contexts, nctx, meter, out);
        lint_counter(events, n, contexts, nctx, meter, L, out);
        lint_ties(events, n, meter, out);
    }
    lint_perc(events, n, meter, L, out);
    lint_cadences(contexts, nctx, horizon, out);
}

/* A2 groove */

typedef struct PercHit
{
    int slot, pitch, vel;
} PercHit;

static bool hit_less(PercHit a, PercHit b)
{
    if (a.slot != b.slot)
        return a.slot < b.slot;
    if (a.pitch != b.pitch)
        return a.pitch < b.pitch;
    return a.vel < b.vel;
}

// Non-fill perc pattern, sorted. Phrase-open crash excluded.
static uint32_t perc_pattern(const AnoMusicEvent *ev, uint32_t n, AnoMeter meter,
                             int bar, PercHit *out)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < n && m < LINT_MAX_PAT; ++i) {
        const AnoMusicEvent *e = &ev[i];
        if (e->core.layer != ANO_MUSIC_PERC || role_is(e, "drum:crash"))
            continue;
        if (ano_meter_bar_of(meter, e->core.start) != bar)
            continue;
        out[m++] = (PercHit){ ano_meter_slot_of(meter, e->core.start), e->core.pitch,
                              e->core.velocity };
    }
    for (uint32_t i = 1; i < m; ++i) { // stable insertion sort
        PercHit k = out[i];
        uint32_t j = i;
        while (j > 0 && hit_less(k, out[j - 1])) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = k;
    }
    return m;
}

// Arp onset slots, sorted. C3 overlay does not re-roll the pattern.
static uint32_t arp_pattern(const AnoMusicEvent *ev, uint32_t n, AnoMeter meter,
                            int bar, int *out)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < n && m < LINT_MAX_PAT; ++i) {
        const AnoMusicEvent *e = &ev[i];
        if (e->core.layer != ANO_MUSIC_ARP || role_is(e, "echo")
            || role_is(e, "imitation"))
            continue;
        if (ano_meter_bar_of(meter, e->core.start) != bar)
            continue;
        int slot = ano_meter_slot_of(meter, e->core.start);
        if (!int_in(out, m, slot))
            out[m++] = slot;
    }
    sort_ints(out, m);
    return m;
}

static bool params_stable(const AnoGenParams *a, const AnoGenParams *b)
{
    if (a->noteDensity != b->noteDensity || a->roughness != b->roughness
        || a->velocityCenter != b->velocityCenter || a->layerCount != b->layerCount)
        return false;
    for (uint32_t i = 0; i < a->layerCount; ++i)
        if (a->layers[i] != b->layers[i])
            return false;
    return true;
}

void ano_lint_groove(const AnoMusicEvent *events, uint32_t n,
                     const AnoHarmonicContext *contexts, const AnoGenParams *params,
                     uint32_t nctx, int horizon, AnoMeter meter, AnoLintReport *out)
{
    // Bars are walked in ascending order and a phrase's start bar is
    // non-decreasing in bar, so the previous bar of the CURRENT phrase is all
    // the state the rule needs 〜 a phrase never resumes after another begins.
    for (int rule = 0; rule < 2; ++rule) {
        bool isPerc = rule == 0;
        bool skipCadence = isPerc; // the cadence fill is the licensed variation

        int order[LINT_MAX_BARS];
        uint32_t no = 0;
        for (uint32_t i = 0; i < nctx && no < LINT_MAX_BARS; ++i)
            if (judged(contexts[i].bar, horizon))
                order[no++] = (int)i;
        for (uint32_t i = 1; i < no; ++i) { // by bar
            int k = order[i];
            uint32_t j = i;
            while (j > 0 && contexts[k].bar < contexts[order[j - 1]].bar) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = k;
        }

        bool have = false;
        int lastPhrase = -1, lastBar = -1;
        AnoGenParams lastFp = { 0 };
        PercHit lastHits[LINT_MAX_PAT], hits[LINT_MAX_PAT];
        int lastSlots[LINT_MAX_PAT], slots[LINT_MAX_PAT];
        uint32_t lastLen = 0, len = 0;

        for (uint32_t i = 0; i < no; ++i) {
            const AnoHarmonicContext *c = &contexts[order[i]];
            const AnoGenParams *p = &params[order[i]];
            if (skipCadence && c->phrasePos == c->phraseBars - 1)
                continue;
            int phrase = c->bar - c->phrasePos;
            len = isPerc ? perc_pattern(events, n, meter, c->bar, hits)
                         : arp_pattern(events, n, meter, c->bar, slots);

            if (have && lastPhrase == phrase && params_stable(&lastFp, p)) {
                bool same = lastLen == len;
                for (uint32_t k = 0; same && k < len; ++k)
                    same = isPerc ? (hits[k].slot == lastHits[k].slot
                                     && hits[k].pitch == lastHits[k].pitch
                                     && hits[k].vel == lastHits[k].vel)
                                  : slots[k] == lastSlots[k];
                if (!same)
                    vio(out, isPerc ? "groove-perc" : "groove-arp", c->bar,
                        "%s pattern re-rolled mid-phrase under stable params "
                        "(bar %d -> bar %d)",
                        isPerc ? "groove-perc" : "groove-arp", lastBar + 1, c->bar + 1);
            }
            lastFp = *p;
            lastPhrase = phrase;
            lastBar = c->bar;
            lastLen = len;
            if (isPerc)
                memcpy(lastHits, hits, len * sizeof hits[0]);
            else
                memcpy(lastSlots, slots, len * sizeof slots[0]);
            have = true;
        }
    }
}

/* A3 outer-voice */

void ano_lint_outer(const AnoMusicEvent *events, uint32_t n,
                    const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                    AnoMeter meter, double contraryMin, AnoLintReport *out)
{
    const AnoMusicEvent *bas[LINT_MAX_LINE], *mel[LINT_MAX_LINE];
    uint32_t nb = 0, nm = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const AnoMusicEvent *e = &events[i];
        if (e->core.layer == ANO_MUSIC_BASS && nb < LINT_MAX_LINE)
            bas[nb++] = e;
        // signature statements are licensed as a whole and echoes ring free
        else if (e->core.layer == ANO_MUSIC_MELODY && !role_is(e, "motif")
                 && !role_is(e, "doubling") && !role_is(e, "echo")
                 && nm < LINT_MAX_LINE)
            mel[nm++] = e;
    }
    sort_evp(bas, nb, by_start);
    sort_evp(mel, nm, by_start);

    int strong[ANO_METER_MAX_SLOTS];
    uint32_t ns = ano_meter_strong_slots(meter, strong);
    double barq = ano_meter_bar_quarters(meter);
    char a[8], b[8];

    bool have = false;
    double pt = 0.0;
    int pm = 0, pb = 0;

    for (uint32_t i = 0; i < nm; ++i) {
        int slot = ano_meter_slot_of(meter, mel[i]->core.start);
        if (!int_in(strong, ns, slot))
            continue;
        const AnoMusicEvent *bs = sounding(bas, nb, mel[i]->core.start);
        if (!bs)
            continue;
        int m2 = mel[i]->core.pitch, b2 = bs->core.pitch;
        double t2 = mel[i]->core.start;
        if (have && t2 - pt <= barq + 1e-9) { // else the frame broke (a rest bar)
            int ic = ano_interval_class(b2, m2);
            const char *name = ic == 0 ? "octaves" : (ic == 7 ? "fifths" : "?");
            int bar = ano_meter_bar_of(meter, t2);
            if (ano_forbidden_parallel(pb, pm, b2, m2))
                vio(out, "outer-parallel", bar,
                    "parallel %s between melody and bass: %s/%s -> %s/%s", name,
                    pname(pm, a), pname(pb, b), pname(m2, (char[8]){ 0 }),
                    pname(b2, (char[8]){ 0 }));
            else if (slot == 0 && ano_forbidden_direct(pb, pm, b2, m2, 2))
                vio(out, "outer-direct", bar,
                    "direct %s into the downbeat: melody leaps %s -> %s in similar "
                    "motion with the bass",
                    name, pname(pm, a), pname(m2, b));
        }
        pt = t2;
        pm = m2;
        pb = b2;
        have = true;
    }

    uint32_t good = 0, total = 0;
    for (uint32_t i = 0; i < nctx; ++i) {
        const AnoHarmonicContext *c = &contexts[i];
        if (!judged(c->bar, horizon) || c->cadenceSlot != ANO_CTX_SLOT_CADENCE)
            continue;
        if (c->phrasePos == 0)
            continue; // an elided cadence (D2) is crashed into, not settled into
        double barStart = c->bar * barq;
        // Melody = surface line; bass = root motion downbeat-to-downbeat.
        const AnoMusicEvent *before = NULL, *after = NULL;
        for (uint32_t k = 0; k < nm; ++k) {
            double s = mel[k]->core.start;
            if (s > barStart - barq - 1e-9 && s < barStart - 1e-9)
                before = mel[k]; // last such
            if (!after && ano_meter_bar_of(meter, s) == c->bar)
                after = mel[k]; // first such
        }
        const AnoMusicEvent *b1 = sounding(bas, nb, barStart - barq);
        const AnoMusicEvent *b2 = sounding(bas, nb, barStart);
        if (before && after && b1 && b2) {
            total++;
            AnoMotion mo = ano_motion(b1->core.pitch, before->core.pitch,
                                      b2->core.pitch, after->core.pitch);
            good += mo == ANO_MOTION_CONTRARY || mo == ANO_MOTION_OBLIQUE;
        }
    }
    if (total >= 4 && (double)good / total < contraryMin)
        vio(out, "outer-cadence", -1,
            "only %u/%u cadences approached in contrary/oblique motion (%.2f < %.2f)",
            good, total, (double)good / total, contraryMin);
}

/* B2 periods */

void ano_lint_periods(const AnoMusicEvent *events, uint32_t n,
                      const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                      AnoMeter meter, AnoLintReport *out)
{
    for (uint32_t i = 0; i < nctx; ++i) {
        const AnoHarmonicContext *c = &contexts[i];
        if (!judged(c->bar, horizon) || c->form != ANO_FORM_CONSEQUENT
            || c->phrasePos != 0)
            continue;
        int ante = c->bar - c->phraseBars;
        const AnoHarmonicContext *ac = ctx_of(contexts, nctx, ante);
        if (!ac || ac->form != ANO_FORM_ANTECEDENT) {
            vio(out, "period", c->bar, "consequent without a recorded antecedent");
            continue;
        }
        // a dramaturg/landmark payoff overrode the answer 〜 the arrival wins
        bool payoff = false;
        for (uint32_t k = 0; k < n && !payoff; ++k) {
            const AnoMusicEvent *e = &events[k];
            if (e->core.layer != ANO_MUSIC_MELODY || !role_is(e, "motif"))
                continue;
            int bar = ano_meter_bar_of(meter, e->core.start);
            payoff = bar >= c->bar && bar < c->bar + c->phraseBars;
        }
        if (payoff)
            continue;

        int q[LINT_MAX_PAT], ans[LINT_MAX_PAT];
        uint32_t nq = 0, na = 0;
        for (uint32_t k = 0; k < n; ++k) {
            const AnoMusicEvent *e = &events[k];
            if (e->core.layer != ANO_MUSIC_MELODY || role_is(e, "doubling"))
                continue; // the question is the surface line
            int bar = ano_meter_bar_of(meter, e->core.start);
            int slot = ano_meter_slot_of(meter, e->core.start);
            if (bar == ante && nq < LINT_MAX_PAT)
                q[nq++] = slot;
            else if (bar == c->bar && na < LINT_MAX_PAT)
                ans[na++] = slot;
        }
        sort_ints(q, nq);
        sort_ints(ans, na);
        if (!nq || !na)
            continue;
        bool same = nq == na;
        for (uint32_t k = 0; same && k < nq; ++k)
            same = q[k] == ans[k];
        if (!same)
            vio(out, "period", c->bar,
                "consequent opening rhythm (%u onsets) does not answer the "
                "antecedent's (%u onsets, bar %d)",
                na, nq, ante + 1);
    }
}

/* Phrase ranks (by phrase start bar) */

static uint32_t phrase_ranks(const AnoHarmonicContext *cx, uint32_t ncx, int horizon,
                             int *starts)
{
    uint32_t ns = 0;
    for (uint32_t i = 0; i < ncx; ++i) {
        if (!judged(cx[i].bar, horizon))
            continue;
        int s = cx[i].bar - cx[i].phrasePos;
        if (!int_in(starts, ns, s) && ns < LINT_MAX_BARS)
            starts[ns++] = s;
    }
    sort_ints(starts, ns);
    return ns;
}

static int rank_of(const int *starts, uint32_t ns, int start)
{
    for (uint32_t i = 0; i < ns; ++i)
        if (starts[i] == start)
            return (int)i;
    return -1;
}

/* C4 texture */

void ano_lint_texture(const AnoMusicEvent *events, uint32_t n,
                      const AnoHarmonicContext *contexts, const AnoGenParams *params,
                      uint32_t nctx, int horizon, AnoMeter meter, AnoLintReport *out)
{
    int starts[LINT_MAX_BARS];
    uint32_t ns = phrase_ranks(contexts, nctx, horizon, starts);

    for (uint32_t r = 0; r < ns; ++r) {
        int bars[LINT_MAX_BARS];
        uint32_t nbars = 0;
        int tex = -1;
        bool mixed = false;
        const AnoGenParams *firstP = NULL;
        const AnoHarmonicContext *firstC = NULL;

        for (uint32_t i = 0; i < nctx; ++i) {
            const AnoHarmonicContext *c = &contexts[i];
            if (!judged(c->bar, horizon)
                || rank_of(starts, ns, c->bar - c->phrasePos) != (int)r)
                continue;
            if (nbars < LINT_MAX_BARS)
                bars[nbars++] = c->bar;
            if (tex < 0)
                tex = params[i].texture;
            else if (tex != (int)params[i].texture)
                mixed = true; // an override flipped mid-phrase: no phrase-level claim
        }
        sort_ints(bars, nbars);
        if (mixed || tex <= ANO_TEX_NONE || !nbars)
            continue;
        firstC = ctx_of(contexts, nctx, bars[0]);
        for (uint32_t i = 0; i < nctx; ++i)
            if (contexts[i].bar == bars[0])
                firstP = &params[i];
        if (!firstC || (int)nbars < firstC->phraseBars)
            continue; // the render truncated this phrase 〜 its claim never got room

        uint32_t nmel = 0, ndbl = 0, nimi = 0, nctr = 0;
        for (uint32_t k = 0; k < n; ++k) {
            const AnoMusicEvent *e = &events[k];
            int bar = ano_meter_bar_of(meter, e->core.start);
            if (!int_in(bars, nbars, bar))
                continue;
            if (e->core.layer == ANO_MUSIC_MELODY && !role_is(e, "doubling"))
                nmel++;
            if (role_is(e, "doubling"))
                ndbl++;
            if (role_is(e, "imitation"))
                nimi++;
            if (e->core.layer == ANO_MUSIC_COUNTER)
                nctr++;
        }
        int first = bars[0];

        if (tex == ANO_TEX_DOUBLED && nmel && !ndbl) {
            vio(out, "texture", first,
                "phrase %u claims 'doubled' but the melody sounds undoubled", r);
        } else if (tex == ANO_TEX_IMITATIVE && !nimi) {
            const AnoGenParams *ep = NULL;
            if (nbars > 1)
                for (uint32_t i = 0; i < nctx; ++i)
                    if (contexts[i].bar == bars[1])
                        ep = &params[i];
            if (ep) {
                bool live = false;
                for (uint32_t i = 0; i < ep->layerCount; ++i)
                    live = live || ep->layers[i] == ANO_MUSIC_MELODY;
                if (live)
                    vio(out, "texture", first,
                        "phrase %u claims 'imitative' but no entry sounds", r);
            }
        } else if (tex == ANO_TEX_COUNTER && !nctr) {
            bool live = false;
            if (firstP)
                for (uint32_t i = 0; i < firstP->layerCount; ++i)
                    live = live || firstP->layers[i] == ANO_MUSIC_COUNTER;
            if (live)
                vio(out, "texture", first,
                    "phrase %u claims 'counter' but the layer is silent", r);
        } else if ((tex == ANO_TEX_MONOPHONIC || tex == ANO_TEX_HOMOPHONIC)
                   && (ndbl || nimi || nctr)) {
            vio(out, "texture", first, "phrase %u claims '%s' but polyphony sounds", r,
                tex == ANO_TEX_MONOPHONIC ? "monophonic" : "homophonic");
        }

        if (tex == ANO_TEX_MONOPHONIC) {
            // a monophonic phrase thins the pad to dyads
            double fat = 0.0;
            bool found = false;
            for (uint32_t k = 0; k < n; ++k) {
                const AnoMusicEvent *e = &events[k];
                if (e->core.layer != ANO_MUSIC_PAD || role_is(e, "imitation"))
                    continue;
                if (!int_in(bars, nbars, ano_meter_bar_of(meter, e->core.start)))
                    continue;
                uint32_t cnt = 0;
                for (uint32_t j = 0; j < n; ++j)
                    if (events[j].core.layer == ANO_MUSIC_PAD
                        && !role_is(&events[j], "imitation")
                        && events[j].core.start == e->core.start
                        && int_in(bars, nbars,
                                  ano_meter_bar_of(meter, events[j].core.start)))
                        cnt++;
                if (cnt > 2 && (!found || e->core.start < fat)) {
                    fat = e->core.start;
                    found = true;
                }
            }
            if (found) {
                uint32_t cnt = 0;
                for (uint32_t j = 0; j < n; ++j)
                    if (events[j].core.layer == ANO_MUSIC_PAD
                        && !role_is(&events[j], "imitation")
                        && events[j].core.start == fat)
                        cnt++;
                vio(out, "texture", ano_meter_bar_of(meter, fat),
                    "monophonic phrase %u voices a %u-note pad", r, cnt);
            }
        }
    }
}

/* C3 imitation */

void ano_lint_imitation(const AnoMusicEvent *events, uint32_t n,
                        const AnoHarmonicContext *contexts, uint32_t nctx, int horizon,
                        const int *cellTag, const AnoMotif *cells,
                        AnoMeter meter, double threshold, AnoLintReport *out)
{
    int starts[LINT_MAX_BARS];
    uint32_t ns = phrase_ranks(contexts, nctx, horizon, starts);

    for (uint32_t r = 0; r < ns; ++r) {
        const AnoMusicEvent *entry[ANO_MOTIF_MAX * 2];
        uint32_t ne = 0;
        for (uint32_t k = 0; k < n && ne < ANO_MOTIF_MAX * 2; ++k) {
            const AnoMusicEvent *e = &events[k];
            if (!role_is(e, "imitation")
                || (e->core.layer != ANO_MUSIC_ARP && e->core.layer != ANO_MUSIC_PAD))
                continue;
            const AnoHarmonicContext *c =
                ctx_of(contexts, nctx, ano_meter_bar_of(meter, e->core.start));
            if (!c || rank_of(starts, ns, c->bar - c->phrasePos) != (int)r)
                continue;
            entry[ne++] = e;
        }
        if (!ne)
            continue;
        sort_evp(entry, ne, by_start);
        int bar = ano_meter_bar_of(meter, entry[0]->core.start);
        if (!ano_phrase_live(cellTag, (int)r)) {
            vio(out, "imitation", bar,
                "imitation events in phrase %u without a recorded source cell", r);
            continue;
        }
        const AnoHarmonicContext *c = ctx_of(contexts, nctx, bar);
        AnoScale ms = c->chord.valid ? ano_chord_scale_for(c->chord, c->scale) : c->scale;
        int pitches[ANO_MOTIF_MAX * 2];
        for (uint32_t i = 0; i < ne; ++i)
            pitches[i] = entry[i]->core.pitch;
        double score = ano_recognizability(&cells[ano_ring_phrase((int)r)],
                                           pitches, ne, ms);
        if (score < threshold)
            vio(out, "imitation", bar,
                "imitation entry no longer carries its cell's contour "
                "(recognizability %.2f < %.2f)",
                score, threshold);
    }
}
